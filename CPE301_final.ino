#include <dht.h>
#include <AccelStepper.h>
#include <LiquidCrystal.h>
#include <RTClib.h>

// Kurt Wagner
// 30 April 2023
// CPE 301 Final Project

// <temp => fan off
#define TEMP_THRESHOLD 19
// >rh => fan off
#define RH_THRESHOLD 60

#define RDA 0x80
#define TBE 0x20

volatile unsigned char* port_b = (unsigned char*) 0x25;
volatile unsigned char* ddr_b = (unsigned char*) 0x24;
volatile unsigned char* pin_b = (unsigned char*) 0x23;

volatile unsigned char* port_h = (unsigned char*) 0x102;
volatile unsigned char* ddr_h = (unsigned char*) 0x101;
volatile unsigned char* pin_h = (unsigned char*) 0x100;

volatile unsigned char* port_l = (unsigned char*) 0x10B;
volatile unsigned char* ddr_l = (unsigned char*) 0x10A;
volatile unsigned char* pin_l = (unsigned char*) 0x109;

// UART Pointers
volatile unsigned char *myUCSR0A  = (unsigned char*) 0xC0;
volatile unsigned char *myUCSR0B  = (unsigned char*) 0xC1;
volatile unsigned char *myUCSR0C  = (unsigned char*) 0xC2;
volatile unsigned int  *myUBRR0   = (unsigned int*) 0xC4;
volatile unsigned char *myUDR0    = (unsigned char*) 0xC6;

// ADC
volatile unsigned char* my_ADMUX = (unsigned char*) 0x7C;
volatile unsigned char* my_ADCSRB = (unsigned char*) 0x7B;
volatile unsigned char* my_ADCSRA = (unsigned char*) 0x7A;
volatile unsigned int* my_ADC_DATA = (unsigned int*) 0x78;

// water sensor
// PB0: vcc
// ADC15: data

// dht
#define DHT_PIN 33
dht DHT;
int temp;
int rh;

// stepper motor
AccelStepper ventStepper(4,23,25,27,29);
unsigned int ventPosition;

// lcd
LiquidCrystal lcd(2,3,4,5,6,7);

// rtc
RTC_DS1307 rtc;

// led
// red: PL0
// yellow: PL2
// green: PL4
// blue: PL6

// state
// error: -1
// disabled: 0
// idle: 1
// running: 2
int state;

void setup() {
  U0Init(9600);
  adc_init();

  // dht
  DHT.read11(DHT_PIN);
  temp = DHT.temperature;
  rh = DHT.humidity;

  // reset, start/stop
  setInput('H', 5);
  setInput('H', 6);

  // water sensor
  setOutput('B', 0);

  // led
  setOutput('L', 0);
  setOutput('L', 2);
  setOutput('L', 4);
  setOutput('L', 6);

  // fan motor
  setOutput('B', 5);
  setOutput('B', 6);
  setOutput('B', 7);

  // stepper motor
  // ADC10: vent direction
  ventStepper.setMaxSpeed(300);
  ventStepper.setAcceleration(100);
  ventPosition = 0;

  // lcd
  lcd.begin(16, 2);

  // rtc
  rtc.begin();
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  // state
  setState(0);
}

void loop() {  
  if (state != -1 && getWaterReading() < 100) {
    setState(-1);
  }

  if (state == -1) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("WATER");
    lcd.setCursor(0,1);
    lcd.print("LOW");
    if (reset() == 1 && getWaterReading() >= 100) {
      delay(500);
      setState(1);
    }
  } else if (state == 0) {
    if (startStop() == 1) {
      setState(1);
    } else {
      //moveVent();
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("DISABLED");
    }
  } else if (state == 1) {
    if (rtc.now().second() == 0) {
      updateReadings();
    }
    if (startStop() == 1) {
      setState(0);
    } else {
      //moveVent();
      DHT.read11(DHT_PIN);
      if (DHT.temperature >= TEMP_THRESHOLD && DHT.humidity <= RH_THRESHOLD) {
        setState(2);
      }
    }
  } else if (state == 2) {
    if (rtc.now().second() == 0) {
      updateReadings();
    }
    if (startStop() == 1) {
      delay(500);
      setState(0);
    }
    //moveVent();
    DHT.read11(DHT_PIN);
    if (DHT.temperature < TEMP_THRESHOLD || DHT.humidity > RH_THRESHOLD) {
      setState(1);
    }
    
  } else {
    state = -1;
  }
}

// state
void setState(int s) {
  state = s;
  Serial.print(rtc.now().hour());
  U0putchar(':');
  Serial.print(rtc.now().minute());
  U0putchar(':');
  Serial.print(rtc.now().second());
  U0putchar(' ');
  U0putchar('-');
  U0putchar(' ');
  
  if (state == -1) {
    led('R');
    fan(0);
    Serial.println("ERROR");
  } else if (state == 0) {
    led('Y');
    fan(0);
    Serial.println("Disabled");
  } else if (state == 1) {
    led('G');
    fan(0);
    updateReadings();
    Serial.println("Idle");
  } else if (state == 2) {
    led('B');
    fan(1);
    updateReadings();
    Serial.println("Running");
  }
  
}

// reset, start/stop
unsigned char reset() {
  return readFromPort('H', 5);
}
unsigned char startStop() {
  return readFromPort('H', 6);
}

// water sensor
unsigned int getWaterReading() {
  writeToPort('B', 0, 1);
  unsigned int i = adc_read(15);
  writeToPort('B', 0, 0);

  return i;
}

// stepper
void moveVent() {
  unsigned int newPosition = adc_read(10) / 200;
  if (newPosition != ventPosition) {
    ventStepper.runToNewPosition(newPosition * 200);
    char message[] = "Vent Position: ";
    for (int i = 0; i < 15; i++) {
      U0putchar(message[i]);
    }
    displayData(newPosition);
    ventPosition = newPosition;
  }
}

// fan
void fan(int state) {
  if (state == 0) {
    writeToPort('B', 7, 0);
    writeToPort('B', 5, 0);
    writeToPort('B', 6, 0);
  } else {
    writeToPort('B', 5, 1);
    writeToPort('B', 6, 0);
    writeToPort('B', 7, 1);
  }
}

// led
void led(char color) {
  if (color == 'R') {
    writeToPort('L', 0, 1);
    writeToPort('L', 2, 0);
    writeToPort('L', 4, 0);
    writeToPort('L', 6, 0);
  } else if (color == 'Y') {
    writeToPort('L', 0, 0);
    writeToPort('L', 2, 1);
    writeToPort('L', 4, 0);
    writeToPort('L', 6, 0);
  } else if (color == 'G') {
    writeToPort('L', 0, 0);
    writeToPort('L', 2, 0);
    writeToPort('L', 4, 1);
    writeToPort('L', 6, 0);
  } else if (color == 'B') {
    writeToPort('L', 0, 0);
    writeToPort('L', 2, 0);
    writeToPort('L', 4, 0);
    writeToPort('L', 6, 1);
  }
}

// dht
void updateReadings() {
  lcd.clear();
  DHT.read11(DHT_PIN);
  lcd.setCursor(0,0);
  lcd.print(DHT.temperature);
  lcd.print(" ");
  lcd.print((char) 223);
  lcd.print("C");
  lcd.setCursor(0,1);
  lcd.print(DHT.humidity);
  lcd.print("% RH");
}

// ADC
void adc_init() {
  *my_ADCSRA |= 0b10000000;
  *my_ADCSRA &= 0b11011111;
  *my_ADCSRA &= 0b11110111;
  *my_ADCSRA &= 0b11111000;
  *my_ADCSRB &= 0b11110111;
  *my_ADCSRB &= 0b11111000;
  *my_ADMUX  &= 0b01111111;
  *my_ADMUX  |= 0b01000000;
  *my_ADMUX  &= 0b11011111;
  *my_ADMUX  &= 0b11100000;
}
unsigned int adc_read(unsigned char adc_channel_num) {
  *my_ADMUX  &= 0b11100000;
  *my_ADCSRB &= 0b11110111;
  if (adc_channel_num > 7) {
    adc_channel_num -= 8;
    *my_ADCSRB |= 0b00001000;
  }
  *my_ADMUX  += adc_channel_num;
  *my_ADCSRA |= 0x40;
  while((*my_ADCSRA & 0x40) != 0);

  return *my_ADC_DATA;
}
void displayData(unsigned int value) {
  unsigned int n = value;
  unsigned int digit;
  //print each place value
  if (n >= 1000) {
    digit = n / 1000; 
    U0putchar('0' + digit);
    n %= 1000;
  }
  if (n >= 100) {
    digit = n / 100;
    U0putchar('0' + digit);
    n %= 100;
  }
  if (n >= 10) {
    digit = n / 10;
    U0putchar('0' + digit);
    n %= 10;
  }
  if (n >= 1) {
    digit = n;
    U0putchar('0' + digit);
  }

  U0putchar('\n');
}

// UART
void U0Init(int U0baud) {
  unsigned long FCPU = 16000000;
  unsigned int tbaud;
  tbaud = (FCPU / 16 / U0baud - 1);
  *myUCSR0A = 0x20;
  *myUCSR0B = 0x18;
  *myUCSR0C = 0x06;
  *myUBRR0  = tbaud;
}
unsigned char kbhit() {
  return *myUCSR0A & RDA;
}
unsigned char getChar() {
  return *myUDR0;
}
void U0putchar(unsigned char U0pdata) {
  while((*myUCSR0A & TBE)==0);
  *myUDR0 = U0pdata;
}

// GPIO
void setInput(char port, unsigned char pin_num) {
  if (port == 'B') {
    *ddr_b &= ~(0x01 << pin_num);
  } else if (port == 'H') {
    *ddr_h &= ~(0x01 << pin_num);
  } else if (port == 'L') {
    *ddr_l &= ~(0x01 << pin_num);
  }
}

void setOutput(char port, unsigned char pin_num) {
  if (port == 'B') {
    *ddr_b |= 0x01 << pin_num;
  } else if (port == 'H') {
    *ddr_h |= 0x01 << pin_num;
  } else if (port == 'L') {
    *ddr_l |= 0x01 << pin_num;
  }
}

unsigned char readFromPort(char port, unsigned char pin_num) {
  if (port == 'B') {
    if (*pin_b & 0x01 << pin_num) {
      return 1;
    } else {
      return 0;
    }
  } else if (port == 'H') {
    if (*pin_h & 0x01 << pin_num) {
      return 1;
    } else {
      return 0;
    }
  } else if (port == 'L') {
    if (*pin_l & 0x01 << pin_num) {
      return 1;
    } else {
      return 0;
    }
  }
}

void writeToPort(char port, unsigned char pin_num, unsigned char state) {
  if (port == 'B') {
    if (state == 0) {
      *port_b &= ~(0x01 << pin_num);
    } else {
      *port_b |= 0x01 << pin_num;
    }
  } else if (port == 'H') {
    if (state == 0) {
      *port_h &= ~(0x01 << pin_num);
    } else {
      *port_h |= 0x01 << pin_num;
    }
  } else if (port == 'L') {
    if (state == 0) {
      *port_l &= ~(0x01 << pin_num);
    } else {
      *port_l |= 0x01 << pin_num;
    }
  }
}
