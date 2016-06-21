ArduinoHondaOBD_Extra (based on the original by kerpz)
===========

An arduino code that reads Honda OBD Protocol and translates it to ELM327 protocol.
This fork will add three Dallas DS18B20 temperature sensors to the system created by kerpz.
The sensors output will be available on the LCD as a second screen OR will be spliced into the OBD data sent to the OBD2 conversion routines. 
Update June 2016 - Unless I change my mind, I will not be using the LCD screen option in my code version, Torque or equivilent apps will be the only display front end. They have the advantage of more screen size choice, and datalogging.

Supports
--------
* Honda ECU's before 2002
* This fork will be tested on a 1999 Honda S2000 used for track days.


Files
-----
* hobd_uni - implements Honda OBD with ELM OBD2 protocol (bluetooth) and LCD display
* LCD_wiring.png - LCD wiring for arduino uno (10k potentiometer)


Wiring for hobd_uni (Joined ELM and LCD codes)
--------------------
    Honda 3 Pin DLC           Arduino Uno
    Gnd --------------------- Gnd
    +12 --------------------- Vin
    K-line ------------------ Pin12

    HC-05 Bluetooth           Arduino Uno               
    Rx ---------------------- Pin11
    Tx ---------------------- Pin10

    LCD 16x2                  Arduino Uno               
    RS ---------------------- Pin9
    Enable ------------------ Pin8
    D4 ---------------------- Pin7
    D5 ---------------------- Pin6
    D6 ---------------------- Pin5
    D7 ---------------------- Pin4

![Alt text](https://raw.github.com/kerpz/ArduinoHondaOBD/master/images/UNI_wiring.png "UNI Wiring Image")

Wiring for hobd_elm (Deprecated use hobd_uni)
--------------------
    Honda 3 Pin DLC           Arduino Uno
    Gnd --------------------- Gnd
    +12 --------------------- Vin
    K-line ------------------ Pin12

    HC-05 Bluetooth           Arduino Uno               
    Rx ---------------------- Pin11
    Tx ---------------------- Pin10


TODO
-----
* Upload alpha version
* Code tidy-up ;)
* 
