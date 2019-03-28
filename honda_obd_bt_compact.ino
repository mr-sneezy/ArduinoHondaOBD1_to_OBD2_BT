/*
  Forked and additions made by @mr-sneezy
  Editing and suggestions by @micooke
  Original Author:- Philip Bordado (kerpz@yahoo.com)

  Hardware:
  - Arduino Nano
  - HC-05 Bluetooth module at pin 10 (Rx) pin 11 (Tx)
  - DLC(K-line) at pin 12

  Software:
  - Arduino 1.8.5
  - SoftwareSerialWithHalfDuplex
   https://github.com/nickstedman/SoftwareSerialWithHalfDuplex

  NOTES :-
  Added Knock, IACV, O2 Sensor #2, throttle pedal position.

*/
#include <SoftwareSerialWithHalfDuplex.h>


SoftwareSerialWithHalfDuplex btSerial(10, 11); // RX, TX out of Arduino. Bluetooth serial connected to these pins (TX pin here goes to RX pin on the BT via 10Kohm)
//SoftwareSerialWithHalfDuplex btSerial(10, 11); // RX, TX    //Bluetooth serial connected to these pins
SoftwareSerialWithHalfDuplex dlcSerial(12, 12, false, false); //Honda OBD1 ECU DLC wire connected to this pin

#define debugSerial Serial                          //Debug is by serial
#define DebugPrint(x) debugSerial.print(x)          //send string no line feed or carriage return.
#define DebugPrintln(x) debugSerial.println(x)      //send string and line feed, carriage return.
#define DebugPrintHex(x) debugSerial.print(x, HEX)  //send value as hex
#define DebugPrintBin(x) debugSerial.print(x, BIN)  //send value as binary
#define DebugPrintBinln(x) debugSerial.println(x, BIN)  //send value as binary

bool elm_memory = false;
bool elm_echo = false;
bool elm_space = false;
bool elm_linefeed = false;
bool elm_header = false;
int  elm_protocol = 0; // auto

//Set Arduino pin aliases to match attached devices
//const int buttonPin = 18;     // the number of the pushbutton pin (pressed = 0)
const int dlcLED =  13;      // the number of the DLC traffic LED pin on Arduino Nano
const int buzzerPin =  9;    // the number of the Buzzer pin for future stuff
const byte myTPSlow = 14;    //your cars zero throttle TPS byte value (trim to get 0% on OBD2 or leave at 0)
const byte myTPShigh = 231;  //your cars full throttle TPS byte value (trim to get 100% on OBD2 or leave at 255)

#define BTDATA_SIZE 20 //set BT serial array buffer size

//Write strings to BT adapter 
void bt_write(char *str) {
  while (*str != '\0')
    btSerial.write(*str++);
}

//Initialise ECU communictions by direct data serial writes(wake up ECU serial diagnostic mode).
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
// Write a DLC command to ECU as called. Return DLC data in to calling function (unually procbtSerial).
int dlcCommand(byte cmd, byte num, byte loc, byte len, byte (&data)[BTDATA_SIZE]) {
  byte crc = (0xFF - (cmd + num + loc + len - 0x01)); // checksum FF - (cmd + num + loc + len - 0x01)

  unsigned long timeOut = millis() + 250; // timeout @ 250 ms

  memset(&data, 0x00, sizeof(data));

  dlcSerial.listen();

  dlcSerial.write(cmd);  // header/cmd read memory ??
  dlcSerial.write(num);  // num of bytes to send
  dlcSerial.write(loc);  // address
  dlcSerial.write(len);  // num of bytes to read
  dlcSerial.write(crc);  // checksum

  int i = 0;
  while (i < (len + 3) && millis() < timeOut) {
    if (dlcSerial.available()) {
      data[i] = dlcSerial.read();
      i++;
    }
  }
  if (data[0] != 0x00 && data[1] != (len + 3)) { // or use checksum?
    digitalWrite(dlcLED, 0);   //Drop dlcLED if dlc data is not running
    return 0; // error
  }
  if (i < (len + 3)) { // timeout
    digitalWrite(dlcLED, 0);   //Drop dlcLED if dlc data is not running
    return 0; // error
  }
  digitalWrite(dlcLED, !digitalRead(dlcLED)); //Toggle D13 LED if dlc data is running OK
  return 1; // success
}

void procbtSerial(void) {
  char btdata1[BTDATA_SIZE] = {'\0'};  // bt data in buffer - btdata1 is an ARRAY of 20 bytes (chars)
  char btdata2[BTDATA_SIZE] = {'\0'};  // bt data out buffer - btdata2 is an ARRAY of 20 bytes (chars)
  byte dlcdata[BTDATA_SIZE] = { 0 };  // dlc data buffer   - btdata is an ARRAY of 20 bytes (chars)

  int i = 0;

  while (i < BTDATA_SIZE)
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

  //  ELM327 message emulations (Hayes serial modem style commands)
  if (!strcmp(btdata1, "ATD")) {
    sprintf_P(btdata2, PSTR("OK\r\n>"));
  }
  else if (!strcmp(btdata1, "ATI")) { // print id / general
    sprintf_P(btdata2, PSTR("Honda-OBD Compact v1.2\r\n>"));
  }
  else if (!strcmp(btdata1, "ATZ")) { // reset all / general
    sprintf_P(btdata2, PSTR("Honda-OBD Compact v1.2\r\n>"));
  }
  else if (!strcmp(btdata1, "AT@1")) { // reset all / general
    sprintf_P(btdata2, PSTR("MR-SNEEZY\r\n>"));
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
 
  //Clear Diagnostic Trouble Codes and stored values
  // 21 04 01 DA / 01 03 FC
  else if (!strcmp(btdata1, "04")) { // clear dtc / stored values / [ use "Clear faults on ECU" via Torque app]
    dlcCommand(0x21, 0x04, 0x01, 0x00, dlcdata); // reset ecu WARNING DONT DO WHILE DRIVING
    sprintf_P(btdata2, PSTR("OK\r\n>"));
  }
  // Numeric (HEX) commands from Torque app (or similar ELM327 protocol compatible apps)
  ///   01XX : OBD2 Mode 1 PID commands
  ///   02XX : OBD2 custom Extended PID commands
  ///	20XX : USER custom PID from Torque app

  //See https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_01

  //PIDs supported [01 - 20] See https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_00
  else if (!strcmp(btdata1, "0100")) {
    sprintf_P(btdata2, PSTR("41 00 BE 3E B8 11\r\n>"));    //added second O2 sensor flag (applies to S2000). If only one O2 sensor then set to "41 00 BE 3E B0 11\r\n>"
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
  //Fuel system status PID - Note - Verified bit 0 of 0F is Closed Loop flag via Custom PID so retest this code, seems probable that 0C bit 0 or bit 1 are PID 0103/4
  else if (!strcmp(btdata1, "0103")) { // fuel system status / 01 03 
    if (dlcCommand(0x20, 0x05, 0x0F, 0x01, dlcdata)) { // flags 0F
      byte a = dlcdata[2] & 1; // get bit 0 on dlcdata[2]
      a = (dlcdata[2] == 1 ? 2 : 1); // convert to comply obd2(not an Arduino function - https://docs.microsoft.com/en-us/cpp/cpp/conditional-operator-q?view=vs-2017)
      sprintf_P(btdata2, PSTR("41 03 %02X 00\r\n>"), a);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

  //Calculated Engine Load PID (Honda CLV)
  else if (!strcmp(btdata1, "0104")) { // engine load (%)
    if (dlcCommand(0x20, 0x05, 0x9c, 0x01, dlcdata)) {      //Note - From HOPE
      sprintf_P(btdata2, PSTR("41 04 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }
  
  //Engine coolant temperature PID
  else if (!strcmp(btdata1, "0105")) { // ect (°C)
    if (dlcCommand(0x20, 0x05, 0x10, 0x01, dlcdata)) {
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
  //Intake manifold absolute pressure PID
  else if (!strcmp(btdata1, "010B")) { // map (kPa) (0-255kpa, 101kpa is sea level pressure )
    if (dlcCommand(0x20, 0x05, 0x12, 0x01, dlcdata)) {
      dlcdata[2] = dlcdata[2] * 0.7; //The Honda S2000 MAP sensor is a 1.8Bar (1800kpa) device, by my calculations and bench testing multiplier required is 0.7kpa/ ADC step. No offset required.
      sprintf_P(btdata2, PSTR("41 0B %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }
  //Engine RPM PID - Two bytes
  else if (!strcmp(btdata1, "010C")) { // rpm
    if (dlcCommand(0x20, 0x05, 0x00, 0x02, dlcdata)) {   //Note 2 bytes used here
        if ((dlcdata[2] == 255) && (dlcdata[3] == 255)) {   //Trap a condition where my test ECU sends 255, 255 when engine is off
            dlcdata[2]=0;                                   //Set a 255 value to 0
            dlcdata[3]=0;
        }
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
  //Timing advance PID - Conversion formula of the Honda value is same as for OBD-II
  else if (!strcmp(btdata1, "010E")) { // timing advance (°BTDC)  ((A/2)-64 = °BTDC
    if (dlcCommand(0x20, 0x05, 0x26, 0x01, dlcdata)) {    
      sprintf_P(btdata2, PSTR("41 0E %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }
  //Intake air temperature PID
  else if (!strcmp(btdata1, "010F")) { // iat (°C)
    if (dlcCommand(0x20, 0x05, 0x11, 0x01, dlcdata)) {
      float f = dlcdata[2];
      f = 155.04149 - f * 3.0414878 + pow(f, 2) * 0.03952185 - pow(f, 3) * 0.00029383913 + pow(f, 4) * 0.0000010792568 - pow(f, 5) * 0.0000000015618437;  //Needs validating
      dlcdata[2] = round(f) + 40; // A-40
      sprintf_P(btdata2, PSTR("41 0F %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }
  //Throttle Position Sensor (sensor potentiometer on throttle body)
  else if (!strcmp(btdata1, "0111")) { // tps (%)
    if (dlcCommand(0x20, 0x05, 0x14, 0x01, dlcdata)) {
      sprintf_P(btdata2, PSTR("41 11 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

  //Oxygen sensors present (in 2 banks) PID (Note - this never gets read by Torque app AFAIK from my serial monitoring??)
  else if (!strcmp(btdata1, "0113")) { // o2 sensor present ???
 //   sprintf_P(btdata2, PSTR("41 13 80\r\n>")); // 10000000 / assume bank 1 present  //Note - Shouldn't it be 00000001 for 1 sensor in Bank 1 but this works ?
//        DebugPrint(F("O2 banks PID read "));              //Serial monitor debug
//        DebugPrintln("now");                     //Serial monitor debug
      sprintf_P(btdata2, PSTR("41 13 C0\r\n>")); // For Honda S2000 = 11000000 / assume bank 1 present  
  }

  //Oxygen Sensor 1 A: Voltage, B: Short term fuel trim PID (OBD2 expects 2 bytes back)
  else if (!strcmp(btdata1, "0114")) { // O2 (V)
    if (dlcCommand(0x20, 0x05, 0x15, 0x01, dlcdata)) {
      dlcdata[2] = dlcdata[2] * 4;                      //Note - O2 sensor ADC input in Honda ECU reads to about 4V full scale
  //        DebugPrint(F("O2_1x1 : "));              //Serial monitor debug
  //        DebugPrintln(dlcdata[2]);                //Serial monitor debug
      sprintf_P(btdata2, PSTR("41 14 %02X FF\r\n>"), dlcdata[2]);  //byte FF means this sensor not used for short term trim
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

  //Oxygen Sensor 2 A: Voltage, B: Short term fuel trim PID (OBD2 expects 2 bytes back)
  else if (!strcmp(btdata1, "0115")) { // O2 (V)
    if (dlcCommand(0x20, 0x05, 0xA0, 0x01, dlcdata)) {    //A0 is the second O2 sensor on the S2000
      dlcdata[2] = dlcdata[2] * 4;                      //Note - O2 sensor ADC input in Honda ECU reads to about 4V full scale
  //        DebugPrint(F("O2_1x2 : "));              //Serial monitor debug
  //        DebugPrintln(dlcdata[2]);                //Serial monitor debug	  
      sprintf_P(btdata2, PSTR("41 15 %02X FF\r\n>"), dlcdata[2]);  //byte FF means this sensor not used for short term trim
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }
  
  // OBD standards that this vehicle conforms to PID - These determines what PID's Torque App will look for.
  // See https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_1C
  else if (!strcmp(btdata1, "011C")) {
    sprintf_P(btdata2, PSTR("41 1C 01\r\n>"));  // Value 1 = OBD-II as defined by the CARB
  }
  //PIDs supported [21 - 40] See https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_00
  else if (!strcmp(btdata1, "0120")) {
    sprintf_P(btdata2, PSTR("41 20 00 00 20 01\r\n>"));  // Baro pressure + next block.
  }
  //else if (!strcmp(btdata1, "012F")) { // fuel level (%)
  //  sprintf_P(btdata2, PSTR("41 2F FF\r\n>")); // max
  //}
  
  //Absolute Barometric Pressure PID
  else if (!strcmp(btdata1, "0133")) { // baro (kPa)
    if (dlcCommand(0x20, 0x05, 0x13, 0x01, dlcdata)) {
      dlcdata[2] = dlcdata[2] * 0.7; //Multiplier is 0.7kpa/ADC step. There is no offset required from my bench testing.
      sprintf_P(btdata2, PSTR("41 33 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }
  //PIDs supported [41 - 60] See https://en.wikipedia.org/wiki/OBD-II_PIDs#Mode_1_PID_00
  else if (!strcmp(btdata1, "0140")) {
    sprintf_P(btdata2, PSTR("41 40 48 00 00 00\r\n>"));
//    sprintf_P(btdata2, PSTR("41 40 48 00 00 00\r\n>"));  //ECU voltage + my relative TPS (pedal position)
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
  
  //Relative Throttle Position Sensor (Pedal Position) PID (scaled from TPS value)
  else if (!strcmp(btdata1, "0145")) { //pedal position
    if (dlcCommand(0x20, 0x05, 0x14, 0x01, dlcdata)) {
//      DebugPrint(F("Pedal pos raw : "));
//      DebugPrintln(dlcdata[2]);
      dlcdata[2] = map(dlcdata[2], myTPSlow,  myTPShigh, 0, 255); //scale raw TPS byte to 0-100% of actual pedal travel, see Constants. Uncomment debug and use serial monitor to get your values
//      DebugPrint(F("Pedal pos scaled : "));
//      DebugPrintln(dlcdata[2]);
      sprintf_P(btdata2, PSTR("41 45 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }

  //Custom PID for IACV position (Idle Air Control Valve opening) From HOPE
  else if (!strcmp(btdata1, "2004")) { // IACV position value (scaled to % by Torque app)
    if (dlcCommand(0x20, 0x05, 0x28, 0x01, dlcdata)) {
//          DebugPrint(F("IACV Raw : "));              //Serial monitor debug
//          DebugPrintln(dlcdata[2]);                     //Serial monitor debug
    sprintf_P(btdata2, PSTR("60 04 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }
  
  //Custom PID for Honda S2000 Knock sensor - From HOPE
  else if (!strcmp(btdata1, "2005")) { // 
    if (dlcCommand(0x20, 0x05, 0x3C, 0x01, dlcdata)) { //use Torque app function = B / 51
//          DebugPrint(F("Knock Raw : "));              //Serial monitor debug
//          DebugPrintln(dlcdata[2]);                     //Serial monitor debug
    sprintf_P(btdata2, PSTR("60 05 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("DATA ERROR\r\n>"));
    }
  }
  
  //Custom PID - Relay and switch flags
  else if (!strcmp(btdata1, "2008")) { // custom hobd mapping / flags
    if (dlcCommand(0x20, 0x05, 0x08, 0x01, dlcdata)) { //Flag byte 0x08, Bit0-Start SW?, Bit1-A/C Relay, Bit3-Brake On, Bit7-VTEC Swich (Active Off)
        DebugPrint(F("08h flags : "));
        DebugPrintBinln(dlcdata[2]);
      sprintf_P(btdata2, PSTR("60 08 %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
    }
  }
 //Custom PID - Relay and switch flags
  else if (!strcmp(btdata1, "200A")) { // custom hobd mapping / flags
    if (dlcCommand(0x20, 0x05, 0x0A, 0x01, dlcdata)) { //Flag byte 0x0A, Bit2-VTEC Solenoid active,
        DebugPrint(F("0Ah flags : "));
        DebugPrintBinln(dlcdata[2]);
      sprintf_P(btdata2, PSTR("60 0A %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
    }
  }
  //Custom PID - Relay and switch flags
  else if (!strcmp(btdata1, "200B")) { // custom hobd mapping / flags
    if (dlcCommand(0x20, 0x05, 0x0B, 0x01, dlcdata)) { //Flag byte 0x0B, Bit0-PGM-FI Relay On, Bit1-A/C Clutch, Bit2-O2 Sensor Heater On, Bit5-MIL On
        DebugPrint(F("0Bh flags : "));
        DebugPrintBinln(dlcdata[2]);
      sprintf_P(btdata2, PSTR("60 0B %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
    }
  }
  //Custom PID - Relay and switch flags
  else if (!strcmp(btdata1, "200C")) { // custom hobd mapping / flags
    if (dlcCommand(0x20, 0x05, 0x0C, 0x01, dlcdata)) { //Flag byte 0x0C, Bit0-Alt-C (from HOPE - might be Closed Loop Off reason bit?), Bit1-injector off (deceleration)or closed loop related?,  Bit2-IAB Solenoid? (from HOPE), Bit3-VTEC Engaged (ECU), 
         DebugPrint(F("0Ch flags : "));
         DebugPrintBinln(dlcdata[2]);
      sprintf_P(btdata2, PSTR("60 0C %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
    }
  }
  //Custom PID - Relay and switch flags
  else if (!strcmp(btdata1, "200F")) { // custom hobd mapping / flags
    if (dlcCommand(0x20, 0x05, 0x0F, 0x01, dlcdata)) { //Flag byte 0x0F, Bit0-Fuel system Closed Loop
        DebugPrint(F("0Fh flags : "));
        DebugPrintBinln(dlcdata[2]);
      sprintf_P(btdata2, PSTR("60 0F %02X\r\n>"), dlcdata[2]);
    }
    else {
      sprintf_P(btdata2, PSTR("NO DATA\r\n>"));
    }
  }

 else {
    sprintf_P(btdata2, PSTR("NO DATA\r\n>"));  //No matching PIDs
  }

//  if (btdata2) bt_write(btdata2); // send reply
//  if (btdata2[0] != '\0') bt_write(btdata2); // send reply

  if (btdata2[0] != '\0') 
  {
    bt_write(btdata2); // send reply
  }  

}

//SETUP #############################################################################################
void setup()
{
  //Serial.begin(9600);
  btSerial.begin(19200); //Tried 38400 but found my Nano stopped working at that speed...
//  btSerial.begin(9600);  
  dlcSerial.begin(9600);

  delay(1000);
  
  dlcInit();

//  pinMode(BT_status, INPUT);       //Bluetooth module status input if it has one(BT connected/not-connected)
  pinMode(buzzerPin, OUTPUT);       //Buzzer output pin
  pinMode(dlcLED,OUTPUT);           //LED on pin D13 of Nano

  digitalWrite (buzzerPin, 1);       //test buzzer start
  delay (500);
  digitalWrite (buzzerPin, 0);       //test buzzer end
  
  debugSerial.begin(9600);
  DebugPrintln(F("HOBD_Debug"));
    DebugPrintln(F("HOBD_Compact_V1.2"));
//  DebugPrintln(F(""));
}
//LOOP  ###########################################################################################
void loop() {
  btSerial.listen();
  if (btSerial.available()) {     //waits for BT serial data then proccess
    procbtSerial();
  }
}

