#!/bin/sh
cd roms
for r in *.rom *.ROM
do
	xxd -i $r $r.h
done
