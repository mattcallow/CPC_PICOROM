#!/bin/env python3
import csv
import struct
import math
import configparser
import os
from io import BytesIO
import zipfile

# Generate UF2 file of ROMS
'''
File will start with an index block (8k), then each ROM (up to 16K each)

typedef struct {
    uint8_t  rom_type;  // 1   0=foreground, 1=background, 2=extension, 0x40=lower, 0x80=basic, 0xff=missing
    char name[33];      // 33
} rom_index_t;

Each ROM index is 34 bytes
max 120 ROMS = 4096 bytes


total ROM space is 1536+4K
1920k = 120 x 16k ROMS
4K = index

Flash Layout
0x10000000  FLASH (504K)
0x1007E000  ROM INDEX (4K)
            ROMS (1536K)
0x1FFFF000 CONFIG (4K)
0x20000000
'''

'''
Config block (4K)

Each config entry is 64 bytes:
NUM_ROM_BANKS = 14
typedef struct {
    uint16_t magic;                     // 2
    uint16_t ver;                       // 2
    uint8_t active;                     // 1
    uint8_t rom7_enable;                // 1
    int8_t lower_rom;                  // 1
    int8_t upper_roms[NUM_ROM_BANKS];  // 14
    char desc[33];                      // 33
    char spare[10];                      // 10
} config_t;

Therefore, 16 configs are possible


'''

'''
UF2

struct UF2_Block {
    // 32 byte header
    uint32_t magicStart0;
    uint32_t magicStart1;
    uint32_t flags;
    uint32_t targetAddr;
    uint32_t payloadSize;
    uint32_t blockNo;
    uint32_t numBlocks;
    uint32_t fileSize; // or familyID;
    uint8_t data[476];
    uint32_t magicEnd;
} UF2_Block;




'''

INDEX_SIZE=4096
BANK_SIZE=16384
NUM_ROM_BANKS=14
MAX_ROMS=120
FAMILY_ID = 0xe48bff56 # RP2040
BLOCK_SIZE = 0x100


__ROM_LEN = 4096 + (MAX_ROMS * 16384)
__CONFIG_LEN = 4096
__FLASH_START = 0x10000000

__FLASH_LEN = (2048*1024) - __ROM_LEN - __CONFIG_LEN
__ROM_START  = __FLASH_START + __FLASH_LEN
__CONFIG_START = __ROM_START + __ROM_LEN




def image2uf2(infile, outfile, start_address):
    blockno = 0
    target = start_address
    print("Start address is 0x%08x" % start_address)
    with open(outfile, 'wb') as f:
        infile.seek(0,2) # go to the end of the file
        size = infile.tell()
        print("image size is %d" % size)
        num_blocks = math.ceil(size/BLOCK_SIZE)
        infile.seek(0)
        while True:
            b = infile.read(256)
            if not b:
                break
            f.write(struct.pack('<LLLL', 0x0A324655, 0x9E5D5157, 0x00002000, target))
            f.write(struct.pack('<LLLL', BLOCK_SIZE, blockno, num_blocks, FAMILY_ID))
            f.write(b)
            f.write(bytes(476-len(b)))
            f.write(struct.pack('<L',0x0AB16F30))
            target += 256
            blockno += 1
    print("created %s" % outfile)


def roms2image(romdir, romlist, out):
    index = {}
    for rom in romlist.keys():
        v=romlist[rom].split(',')
        if len(v) < 1:
            continue
        filename=v[0]
        name = None
        if len(v) > 1:
            name = v[1]
        type = None
        if len(v) >2:
            if v[2].upper() == 'L':
                type = 0x40
            else:
                type = int(v[2])
        rom=int(rom)
        if rom >= MAX_ROMS:
            raise ValueError('max ROM number is is %d' % (MAX_ROMS - 1))
        offset = INDEX_SIZE + BANK_SIZE * rom
        out.seek(offset)
        path = os.path.join(romdir, filename)
        if zipfile.is_zipfile(path):
            z = zipfile.ZipFile(path)
            rom_name = [n for n in z.namelist() if n.upper().endswith(".ROM")][0]
            buf = z.read(rom_name)
            z.close()
        else:
            f = open(path, 'rb')
            buf = f.read()
            f.close()
        if type is None:
            # Get rom type from the image file
            type = buf[0]
        if name is None:
            # get ROM name from image file
            name_table = buf[4] + 256*buf[5] - 0xc000
            name = ""
            while buf[name_table] < 128:
                name += chr(buf[name_table] & 127)
                name_table += 1
            name += chr(buf[name_table] & 127)
        name = name.strip()
        out.write(buf)
        size = len(buf)
        print("Added rom #%2d at offset 0x%08x: type 0x%02x, size %s %s" % (rom, offset, type, size, name))                
        index[rom] = (type, name)
    # go back to the start of the file and add the index
    out.seek(0)
    for i in range(0,MAX_ROMS):
        if i in index:
            out.write(struct.pack('<B32sx', index[i][0], bytes(index[i][1], 'ascii')))
        else:
            out.write(struct.pack('<B33x', 0xff))


CONFIG_MAGIC = 0x7b0
CONFIG_VERSION = 1
def config2image(config, out):
    for section in config.sections():
        if not section.startswith('CONFIG'):
            continue
        print("Adding %s" % section)

        cfg = struct.pack('<HHBBb14b32sx10x', 
                  CONFIG_MAGIC,
                  CONFIG_VERSION,
                  config[section].getint('ACTIVE', 0),
                  1 if config[section].getint('BANK7', -1) >=0 else 0,
                  int(config[section].get('LOWER', -1)),
                  *[config[section].getint(f'BANK%d' % slot, -1) for slot in range(NUM_ROM_BANKS)],
                  bytes(config[section]['DESCRIPTION'],'ascii'))
        out.write(cfg)


if __name__ == '__main__':
    config = configparser.ConfigParser()
    config.read('romtool.ini')
    buf = BytesIO()
    roms2image(config['INPUT']['romdir'], config['ROMS'], buf)
    buf.seek(0)
    with open('roms.bin', 'wb') as f:
        f.write(buf.read())
    buf.seek(0)
    image2uf2(buf, config['OUTPUT']['romfile'], __ROM_START) 
    
    buf = BytesIO()
    buf.seek(4095)
    buf.write(b'\0')
    buf.seek(0)
    config2image(config, buf)
    buf.seek(0)
    with open('config.bin', 'wb') as f:
        f.write(buf.read())
    buf.seek(0)
    image2uf2(buf, config['OUTPUT']['configfile'], __CONFIG_START)

    if 'combined' in config['OUTPUT']:
        with open(config['OUTPUT']['combined'], 'wb') as f:
            fi = open(config['OUTPUT']['romfile'], 'rb')
            f.write(fi.read())
            fi.close()
            fi = open(config['OUTPUT']['configfile'], 'rb')
            f.write(fi.read())
            fi.close()



