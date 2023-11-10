#!python3
import csv
import struct
import math
import configparser
import os
from io import BytesIO
# Generate UF2 file of ROMS
'''
File will start with an index block (8k), then each ROM (up to 16K each)

typedef struct {
    uint32_t rom_offset;// 4 bytes
    uint16_t rom_size;  // 2
    uint8_t  rom_type;  // 1   0=foreground, 1=background, 2=extension, 0x80=basic, 0xff=lower
    char name[35];      // 35
} rom_index_t;

Each ROM index is 42 bytes
max 96 ROMS = 4096 bytes


total ROM space is 1536+4K
1536 = 96 x 16k ROMS
4K = index

Flash Layout
0x10000000  FLASH (504K)
0x1007E000  ROM INDEX (4K)
            ROMS (1536K)
0x1FFFF000 CONFIG (4K)
0x20000000
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

__ROM_LEN = (1536 * 1024) + (4 * 1024)
__CONFIG_LEN = (4 * 1024)
__FLASH_START = 0x10000000
__FLASH_LEN = (2048 * 1024) - __ROM_LEN - __CONFIG_LEN
__CONFIG_START = 0x20000000 - __CONFIG_LEN
__ROM_START = __FLASH_START + __FLASH_LEN



INDEX_SIZE=4096
SLOT_SIZE=16384
MAX_ROMS=96
FAMILY_ID = 0xe48bff56 # RP2040
BLOCK_SIZE = 0x100

def image2uf2(infile, outfile):
    blockno = 0
    target = __ROM_START
    print("ROM address is 0x%08x" % __ROM_START)
    with open(outfile, 'wb') as f:
        infile.seek(0,2) # go to the end of the file
        size = infile.tell()
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
    index = []
    for rom in romlist.keys():
        v=romlist[rom].split(',')
        if len(v) < 2:
            continue
        filename=v[0]
        name = v[1]
        type = None
        if len(v) >2:
            if v[2].upper() == 'L':
                type = 0xff
            else:
                type = int(v[2])
        rom=int(rom)
        if rom >= MAX_ROMS:
            raise ValueError('max ROM number is is %d' % (MAX_ROMS - 1))
        offset = INDEX_SIZE + SLOT_SIZE * rom
        out.seek(offset)
        with open(os.path.join(romdir, filename), 'rb') as f:
            if type is None:
                # Get rom type from the image file
                type = f.read(1)
                out.write(type)
                type = ord(type)
            out.write(f.read())
            size = f.tell()
            print("Added rom #%2d at offset 0x%08x: %s, size %s, type 0x%02x" % (rom, offset, name, size, type))                
            index.append((offset,size,type,name))
    # go back to the start of the file and add the index
    out.seek(0)
    for i in index:
        out.write(struct.pack('<LHB34sx', i[0], i[1], i[2], bytes(i[3], 'ascii')))



if __name__ == '__main__':
    config = configparser.ConfigParser()
    config.read('romtool.ini')
    buf = BytesIO()
    roms2image(config['INPUT']['romdir'], config['ROMS'], buf)
    buf.seek(0)
    # with open('roms.bin', 'wb') as f:
    #     f.write(buf.read())
    # buf.seek(0)
    image2uf2(buf, config['OUTPUT']['romfile'])

