/*
Forked and additions made by Sneezy
Editing and suggestions by MiCookie
Original Author:
- Philip Bordado (kerpz@yahoo.com)
Hardware:
- Arduino Nano
- 3x Dallas DS18B20 OneWire temp sensors
- HC-05 Bluetooth module at pin 10 (Rx) pin 11 (Tx)
- LCD(K-line) at pin 12
- Voltage divider @ 12v Car to pin A0 (680k ohms and 220k ohms)
Software:
- Arduino 1.0.5
- SoftwareSerialWithHalfDuplex
https://github.com/nickstedman/SoftwareSerialWithHalfDuplex

Formula:
- IMAP = RPM * MAP / IAT / 2
- MAF = (IMAP/60)*(VE/100)*(Eng Disp)*(MMA)/(R)
Where: VE = 80% (Volumetric Efficiency), R = 8.314 J/°K/mole, MMA = 28.97 g/mole (Molecular mass of air)
http://www.lightner.net/obd2guru/IMAP_AFcalc.html
http://www.installuniversity.com/install_university/installu_articles/volumetric_efficiency/ve_computation_9.012000.htm
*/

#include <Arduino.h>
#include <LiquidCrystal.h>
#include <SoftwareSerialWithHalfDuplex.h>

//SNEEZY HACKING - Additions for 3x Dallas 1-wire temp sensors on a bus(async mode).
#include <OneWire.h> // https://github.com/PaulStoffregen/OneWire
#include <DallasTemperature.h> // https://github.com/milesburton/Arduino-Temperature-Control-Library

#define OBD2_BUFFER_LENGTH 20

#define _DEBUG 1

#if (_DEBUG == 1)
#define DebugPrint(x) Serial.print(x)
#define DebugPrintln(x) Serial.println(x)
#define DebugPrintHex(x) Serial.print(x, HEX)
#else
#define DebugPrint(x)
#define DebugPrintln(x)
#define DebugPrintHex(x)
#endif

// Data wire is plugged into pin 2 on the Arduino
#define ONE_WIRE_BUS 2

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// Assign the addresses of your own 1-Wire temp sensors.
// See the tutorial on how to obtain these addresses:
// http://www.hacktronics.com/Tutorials/arduino-1-wire-address-finder.html
DeviceAddress Sensor1_Thermometer =
{ 0x28, 0xFF, 0x0C, 0x1B, 0xA3, 0x15, 0x04, 0xA5 };
DeviceAddress Sensor2_Thermometer =
{ 0x28, 0xFF, 0x0F, 0x4B, 0xA3, 0x15, 0x04, 0x33 };
DeviceAddress Sensor3_Thermometer =
{ 0x28, 0xFF, 0x75, 0x70, 0xA3, 0x15, 0x01, 0x00 };

// input signals: ign, door
// output signals: alarm horn, lock door, unlock door, immobilizer

/*
* LCD RS pin 9
* LCD Enable pin 8
* LCD D4 pin 7
* LCD D5 pin 6
* LCD D6 pin 5
* LCD D7 pin 4
* LCD R/W pin to ground
* 10K potentiometer:
* ends to +5V and ground
* wiper to LCD VO pin (pin 3)
*/
LiquidCrystal lcd(9, 8, 7, 6, 5, 4);

//SNEEZY HACKING - Button stuff
// set pin numbers:
const uint8_t buttonPin = 3;      // the number of the pushbutton pin
const uint8_t ledPin =  13;       // the number of the LED pin

bool Led_State = LOW;          // the current state of the output pin
bool Old_Button_State = LOW;   // the previous reading from the input pin
//bool Screen_Number = LOW;      // the LCD screen to show
bool Screen_Number = false;   // the LCD screen to show
//uint8_t DS18B20_1 = 0;               //global variable for ext temp sensor.

uint32_t t0 = 0, tLastBluetooth = 0;
enum state {DATA_ERROR, DATA, RESET, OK};

//Variables for temperature sensor readings and calculations.

SoftwareSerialWithHalfDuplex btSerial(10, 11); // RX, TX
SoftwareSerialWithHalfDuplex lcdSerial(12, 12, false, false);

bool elm_mode = false;
bool elm_memory = false; // TODO : Remove (this is set, but unused)
bool elm_echo = false;  // TODO : Remove (this is set, but unused)
bool elm_space = false; // TODO : Remove (this is set, but unused)
bool elm_linefeed = false; // TODO : Remove (this is set, but unused)
bool elm_header = false; // TODO : Remove (this is set, but unused)
bool pin_13 = false;
uint8_t  elm_protocol = 0; // auto

// function declaration
// Convert 4 digit character array to its decimal value
// i.e. CharArrayToDec("23A3") = 9123
uint16_t CharArrayToDec(const char (&in)[OBD2_BUFFER_LENGTH], const uint8_t length = 4);
// Convert a single hex character to its decimal value
// i.e. CharToDec("A") = 10
uint8_t CharToDec(const char &in);

void bt_write(char *str)
{
   while (*str != '\0')
   btSerial.write(*str++);
}

void lcdInit()
{
   lcdSerial.write(0x68);
   lcdSerial.write(0x6a);
   lcdSerial.write(0xf5);
   lcdSerial.write(0xaf);
   lcdSerial.write(0xbf);
   lcdSerial.write(0xb3);
   lcdSerial.write(0xb2);
   lcdSerial.write(0xc1);
   lcdSerial.write(0xdb);
   lcdSerial.write(0xb3);
   lcdSerial.write(0xe9);
}

state lcdCommand(byte cmd, byte num, byte loc, byte len, byte (&data)[OBD2_BUFFER_LENGTH])
{
   byte crc = (0xFF - (cmd + num + loc + len - 0x01)); // checksum FF - (cmd + num + loc + len - 0x01)

   unsigned long timeOut = millis() + 250; // timeout @ 250 ms
   // Not needed, all arrays passed to this function have been initialised as all zeros
   // memset(data, 0, sizeof(data) * OBD2_BUFFER_LENGTH);

   lcdSerial.listen();

   lcdSerial.write(cmd);  // header/cmd read memory ??
   lcdSerial.write(num);  // num of bytes to send
   lcdSerial.write(loc);  // address
   lcdSerial.write(len);  // num of bytes to read
   lcdSerial.write(crc);  // checksum
   
   int i = 0;
   while (i < (len+3) && millis() < timeOut)
   {
      if (lcdSerial.available())
      {
         data[i] = lcdSerial.read();
         i++;
      }
   }
   if (data[0] != 0x00 && data[1] != (len+3))
   { // or use checksum?
      return DATA_ERROR; // error
   }
   if (i < (len+3))
   { // timeout
      return DATA_ERROR; // error
   }
   return DATA; // success
}

void procbtSerial(void)
{
   // initialise all arrays as zeros
   char btdata1[OBD2_BUFFER_LENGTH] = {0};  // bt data in buffer
   char btdata2[OBD2_BUFFER_LENGTH] = {0};  // bt data out buffer
   byte lcddata[OBD2_BUFFER_LENGTH] = {0};  // lcd data buffer

   state response = OK; // Default response
   
   uint8_t command_length = 0;
   
   // clear the command string
   //memset(btdata1, 0, sizeof(btdata1) * OBD2_BUFFER_LENGTH);
   
   // get the command string
   while (command_length < 20)
   {
      if (btSerial.available())
      {
         btdata1[command_length] = toupper(btSerial.read());
         if (btdata1[command_length] == '\r')
         { // terminate at \r
            btdata1[command_length] = '\0';
            break;
         }
         else if (btdata1[command_length] != ' ')
         { // ignore space
            ++command_length;
         }
      }
   }
   
   DebugPrint(F("BT Command :>"));
   DebugPrint(btdata1);
   DebugPrint(F("< Length = "));
   DebugPrintln(command_length);
   
   // clear the response string
   //memset(btdata2, 0, sizeof(btdata2) * OBD2_BUFFER_LENGTH);
   
   /// ELM327 (Hayes modem style) AT commands
   if (!strcmp(btdata1, "ATD"))
   {
      // response = OK; // Note: this is the default
   }
   //               print id / general | reset all / general
   else if ( (!strcmp(btdata1, "ATI")) | (!strcmp(btdata1, "ATZ")) )
   {
      response = RESET;
   }
   // echo on/off / general
   else if (strstr(btdata1, "ATE"))
   {
      elm_echo = (btdata1[3] == '1' ? true : false);
   }
   // line feed on/off / general
   else if (strstr(btdata1, "ATL"))
   {
      elm_linefeed = (btdata1[3] == '1' ? true : false);
   }
   // memory on/off / general
   else if (strstr(btdata1, "ATM"))
   {
      elm_memory = (btdata1[3] == '1' ? true : false);
   }
   // space on/off / obd
   else if (strstr(btdata1, "ATS"))
   {
      elm_space = (btdata1[3] == '1' ? true : false);
   }
   // headers on/off / obd
   else if (strstr(btdata1, "ATH"))
   {
      elm_header = (btdata1[3] == '1' ? true : false);
   }
   // set protocol to ? and save it / obd
   else if (!strcmp(btdata1, "ATSP"))
   {
      //elm_protocol = atoi(data[4]);
   }
   // pin 13 test
   else if (strstr(btdata1, "AT13"))
   {
      if (btdata1[4] == 'T')
      {
         pin_13 = !pin_13;
      }
      else
      {
         pin_13 = (bool)btdata1[4];
      }
      
      if (pin_13 == false)
      {
         digitalWrite(13, LOW);
      }
      else if (pin_13 == true)
      {
         digitalWrite(13, HIGH);
      }
   }
   // read voltage in float / volts
   else if (!strcmp(btdata1, "ATRV"))
   {
      // Read 1.1V reference against AVcc
      // set the reference to Vcc and the measurement to the internal 1.1V reference
      #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
      ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
      #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
      ADMUX = _BV(MUX5) | _BV(MUX0);
      #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
      ADMUX = _BV(MUX3) | _BV(MUX2);
      #else
      ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
      #endif
      
      delay(2); // Wait for Vref to settle
      ADCSRA |= _BV(ADSC); // Start conversion
      while (bit_is_set(ADCSRA,ADSC)); // measuring
      
      uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH
      uint8_t high = ADCH; // unlocks both
      
      long vcc = (high<<8) | low;
      
      //result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
      vcc = 1125.3 / vcc; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
      
      // kerpz haxx
      float R1 = 680000.0; // Resistance of R1
      float R2 = 220000.0; // Resistance of R2
      float volt2 = (((analogRead(14) * vcc) / 1024.0) / (R2/(R1+R2))) * 10.0; // conversion & voltage divider
      
      sprintf_P(btdata2, PSTR("%2d.0V\r\n>"), (uint8_t)volt2);
      response = DATA;
   }
   // sprintf_P(cmd_str, PSTR("%02X%02X\r"), mode, pid);
   // sscanf(data, "%02X%02X", mode, pid)
   // reset dtc/ecu honda
   // 21 04 01 DA / 01 03 FC
   
   // clear dtc / stored values SNEEZY NOTE - OBD2 Mode 04 (clear DTC memory and MIL)
   else if (!strcmp(btdata1, "04"))
   {
      response = lcdCommand(0x21, 0x04, 0x01, 0x00, lcddata); // reset ecu
   }
   // Numeric (HEX) command
   ///   01XX : OBD2 Mode 1 commands
   /// 02FFXX : OBD2 custom Extended PID commands
   else
   {
      uint16_t OBD2_Command = CharArrayToDec(btdata1, command_length);
      
      DebugPrintln(F("##########"));
      DebugPrint(F("OBD2 Command : "));
      DebugPrint(btdata1);
      DebugPrint(F(" => "));
      DebugPrintHex( OBD2_Command);
      DebugPrintln("");
      DebugPrintln(F("##########"));
      
      float Temperature = 0;
      
      response = DATA; // Change default response to DATA
      
      switch (OBD2_Command)
      {
         ///   01XX : OBD2 Mode 1 commands
         ///
         // Requests 4 byte indicating if any of the next 32 PIDs are available (used by the ECU).
         // Programmer to calculate https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_00
         case 0x0100:
         sprintf_P(btdata2, PSTR("41 00 BE 3E B0 11\r\n>"));
         // dtc / AA BB CC DD / A7 = MIL on/off, A6-A0 = DTC_CNT
         break;
         case 0x0101:
         response = lcdCommand(0x20, 0x05, 0x0B, 0x01, lcddata);
         if (response == DATA)
         {
            byte a = ((lcddata[2] >> 5) & 1) << 7; // get bit 5 on lcddata[2], set it to a7
            sprintf_P(btdata2, PSTR("41 01 %02X 00 00 00\r\n>"), a);
         }
         break;
         case 0x0103: // fuel system status / 01 00 ???
         //response = lcdCommand(0x20, 0x05, 0x0F, 0x01, lcddata);
         //if (response == DATA)
         //{ // flags
         //  byte a = lcddata[2] & 1; // get bit 0 on lcddata[2]
         //  a = (lcddata[2] == 1 ? 2 : 1); // convert to comply obd2
         //  sprintf_P(btdata2, PSTR("41 03 %02X 00\r\n>"), a);
         // }
         response = lcdCommand(0x20, 0x05, 0x9a, 0x02, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("41 03 %02X %02X\r\n>"), lcddata[2], lcddata[3]);
         }
         break;
         case 0x0104: // engine load (%)
         response = lcdCommand(0x20, 0x05, 0x9c, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("41 04 %02X\r\n>"), lcddata[2]);
         }
         break;
         case 0x0105: // ect (°C)
         response = lcdCommand(0x20, 0x05, 0x10, 0x01, lcddata);
         if (response == DATA)
         {
            float f = lcddata[2];
            f = 155.04149 - f * 3.0414878 + pow(f, 2) * 0.03952185 - pow(f, 3) * 0.00029383913 + pow(f, 4) * 0.0000010792568 - pow(f, 5) * 0.0000000015618437;
            lcddata[2] = round(f) + 40; // A-40
            sprintf_P(btdata2, PSTR("41 05 %02X\r\n>"), lcddata[2]);
         }
         break;
         case 0x0106: // short FT (%)
         response = lcdCommand(0x20, 0x05, 0x20, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("41 06 %02X\r\n>"), lcddata[2]);
         }
         break;
         case 0x0107: // long FT (%)
         response = lcdCommand(0x20, 0x05, 0x22, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("41 07 %02X\r\n>"), lcddata[2]);
         }
         break;
         // case 0x010A: // fuel pressure
         //btSerial.print("41 0A EF\r\n");
         //response = DATA;
         //break;
         case 0x010B: // map (kPa)
         response = lcdCommand(0x20, 0x05, 0x12, 0x01, lcddata);
         if (response == DATA)
         {
            lcddata[2] = (lcddata[2] * 69)/100;   //Sneezy Note - Convert what seems to be 10xPSI into kPa for OBD2 compatibility (PSI to kPa = PSI x 6.9).
            sprintf_P(btdata2, PSTR("41 0B %02X\r\n>"), lcddata[2]);

         }
         break;
         case 0x010C: // rpm
         response = lcdCommand(0x20, 0x05, 0x00, 0x02, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("41 0C %02X %02X\r\n>"), lcddata[2], lcddata[3]);
         }
         break;
         case 0x010D: // vss (km/h)
         response = lcdCommand(0x20, 0x05, 0x02, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("41 0D %02X\r\n>"), lcddata[2]);
         }
         break;
         case 0x010E: // timing advance (°)
         response = lcdCommand(0x20, 0x05, 0x26, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("41 0E %02X\r\n>"), lcddata[2]);
         }
         break;
         case 0x010F: // iat (°C)
         response = lcdCommand(0x20, 0x05, 0x11, 0x01, lcddata);
         if (response == DATA)
         {
            float f = lcddata[2];
            f = 155.04149 - f * 3.0414878 + pow(f, 2) * 0.03952185 - pow(f, 3) * 0.00029383913 + pow(f, 4) * 0.0000010792568 - pow(f, 5) * 0.0000000015618437;
            lcddata[2] = round(f) + 40; // A-40
            sprintf_P(btdata2, PSTR("41 0F %02X\r\n>"), lcddata[2]);
         }
         break;
         case 0x0111: // tps (%)
         response = lcdCommand(0x20, 0x05, 0x14, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("41 11 %02X\r\n>"), lcddata[2]);
         }
         break;
         case 0x0113: // o2 sensor present ???
         sprintf_P(btdata2, PSTR("41 13 80\r\n>")); // 10000000 / assume bank 1 present
         break;
         case 0x0114: // o2 (V)
         response = lcdCommand(0x20, 0x05, 0x15, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("41 14 %02X FF\r\n>"), lcddata[2]);
         }
         break;
         case 0x011C: // obd2
         sprintf_P(btdata2, PSTR("41 1C 01\r\n>"));
         break;
         case 0x0120:
         //SNEEZY NOTE - forth byte of "01h" just flags that the next set of 32 bit flags is used (with some bits valid)
         sprintf_P(btdata2, PSTR("41 20 00 00 00 01\r\n>"));
         break;
         case 0x012F: // fuel level (%)
         //  sprintf_P(btdata2, PSTR("41 2F FF\r\n>")); // max
         break;
         case 0x0130:
         //SNEEZY NOTE - forth byte of "01h" just flags that the next set of 32 bit flags is used (with some bits valid), first byte has some valid flags.
         sprintf_P(btdata2, PSTR("41 30 20 00 00 01\r\n>"));
         break;
         case 0x0133: // baro (kPa)
         response = lcdCommand(0x20, 0x05, 0x13, 0x01, lcddata);
         if (response == DATA)
         {
            lcddata[2] = (lcddata[2] * 69)/100;   //Sneezy Note - Convert what seems to be 10xPSI into kPa for OBD2 compatibility (PSI to kPa = PSI x 6.9).
            sprintf_P(btdata2, PSTR("41 33 %02X\r\n>"), lcddata[2]);
         }
         break;
         case 0x0140:
         //SNEEZY NOTE - first byte has some valid flags, also one in the last byte, no further supported PIDs.
         sprintf_P(btdata2, PSTR("41 40 48 00 00 10\r\n>"));
         break;
         case 0x0142: // ecu voltage (V)
         response = lcdCommand(0x20, 0x05, 0x17, 0x01, lcddata);
         if (response == DATA)
         {
            float f = lcddata[2];
            f = f / 10.45;
            unsigned int u = f * 1000; // ((A*256)+B)/1000
            sprintf_P(btdata2, PSTR("41 42 %02X %02X\r\n>"), highByte(u), lowByte(u));
         }
         break;
         case 0x0145: // iacv / relative throttle position
         response = lcdCommand(0x20, 0x05, 0x28, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("41 45 %02X\r\n>"), lcddata[2]);
         }
         break;
         // Added for Engine Oil Temp sensor (my external One Wire DS18B20)
         case 0x015C: // EOT - Engine Oil Temp
         // OBD2 sensor conversion EOT = A - 40, so add 40 to balance it, + 0.5 for rounding up
         Temperature = getTemperature(Sensor1_Thermometer) + 40.5;
         sprintf_P(btdata2, PSTR("41 5C %02X\r\n>"), (byte)Temperature);	// Send the sensor temp; typecast from a float to an uint8_t / byte / unsigned char
         response = DATA;
         break;
         /// OBD2 custom Extended PID commands
         ///
         // NOTE : Had to go to THREE BYTES because two byte custom PIDs are too short for
         // Torque Pros Extended PID parsing (so added FF in the middle of the response)
         //
         //  All 0x20XX commands below are actually 0x20FFXX, the FF is ignored for this switch statement
         // (See CharArrayToDec() function )
         // Additional custom PIDs for Dallas temperature sensor
         case 0x2002: // Gear Temperature
         Temperature = getTemperature(Sensor2_Thermometer);   //OBD2 sensor conversion (no conversion)
         sprintf_P(btdata2, PSTR("60 FF 02 %02X\r\n>"), (byte)Temperature);  // Send the sensor temp; typecast from a float to an uint8_t / byte / unsigned char
         response = DATA;
         break;
         case 0x2003: //  Diff Temperature
         Temperature = getTemperature(Sensor3_Thermometer);   //OBD2 sensor conversion (no conversion)
         sprintf_P(btdata2, PSTR("60 FF 03 %02X\r\n>"), (byte)Temperature);  // Send the sensor temp; typecast from a float to an uint8_t / byte / unsigned char
         response = DATA;
         break;
         case 0x2008: // custom hobd mapping / flags
         response = lcdCommand(0x20, 0x05, 0x08, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("60 FF 08 %02X\r\n>"), lcddata[2]);
         }
         break;
         case 0x2009: // custom hobd mapping / flags
         response = lcdCommand(0x20, 0x05, 0x09, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("60 FF 09 %02X\r\n>"), lcddata[2]);
         }
         break;
         case 0x200A:  // custom hobd mapping / flags
         response = lcdCommand(0x20, 0x05, 0x0A, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("60 FF 0A %02X\r\n>"), lcddata[2]);
         }
         break;
         case 0x200B: // custom hobd mapping / flags
         //sprintf_P(btdata2, PSTR("60 0C AA\r\n>")); // 10101010 / test data
         response = lcdCommand(0x20, 0x05, 0x0B, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("60 FF 0B %02X\r\n>"), lcddata[2]);
         }
         break;
         case 0x200C:  // custom hobd mapping / flags
         response = lcdCommand(0x20, 0x05, 0x0C, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("60 FF 0C %02X\r\n>"), lcddata[2]);
         }
         break;
         // custom hobd mapping / flags
         case 0x200F:
         response = lcdCommand(0x20, 0x05, 0x0F, 0x01, lcddata);
         if (response == DATA)
         {
            sprintf_P(btdata2, PSTR("60 FF 0F %02X\r\n>"), lcddata[2]);
         }
         break;
         default:
         response = DATA_ERROR;
      }
   }
   
   switch(response)
   {
      case RESET:
      sprintf_P(btdata2, PSTR("Honda OBD v1.0\r\n>")); break;
      case OK:
      sprintf_P(btdata2, PSTR("OK\r\n>")); break;
      case DATA_ERROR:
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>")); break;
      default: // Default case is DATA - btdata2 is already set
      break;
   }
   
   DebugPrint(F("BT Response >"));
   DebugPrint(btdata2);
   DebugPrintln(F("<"));
   
   if (btdata2 != NULL) bt_write(btdata2); // send reply
}

void lcdPaddedPrint(const uint16_t &in, const uint16_t &len, const bool zero_pad = false)
{
   uint16_t i = in;
   switch (len)
   {
      case 4:
      i %= 10000;
      (i / 1000) ? lcd.print(i / 1000) : (zero_pad & (i < 1000)) ? lcd.print(0) : lcd.print(" ");
      case 3:
      i %= 1000;
      (i / 100) ? lcd.print(i / 100) : (zero_pad & (i < 100)) ? lcd.print(0) : lcd.print(" ");
      case 2:
      i %= 100;
      (i / 10) ? lcd.print(i / 10) : (zero_pad & (i < 10)) ? lcd.print(0) : lcd.print(" ");
      default:
      i %= 10;
      lcd.print(i);
   }
}

void lcdClearSection(const uint8_t &col, const uint8_t &row, const uint8_t &length, const bool &moveFirst = false)
{
   if (moveFirst)
   {
      lcd.setCursor(col,row);
   }
   for (uint8_t i=0;i<length;++i)
   {
      lcd.print(" ");
   }
   lcd.setCursor(col,row);
}

void proclcdSerial(void)
{
   //char h_initobd2[12] = {0x68,0x6a,0xf5,0xaf,0xbf,0xb3,0xb2,0xc1,0xdb,0xb3,0xe9}; // 200ms - 300ms delay
   //byte h_cmd1[6] = {0x20,0x05,0x00,0x10,0xcb}; // row 1
   //byte h_cmd2[6] = {0x20,0x05,0x10,0x10,0xbb}; // row 2
   //byte h_cmd3[6] = {0x20,0x05,0x20,0x10,0xab}; // row 3
   //byte h_cmd4[6] = {0x20,0x05,0x76,0x0a,0x5b}; // ecu id
   // Initialise data as all zeros
   byte data[OBD2_BUFFER_LENGTH] = {0};  //Received data from ECU has two byte header, data begins at 3rd byte [2]
   unsigned int rpm=0,vss=0,ect=0,iat=0,maps=0,baro=0,tps=0,volt=0, imap=0;

   if (lcdCommand(0x20,0x05,0x00,0x10,data))
   { // row 1
      //rpm = 1875000 / (data[2] * 256 + data[3] + 1); // OBD1
      rpm = (data[2] * 256 + data[3]) / 4; // OBD2
      vss = data[4];
   }
   
   if(lcdCommand(0x20,0x05,0x10,0x10,data))
   { // row2
      float f;
      f = data[2];
      f = 155.04149 - f * 3.0414878 + pow(f, 2) * 0.03952185 - pow(f, 3) * 0.00029383913 + pow(f, 4) * 0.0000010792568 - pow(f, 5) * 0.0000000015618437;
      ect = round(f);
      f = data[3];
      f = 155.04149 - f * 3.0414878 + pow(f, 2) * 0.03952185 - pow(f, 3) * 0.00029383913 + pow(f, 4) * 0.0000010792568 - pow(f, 5) * 0.0000000015618437;
      iat = round(f);
      maps = data[4]; // data[4] * 0.716-5
      baro = data[5]; // data[5] * 0.716-5 // UNUSED!
      tps = data[6]; // (data[6] - 24) / 2;
      f = data[9];
      f = (f / 10.45) * 10.0; // cV
      volt = round(f); // UNUSED!
      //alt_fr = data[10] / 2.55
      //eld = 77.06 - data[11] / 2.5371
   }
   
   // IMAP = RPM * MAP / IAT / 2
   // MAF = (IMAP/60)*(VE/100)*(Eng Disp)*(MMA)/(R)
   // Where: VE = 80% (Volumetric Efficiency), R = 8.314 J/°K/mole, MMA = 28.97 g/mole (Molecular mass of air)
   imap = (rpm * maps) / (iat + 273);
   // ve = 75, ed = 1.5.95, afr = 14.7
   float maf = (imap / 120) * (80 / 100) * 1.595 * 28.9644 / 8.314472; // UNUSED!
   
   // Read 1.1V reference against AVcc
   // set the reference to Vcc and the measurement to the internal 1.1V reference
   #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
   ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
   #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
   ADMUX = _BV(MUX5) | _BV(MUX0);
   #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
   ADMUX = _BV(MUX3) | _BV(MUX2);
   #else
   ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
   #endif
   
   delay(2); // Wait for Vref to settle
   ADCSRA |= _BV(ADSC); // Start conversion
   while (bit_is_set(ADCSRA,ADSC)); // measuring
   
   uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH
   uint8_t high = ADCH; // unlocks both
   
   long vcc = (high<<8) | low;
   
   //result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
   vcc = 1125.3 / vcc; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
   
   // kerpz haxx
   float R1 = 680000.0; // Resistance of R1
   float R2 = 220000.0; // Resistance of R2
   unsigned int volt2 = (((analogRead(14) * vcc) / 1024.0) / (R2/(R1+R2))) * 10.0; // conversion & voltage divider
   //temp = ((analogRead(pinTemp) * vcc) / 1024.0) * 100.0; // LM35 celcius
   
   //Use Kerpz original screen - with space padding
   if (Screen_Number == false)
   {  // lcd display format
      //           111111
      // 0123456789012345
      // R0000 S000 V00.0
      // E00 I00 M000 T00
      unsigned short i = 0;
      
      // Line 1
      lcdClearSection(0,0,6,true);
      lcd.print("R");
      lcdPaddedPrint(rpm,4);
      
      lcdClearSection(6,0,5);
      lcd.print("S");
      lcdPaddedPrint(vss,3);
      
      lcdClearSection(11,0,5);
      lcd.print("V");
      lcdPaddedPrint(volt2/10,2);
      lcd.print(".");
      lcd.print(volt2 % 10);

      // Line 2
      lcdClearSection(0,1,4,true);
      lcd.print("E");
      lcdPaddedPrint(ect,2);

      lcdClearSection(4,1,4);
      lcd.print("I");
      lcdPaddedPrint(iat,2);

      lcdClearSection(8,1,5);
      lcd.print("M");
      lcdPaddedPrint(maps,3);
      
      lcdClearSection(13,1,3);
      lcd.print("T");
      lcdPaddedPrint(tps,2);
   }


   //SNEEZY HACKING -
   if (Screen_Number == true)
   {  //Use SNEEZY screen No2

      float Oil_Temp = getTemperature(Sensor1_Thermometer);
      
      // set the cursor to column 0, line 1
      // (note: line 1 is the second row, since counting begins with 0):
      lcd.setCursor(0, 0);
      lcd.print(F("Oil "));
      lcd.print(Oil_Temp, 0);
      lcd.print((char)223); // This display uses a HITACHI ascii table - : http://www.electronic-engineering.ch/microchip/datasheets/lcd/charset.gif

      float Diff_Temp = getTemperature(Sensor2_Thermometer);
      
      // set the cursor to column 0, line 1
      // (note: line 1 is the second row, since counting begins with 0):
      lcd.setCursor(7, 0);
      lcd.print(F(" Dif "));
      lcd.print(Diff_Temp, 0);
      lcd.print((char)223);

      lcd.print(" "); //clear remaining row LCD chars (to avoid use of lcd.clear() as it flickers the screen).

      float Gear_Temp = getTemperature(Sensor3_Thermometer);
      
      // set the cursor to column 0, line 1
      // (note: line 1 is the second row, since counting begins with 0):
      lcd.setCursor(0, 1);
      lcd.print(F("Gbox "));
      lcd.print(Gear_Temp, 1);
      lcd.print((char)223);

      lcd.print(F("      ")); //clear remaining LCD chars (to avoid use of lcd.clear() as it flickers the screen).
      
      digitalWrite(ledPin, !digitalRead(ledPin));  //toggle LED pin for DEBUG
   }
}

void My_Buttons()
{
   const int CURRENT_BUTTON_STATE  = digitalRead(buttonPin);
   if (CURRENT_BUTTON_STATE != Old_Button_State && CURRENT_BUTTON_STATE == HIGH)
   {
      Led_State = (Led_State == LOW) ? HIGH : LOW;
      //  Screen_Number = (Screen_Number == LOW) ? HIGH : LOW;
      Screen_Number = !Screen_Number;  //Toggle screen bool
      digitalWrite(ledPin, Led_State);
      //delay(50);
   }
   Old_Button_State = CURRENT_BUTTON_STATE;
}

float getTemperature(DeviceAddress deviceAddress)
{
   float tempC = sensors.getTempC(deviceAddress);
   if (tempC == -127.00)
   {
      //Serial.print("Error getting temperature");
      // set the cursor to column 0, line 1
      // (note: line 1 is the second row, since counting begins with 0):
      // This is a debug statement, so ive included in a #define guard
      #if ( _DEBUG == 1 )
      lcd.clear();
      lcd.print(F("Error getting   "));
      lcd.setCursor(0, 1);
      lcd.print(F("temperature     "));
      delay(2000);
      lcd.clear();
      #endif // _DEBUG
   }
   
   return tempC;
}

// Convert 4 digit character array to its decimal value
// i.e. CharArrayToDec("23A3") = 9123
uint16_t CharArrayToDec(const char (&in)[OBD2_BUFFER_LENGTH], const uint8_t length)
{
   if (length == 4) // length = 4 : OBD2 Mode 1 commands
   {
      // "0101" -> 0x0101
      return ( CharToDec(in[0])*4096 + /* 16*16*16 = 4096 */
      CharToDec(in[1])*256 +           /* 16*16 = 256 */
      CharToDec(in[2])*16 +
      CharToDec(in[3]) );
   }
   else if (length == 6) // length = 6 : OBD2 custom Extended PID commands
   {
      // "20FF08" -> 0x2008
      return ( CharToDec(in[0])*4096 + /* 16*16*16 = 4096 */
      CharToDec(in[1])*256 +           /* 16*16 = 256 */
      CharToDec(in[4])*16 +
      CharToDec(in[5]) );
   }
   else
   {
      return 0x0000;
   }
}
// Convert a single hex character to its decimal value
// i.e. CharToDec("A") = 10
uint8_t CharToDec(const char &in)
{
   return (in > '9')?(10 + in - 'A'):(in - '0');
}

void setup()
{
   #if ( _DEBUG == 1 )
   Serial.begin(9600);
   #endif
   btSerial.begin(9600);
   lcdSerial.begin(9600);

   delay(100);
   lcdInit();
   delay(300);

   // set up the LCD's number of columns and rows:
   lcd.begin(16, 2);

   lcd.clear();
   lcd.setCursor(0, 0);
   lcd.print(F("Honda OBD v1.0"));
   lcd.setCursor(0,1);
   lcd.print(F("LCD 16x2 Mode"));
   delay(1000);
   
   // Start up the Dallas sensor library
   sensors.begin();
   // set the resolution to 9 bit (good enough?)
   sensors.setResolution(Sensor1_Thermometer, 9);
   sensors.setResolution(Sensor2_Thermometer, 9);
   sensors.setResolution(Sensor3_Thermometer, 9);
   
   // Async type request for temps takes only 2mS, programmer then must take following conversion time into account (and do more useful stuff while waiting).
   sensors.setWaitForConversion(false);  // Makes it async and saves delay time.

   //MS - Button stuff
   // initialize the LED pin as an output:
   pinMode(ledPin, OUTPUT);
   // initialize the pushbutton pin as an input:
   pinMode(buttonPin, INPUT);
   
   // Start the bluetooth listening
   btSerial.listen();
   
   t0 = millis();
   DebugPrintln(F("Setup"));
}

void loop()
{
   //SNEEZY HACKING -
   sensors.requestTemperatures();
   My_Buttons();   //My button check for screen change
   //END SNEEZY HACKING
   
   // LCD mode with 300ms bluetooth sniff
   if (!elm_mode)
   {
      proclcdSerial();
      DebugPrintln(F("LCD Mode"));
      
      btSerial.listen();
      delay(300);
      DebugPrint(F("BT Available? "));
      if (btSerial.available())
      {
         DebugPrintln(F("Yes!"));
         
         elm_mode = true;
         lcd.clear();
         lcd.setCursor(0, 0);
         lcd.print(F("Honda OBD v1.0"));
         lcd.setCursor(0,1);
         lcd.print(F("Bluetooth Mode"));
         
         DebugPrintln(F("BT Mode"));
         
         procbtSerial();
         
         btSerial.listen();
         t0 = millis();
         tLastBluetooth = millis();
      }
      else
      {
         DebugPrintln(F("No!"));
      }
   }
   // Bluetooth mode
   else if (millis() - t0 > 300)
   {
      DebugPrint(F("BT Available? "));
      if (btSerial.available())
      {
         DebugPrintln(F("Yes!"));
         procbtSerial();
         tLastBluetooth = millis();
      }
      else
      {
         DebugPrintln(F("No!"));
      }
      btSerial.listen();
      
      t0 = millis();
   }
   // If no bluetooth in the last 5 seconds then switch back to LCD mode / Bluetooth search
   else if (millis() - tLastBluetooth > 5000)
   {
      elm_mode = false;
      DebugPrintln(F("No bluetooth in 5s\nEntering LCD mode / Bluetooth sniff"));
   }
}
