CPC ROM Emulator using Pi Pico

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
* LED - Control the PICO LED
* ROMDIR - list the available ROMS
* CPC464 - Load 464 ROMS
* CPC664 - Load 664 ROMS
* CPC6128 - Load 6128 ROMS
* FW31 - Load FW31 ROMS
* ROMIN - Load a ROM into a slot
* ROMOUT - Remove a ROM from a slot
* ROMLIST - List ROM slots
* CFGLOAD - Load a ROM configuration (not yet implemented)
* CFGSAVE - Save current ROM configuration (not yet implemented)


