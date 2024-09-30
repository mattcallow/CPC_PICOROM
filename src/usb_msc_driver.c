#include <ctype.h>
#include <bsp/board.h>
#include <tusb.h>
#include "flash.h"

#ifndef MSC_DRIVER_DEBUG
#define MSC_DRIVER_DEBUG 0
#endif
// whether host does safe-eject
static bool ejected = false;

static void fatal(int flashes) {
    while(1) {
        for (int i=0;i<flashes;i++) {
            gpio_put(PICO_DEFAULT_LED_PIN, 1);
            sleep_ms(100);
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
            sleep_ms(200);   
        }
        sleep_ms(500);
    }
}

static void print_block(uint8_t *buffer, size_t l) {
    for (size_t i = 0; i < l; ++i) {
        printf("%02x", buffer[i]);
        if (i % 16 == 15)
            printf("\n");
        else
            printf(", ");
    }
}


// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void) lun;

    const char vid[] = "TinyUSB";
    const char pid[] = "PicoROM";
    const char rev[] = "1.0";

    memcpy(vendor_id  , vid, strlen(vid));
    memcpy(product_id , pid, strlen(pid));
    memcpy(product_rev, rev, strlen(rev));
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void) lun;
    // RAM disk is ready until ejected
    if (ejected) {
        // Additional Sense 3A-00 is NOT_FOUND
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
        return false;
    }
    return true;
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)
{
    (void) lun;
    *block_count = get_lba_count();
    *block_size  = get_lba_size();
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void) lun;
    (void) power_condition;

    if (start && load_eject) {
        flash_init();
    } else if (!start && load_eject) {
        flash_persist();
        ejected = true;
    }
    return true;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
    (void) lun;
    #if MSC_DRIVER_DEBUG
    printf("tud_msc_read10_cb(%d, %lu, %lu, buffer, %lu)\n", lun, lba, offset, bufsize);
    if (offset != 0) printf("ERROR offset is not 0\n");
    if (bufsize != get_lba_size()) printf("ERROR bufsize mismatch %d <> %d\n", bufsize, get_lba_size());
    #endif
    // out of ramdisk
    if (lba >= get_lba_count()) {
        printf("read10 out of ramdisk: lba=%u\n", lba);
        return -1;
    }
    flash_read(lba, buffer);

    return (int32_t) bufsize;
}

bool tud_msc_is_writable_cb (uint8_t lun)
{
    (void) lun;
    return true;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
    (void) lun;
    #if MSC_DRIVER_DEBUG
    printf("tud_msc_write10_cb(%d, %lu, %lu, buffer, %lu)\n", lun, lba, offset, bufsize);
    if (offset != 0) printf("ERROR offset is not 0\n");
    if (bufsize != get_lba_size()) printf("ERROR bufsize mismatch %d <> %d\n", bufsize, get_lba_size());
    #endif
    // out of ramdisk
    if (lba >= get_lba_count()) {
        printf("write10 out of ramdisk: lba=%u\n", lba);
        return -1;
    }

    flash_write(lba, buffer);
    return (int32_t)bufsize;
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize)
{
    const int SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL = 0x1E;
    const int SCSI_CMD_START_STOP_UNIT              = 0x1B;
    const int SCSI_SENSE_ILLEGAL_REQUEST = 0x05;

    // read10 & write10 has their own callback and MUST not be handled here
    void const* response = NULL;
    int32_t resplen = 0;
    // most scsi handled is input
    bool in_xfer = true;
    scsi_start_stop_unit_t const * start_stop = (scsi_start_stop_unit_t const *) scsi_cmd;
    switch (scsi_cmd[0]) {
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        // Host is about to read/write etc ... better not to disconnect disk
        if (scsi_cmd[4] & 1) {
            flash_init();
        }
        resplen = 0;
        break;
    case SCSI_CMD_START_STOP_UNIT:
        // Host try to eject/safe remove/poweroff us. We could safely disconnect with disk storage, or go into lower power
        if (!start_stop->start && start_stop->load_eject) {
            flash_persist();
        } else if (start_stop->start && start_stop->load_eject) {
            flash_init();
        }
        resplen = 0;
        break;
    default:
        // Set Sense = Invalid Command Operation
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        // negative means error -> tinyusb could stall and/or response with failed status
        resplen = -1;
        break;
    }

    // return resplen must not larger than bufsize
    if ( resplen > bufsize )
        resplen = bufsize;

    if (response && (resplen > 0)) {
        if(in_xfer) {
            memcpy(buffer, response, (size_t)resplen);
        } else {
            ; // SCSI output
        }
    }

    return (int32_t)resplen;
}
