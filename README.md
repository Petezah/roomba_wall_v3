# roomba_wall_v3

An AVR device, based on ATTiny45, to transmit a virtual wall signal for an iRobot Roomba

<img src="roomba_wall_v3.jpg"/>

## Folder layout

* /firmware
  * Source for the ATTiny45 firmware
* /hardware
  * Files describing the board layout, etc

## Building

### Prerequisites for "happy path":

* An AVR programmer
  * If you don't have one, you can build mine: https://oshpark.com/shared_projects/O24XLDbo
  * Though, this is a chicken-egg problem.  You can always program the programmer with an Arduino or a Raspberry Pi.
* AVR-GCC installed
  *  Easiest way to get this is 'sudo apt-get install arduino'
* A 6-position, dual-row IDC cable
* A 6-position, dual-row, male header to either:
  * Solder onto your board for programming, or
  * Plug into the IDC cable and "wedge" it into the ISP header during programming (recommended, and is what I do)

### Steps

#### hardware

* Order a board based on the files in https://github.com/Petezah/roomba_wall_v3/tree/master/hardware
  * Alternately, order one directly from OSH Park here: https://oshpark.com/shared_projects/i2FdXslA
* Purchase parts from [Mouser electronics](http://mouser.com) using the current BOM
  * Use the ones with "mouser" in their filenames
* Assemble the board--part references are all marked on the silkscreen
  * NB: The ISP header should not be soldered to the board--merely insert the header pins into the IDC cable and wedge the pins into the vias during programming

#### firmware

There are two methods to wire the board for programming:

* *Recommended way*:
  *  Order my AVR programmer board here: https://oshpark.com/shared_projects/O24XLDbo
  *  Assemble, and use an IDC cable as described above

* Alternate way: 
  *  Order my Raspberry Pi ISP board here: https://oshpark.com/shared_projects/iYtnPahC
  * Or hook up the ISP pins on the assembled board to your Raspberry PI as follows:
    * ISP Pin 1 --> Raspberry Pi Pin 21 (MISO)
    * ISP Pin 2 --> Raspberry Pi Pin 17 (3.3V)
    * ISP Pin 3 --> Raspberry Pi Pin 23 (SCK)
    * ISP Pin 4 --> Raspberry Pi Pin 19 (MOSI)
    * ISP Pin 5 --> Raspberry Pi Pin 15 (GPIO22)
    * ISP Pin 6 --> Raspberry Pi Pin 25 (GND)

Once wiring is accomplished:
* Execute the following on the Raspberry Pi command line
  * cd firmware
  * make fuse
  * make install
