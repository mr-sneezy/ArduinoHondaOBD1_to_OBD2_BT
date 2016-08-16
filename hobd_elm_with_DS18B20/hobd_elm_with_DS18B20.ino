/*
Forked and additions made by @mr-sneezy
Editing and suggestions by @micooke
Original Author:
- Philip Bordado (kerpz@yahoo.com)
 Hardware:
 - Arduino Nano
 - 3x Dallas DS18B20 OneWire temp sensors
 - HC-05 Bluetooth module at pin 10 (Rx) pin 11 (Tx) 
 - DLC(K-line) at pin 12
 Software:
 - Arduino 1.6.5 r5
 - SoftwareSerialWithHalfDuplex
   https://github.com/nickstedman/SoftwareSerialWithHalfDuplex
   
*/
#include <SoftwareSerialWithHalfDuplex.h>
//Sneezys note - Additions for 3x Dallas 1-wire temp sensors on a bus(async mode). Load these libraries to your Arduino libraries folder
#include <OneWire.h>            // https://github.com/PaulStoffregen/OneWire
#include <DallasTemperature.h> // https://github.com/milesburton/Arduino-Temperature-Control-Library

// Data wire is plugged into pin 2 on the Arduino
#define ONE_WIRE_BUS 2

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// Assign the addresses of your own 1-Wire temp sensors. Each sensor has a unique 64bit address. When more than one is used this info is needed.
// See the tutorial on how to obtain these addresses:  http://www.hacktronics.com/Tutorials/arduino-1-wire-address-finder.html
DeviceAddress Sensor1_Thermometer =
{ 0x28, 0x98, 0xEB, 0x1E, 0x00, 0x00, 0x80, 0x05 };		//Engine Oil DS18B20
DeviceAddress Sensor2_Thermometer =
{ 0x28, 0x07, 0xEC, 0x1E, 0x00, 0x00, 0x80, 0xC1 };		//Gearbox Oil DS18B20
DeviceAddress Sensor3_Thermometer =
{ 0x28, 0xFF, 0xFF, 0x73, 0xA3, 0x15, 0x01, 0xFE };		//Differential Oil DS18B20

/*
DeviceAddress Sensor1_Thermometer =
{ 0x28, 0xFF, 0x0C, 0x1B, 0xA3, 0x15, 0x04, 0xA5 };
DeviceAddress Sensor2_Thermometer =
{ 0x28, 0xFF, 0x0F, 0x4B, 0xA3, 0x15, 0x04, 0x33 };
DeviceAddress Sensor3_Thermometer =
{ 0x28, 0xFF, 0x75, 0x70, 0xA3, 0x15, 0x01, 0x00 };
*/

SoftwareSerialWithHalfDuplex btSerial(10, 11); // RX, TX    //Bluetooth serial connected to these pins
SoftwareSerialWithHalfDuplex dlcSerial(12, 12, false, false); //Honda OBD1 ECU DLC wire connected to this pin

#define debugSerial Serial                          //Debug is by serial
#define DebugPrint(x) debugSerial.print(x)          //send string no line feed or carriage return.
#define DebugPrintln(x) debugSerial.println(x)      //send string and line feed, carriage return.
#define DebugPrintHex(x) debugSerial.print(x, HEX)  //send value as hex

bool elm_memory = false;
bool elm_echo = false;
bool elm_space = false;
bool elm_linefeed = false;
bool elm_header = false;
int  elm_protocol = 0; // auto

bool DS18B20_fail = false;
 
//Set Arduino pin aliases to match attached devices
const int buttonPin = 18;     // the number of the pushbutton pin (pressed = 0)
const int failLED =  17;      // the number of the Fault LED pin
const int buzzerPin =  16;    // the number of the Buzzer pin

void bt_write(char *str) {
  while (*str != '\0')
    btSerial.write(*str++);
}

//Initialise ECU communictions (wake up ECU serial diagnostic mode).
void dlcInit() {
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

int dlcCommand(byte cmd, byte num, byte loc, byte len, byte data[]) {
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
  while (i < (len+3) && millis() < timeOut) {
    if (dlcSerial.available()) {
      data[i] = dlcSerial.read();
      i++;
    }
  }
  if (data[0] != 0x00 && data[1] != (len+3)) { // or use checksum?
    return 0; // error
  }
  if (i < (len+3)) { // timeout
    return 0; // error
  }
  return 1; // success
}

void procbtSerial(void) {
  char btdata1[20];  // bt data in buffer - btdata1 is an ARRAY of 20 bytes (chars)
  char btdata2[20];  // bt data out buffer - btdata2 is an ARRAY of 20 bytes (chars)
  byte dlcdata[20];  // dlc data buffer   - btdata is an ARRAY of 20 bytes (chars)
  int i = 0;

  float Temperature = 0;    //used with Dallas sensor code block

  memset(btdata1, 0, sizeof(btdata1));
  while (i < 20)
  {
    if (btSerial.available()) {
      btdata1[i] = toupper(btSerial.read());
      if (btdata1[i] == '\r') { // terminate at \r
        btdata1[i] = '\0';
        break;
      }
      else if (btdata1[i] != ' ') { // ignore space
        ++i;
      }
    }
  }

  memset(btdata2, 0, sizeof(btdata2));

//  ELM327 message emulations (Hayes serial modem style commands)
  if (!strcmp(btdata1, "ATD")) {
    sprintf_P(btdata2, PSTR("OK\r\n>"));
  }
  else if (!strcmp(btdata1, "ATI")) { // print id / general
    sprintf_P(btdata2, PSTR("HOBD v1.0\r\n>"));
  }
  else if (!strcmp(btdata1, "ATZ")) { // reset all / general
    sprintf_P(btdata2, PSTR("HOBD v1.0\r\n>"));
  }
  else if (strstr(btdata1, "ATE")) { // echo on/off / general
    elm_echo = (btdata1[3] == '1' ? true : false);
    sprintf_P(btdata2, PSTR("OK\r\n>"));
  }
  else if (strstr(btdata1, "ATL")) { // linfeed on/off / general
    elm_linefeed = (btdata1[3] == '1' ? true : false);
    sprintf_P(btdata2, PSTR("OK\r\n>"));
  }
  else if (strstr(btdata1, "ATM")) { // memory on/off / general
    elm_memory = (btdata1[3] == '1' ? true : false);
    sprintf_P(btdata2, PSTR("OK\r\n>"));
  }
  else if (strstr(btdata1, "ATS")) { // space on/off / obd
    elm_space = (btdata1[3] == '1' ? true : false);
    sprintf_P(btdata2, PSTR("OK\r\n>"));
  }
  else if (strstr(btdata1, "ATH")) { // headers on/off / obd
    elm_header = (btdata1[3] == '1' ? true : false);
    sprintf_P(btdata2, PSTR("OK\r\n>"));
  }
  else if (!strcmp(btdata1, "ATSP")) { // set protocol to ? and save it / obd
    //elm_protocol = atoi(data[4]);
    sprintf_P(btdata2, PSTR("OK\r\n>"));
  }
  //else if (!strcmp(data, "ATRV")) { // read voltage in float / volts
  //  btSerial.print("12.0V\r\n>");
  //}
  // sprintf_P(cmd_str, PSTR("%02X%02X\r"), mode, pid);
  // sscanf(data, "%02X%02X", mode, pid)
  
//Clear Diagnostic Trouble Codes and stored values
  // 21 04 01 DA / 01 03 FC
  else if (!strcmp(btdata1, "04")) { // clear dtc / stored values / [ use "Clear faults on ECU" via Torque app]
    dlcCommand(0x21, 0x04, 0x01, 0x00, dlcdata); // reset ecu WARNING DONT DO WHILE DRIVING
    sprintf_P(btdata2, PSTR("OK\r\n>"));
  }
 
// Numeric (HEX) commands from Torque app (or similar ELM327 compatible apps)
   ///   01XX : OBD2 Mode 1 PID commands
   ///   02XX : OBD2 custom Extended PID commands

   ///See https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_01
     
//PIDs supported [01 - 20] See https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_00
  else if (!strcmp(btdata1, "0100")) {
    sprintf_P(btdata2, PSTR("41 00 BE 3E B0 11\r\n>"));
  }

//Monitor status since DTCs cleared PID. (Includes malfunction indicator lamp (MIL) status and number of DTCs.)  
  else if (!strcmp(btdata1, "0101")) { // dtc / AA BB CC DD / A7 = MIL on/off, A6-A0 = DTC_CNT
    if (dlcCommand(0x20, 0x05, 0x0B, 0x01, dlcdata)) {
      byte a = ((dlcdata[2] >> 5) & 1) << 7; // get bit 5 on dlcdata[2], set it to a7
      sprintf_P(btdata2, PSTR("41 01 %02X 00 00 00\r\n>"), a);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }
  //else if (!strcmp(btdata1, "0102")) { // freeze dtc / 00 61 ???
  //  if (dlcCommand(0x20, 0x05, 0x98, 0x02, dlcdata)) {
  //    sprintf_P(btdata2, PSTR("41 02 %02X %02X\r\n>"), dlcdata[2], dlcdata[3]);
  //  }
  //  else {
  //    sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
  //  }
  //}
  
  //Fuel system status PID
  else if (!strcmp(btdata1, "0103")) { // fuel system status / 01 00 ???
    //if (dlcCommand(0x20, 0x05, 0x0F, 0x01, dlcdata)) { // flags
    //  byte a = dlcdata[2] & 1; // get bit 0 on dlcdata[2]
    //  a = (dlcdata[2] == 1 ? 2 : 1); // convert to comply obd2
    //  sprintf_P(btdata2, PSTR("41 03 %02X 00\r\n>"), a);
    // }
    if (dlcCommand(0x20, 0x05, 0x9a, 0x02, dlcdata)) {    //Sneezy note - No reference info for HOBD 0x9A found ???
      sprintf_P(btdata2, PSTR("41 03 %02X %02X\r\n>"), dlcdata[2], dlcdata[3]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }
 
 //Calculated engine load PID
  else if (!strcmp(btdata1, "0104")) { // engine load (%)
    if (dlcCommand(0x20, 0x05, 0x9c, 0x01, dlcdata)) {      //Sneezy note - No reference info for HOBD 0x9C found ???
      sprintf_P(btdata2, PSTR("41 04 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

//Engine coolant temperature PID 
  else if (!strcmp(btdata1, "0105")) { // ect (°C)
    if (dlcCommand(0x20, 0x05, 0x10, 0x01, dlcdata)) {
      
//      DebugPrint(F("ECT raw : "));   //Serial monitor debug 
//      DebugPrintln(dlcdata[2]);      //Serial monitor debug  
      
      float f = dlcdata[2];
      f = 155.04149 - f * 3.0414878 + pow(f, 2) * 0.03952185 - pow(f, 3) * 0.00029383913 + pow(f, 4) * 0.0000010792568 - pow(f, 5) * 0.0000000015618437;
      dlcdata[2] = round(f) + 40; // A-40
      sprintf_P(btdata2, PSTR("41 05 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

//Short term fuel trim—Bank 1 PID  
  else if (!strcmp(btdata1, "0106")) { // short FT (%)
    if (dlcCommand(0x20, 0x05, 0x20, 0x01, dlcdata)) {
      sprintf_P(btdata2, PSTR("41 06 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

//Long term fuel trim—Bank 1 PID  
  else if (!strcmp(btdata1, "0107")) { // long FT (%)
    if (dlcCommand(0x20, 0x05, 0x22, 0x01, dlcdata)) {
      sprintf_P(btdata2, PSTR("41 07 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }
  
  //else if (!strcmp(data, "010A")) { // fuel pressure
  //  btSerial.print("41 0A EF\r\n");
  //}

//Intake manifold absolute pressure PID  
  else if (!strcmp(btdata1, "010B")) { // map (kPa) (0-255kpa, 101kpa is sea level pressure )
    if (dlcCommand(0x20, 0x05, 0x12, 0x01, dlcdata)) {
      dlcdata[2] = dlcdata[2]*0.7;  //The Honda MAP sensor is a 1.8Bar (1800kpa) device, by my calculations and bench testing multiplier required is 0.7kpa/ ADC step. No offset required.
      sprintf_P(btdata2, PSTR("41 0B %02X\r\n>"), dlcdata[2]);
//      DebugPrintln(dlcdata[2]);   
    
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

 //Engine RPM PID 
  else if (!strcmp(btdata1, "010C")) { // rpm
    if (dlcCommand(0x20, 0x05, 0x00, 0x02, dlcdata)) {   //Note 2 bytes used here
      sprintf_P(btdata2, PSTR("41 0C %02X %02X\r\n>"), dlcdata[2], dlcdata[3]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

//Vehicle speed PID  
  else if (!strcmp(btdata1, "010D")) { // vss (km/h)
    if (dlcCommand(0x20, 0x05, 0x02, 0x01, dlcdata)) {
      sprintf_P(btdata2, PSTR("41 0D %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

//Timing advance PID  
  else if (!strcmp(btdata1, "010E")) { // timing advance (°BTDC)  ((A/2)-64 = °BTDC
    if (dlcCommand(0x20, 0x05, 0x26, 0x01, dlcdata)) {    //Sneezy note - Formating of the Honda byte is unknown ??
      sprintf_P(btdata2, PSTR("41 0E %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

//Intake air temperature PID 
  else if (!strcmp(btdata1, "010F")) { // iat (°C)
    if (dlcCommand(0x20, 0x05, 0x11, 0x01, dlcdata)) {
      
//  DebugPrint(F("IAT raw : "));
//  DebugPrintln(dlcdata[2]);
      
      float f = dlcdata[2];
      f = 155.04149 - f * 3.0414878 + pow(f, 2) * 0.03952185 - pow(f, 3) * 0.00029383913 + pow(f, 4) * 0.0000010792568 - pow(f, 5) * 0.0000000015618437;  //Needs validating
      dlcdata[2] = round(f) + 40; // A-40
      sprintf_P(btdata2, PSTR("41 0F %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

//Throttle position PID
  else if (!strcmp(btdata1, "0111")) { // tps (%)
    if (dlcCommand(0x20, 0x05, 0x14, 0x01, dlcdata)) {
      sprintf_P(btdata2, PSTR("41 11 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

//Oxygen sensors present (in 2 banks) PID
  else if (!strcmp(btdata1, "0113")) { // o2 sensor present ???
    sprintf_P(btdata2, PSTR("41 13 80\r\n>")); // 10000000 / assume bank 1 present  //Sneezy note - Shouldn't it be 00000001 for 1 sensor in Bank 1 but this works ?
  }

//Oxygen Sensor 1 A: Voltage B: Short term fuel trim PID 
  else if (!strcmp(btdata1, "0114")) { // o2 (V)
    if (dlcCommand(0x20, 0x05, 0x15, 0x01, dlcdata)) {
      dlcdata[2] = dlcdata[2] * 4;                      //Sneezy note - O2 sensor ADC input in Honda ECU reads to about 4V full scale
      sprintf_P(btdata2, PSTR("41 14 %02X FF\r\n>"), dlcdata[2]);  //FF means this sensor not used for short term trim
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

// OBD standards this vehicle conforms to PID
// See https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_1C
  else if (!strcmp(btdata1, "011C")) { 
    sprintf_P(btdata2, PSTR("41 1C 01\r\n>"));  // Value 1 = OBD-II as defined by the CARB
  }
  //PIDs supported [21 - 40] See https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_00
  else if (!strcmp(btdata1, "0120")) {
    sprintf_P(btdata2, PSTR("41 20 00 00 00 01\r\n>"));
  }
  //else if (!strcmp(btdata1, "012F")) { // fuel level (%)
  //  sprintf_P(btdata2, PSTR("41 2F FF\r\n>")); // max
  //}
  //else if (!strcmp(btdata1, "0130")) {
  //  sprintf_P(btdata2, PSTR("41 30 20 00 00 01\r\n>"));
  //}

//Absolute Barometric Pressure PID  
  else if (!strcmp(btdata1, "0133")) { // baro (kPa)
    if (dlcCommand(0x20, 0x05, 0x13, 0x01, dlcdata)) {
      dlcdata[2] = dlcdata[2]*0.7;  //Multiplier is 0.7kpa/ADC step. There is no offset required.
      sprintf_P(btdata2, PSTR("41 33 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }
 //PIDs supported [41 - 60] See https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_00  
  else if (!strcmp(btdata1, "0140")) {
//    sprintf_P(btdata2, PSTR("41 40 48 00 00 00\r\n>"));
    sprintf_P(btdata2, PSTR("41 40 48 00 00 10\r\n>"));  //Added oil temp PID flag bit
  }

//Control module voltage PID (ECU Voltage)  
  else if (!strcmp(btdata1, "0142")) { // ecu voltage (V) 
    if (dlcCommand(0x20, 0x05, 0x17, 0x01, dlcdata)) {
      float f = dlcdata[2];
      f = f / 10.45;
      unsigned int u = f * 1000; // ((A*256)+B)/1000
      sprintf_P(btdata2, PSTR("41 42 %02X %02X\r\n>"), highByte(u), lowByte(u));
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }
//Relative throttle position PID (After ECU TPS leaning ?)
  else if (!strcmp(btdata1, "0145")) { // iacv / relative throttle position 0-100%
    if (dlcCommand(0x20, 0x05, 0x28, 0x01, dlcdata)) {
      sprintf_P(btdata2, PSTR("41 45 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

//Engine Oil Temp sensor PID (from external One Wire DS18B20)
//OBD2 sensor conversion EOT = A - 40, so add 40 to balance it, + 0.5 for rounding up   
  else if (!strcmp(btdata1, "015C")) { // Engine Oil temp
//  Debug_pulse_out();
//------------DS18B20 code-----------------  
      Temperature = getTemperature(Sensor1_Thermometer) + 40.5;     
      sprintf_P(btdata2, PSTR("41 5C %02X\r\n>"), (byte)Temperature);  // Send the sensor temp; 

	  //         response = DATA;
     
//       else {
//      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
//    }
  }   

//Custom PID for Gearbox Temp sensor (external One Wire DS18B20)
// OBD2 sensor conversion GBT = A - 40, so add 40 to balance it, + 0.5 for rounding up   
  else if (!strcmp(btdata1, "2001")) { // Gear Box Oil temp
//------------DS18B20 code-----------------  
      Temperature = getTemperature(Sensor2_Thermometer) + 40.5;     
      sprintf_P(btdata2, PSTR("60 01 %02X\r\n>"), (byte)Temperature);  // Send the sensor temp; 
//         response = DATA;
     
//       else {
//      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
//    }
  }   

//Custom PID for Differential Oil Temp sensor (external One Wire DS18B20)
// OBD2 sensor conversion DOT = A - 40, so add 40 to balance it, + 0.5 for rounding up   
  else if (!strcmp(btdata1, "2002")) { // Diff Oil temp

//  Debug_pulse_out();
  
//------------DS18B20 code-----------------  
      Temperature = getTemperature(Sensor3_Thermometer) + 40.5;     
      sprintf_P(btdata2, PSTR("60 02 %02X\r\n>"), (byte)Temperature);  // Send the sensor temp; 
//         response = DATA;
     
//       else {
//      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
//    }
  }   
//Custom PID - Relay and switch flags
  else if (!strcmp(btdata1, "2008")) { // custom hobd mapping / flags 
    if (dlcCommand(0x20, 0x05, 0x08, 0x01, dlcdata)) { //Flag byte 0x08, Bit0-Start SW, Bit1-A/C On, Bit3-Brake On
      sprintf_P(btdata2, PSTR("60 08 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
    }
  }
//Custom PID - Relay and switch flags
   else if (!strcmp(btdata1, "200A")) { // custom hobd mapping / flags
    //sprintf_P(btdata2, PSTR("60 0C AA\r\n>")); // 10101010 / test data
    if (dlcCommand(0x20, 0x05, 0x0A, 0x01, dlcdata)) { //Flag byte 0x0A, Bit2-VTS (VTEC Oil Pressure Switch)
      sprintf_P(btdata2, PSTR("60 0A %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
    }
  }
//Custom PID - Relay and switch flags
  else if (!strcmp(btdata1, "200B")) { // custom hobd mapping / flags
    //sprintf_P(btdata2, PSTR("60 0C AA\r\n>")); // 10101010 / test data
    if (dlcCommand(0x20, 0x05, 0x0B, 0x01, dlcdata)) { //Flag byte 0x0B, Bit0-Main Relay On, Bit2-O2 Sensor Heater On, Bit5-MIL On
      sprintf_P(btdata2, PSTR("60 0B %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
    }
  }
//Custom PID - Relay and switch flags  
  else if (!strcmp(btdata1, "200C")) { // custom hobd mapping / flags
    if (dlcCommand(0x20, 0x05, 0x0C, 0x01, dlcdata)) { //Flag byte 0x0C, Bit3-VTEC Active (ECU), Bit7-Econo ??
      sprintf_P(btdata2, PSTR("60 0C %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
    }
  }
//Custom PID - Relay and switch flags
  else if (!strcmp(btdata1, "200F")) { // custom hobd mapping / flags
    if (dlcCommand(0x20, 0x05, 0x0F, 0x01, dlcdata)) { //F
      sprintf_P(btdata2, PSTR("60 0F %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
    }
  }
  else {
    sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
  }

  if (btdata2) bt_write(btdata2); // send reply
  
//Check/set any warnings (DS18B20 fail) or cancel is button is pressed
	if (DS18B20_fail == true)	{
		digitalWrite (17,1);  //Set a probe failure on digital pin 17
		digitalWrite (16,1);  //Set a probe failure on digital pin 16
		}
	else {
		if (digitalRead(buttonPin)==0){	//Button pressed  is a LOW
			digitalWrite (failLED,0);  //Cancel a probe failure on digital pin 17
			digitalWrite (buzzerPin,0);  //Cancel a probe failure on digital pin 16	
		}

		}	

//Request all DS18B20 sensors to start a temperature sample - Takes about 2mS, and program must take longer than that before a read request
  sensors.requestTemperatures(); //Do once each time Torque requests 'ANY' PID value. 
}

//Handle a request for a DS18B20 read
float getTemperature(DeviceAddress deviceAddress) //Read (get) the last sampled temperature of the probe as addressed by calling routine
{
   float tempC = sensors.getTempC(deviceAddress);
   
//      DebugPrint(F("DS18B20 raw : "));   //Serial monitor debug 
//      DebugPrintln(tempC);      //Serial monitor debug 
      
   if (tempC == -127.00)  //Open circuit or otherwise unresponsive probe returns -127 from Dallas DS18B20 library routines
   {
    //ADD a routine here to drop out an Arduino LED (of three ?) indicating a digital temp sensor has died...
    //Could be done via Custom PID and Torque also
//Indicate a probe failure on Digital pin 17 via the DS18B20 fail flage, LED will flicker each time a faulty probe is read 
		DS18B20_fail = true;	//Probe failed	
		tempC = -40;        //Set temp output to show -40C on Torque via OBDII value conversion (-40C is an obvious fault in most of the world !)
   }
		else
		{
		DS18B20_fail = false; //Probe not failed
		}    
  
   return tempC;
}

//-----logic pulse for debug via Saleae on Pin19 (ADC5)
void Debug_pulse_out()
{
  digitalWrite (19,1);
  delayMicroseconds(100);
  digitalWrite (19,0);
}
//SETUP
void setup()
{
  //Serial.begin(9600);
  btSerial.begin(9600);
  dlcSerial.begin(9600);

  delay(100);
  dlcInit();

  // Start up the Dallas sensor library
   sensors.begin();
   // set the resolution to 9 bit (0.5C resolution & fastest measurement speed)
   sensors.setResolution(Sensor1_Thermometer, 9);
   sensors.setResolution(Sensor2_Thermometer, 9);
   sensors.setResolution(Sensor3_Thermometer, 9);
   
   // Async type request for temps takes only 2mS, programmer then must take following conversion time into account (and do more useful stuff while waiting).
   sensors.setWaitForConversion(false);  // Makes it async and saves delay time.

	pinMode(15,INPUT);		//Bluetooth module status input (BT connected/not-connected)
	pinMode(buzzerPin,OUTPUT);		//Buzzer output pin 
	pinMode(failLED,OUTPUT);		//Fail LED output - Currently used for DS18B20 fault indication
	pinMode(buttonPin,INPUT);		//Button input via pullup (0 = active)		
	pinMode(19,OUTPUT);		//Direct to the monitor connector for I/O or debug (using a fast pulse etc)

   debugSerial.begin(9600);
   DebugPrintln(F("HOBD_Debug"));
   DebugPrintln(F("BT Mode"));
}
//LOOP
void loop() {
  btSerial.listen();
  if (btSerial.available()) {     //waits for BT serial data then proccess
    procbtSerial();
 
  }
}

