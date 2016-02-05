/*
Author:
- Philip Bordado (kerpz@yahoo.com)
Hardware:
- HC-05 Bluetooth module at pin 10 (Rx) pin 11 (Tx)
- DLC(K-line) at pin 12
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

#define _DEBUG 1 // COOKIE : 
#include <Arduino.h> // COOKIE : Required for Atmel Studio / Visual Studio (stops it underlining Arduino inbuilt functions, macros etc. as errors)
#include <LiquidCrystal.h>
#include <SoftwareSerialWithHalfDuplex.h>

//SNEEZY HACKING - Additions for 3x Dallas 1-wire temp sensors on a bus(async mode).
#include <OneWire.h> // COOKIE : https://github.com/PaulStoffregen/OneWire
#include <DallasTemperature.h> // COOKIE : https://github.com/milesburton/Arduino-Temperature-Control-Library

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
const int buttonPin = 3;      // the number of the pushbutton pin
const int ledPin =  13;       // the number of the LED pin

int Led_State = LOW;          // the current state of the output pin
int Old_Button_State = LOW;   // the previous reading from the input pin
//int Screen_Number = LOW;      // the LCD screen to show
boolean Screen_Number = false;   // the LCD screen to show
//int DS18B20_1 = 0;               //global variable for ext temp sensor.

//Variables for temperature sensor readings and calculations.

// COOKIE : Im converting these to local variables instead of global variables
//float Temp_Var;
//float Oil_Temp;
//float Gear_Temp;
//float Diff_Temp;
//End SNEEZY HACKING

SoftwareSerialWithHalfDuplex btSerial(10, 11); // RX, TX
SoftwareSerialWithHalfDuplex dlcSerial(12, 12, false, false);

bool elm_mode = false;
bool elm_memory = false;
bool elm_echo = false;
bool elm_space = false;
bool elm_linefeed = false;
bool elm_header = false;
bool pin_13 = false;
int  elm_protocol = 0; // auto

void bt_write(char *str)
{
   while (*str != '\0')
   btSerial.write(*str++);
}

void dlcInit()
{
   dlcSerial.write(0x68);
   dlcSerial.write(0x6a);
   dlcSerial.write(0xf5);
   dlcSerial.write(0xaf);
   dlcSerial.write(0xbf);
   dlcSerial.write(0xb3);
   dlcSerial.write(0xb2);
   dlcSerial.write(0xc1);
   dlcSerial.write(0xdb);
   dlcSerial.write(0xb3);
   dlcSerial.write(0xe9);
   delay(300);
}

int dlcCommand(byte cmd, byte num, byte loc, byte len, byte data[])
{
   byte crc = (0xFF - (cmd + num + loc + len - 0x01)); // checksum FF - (cmd + num + loc + len - 0x01)

   unsigned long timeOut = millis() + 250; // timeout @ 250 ms
   memset(data, 0, sizeof(data));

   dlcSerial.listen();

   dlcSerial.write(cmd);  // header/cmd read memory ??
   dlcSerial.write(num);  // num of bytes to send
   dlcSerial.write(loc);  // address
   dlcSerial.write(len);  // num of bytes to read
   dlcSerial.write(crc);  // checksum
   
   int i = 0;
   while (i < (len+3) && millis() < timeOut)
   {
      if (dlcSerial.available())
      {
         data[i] = dlcSerial.read();
         i++;
      }
   }
   if (data[0] != 0x00 && data[1] != (len+3))
   { // or use checksum?
      return 0; // error
   }
   if (i < (len+3))
   { // timeout
      return 0; // error
   }
   return 1; // success
}

void procbtSerial(void)
{
   char btdata1[20];  // bt data in buffer
   char btdata2[20];  // bt data out buffer
   byte dlcdata[20];  // dlc data buffer
   
   int i = 0;

   memset(btdata1, 0, sizeof(btdata1));
   while (i < 20)
   
   {
      if (btSerial.available())
      {
         btdata1[i] = toupper(btSerial.read());
         if (btdata1[i] == '\r')
         { // terminate at \r
            btdata1[i] = '\0';
            break;
         }
         else if (btdata1[i] != ' ')
         { // ignore space
            ++i;
         }
      }
   }

   memset(btdata2, 0, sizeof(btdata2));

   if (!strcmp(btdata1, "ATD"))
   {
      sprintf_P(btdata2, PSTR("OK\r\n>"));
   }
   else if (!strcmp(btdata1, "ATI"))
   { // print id / general
      sprintf_P(btdata2, PSTR("Honda OBD v1.0\r\n>"));
   }
   else if (!strcmp(btdata1, "ATZ"))
   { // reset all / general
      sprintf_P(btdata2, PSTR("Honda OBD v1.0\r\n>"));
   }
   else if (strstr(btdata1, "ATE"))
   { // echo on/off / general
      elm_echo = (btdata1[3] == '1' ? true : false);
      sprintf_P(btdata2, PSTR("OK\r\n>"));
   }
   else if (strstr(btdata1, "ATL"))
   { // linfeed on/off / general
      elm_linefeed = (btdata1[3] == '1' ? true : false);
      sprintf_P(btdata2, PSTR("OK\r\n>"));
   }
   else if (strstr(btdata1, "ATM"))
   { // memory on/off / general
      elm_memory = (btdata1[3] == '1' ? true : false);
      sprintf_P(btdata2, PSTR("OK\r\n>"));
   }
   else if (strstr(btdata1, "ATS"))
   { // space on/off / obd
      elm_space = (btdata1[3] == '1' ? true : false);
      sprintf_P(btdata2, PSTR("OK\r\n>"));
   }
   else if (strstr(btdata1, "ATH"))
   { // headers on/off / obd
      elm_header = (btdata1[3] == '1' ? true : false);
      sprintf_P(btdata2, PSTR("OK\r\n>"));
   }
   else if (!strcmp(btdata1, "ATSP"))
   { // set protocol to ? and save it / obd
      //elm_protocol = atoi(data[4]);
      sprintf_P(btdata2, PSTR("OK\r\n>"));
   }
   else if (strstr(btdata1, "AT13"))
   { // pin 13 test
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
      sprintf_P(btdata2, PSTR("OK\r\n>"));
   }
   else if (!strcmp(btdata1, "ATRV"))
   { // read voltage in float / volts
      btSerial.print("12.0V\r\n>");
      //sprintf_P(btdata2, PSTR("%dV\r\n>"), volt2);
   }
   // sprintf_P(cmd_str, PSTR("%02X%02X\r"), mode, pid);
   // sscanf(data, "%02X%02X", mode, pid)
   // reset dtc/ecu honda
   // 21 04 01 DA / 01 03 FC
   else if (!strcmp(btdata1, "04"))
   { // clear dtc / stored values SNEEZY NOTE - OBDII Mode 04 (clear DTC memory and MIL)
      dlcCommand(0x21, 0x04, 0x01, 0x00, dlcdata); // reset ecu
      sprintf_P(btdata2, PSTR("OK\r\n>"));
   }
   else if (!strcmp(btdata1, "0100"))
   {	//SNEEZY NOTE - Requests 4 byte indicating if any of the next 32 PIDs are available (used by the ECU).
      sprintf_P(btdata2, PSTR("41 00 BE 3E B0 11\r\n>"));  //SNEEZY NOTE - Programmer to calculate https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_00
   }
   else if (!strcmp(btdata1, "0101"))
   { // dtc / AA BB CC DD / A7 = MIL on/off, A6-A0 = DTC_CNT
      if (dlcCommand(0x20, 0x05, 0x0B, 0x01, dlcdata))
      {
         byte a = ((dlcdata[2] >> 5) & 1) << 7; // get bit 5 on dlcdata[2], set it to a7
         sprintf_P(btdata2, PSTR("41 01 %02X 00 00 00\r\n>"), a);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   //else if (!strcmp(btdata1, "0102")) // freeze dtc / 00 61 ???
   //{
   //  if (dlcCommand(0x20, 0x05, 0x98, 0x02, dlcdata))
   //{
   //    sprintf_P(btdata2, PSTR("41 02 %02X %02X\r\n>"), dlcdata[2], dlcdata[3]);
   //  }
   //  else
   //{
   //    sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
   //  }
   //}
   else if (!strcmp(btdata1, "0103"))
   { // fuel system status / 01 00 ???
      //if (dlcCommand(0x20, 0x05, 0x0F, 0x01, dlcdata))
      //{ // flags
      //  byte a = dlcdata[2] & 1; // get bit 0 on dlcdata[2]
      //  a = (dlcdata[2] == 1 ? 2 : 1); // convert to comply obd2
      //  sprintf_P(btdata2, PSTR("41 03 %02X 00\r\n>"), a);
      // }
      if (dlcCommand(0x20, 0x05, 0x9a, 0x02, dlcdata))
      {
         sprintf_P(btdata2, PSTR("41 03 %02X %02X\r\n>"), dlcdata[2], dlcdata[3]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "0104"))
   { // engine load (%)
      if (dlcCommand(0x20, 0x05, 0x9c, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("41 04 %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "0105"))
   { // ect (°C)
      if (dlcCommand(0x20, 0x05, 0x10, 0x01, dlcdata))
      {
         float f = dlcdata[2];
         f = 155.04149 - f * 3.0414878 + pow(f, 2) * 0.03952185 - pow(f, 3) * 0.00029383913 + pow(f, 4) * 0.0000010792568 - pow(f, 5) * 0.0000000015618437;
         dlcdata[2] = round(f) + 40; // A-40
         sprintf_P(btdata2, PSTR("41 05 %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "0106"))
   { // short FT (%)
      if (dlcCommand(0x20, 0x05, 0x20, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("41 06 %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "0107"))
   { // long FT (%)
      if (dlcCommand(0x20, 0x05, 0x22, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("41 07 %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   //else if (!strcmp(data, "010A"))
   //{ // fuel pressure
   //  btSerial.print("41 0A EF\r\n");
   //}
   else if (!strcmp(btdata1, "010B"))
   { // map (kPa)
      if (dlcCommand(0x20, 0x05, 0x12, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("41 0B %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "010C"))
   { // rpm
      if (dlcCommand(0x20, 0x05, 0x00, 0x02, dlcdata))
      {
         sprintf_P(btdata2, PSTR("41 0C %02X %02X\r\n>"), dlcdata[2], dlcdata[3]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "010D"))
   { // vss (km/h)
      if (dlcCommand(0x20, 0x05, 0x02, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("41 0D %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "010E"))
   { // timing advance (°)
      if (dlcCommand(0x20, 0x05, 0x26, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("41 0E %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "010F"))
   { // iat (°C)
      if (dlcCommand(0x20, 0x05, 0x11, 0x01, dlcdata))
      {
         float f = dlcdata[2];
         f = 155.04149 - f * 3.0414878 + pow(f, 2) * 0.03952185 - pow(f, 3) * 0.00029383913 + pow(f, 4) * 0.0000010792568 - pow(f, 5) * 0.0000000015618437;
         dlcdata[2] = round(f) + 40; // A-40
         sprintf_P(btdata2, PSTR("41 0F %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "0111"))
   { // tps (%)
      if (dlcCommand(0x20, 0x05, 0x14, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("41 11 %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "0113"))
   { // o2 sensor present ???
      sprintf_P(btdata2, PSTR("41 13 80\r\n>")); // 10000000 / assume bank 1 present
   }
   else if (!strcmp(btdata1, "0114"))
   { // o2 (V)
      if (dlcCommand(0x20, 0x05, 0x15, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("41 14 %02X FF\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "011C"))
   { // obd2
      sprintf_P(btdata2, PSTR("41 1C 01\r\n>"));
   }
   else if (!strcmp(btdata1, "0120"))
   {
      sprintf_P(btdata2, PSTR("41 20 00 00 00 01\r\n>"));	//SNEEZY NOTE - forth byte of "01h" just flags that the next set of 32 bit flags is used (with some bits valid)
   }
   //else if (!strcmp(btdata1, "012F"))
   //{ // fuel level (%)
   //  sprintf_P(btdata2, PSTR("41 2F FF\r\n>")); // max
   //}
   else if (!strcmp(btdata1, "0130"))
   {
      sprintf_P(btdata2, PSTR("41 30 20 00 00 01\r\n>")); //SNEEZY NOTE - forth byte of "01h" just flags that the next set of 32 bit flags is used (with some bits valid), first byte has some valid flags.
   }
   else if (!strcmp(btdata1, "0133"))
   { // baro (kPa)
      if (dlcCommand(0x20, 0x05, 0x13, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("41 33 %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "0140"))
   {
      sprintf_P(btdata2, PSTR("41 40 48 00 00 10\r\n>")); //SNEEZY NOTE - first byte has some valid flags, also one in the last byte, no further supported PIDs.
   }
   else if (!strcmp(btdata1, "0142"))
   { // ecu voltage (V)
      if (dlcCommand(0x20, 0x05, 0x17, 0x01, dlcdata))
      {
         float f = dlcdata[2];
         f = f / 10.45;
         unsigned int u = f * 1000; // ((A*256)+B)/1000
         sprintf_P(btdata2, PSTR("41 42 %02X %02X\r\n>"), highByte(u), lowByte(u));
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "0145"))
   { // iacv / relative throttle position
      if (dlcCommand(0x20, 0x05, 0x28, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("41 45 %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "015C"))
   { // EOT - Engine Oil Temp	SNEEZY NOTE - Added for Engine Oil Temp sensor (my external One Wire DS18B20)
      //printTemperature(Sensor1_Thermometer);
      //Oil_Temp = Temp_Var + 40.5;		//OBDII sensor conversion EOT = A - 40, so add 40 to balance it, + 0.5 for rounding up.
      // COOKIE
      float Oil_Temp = getTemperature(Sensor1_Thermometer) + 40.5;		//OBDII sensor conversion EOT = A - 40, so add 40 to balance it, + 0.5 for rounding up.
      byte _Oil_Temp = (byte)Oil_Temp;
      
      sprintf_P(btdata2, PSTR("41 5C %02X\r\n>"), _Oil_Temp);	// OBDII converted EOT value is in Oil_Temp, typecasting Oil_Temp to an uint8_t / byte / unsigned char
   }
   else if (!strcmp(btdata1, "2008"))
   { // custom hobd mapping / flags
      if (dlcCommand(0x20, 0x05, 0x08, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("60 08 %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "2009"))
   { // custom hobd mapping / flags
      if (dlcCommand(0x20, 0x05, 0x09, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("60 09 %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "200A"))
   { // custom hobd mapping / flags
      if (dlcCommand(0x20, 0x05, 0x0A, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("60 0A %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "200B"))
   { // custom hobd mapping / flags
      //sprintf_P(btdata2, PSTR("60 0C AA\r\n>")); // 10101010 / test data
      if (dlcCommand(0x20, 0x05, 0x0B, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("60 0B %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "200C"))
   { // custom hobd mapping / flags
      if (dlcCommand(0x20, 0x05, 0x0C, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("60 0C %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
      }
   }
   else if (!strcmp(btdata1, "200F"))
   { // custom hobd mapping / flags
      if (dlcCommand(0x20, 0x05, 0x0F, 0x01, dlcdata))
      {
         sprintf_P(btdata2, PSTR("60 0F %02X\r\n>"), dlcdata[2]);
      }
      else
      {
         sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
      }
   }
   else
   {
      sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
   }

   if (btdata2 != NULL) bt_write(btdata2); // send reply
}

void procdlcSerial(void)
{
   //char h_initobd2[12] = {0x68,0x6a,0xf5,0xaf,0xbf,0xb3,0xb2,0xc1,0xdb,0xb3,0xe9}; // 200ms - 300ms delay
   //byte h_cmd1[6] = {0x20,0x05,0x00,0x10,0xcb}; // row 1
   //byte h_cmd2[6] = {0x20,0x05,0x10,0x10,0xbb}; // row 2
   //byte h_cmd3[6] = {0x20,0x05,0x20,0x10,0xab}; // row 3
   //byte h_cmd4[6] = {0x20,0x05,0x76,0x0a,0x5b}; // ecu id
   byte data[20];  //Received data from ECU has two byte header, data begins at 3rd byte [2]
   unsigned int rpm=0,vss=0,ect=0,iat=0,maps=0,baro=0,tps=0,volt=0, imap=0;

   if (dlcCommand(0x20,0x05,0x00,0x10,data))
   { // row 1
      //rpm = 1875000 / (data[2] * 256 + data[3] + 1); // OBD1
      rpm = (data[2] * 256 + data[3]) / 4; // OBD2
      vss = data[4];
   }
   
   if (dlcCommand(0x20,0x05,0x10,0x10,data))
   { // row2
      float f;
      f = data[2];
      f = 155.04149 - f * 3.0414878 + pow(f, 2) * 0.03952185 - pow(f, 3) * 0.00029383913 + pow(f, 4) * 0.0000010792568 - pow(f, 5) * 0.0000000015618437;
      ect = round(f);
      f = data[3];
      f = 155.04149 - f * 3.0414878 + pow(f, 2) * 0.03952185 - pow(f, 3) * 0.00029383913 + pow(f, 4) * 0.0000010792568 - pow(f, 5) * 0.0000000015618437;
      iat = round(f);
      maps = data[4]; // data[4] * 0.716-5
      baro = data[5]; // data[5] * 0.716-5
      tps = data[6]; // (data[6] - 24) / 2;
      f = data[9];
      f = (f / 10.45) * 10.0; // cV
      volt = round(f);
      //alt_fr = data[10] / 2.55
      //eld = 77.06 - data[11] / 2.5371
   }
   
   // IMAP = RPM * MAP / IAT / 2
   // MAF = (IMAP/60)*(VE/100)*(Eng Disp)*(MMA)/(R)
   // Where: VE = 80% (Volumetric Efficiency), R = 8.314 J/°K/mole, MMA = 28.97 g/mole (Molecular mass of air)
   float maf = 0.0;
   imap = (rpm * maps) / (iat + 273);
   // ve = 75, ed = 1.5.95, afr = 14.7
   maf = (imap / 120) * (80 / 100) * 1.595 * 28.9644 / 8.314472;

   
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
   unsigned int volt2 = (((analogRead(14) * vcc) / 1024.0) / (R2/(R1+R2))) * 10.0; // convertion & voltage divider
   //temp = ((analogRead(pinTemp) * vcc) / 1024.0) * 100.0; // LM35 celcius
   
   if (Screen_Number == false)
   {  //Use Kerpz original screen
      // display
      // R0000 S000 V00.0
      // E00 I00    V00.0
      unsigned short i = 0;
      
      lcd.clear();
      lcd.setCursor(0,0);

      lcd.print("R");
      i = rpm;
      lcd.print(i/1000);
      i %= 1000;
      lcd.print(i/100);
      i %= 100;
      lcd.print(i/10);
      i %= 10;
      lcd.print(i);

      lcd.print(" ");
      
      lcd.print("S");
      i = vss;
      lcd.print(i/100);
      i %= 100;
      lcd.print(i/10);
      i %= 10;
      lcd.print(i);

      lcd.print(" ");
      
      lcd.print("V");
      i = volt2;
      lcd.print(i/100);
      i %= 100;
      lcd.print(i/10);
      i %= 10;
      lcd.print(".");
      lcd.print(i);

      lcd.setCursor(0,1);

      lcd.print("E");
      i = ect;
      lcd.print(i/10);
      i %= 10;
      lcd.print(i);

      lcd.print(" ");

      lcd.print("I");
      i = iat;
      lcd.print(i/10);
      i %= 10;
      lcd.print(i);

      lcd.print(" ");

      lcd.print("M");
      i = maps;
      lcd.print(i/100);
      i %= 100;
      lcd.print(i/10);
      i %= 10;
      lcd.print(i);
      
      lcd.print(" ");

      lcd.print("T");
      i = tps;
      lcd.print(i/10);
      i %= 10;
      lcd.print(i);
   }


   //SNEEZY HACKING -
   if (Screen_Number == true)
   {  //Use SNEEZY screen No2

      //printTemperature(Sensor1_Thermometer);
      //Oil_Temp = Temp_Var;
      float Oil_Temp = getTemperature(Sensor1_Thermometer); // COOKIE
      
      // set the cursor to column 0, line 1
      // (note: line 1 is the second row, since counting begins with 0):
      lcd.setCursor(0, 0);
      lcd.print("Oil ");
      lcd.print(Oil_Temp, 0);
      lcd.print((char)223); // COOKIE : This display has a messed up ascii table.. handy if you are Japanese though : http://www.electronic-engineering.ch/microchip/datasheets/lcd/charset.gif

      //printTemperature(Sensor2_Thermometer);
      //Diff_Temp = Temp_Var;
      float Diff_Temp = getTemperature(Sensor2_Thermometer); // COOKIE
      
      // set the cursor to column 0, line 1
      // (note: line 1 is the second row, since counting begins with 0):
      lcd.setCursor(7, 0);
      lcd.print(" Dif ");
      lcd.print(Diff_Temp, 0);
      lcd.print((char)223);

      lcd.print(" "); //clear remaining row LCD chars (to avoid use of lcd.clear() as it flickers the screen).

      //printTemperature(Sensor3_Thermometer);
      //Gear_Temp = Temp_Var;
      float Gear_Temp = getTemperature(Sensor3_Thermometer); // COOKIE
      
      // set the cursor to column 0, line 1
      // (note: line 1 is the second row, since counting begins with 0):
      lcd.setCursor(0, 1);
      lcd.print("Gbox ");
      lcd.print(Gear_Temp, 1);
      lcd.print((char)223);

      lcd.print("      "); //clear remaining LCD chars (to avoid use of lcd.clear() as it flickers the screen).
      
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
      Screen_Number = !Screen_Number;  //Toggle screen boolean
      digitalWrite(ledPin, Led_State);
      delay(50);
   }
   Old_Button_State = CURRENT_BUTTON_STATE;
}

void setup()
{
   //Serial.begin(9600);
   btSerial.begin(9600);
   dlcSerial.begin(9600);

   delay(100);
   dlcInit();

   // set up the LCD's number of columns and rows:
   lcd.begin(16, 2);

   lcd.clear();
   lcd.setCursor(0, 0);
   lcd.print("Honda OBD v1.0");
   lcd.setCursor(0,1);
   lcd.print("LCD 16x2 Mode");

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
}

// COOKIE : Recommended change
float getTemperature(DeviceAddress deviceAddress)
{
   float tempC = sensors.getTempC(deviceAddress);
   if (tempC == -127.00)
   {
      //Serial.print("Error getting temperature");
      // set the cursor to column 0, line 1
      // (note: line 1 is the second row, since counting begins with 0):
      // This is a debug statement, so ive included in a #define guard
#ifdef _DEBUG
      lcd.clear();
      lcd.print("Error getting   ");
      lcd.setCursor(0, 1);
      lcd.print("temperature     ");
      delay(2000);
      lcd.clear();
#endif // _DEBUG
   }
   
   return tempC;
}
/*
void printTemperature(DeviceAddress deviceAddress)
{
   float tempC = sensors.getTempC(deviceAddress);
   if (tempC == -127.00)
   {
      //Serial.print("Error getting temperature");
      // set the cursor to column 0, line 1
      // (note: line 1 is the second row, since counting begins with 0):
      lcd.clear();
      lcd.print("Error getting   ");
      lcd.setCursor(0, 1);
      lcd.print("temperature     ");
      delay(2000);
      lcd.clear();
   }
   else
   {
      Temp_Var = tempC;
   }
}
*/
void loop()
{
   //SNEEZY HACKING -
   sensors.requestTemperatures();
   My_Buttons();   //My button check for screen change
   //END SNEEZY HACKING
            
   if (btSerial.available())
   {
      elm_mode = true;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Honda OBD v1.0");
      lcd.setCursor(0,1);
      lcd.print("Bluetooth Mode");
      procbtSerial();
   }
   if (!elm_mode)
   {
      procdlcSerial();
   }
}

