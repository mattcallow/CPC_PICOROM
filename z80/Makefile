Z80ASM=/home/matt/Projects/z88dk/bin/z88dk-z80asm
Z80ASM=z88dk.z88dk-z80asm

picorom.rom: picorom.s
	 # z80asm -l --opicorom.rom picorom.s
	 # z88dk.z88dk-z80asm -v -s -l -m  -b -mz80 -opicorom.rom picorom.s
	${Z80ASM} -v -s -l -m  -b -mz80 -opicorom.rom picorom.s
	cp $@ ../roms
	cp $@ ../firmware

	 
