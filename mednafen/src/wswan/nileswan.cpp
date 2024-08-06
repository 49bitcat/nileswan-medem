#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "nileswan.h"
#include "nileswan_hardware.h"

#define PSRAM_MAX_BANK 255
#define SRAM_MAX_BANK 7

namespace MDFN_IEN_WSWAN
{
extern uint8_t *wsCartROM;
extern uint32_t rom_size;
}
using namespace MDFN_IEN_WSWAN;

/* === SPI devices === */

/* Configuration */
#define TF_STOP_TRANSFER_BUSY_DELAY_BYTES 8
#define TF_DATA_BLOCK_READ_DELAY_BYTES 16

#define SPI_DEVICE_BUFFER_SIZE_BYTES 4096

typedef struct {
    uint8_t data[SPI_DEVICE_BUFFER_SIZE_BYTES];
    uint32_t pos;
} nile_spi_device_buffer_t;

struct {
    nile_spi_device_buffer_t tx;
    nile_spi_device_buffer_t rx;

    uint8_t sr1, sr2, sr3;

    uint8_t mode;
    uint32_t position;
} spi_flash;
static FILE *file_flash;

#define SPI_TF_WRITING_SINGLE 1
#define SPI_TF_WRITING_MULTIPLE 2

#define TF_ILLEGAL_COMMAND 0x04
#define TF_PARAMETER_ERROR 0x40

struct {
    nile_spi_device_buffer_t tx;
    nile_spi_device_buffer_t rx;

    uint8_t status;

    bool reading;
    uint8_t writing;
} spi_tf;
static FILE *file_tf;

static uint16_t bank_rom0, bank_rom1, bank_romL, bank_ram;
static uint8_t flash_enable;
static uint8_t nile_pow_cnt, nile_irq;
static uint16_t nile_spi_cnt, nile_bank_mask;

static void spi_buffer_push(nile_spi_device_buffer_t *buffer, const uint8_t *data, uint32_t length) {
    if (buffer->pos + length >= SPI_DEVICE_BUFFER_SIZE_BYTES) {
        printf("nileswan/spi: !!! BUFFER OVERRUN !!! (%d + %d >= %d)\n", buffer->pos, length, SPI_DEVICE_BUFFER_SIZE_BYTES);
        exit(1);
    }
    if (data != NULL) {
        memcpy(buffer->data + buffer->pos, data, length);
    } else {
        memset(buffer->data + buffer->pos, 0xFF, length);
    }
    buffer->pos += length;
}

static bool spi_buffer_pop(nile_spi_device_buffer_t *buffer, uint8_t *data, uint32_t length) {
    uint32_t copy_length = length;
    if (length > buffer->pos) {
        copy_length = buffer->pos;
    }

    buffer->pos -= copy_length;
    length -= copy_length;
    if (data != NULL) {
        memcpy(data, buffer->data, copy_length);
        data += copy_length;
        while (length > 0) {
            *(data++) = 0xFF;
            length--;
        }
    }
    if (buffer->pos > 0) {
        memmove(buffer->data, buffer->data + copy_length, buffer->pos);
    }
    return copy_length > 0;
}

static const uint8_t spi_flash_mfr_id = 0xEF;
static const uint8_t spi_flash_dev_id = 0x14;
static const uint8_t spi_flash_jedec_id[] = { spi_flash_mfr_id, 0x40, 0x15 };
static const uint8_t spi_flash_uuid[] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };

static uint8_t spi_flash_exchange(uint8_t tx) {
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

static uint8_t spi_tf_exchange(uint8_t tx) {
    uint8_t rx;
    uint8_t response[1024];

    if (((nile_spi_cnt & NILE_SPI_DEV_MASK) == NILE_SPI_DEV_TF) && !(nile_pow_cnt & NILE_POW_TF))
        return 0xFF;

    if (spi_tf.rx.pos || spi_tf.writing || tx < 0x80)
        spi_buffer_push(&spi_tf.rx, &tx, 1);
    spi_buffer_pop(&spi_tf.tx, &rx, 1);

    if (spi_tf.writing) {
        // handle card output
        if (rx != 0xFF)
            return rx;

        // remove stall bytes
        while (spi_tf.rx.data[0] == 0xFF && spi_tf.rx.pos)
            spi_buffer_pop(&spi_tf.rx, NULL, 1);

        // data token present?
        if (!spi_tf.rx.pos)
            return 0xFF;

        if (spi_tf.writing == SPI_TF_WRITING_SINGLE) {
            if (spi_tf.rx.data[0] != 0xFE) {
                printf("nileswan/spi/tf: unexpected data block start %02x\n", spi_tf.rx.data[0]);
                spi_tf.writing = 0;
                spi_buffer_pop(&spi_tf.rx, NULL, 1);
                return 0xFF;
            }
            // write data block
            if (spi_tf.rx.pos < 515)
                return 0xFF;
            if (!feof(file_tf))
                fwrite(spi_tf.rx.data + 1, 512, 1, file_tf);
            spi_tf.writing = 0;
            spi_buffer_pop(&spi_tf.rx, NULL, 515);

            response[0] = 0xE5;
            spi_buffer_push(&spi_tf.tx, response, 1);
        }

        if (spi_tf.writing == SPI_TF_WRITING_MULTIPLE) {
            if (spi_tf.rx.data[0] != 0xFC) {
                if (spi_tf.rx.data[0] != 0xFD)
                    printf("nileswan/spi/tf: unexpected data block start %02x\n", spi_tf.rx.data[0]);
                spi_tf.writing = 0;
                spi_buffer_pop(&spi_tf.rx, NULL, 1);
                return 0xFF;
            }
            // write data block
            if (spi_tf.rx.pos < 515)
                return 0xFF;
            if (!feof(file_tf))
                fwrite(spi_tf.rx.data + 1, 512, 1, file_tf);
            spi_buffer_pop(&spi_tf.rx, NULL, 515);

            response[0] = 0xE5;
            spi_buffer_push(&spi_tf.tx, response, 1);
        }
    }

    if (spi_tf.reading && !spi_tf.tx.pos) {
        response[0] = 0xFE;
        for (int i = 0; i < 512; i++) {
            response[1 + i] = file_tf != NULL ? fgetc(file_tf) : i;
        }
        // TODO: CRC
        spi_buffer_push(&spi_tf.tx, response, 515);
    }

    while (spi_tf.rx.pos >= 6) {
        while (spi_tf.rx.data[0] >= 0x80 && spi_tf.rx.pos)
            spi_buffer_pop(&spi_tf.rx, NULL, 1);
        if (spi_tf.rx.pos < 6)
            break;

        uint8_t cmd = spi_tf.rx.data[0];
        uint32_t arg = 
            (spi_tf.rx.data[1] << 24) | 
            (spi_tf.rx.data[2] << 16) | 
            (spi_tf.rx.data[3] << 8) | 
            spi_tf.rx.data[4];
        uint32_t response_length = 1;
        response[0] = 0;
        switch (cmd & 0x3F) {
            case 0:
                printf("nileswan/spi/tf: reset\n");
                spi_tf.status = 0x01;
                break;
            case 1:
                printf("nileswan/spi/tf: init\n");
                spi_tf.status = 0x00;
                break;
            case 8:
                printf("nileswan/spi/tf: read interface configuration\n");
                response[1] = 0;
                response[2] = 0;
                response[3] = 0x1;
                response[4] = arg & 0xFF;
                response_length = 5;
                break;
            case 12:
                printf("nileswan/spi/tf: stop reading\n");
                spi_tf.reading = false;
                spi_tf.tx.pos = 0;
                response[0] = 0xFF; // skipped byte
		response[1] = 0xFF; // command processing delay
		response[2] = 0x00; // command response
		memset(response + 3, 0, TF_STOP_TRANSFER_BUSY_DELAY_BYTES);
                response[3 + TF_STOP_TRANSFER_BUSY_DELAY_BYTES] = 0xFF;
                response_length = 4 + TF_STOP_TRANSFER_BUSY_DELAY_BYTES;
                break;
            case 16:
                printf("nileswan/spi/tf: set block length = %d\n", arg);
                if (arg != 512)
                    response[0] |= TF_PARAMETER_ERROR;
                break;
            case 17:
            case 18: {
                printf("nileswan/spi/tf: reading %s @ %08X\n",
                    (cmd & 0x3F) == 18 ? "multiple sectors" : "single sector",
                    arg);
                int data_ofs = TF_DATA_BLOCK_READ_DELAY_BYTES;
                response_length = data_ofs + 515;
                memset(response + 1, 0xFF, response_length - 1);
                if (file_tf != NULL) {
                    fseek(file_tf, arg, SEEK_SET);
                }
                response[data_ofs] = 0xFE;
                for (int i = 0; i < 512; i++) {
                    response[data_ofs + 1 + i] = file_tf != NULL ? fgetc(file_tf) : i;
                }
                // TODO: CRC
                if ((cmd & 0x3F) == 18) {
                    spi_tf.reading = true;
                }
            } break;
            case 24:
            case 25: {
                printf("nileswan/spi/tf: writing %s @ %08X\n",
                    (cmd & 0x3F) == 25 ? "multiple sectors" : "single sector",
                    arg);
                if (file_tf != NULL) {
                    fseek(file_tf, arg, SEEK_SET);
                }
                spi_tf.writing = (cmd & 0x3F) == 25 ? SPI_TF_WRITING_MULTIPLE : SPI_TF_WRITING_SINGLE;
            } break;
            default:
                printf("nileswan/spi/tf: unknown command %d\n", cmd & 0x3F);
                response[0] |= TF_ILLEGAL_COMMAND;
                break;
        }
        spi_buffer_pop(&spi_tf.rx, NULL, 6);
        response[0] |= spi_tf.status;
        spi_buffer_push(&spi_tf.tx, response, response_length);
    }
    return rx;
}

/* === Constants and initialization === */

#define PSRAM_SIZE_BYTES ((PSRAM_MAX_BANK + 1) * 0x10000)
#define SRAM_SIZE_BYTES ((SRAM_MAX_BANK + 1) * 0x10000)
#define PSRAM_MASK_BYTES (PSRAM_SIZE_BYTES - 1)
#define SRAM_MASK_BYTES (SRAM_SIZE_BYTES - 1)
#define SPI_BUFFER_SIZE_BYTES 512
#define SPI_BUFFER_MASK_BYTES (SPI_BUFFER_SIZE_BYTES - 1)

static uint8_t *nile_psram, *nile_sram;
static bool nileswan_initialized = false;

static uint8_t nile_spi_rx[SPI_BUFFER_SIZE_BYTES * 2];
static uint8_t nile_spi_tx[SPI_BUFFER_SIZE_BYTES * 2];

bool nileswan_is_active(void) {
    return nileswan_initialized;
}

bool nileswan_init(void) {
    bank_rom0 = 0xFFFF;
    bank_rom1 = 0xFFFF;
    bank_romL = 0x00FF;
    bank_ram = 0xFFFF;
    flash_enable = 0;
    nile_spi_cnt = 0;
    nile_pow_cnt = 0x01;
    nile_irq = 0;
    nile_bank_mask = 0xFFFF;
    
    memset(&spi_flash, 0, sizeof(spi_flash));
    memset(&spi_tf, 0, sizeof(spi_tf));

    if (!nileswan_initialized) {
        nile_psram = (uint8_t*) malloc(PSRAM_SIZE_BYTES);
        nile_sram = (uint8_t*) malloc(SRAM_SIZE_BYTES);
    }
    nileswan_initialized = true;

    return true;
}

void nileswan_quit(void) {
    if (!nileswan_initialized) {
        return;
    }
    nileswan_initialized = false;
    free(nile_psram);
    free(nile_sram);
}

void nileswan_open_spi(const char *path) {
    file_flash = fopen(path, "r+b");
}

void nileswan_open_tf(const char *path) {
    file_tf = fopen(path, "r+b");
}

/* === IO handling === */

static uint32_t get_spi_bank_offset(bool is_swan) {
    bool is_back_buffer = (nile_spi_cnt & NILE_SPI_BUFFER_IDX) != 0;
    is_back_buffer ^= is_swan;
    return is_back_buffer ? 0 : SPI_BUFFER_SIZE_BYTES;
}

static uint8_t spi_exchange(uint8_t tx) {
    if ((nile_spi_cnt & NILE_SPI_DEV_MASK) == NILE_SPI_DEV_TF) {
        return spi_tf_exchange(tx);
    } else if ((nile_spi_cnt & NILE_SPI_DEV_MASK) == NILE_SPI_DEV_FLASH) {
        return spi_flash_exchange(tx);
    } else {
        return 0xFF;
    }
}

static void spi_cnt_update(uint16_t prev_spi_cnt) {
    if (!(nile_spi_cnt & NILE_SPI_390KHZ) && !(nile_pow_cnt & NILE_POW_CLOCK))
        return;

    if ((nile_spi_cnt & NILE_SPI_DEV_MASK) != NILE_SPI_DEV_FLASH) {
        spi_flash.tx.pos = 0;
        spi_flash.rx.pos = 0;
        spi_flash.mode = 0;
    }

    if (nile_spi_cnt & NILE_SPI_BUSY) {
        const char *device_name = "none";
	if ((nile_spi_cnt & NILE_SPI_DEV_MASK) == NILE_SPI_DEV_TF)
            device_name = "TF card";
	else if ((nile_spi_cnt & NILE_SPI_DEV_MASK) == NILE_SPI_DEV_FLASH)
            device_name = "SPI flash";
	else if ((nile_spi_cnt & NILE_SPI_DEV_MASK) == NILE_SPI_DEV_MCU)
            device_name = "MCU";

        uint8_t *tx_buffer = nile_spi_tx + get_spi_bank_offset(false);
        uint8_t *rx_buffer = nile_spi_rx + get_spi_bank_offset(false);
        uint32_t length = (nile_spi_cnt & 0x1FF) + 1;
        uint32_t pos = 0;
        uint16_t mode = nile_spi_cnt & NILE_SPI_MODE_MASK;
        if (mode == NILE_SPI_MODE_WAIT_READ) {
            int32_t timeout = 8192;
            uint32_t bytes_skipped = 0;
            while (--timeout) {
                if ((rx_buffer[0] = spi_exchange(0xFF)) != 0xFF)
                    break;
                bytes_skipped++;
            }
            if (timeout <= 0) {
                printf("nileswan/spi: !!! WAIT_READ timeout for %s !!!\n", device_name);
                return;
            } else if (bytes_skipped > 0) {
                printf("nileswan/spi: skipped %d bytes\n", bytes_skipped);
            }
            pos++;
        }
        bool mode_reads = mode != NILE_SPI_MODE_WRITE;
        bool mode_writes = mode == NILE_SPI_MODE_WRITE || mode == NILE_SPI_MODE_EXCH;
        for (; pos < length; pos++) {
            uint8_t rx = spi_exchange(mode_writes ? tx_buffer[pos] : 0xFF);
            if (mode_reads) rx_buffer[pos] = rx;
        }
        printf("nileswan/spi: %s %d bytes %s %s",
            mode_reads ? (mode_writes ? "exchanging" : "reading") : (mode_writes ? "writing" : "???"),
            length,
            mode_reads ? (mode_writes ? "with" : "from") : (mode_writes ? "to" : "with"),
            device_name
        );
        if (mode_writes) {
            printf(" [%02x", tx_buffer[0]);
            for(pos = 1; pos < length; pos++)
                printf(" %02x", tx_buffer[pos]);
            printf("]");
        }
        if (mode_reads) {
            printf(" [%02x", rx_buffer[0]);
            for(pos = 1; pos < length; pos++)
                printf(" %02x", rx_buffer[pos]);
            printf("]");
        }
        printf("\n");
        nile_spi_cnt = nile_spi_cnt & ~NILE_SPI_BUSY;
    }
}

static void pow_cnt_update(void) {
    if (!(nile_pow_cnt & NILE_POW_TF)) {
        memset(&spi_tf, 0, sizeof(spi_tf));
    }
}

/* === Cartridge memory/IO routing === */

uint8_t nileswan_io_read(uint32_t index, bool is_debugger) {
    switch (index) {
        case IO_CART_FLASH:
            return flash_enable;
        case IO_BANK_ROM_LINEAR:
            return bank_romL;
        case IO_BANK_RAM:
        case IO_BANK_2003_RAM:
            return bank_ram;
        case IO_BANK_2003_RAM+1:
            return bank_ram >> 8;
        case IO_BANK_ROM0:
        case IO_BANK_2003_ROM0:
            return bank_rom0;
        case IO_BANK_2003_ROM0+1:
            return bank_rom0 >> 8;
        case IO_BANK_ROM1:
        case IO_BANK_2003_ROM1:
            return bank_rom1;
        case IO_BANK_2003_ROM1+1:
            return bank_rom1 >> 8;
        case IO_NILE_POW_CNT:
            return nile_pow_cnt;
        case IO_NILE_IRQ:
            return nile_irq;
        case IO_NILE_SEG_MASK:
            return nile_bank_mask;
        case IO_NILE_SEG_MASK + 1:
            return nile_bank_mask >> 8;
        case IO_NILE_SPI_CNT:
            return nile_spi_cnt;
        case IO_NILE_SPI_CNT + 1:
            return nile_spi_cnt >> 8;
        default:
            return 0x00;
    }
}

void nileswan_io_write(uint32_t index, uint8_t value) {
    switch (index) {
        case IO_CART_FLASH:
            flash_enable = value & 0x01;
            break;
        case IO_BANK_ROM_LINEAR:
            bank_romL = value;
            break;
        case IO_BANK_RAM:
        case IO_BANK_2003_RAM:
            bank_ram = (bank_ram & 0xFF00) | value;
            break;
        case IO_BANK_2003_RAM+1:
            bank_ram = (bank_ram & 0xFF) | (value << 8);
            break;
        case IO_BANK_ROM0:
        case IO_BANK_2003_ROM0:
            bank_rom0 = (bank_rom0 & 0xFF00) | value;
            break;
        case IO_BANK_2003_ROM0+1:
            bank_rom0 = (bank_rom0 & 0xFF) | (value << 8);
            break;
        case IO_BANK_ROM1:
        case IO_BANK_2003_ROM1:
            bank_rom1 = (bank_rom1 & 0xFF00) | value;
            break;
        case IO_BANK_2003_ROM1+1:
            bank_rom1 = (bank_rom1 & 0xFF) | (value << 8);
            break;
        case IO_NILE_POW_CNT:
            nile_pow_cnt = value;
            pow_cnt_update();
            break;
        case IO_NILE_IRQ:
            nile_irq = value;
            break;
        case IO_NILE_SEG_MASK:
            nile_bank_mask = (nile_bank_mask & 0xFF00) | value;
            break;
        case IO_NILE_SEG_MASK + 1:
            nile_bank_mask = (nile_bank_mask & 0xFF) | (value << 8);
            break;
        case IO_NILE_SPI_CNT:
            if (nile_spi_cnt & NILE_SPI_BUSY)
                break;
            nile_spi_cnt = (nile_spi_cnt & 0xFF00) | value;
            // spi_cnt_update();
            break;
        case IO_NILE_SPI_CNT + 1: {
            if (nile_spi_cnt & NILE_SPI_BUSY)
                break;
            uint16_t old_spi_cnt = nile_spi_cnt;
            nile_spi_cnt = (nile_spi_cnt & 0xFF) | (value << 8);
            spi_cnt_update(old_spi_cnt);
        } break;
    }
}

static inline void resolve_bank(uint32_t address, uint8_t **buffer, bool write, bool is_debugger) {
    uint8_t cpu_bank = (address >> 16) & 0xF;
    bool is_ram = (cpu_bank == 1) && !flash_enable;

    uint32_t physical_bank;
    if (cpu_bank == 1) {
        physical_bank = bank_ram;
    } else if (cpu_bank == 2) {
        physical_bank = bank_rom0;
    } else if (cpu_bank == 3) {
        physical_bank = bank_rom1;
    } else {
        physical_bank = (bank_romL << 4) | cpu_bank;
    }
    *buffer = NULL;
    if (is_ram) {
        physical_bank &= (nile_bank_mask >> 12);
        uint32_t physical_address = (physical_bank << 16) | (address & 0xFFFF);

        if (physical_bank <= SRAM_MAX_BANK) {
            *buffer = nile_sram + physical_address;
        } else if (physical_bank == NILE_SEG_RAM_TX && (write || is_debugger)) {
            *buffer = nile_spi_tx + (physical_address & SPI_BUFFER_MASK_BYTES) + get_spi_bank_offset(true);
        }
    } else {
        physical_bank &= (nile_bank_mask & 0x1FF);
        uint32_t physical_address = (physical_bank << 16) | (address & 0xFFFF);

        if (physical_bank <= PSRAM_MAX_BANK) {
            *buffer = nile_psram + physical_address;
        } else if (physical_bank == NILE_SEG_ROM_BOOT) {
            *buffer = wsCartROM + (rom_size - 512) + (physical_address & 511);
        } else if (physical_bank == NILE_SEG_ROM_RX && (!write || is_debugger)) {
            *buffer = nile_spi_rx + (physical_address & SPI_BUFFER_MASK_BYTES) + get_spi_bank_offset(true);
        }
    }
}

uint8_t nileswan_cart_read(uint32_t index, bool is_debugger) {
    uint8_t *buffer;
    resolve_bank(index, &buffer, false, is_debugger);
    if (buffer != NULL) {
        return *buffer;
    } else {
        return 0xFF;
    }
}

void nileswan_cart_write(uint32_t index, uint8_t value) {
    uint8_t *buffer;
    resolve_bank(index, &buffer, true, false);
    if (buffer != NULL) {
        *buffer = value;
    }
}
