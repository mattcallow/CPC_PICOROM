# CPC ROM Emulator using Pi Pico

## Features

* Easy to build - only  Pi Pico and a handful of passive components requried
* Emulates the lower ROM and up to 12 upper ROMs (number of ROMs is limited by available Pico RAM)
* Acts as USB flash drive when plugged into a PC - easy to copy ROMs
* Handles plain ROMs, or ROMs with 128byte headers
* Companion ROM to control the board from the CPC
* Optional USB module - partially compatible with Albireo (only supports USB drive) https://www.cpcwiki.eu/index.php/Albireo


## Quickstart

### To load the firmware image:
* Press and hold the bootsel button on the pico and  plug the pico into a PC. It should appear as a USB drive alled RPI-RP2.
* Copy the firmware image firmware/cpc_rom_emulator.uf2 onto the Pico USB drive

You have now programed your Pico. It should reboot and appear a USB drive called PICOROM with about 1.5MB capacity. The LED should slowly blink about once per second.

### Format the PICOROM
If this is the first time you have have installed the software, or you are upgrading from an earlier verion, you need to format the drive. 
 * Press and hold the bootsel button until the LED stays on (about 10 seconds). 
 * Release the button, the LED should turn off and the flash drive will be formatted.

### Obtain ROM images
You will need some CPC ROM images. You will also need the picorom.rom file, which is in the firmware directory. 
There are many ROM iamges available here https://www.cpcwiki.eu/index.php/ROM_List  
If the PICOROM detects that the ROM file has a AMSDOS header, it will be removed before loading. 
* Copy your ROM images onto the PICOROM USB drive

As a minimm you will need OS_6128.ROM and BASIC_1.1.ROM. You should also add picorom.rom 

At this point, you can test your PICOROM by unplugging the USB cable and plugging it into your CPC. If all is well, the CPC shold boot into Basic 1.1

### Create config files

You can create config files from the PC to define what ROMS to load. These files consist of one or more lines like this:

```
<SLOT>:<ROMFILE>
```

Where ```<SLOT>``` = L for lower ROM or 0-13 for upper ROM bank  
and ```<ROMFILE>``` = the filename of the ROM to load

For example:
```
# Default config
L:OS_6128.ROM
0:BASIC_1.1.ROM
1:picorom.rom
2:maxam15.ROM
3:Protext.rom
4:Utopia_v1_25b.ROM
5:Manic_Miner.rom
6:Chuckie.rom
```

At startup, if the Pico finds a file called DEFAULT.CFG it will load that. Otherwise if will try to load OS_6128.ROM and BASIC_1.1.ROM

### Status LED

The LED shows the status of the board:
* solid on - emulating ROMs for the CPC
* 1/2 second on/off - emulating USB drive for PC
* off - bootloader mode (or no power!)
* Repeating rapid flashes indicate an error:
  * 4 flashes = Failed to load OS ROM
  * 5 flashes = Failed to load Basic ROM

### ROM Commands

If you have loaded the picorom.rom, the you get some new commands on the CPC which let you control the board:

* |PUSB - start emulating a USB drive. CPC will stop working.
* |LED,n - Control the PICO LED n=1 for on, n=0 for off
* |ROMSET,"```<config file>```" - load a new config from the Pico.
* |PDIR - list all available ROMS on the Pico
* |ROMS - List currently inserted ROMs 
* |ROMOUT,n - remove a ROM from slot n
* |ROMIN,n,"```<rom file>```" - loads rom into slot n

## TODO

* Support listing subdirectories from CPC

## More details

![Completed Board](/hardware/PicoROM_Assembled.jpg)


|Signal     |GPIO         |CPC Pin|Pico Pin          |
|-----------|-------------|-------|------------------|
|A0-A13     |GPIO0-GPIO13 |18-5   |1-2,4-7,9-12,14-17|
|D0-D7      |GPIO14-GPIO21|26-19  |19-22,24-27       |
|~ROMEN     |GPIO22       |42     |29                |
|ROMDIS     |-            |43     |-                 |
|A15        |GPIO26       |3      |31|
|WRITE_LATCH|GPIO27       |-      |32|
|CPC RESET  |GPIO28       |40*    |34|
|Pico RESET |-            |-      |30|


\* GPIO28 connected to CPC RESET via a diode
```
CPC RESET __|\_|___ GPIO28
            |/ |

```
CPC Reset also has a push button to 0V


WRITE_LATCH signal is created using a Diode-OR gate:
```
  A13 __|\|___
        |/|  |
             |
  ~WR __|\|__|____  WRITE_LATCH (GPIO27)
        |/|  |
             |     2k2
~IORQ __|\|__|__/\/\/\/\___
        |/|                |
                           |
                          GND
             
```
All diodes IN4148 or similar. Pulldown via 2k2 resistor to ground.

Note: Rev1 board do not have the resistor pulldown, but it can be added as shown below
![Pulldown mod](/hardware/pulldown_mod.jpg)

ROMDIS is connected to 5V

Pico is powered from 5V via a IN4148 diode to VSYS


Pico Reset is a push button to 0V

## Notes

At startup, ROMs are loaded into RAM arrays, the the second core emulates all ROM
The first core handles the ROM latch at 0xDFxx with the help of a PIO state machine. The same IO port is also used to send commands to the PICO. This is done by writing a series of bytes to the port, startign with a 0xfc (which I don't think is a valid ROM number). Format is as follows:
* 0xfc - cmd prefix
* cmd byte
* 0 to 4 parameter bytes

Data is sent from the PICO to the CPC via a 0xff byte area in the ROM at 0xC100. Format is as follows:
* sequence number - incremented when the PICO has completed the command
* status code. 0=OK
* data type. 1 = null terminated string
* data ( 0 or more bytes)

There is a CPC ROM which provides a control over the ROM emulator.

## Flash drive

The flash drive is emulated as a USB MSC device. The SPIFTL library is used to provide wear leveling for the flash.

## PCB
**WARNING** There is an error on the schematic and PCB silkscreen. D2 is reversed. So, if you are going to build this, make sure that you insert D2 with the cathode (stripe) at the bottom.
----
There is a schematic and PCB layout which includes an optional USB interface.
![Schematic](hardware/schematic.png)
![PCB](hardware/pcb.png)

My PCBs were made by PCBWay. The [gerbers](hardware/gerbers.zip) I used are also avalilable.

# Credits
FatFs - http://elm-chan.org/fsw/ff/ 

```
/*----------------------------------------------------------------------------/
/  FatFs - Generic FAT Filesystem Module  Rx.xx                               /
/-----------------------------------------------------------------------------/
/
/ Copyright (C) 20xx, ChaN, all right reserved.
/
/ FatFs module is an open source software. Redistribution and use of FatFs in
/ source and binary forms, with or without modification, are permitted provided
/ that the following condition is met:
/
/ 1. Redistributions of source code must retain the above copyright notice,
/    this condition and the following disclaimer.
/
/ This software is provided by the copyright holder and contributors "AS IS"
/ and any warranties related to this software are DISCLAIMED.
/ The copyright owner or contributors be NOT LIABLE for any damages caused
/ by use of this software.
/----------------------------------------------------------------------------*/
```

USB flash drive code - https://github.com/oyama/pico-usb-flash-drive

```
Copyright 2024, Hiroyuki OYAMA. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.
- Neither the name of the copyright holder nor the names of its contributors may
  be used to endorse or promote products derived from this software without
  specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “  AS IS”   AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

```

SPIFTL code - https://github.com/earlephilhower/SPIFTL

```
# SPIFTL - Embedded, Static Wear-Leveling Library
## Copyright 2024, Earle F. Philhower III

                   GNU LESSER GENERAL PUBLIC LICENSE
                       Version 3, 29 June 2007

 Copyright (C) 2007 Free Software Foundation, Inc. <https://fsf.org/>
 Everyone is permitted to copy and distribute verbatim copies
 of this license document, but changing it is not allowed.


  This version of the GNU Lesser General Public License incorporates
the terms and conditions of version 3 of the GNU General Public
License, supplemented by the additional permissions listed below.

  0. Additional Definitions.

  As used herein, "this License" refers to version 3 of the GNU Lesser
General Public License, and the "GNU GPL" refers to version 3 of the GNU
General Public License.

  "The Library" refers to a covered work governed by this License,
other than an Application or a Combined Work as defined below.

  An "Application" is any work that makes use of an interface provided
by the Library, but which is not otherwise based on the Library.
Defining a subclass of a class defined by the Library is deemed a mode
of using an interface provided by the Library.

  A "Combined Work" is a work produced by combining or linking an
Application with the Library.  The particular version of the Library
with which the Combined Work was made is also called the "Linked
Version".

  The "Minimal Corresponding Source" for a Combined Work means the
Corresponding Source for the Combined Work, excluding any source code
for portions of the Combined Work that, considered in isolation, are
based on the Application, and not on the Linked Version.

  The "Corresponding Application Code" for a Combined Work means the
object code and/or source code for the Application, including any data
and utility programs needed for reproducing the Combined Work from the
Application, but excluding the System Libraries of the Combined Work.

  1. Exception to Section 3 of the GNU GPL.

  You may convey a covered work under sections 3 and 4 of this License
without being bound by section 3 of the GNU GPL.

  2. Conveying Modified Versions.

  If you modify a copy of the Library, and, in your modifications, a
facility refers to a function or data to be supplied by an Application
that uses the facility (other than as an argument passed when the
facility is invoked), then you may convey a copy of the modified
version:

   a) under this License, provided that you make a good faith effort to
   ensure that, in the event an Application does not supply the
   function or data, the facility still operates, and performs
   whatever part of its purpose remains meaningful, or

   b) under the GNU GPL, with none of the additional permissions of
   this License applicable to that copy.

  3. Object Code Incorporating Material from Library Header Files.

  The object code form of an Application may incorporate material from
a header file that is part of the Library.  You may convey such object
code under terms of your choice, provided that, if the incorporated
material is not limited to numerical parameters, data structure
layouts and accessors, or small macros, inline functions and templates
(ten or fewer lines in length), you do both of the following:

   a) Give prominent notice with each copy of the object code that the
   Library is used in it and that the Library and its use are
   covered by this License.

   b) Accompany the object code with a copy of the GNU GPL and this license
   document.

  4. Combined Works.

  You may convey a Combined Work under terms of your choice that,
taken together, effectively do not restrict modification of the
portions of the Library contained in the Combined Work and reverse
engineering for debugging such modifications, if you also do each of
the following:

   a) Give prominent notice with each copy of the Combined Work that
   the Library is used in it and that the Library and its use are
   covered by this License.

   b) Accompany the Combined Work with a copy of the GNU GPL and this license
   document.

   c) For a Combined Work that displays copyright notices during
   execution, include the copyright notice for the Library among
   these notices, as well as a reference directing the user to the
   copies of the GNU GPL and this license document.

   d) Do one of the following:

       0) Convey the Minimal Corresponding Source under the terms of this
       License, and the Corresponding Application Code in a form
       suitable for, and under terms that permit, the user to
       recombine or relink the Application with a modified version of
       the Linked Version to produce a modified Combined Work, in the
       manner specified by section 6 of the GNU GPL for conveying
       Corresponding Source.

       1) Use a suitable shared library mechanism for linking with the
       Library.  A suitable mechanism is one that (a) uses at run time
       a copy of the Library already present on the user's computer
       system, and (b) will operate properly with a modified version
       of the Library that is interface-compatible with the Linked
       Version.

   e) Provide Installation Information, but only if you would otherwise
   be required to provide such information under section 6 of the
   GNU GPL, and only to the extent that such information is
   necessary to install and execute a modified version of the
   Combined Work produced by recombining or relinking the
   Application with a modified version of the Linked Version. (If
   you use option 4d0, the Installation Information must accompany
   the Minimal Corresponding Source and Corresponding Application
   Code. If you use option 4d1, you must provide the Installation
   Information in the manner specified by section 6 of the GNU GPL
   for conveying Corresponding Source.)

  5. Combined Libraries.

  You may place library facilities that are a work based on the
Library side by side in a single library together with other library
facilities that are not Applications and are not covered by this
License, and convey such a combined library under terms of your
choice, if you do both of the following:

   a) Accompany the combined library with a copy of the same work based
   on the Library, uncombined with any other library facilities,
   conveyed under the terms of this License.

   b) Give prominent notice with the combined library that part of it
   is a work based on the Library, and explaining where to find the
   accompanying uncombined form of the same work.

  6. Revised Versions of the GNU Lesser General Public License.

  The Free Software Foundation may publish revised and/or new versions
of the GNU Lesser General Public License from time to time. Such new
versions will be similar in spirit to the present version, but may
differ in detail to address new problems or concerns.

  Each version is given a distinguishing version number. If the
Library as you received it specifies that a certain numbered version
of the GNU Lesser General Public License "or any later version"
applies to it, you have the option of following the terms and
conditions either of that published version or of any later version
published by the Free Software Foundation. If the Library as you
received it does not specify a version number of the GNU Lesser
General Public License, you may choose any version of the GNU Lesser
General Public License ever published by the Free Software Foundation.

  If the Library as you received it specifies that a proxy can decide
whether future versions of the GNU Lesser General Public License shall
apply, that proxy's public statement of acceptance of any version is
permanent authorization for you to choose that version for the
Library.
```