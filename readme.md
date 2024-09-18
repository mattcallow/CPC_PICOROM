CPC ROM Emulator using Pi Pico
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
             |
~IORQ __|\|__|
        |/|
```
All diodes IN4148 or similar. Pulldown provided by GPIO pin.

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

## ROM Commands
* PICOLOAD - reboot the PICO into bootloader mode
* LED,n - Control the PICO LED n=1 for on, n=0 for off
* ROMDIR - list the available ROMS
* CPC464 - Load 464 ROMS
* CPC664 - Load 664 ROMS
* CPC6128 - Load 6128 ROMS
* FW31 - Load FW31 ROMS
* ROMIN,n,m - Load ROM m into a slot n
* ROMOUT,n - Remove ROM from a slot n
* ROMLIST - List ROM slots
* CFGLOAD,n - Load a ROM configuration from config slot n
* CFGSAVE,n - Save current ROM configuration into config slot n
* ROM7,n - Enable (n=1) or disable (n=0) ROM slot 7

## PCB
**WARNING** There is an error on the schematic and PCB silkscreen. D2 is reversed. So, if you are going to build this, make sure that you insert D2 with the cathode (stripe) at the bottom.
----
There is a schematic and PCB layout which includes an optional USB interface.
![Schematic](hardware/schematic.png)
![PCB](hardware/pcb.png)

My PCBs were made by PCBWay. The [gerbers](hardware/gerbers.zip) I used are also avalilable.

## Software Installation

Once you have a built board, you need to install the firmware and some CPC ROMs onto the Pico.
Installation should be done before installing the board in your CPC.

### To load the firmware image:
* Press and hold the bootsel button on the pico
* Plug the pico into a PC/Mac. It should appear as a USB drive
* Copy the firmware image cpc_rom_emulator.uf2 onto the Pico USB drive

### To create the ROM images:
You will need python3 and some CPC ROM images. You will also need the picorom.rom file, which is in the firmware directory. The ROM images need to be plain ROM dumps, with no additional headers. They should be 16384 bytes in size. There are many ROM iamges available at the CPC Wiki  
You will need to edit romtool.ini and then run romtool.py to create a uf2 image that can be flashed to the Pico.

#### romtool.ini
Consists of multiple sections:
* [INPUT] = where to read ROM from
* [OUTPUT] = where to write the output files
* [ROMS] = list of ROMS to include in the image
each line consists of:

```<#>=<ROMFILE>,[ROMNAME],[ROMTYPE]```

<#> is the ROM number, used to reference the ROM later in the file  
<ROMFILE> is the filename of the ROM in the romdir directory  
[ROMNAME] is an optional ROM name. Can be omitted  
[ROMTYPE] should be L for lower (OS) ROMS. for any other ROM it can be omitted  

* [CONFIG#] = a config entry, which is basically a set of ROMS that will be loaded into the Pico and made available for the CPC. You can have multiple CONFIG entries, and each one can be selected from the CPC. you can also define one config as active, this will be the default on startup. Each config should consist of at least a lowerrom, and basic rom in bank 0. For example, assuming the following ROMS section:

>[ROMS]  
>0=OS_464.ROM,CPC 464 OS,L  
>1=OS_664.ROM,CPC 664 OS,L  
>2=OS_6128.ROM,CPC 6128 OS,L  
>  
>10=BASIC_1.0.ROM,Locomotive BASIC 1.0  
>11=BASIC_664.ROM,Basic 1.1  
>12=BASIC_1.1.ROM,Basic 1.1 (6128)  
>  
> 20=AMSDOS_0.7.ROM,AMS Dos 0.7  
> 30=picorom.rom


This defines 3 OS ROMS as ROMS 0,1,2  3 Basic ROMS as 10,11,12 , AMSDOS as ROM 20 and PICOROM as ROM 30


>[CONFIG0]  
> DESCRIPTION=Default CPC 464  
> LOWER=0  
> BANK0=10  
> ACTIVE=1    

This loads ROM0 into the lower bank and ROM10 into BANK0, and makes it active. Which is basically a stock 464

> [CONFIG1]  
> DESCRIPTION=CPC 464 with AMSDOS 0.7  
> LOWER=0  
> BANK0=10  
> BANK7=20  
> ACTIVE=0  

This adds AMSDOS into bank 7

You can use up to 14 banks in each config, numbered 0 to 13
If you want to control the PICOROM from the CPC, you will also need to load the picorom.rom image into one of the banks. for example

>[CONFIG2]  
> DESCRIPTION=Default CPC 464 with PICOROM
> LOWER=0  
> BANK0=10
> BANK6=30
> ACTIVE=1 

This adds the picorom ROM into bank 6

See the example .ini file for more configurations.

Once you have your ROMs and your config file, run romtool.py. If all is well, this will produce 3 files in the build directory:

* config.uf2
* roms+config.uf2
* roms.uf2

You need to load roms+config.uf2 onto the Pico. Follow the same steps used for the firmware upload.


Once you have flashed the firmware and ROMs to the Pico, remove the USB cable and plug into your CPC. If all is well, the CPC should boot with the new ROMS.

If the LED flashes on the Pico, something went wrong:  
1 flash = No active config  
2 flashes = Invalid config  





 







