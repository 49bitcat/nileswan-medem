#include "wswan.h"
#include "memory.h"
#include "comm.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "nileswan.h"
#include "nileswan_hardware.h"

using namespace MDFN_IEN_WSWAN;

struct {
    nile_spi_device_buffer_t tx;
    nile_spi_device_buffer_t rx;

    uint8_t sr1, sr2, sr3;

    uint8_t mode;
    uint32_t position;
    bool sleeping;
} spi_flash;
FILE *file_flash;

static const uint8_t spi_flash_mfr_id = 0xEF;
static const uint8_t spi_flash_dev_id = 0x14;
static const uint8_t spi_flash_jedec_id[] = { spi_flash_mfr_id, 0x40, 0x15 };
static const uint8_t spi_flash_uuid[] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };

uint8_t nile_spi_flash_exchange(uint8_t tx) {
    uint8_t rx = 0xFF;
    uint32_t size = 0;

    if (spi_flash.mode == NILE_FLASH_CMD_READ) {
        if (file_flash != NULL)
            rx = feof(file_flash) ? 0xFF : fgetc(file_flash);
        else
            rx = 0x90;
        spi_flash.position++;
    } else if (spi_flash.mode == NILE_FLASH_CMD_WRITE) {
        // TODO: only set bits one way
        // TODO: handle WEL
        fseek(file_flash, spi_flash.position, SEEK_SET);
        fputc(tx, file_flash);

        // increment within page
        spi_flash.position = ((spi_flash.position + 1) & 0xFF) | (spi_flash.position & 0xFFFFFF00);
    } else if (spi_flash.mode == NILE_FLASH_CMD_RDSR1) {
        return spi_flash.sr1;
    } else if (spi_flash.mode == NILE_FLASH_CMD_RDSR2) {
        return spi_flash.sr2;
    } else if (spi_flash.mode == NILE_FLASH_CMD_RDSR3) {
        return spi_flash.sr3;
    } else {
        spi_buffer_push(&spi_flash.rx, &tx, 1);
        spi_buffer_pop(&spi_flash.tx, &rx, 1);

        if (spi_flash.sleeping && spi_flash.rx.data[0] != NILE_FLASH_CMD_WAKE_ID) {
            printf("nileswan/spi/flash: !! byte %02X sent while asleep !!\n", spi_flash.rx.data[0]);
            return 0xFF;
        }

        switch (spi_flash.rx.data[0]) {
            case NILE_FLASH_CMD_ERASE_4K:
                if (!size) size = 4096;
            case NILE_FLASH_CMD_ERASE_32K:
                if (!size) size = 32768;
            case NILE_FLASH_CMD_ERASE_64K:
                if (!size) size = 65536;
            case NILE_FLASH_CMD_WRITE:
            case NILE_FLASH_CMD_READ:
                if (spi_flash.rx.pos >= 4) {
                    spi_flash.mode = spi_flash.rx.data[0];
                    spi_flash.position =
                        (spi_flash.rx.data[1] << 16) |
                        (spi_flash.rx.data[2] << 8) |
                        spi_flash.rx.data[3];
                    spi_buffer_pop(&spi_flash.rx, NULL, 4);
                    if (file_flash != NULL)
                        fseek(file_flash, spi_flash.position, SEEK_SET);
                    if (spi_flash.mode == NILE_FLASH_CMD_WRITE) printf("nileswan/spi/flash: write starting at location %06X\n", spi_flash.position);
                    else if (spi_flash.mode == NILE_FLASH_CMD_READ) printf("nileswan/spi/flash: read starting at location %06X\n", spi_flash.position);
                    else printf("nileswan/spi/flash: erasing %d bytes at location %06X\n", size, spi_flash.position);
                }
                break;
            case NILE_FLASH_CMD_RDSR1:
            case NILE_FLASH_CMD_RDSR2:
            case NILE_FLASH_CMD_RDSR3:
                spi_flash.mode = spi_flash.rx.data[0];
                spi_buffer_pop(&spi_flash.rx, NULL, 1);
                break;
            case NILE_FLASH_CMD_RDUUID:
                spi_buffer_pop(&spi_flash.rx, NULL, 1);
                spi_buffer_push(&spi_flash.tx, NULL, 4);
                spi_buffer_push(&spi_flash.tx, spi_flash_uuid, 8);
                break;
            case NILE_FLASH_CMD_MFR_ID:
                if (spi_flash.rx.pos >= 4) {
                    spi_buffer_pop(&spi_flash.rx, NULL, 4);
                    spi_buffer_push(&spi_flash.tx, &spi_flash_mfr_id, 1);
                    spi_buffer_push(&spi_flash.tx, &spi_flash_dev_id, 1);
                }
                break;
            case NILE_FLASH_CMD_RDID:
                spi_buffer_pop(&spi_flash.rx, NULL, 1);
                spi_buffer_push(&spi_flash.tx, spi_flash_jedec_id, 3);
                break;
            case NILE_FLASH_CMD_WAKE_ID:
                printf("nileswan/spi/flash: waking\n");
                spi_flash.sleeping = false;
                spi_buffer_pop(&spi_flash.rx, NULL, 1);
                spi_buffer_push(&spi_flash.tx, NULL, 3);
                spi_buffer_push(&spi_flash.tx, &spi_flash_dev_id, 1);
                break;
            case NILE_FLASH_CMD_WRDI:
                printf("nileswan/spi/flash: write disable\n");
                spi_flash.sr1 &= ~NILE_FLASH_SR1_WEL;
                spi_buffer_pop(&spi_flash.rx, NULL, 1);
                break;
            case NILE_FLASH_CMD_WREN:
                printf("nileswan/spi/flash: write enable\n");
                spi_flash.sr1 |= NILE_FLASH_SR1_WEL;
                spi_buffer_pop(&spi_flash.rx, NULL, 1);
                break;
            case NILE_FLASH_CMD_SLEEP:
                printf("nileswan/spi/flash: sleeping\n");
                spi_flash.sleeping = true;
                spi_buffer_pop(&spi_flash.rx, NULL, 1);
                break;
            default:
                printf("nileswan/spi/flash: unknown command %02X\n", spi_flash.rx.data[0]);
                spi_flash.rx.pos = 0;
                break;
        }
    }
    return rx;
}

void nile_spi_flash_reset(bool full) {
    if (full) {
        memset(&spi_flash, 0, sizeof(spi_flash));
    }
    spi_flash.tx.pos = 0;
    spi_flash.rx.pos = 0;
    spi_flash.mode = 0;
}
