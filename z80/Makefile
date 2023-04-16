picorom.rom.h: picorom.rom
	xxd -i picorom.rom picorom.rom.h

picorom.rom: picorom.s
	 # z80asm -l --opicorom.rom picorom.s
	 z88dk.z88dk-z80asm -v -s -l -m  -b -mz80 -opicorom.rom picorom.s
	 
