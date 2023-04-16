#!/bin/sh
cd roms
for r in *.rom *.ROM
do
	xxd -i $r $r.h
	sed -i "s/^unsigned char/const unsigned char/" $r.h
done

