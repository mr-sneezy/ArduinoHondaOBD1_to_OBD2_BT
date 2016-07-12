ArduinoHondaOBD_Extra (based on the original by kerpz)
===========

An arduino code that reads Honda OBD Protocol and translates it to ELM327 protocol.
This fork will add three Dallas DS18B20 temperature sensors to the system created by kerpz.
The sensors output will be available on the LCD as a second screen OR will be spliced into the OBD data sent to the OBD2 conversion routines.

Update June 2016 - Unless I change my mind, I will not be using the LCD screen option in my code version, Torque or equivilent apps will be the only display front end. They have the advantage of more screen size choice, and datalogging.

Supports
--------
* Honda ECU's before 2002 in countries other than the USA (who had OBDII after 1996).
* This fork will be tested on an OzDM 1999 Honda S2000 used for track days.


Files
-----
* hobd_elm_with_DS18B20 - My current main version with no LCD and three extra temperature sensors.
* hobd_uni_S2000_alpha - A joint reworking of Kerpz original code by myself and MiCookie, before I decided to go without the LCD , and * simplify the code to my level :). 
* 


Basic wiring for ArduinoHondaOBD_Extra
--------------------
    Honda 3 Pin DLC           Arduino Nano
    Gnd --------------------- Gnd
    +12 --------------------- Vin
    K-line ------------------ Pin12

    HC-05 Bluetooth           Arduino Nano               
    Rx ---------------------- Pin11
    Tx ---------------------- Pin10

    LCD 16x2                  Arduino Nano               
    RS ---------------------- Pin9
    Enable ------------------ Pin8
    D4 ---------------------- Pin7
    D5 ---------------------- Pin6
    D6 ---------------------- Pin5
    D7 ---------------------- Pin4

![Alt text](https://raw.github.com/kerpz/ArduinoHondaOBD/master/images/UNI_wiring.png "UNI Wiring Image")
(Taken from Kerpz Git repo)

Wiring for hobd_elm_DS18B20
--------------------
    *See the schematic PDF in the schematic folder for my full circuit.
    My circuit follows Kerpz original as much as possible for compatibility.
    I have 'industrialised' the circuit a little to make it robust in an automotive environment.
    Kerpz's wiring diagram can be used with my code additions by adding the DS18B20's. 
    The extra complexity of my cicuits includes provision for the PCB to be used with an LCD,
    and access to key pins for monitoring with a CRO or Logic analyser. 


TODO
-----
* Upload Eagle files
* Code tidy-up ongoing ;)
* 
