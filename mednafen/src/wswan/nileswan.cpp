#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "nileswan.h"
#include "nileswan_hardware.h"

#define PSRAM_MAX_BANK 127
#define SRAM_MAX_BANK 7

namespace MDFN_IEN_WSWAN
{
extern uint8_t *wsCartROM;
extern uint32_t rom_size;
}
using namespace MDFN_IEN_WSWAN;

/* === SPI devices === */

#define SPI_DEVICE_BUFFER_SIZE_BYTES 4096

typedef struct {
    uint8_t data[SPI_DEVICE_BUFFER_SIZE_BYTES];
    uint32_t pos;
} nile_spi_device_buffer_t;

struct {
    nile_spi_device_buffer_t tx;
    nile_spi_device_buffer_t rx;

    bool reading;
    uint32_t read_position;
} spi_flash;
static FILE *file_flash;

struct {
    nile_spi_device_buffer_t tx;
    nile_spi_device_buffer_t rx;

    uint8_t status;

    bool reading;
} spi_tf;
static FILE *file_tf;

static void spi_buffer_push(nile_spi_device_buffer_t *buffer, const uint8_t *data, uint32_t length) {
    if (buffer->pos + length >= SPI_DEVICE_BUFFER_SIZE_BYTES) {
        printf("nileswan/spi: !!! BUFFER OVERRUN !!! (%d + %d >= %d)\n", buffer->pos, length, SPI_DEVICE_BUFFER_SIZE_BYTES);
        exit(1);
    }
    memcpy(buffer->data + buffer->pos, data, length);
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

static uint8_t spi_flash_exchange(uint8_t tx) {
    uint8_t rx;
    if (spi_flash.reading) {
        if (file_flash != NULL)
            rx = feof(file_flash) ? 0xFF : fgetc(file_flash);
        else
            rx = 0x90;
        spi_flash.read_position++;
    } else {
        spi_buffer_push(&spi_flash.rx, &tx, 1);
        spi_buffer_pop(&spi_flash.tx, &rx, 1);

        switch (spi_flash.rx.data[0]) {
            case 0x03: // Read Data
                if (spi_flash.rx.pos >= 4) {
                    spi_flash.reading = true;
                    spi_flash.read_position =
                        (spi_flash.rx.data[1] << 16) |
                        (spi_flash.rx.data[2] << 8) |
                        spi_flash.rx.data[3];
                    spi_buffer_pop(&spi_flash.rx, NULL, 4);
                    if (file_flash != NULL)
                        fseek(file_flash, spi_flash.read_position, SEEK_SET);
                    printf("nileswan/spi/flash: read starting at location %06X\n", spi_flash.read_position);
                }
                break;
        }
    }
    return rx;
}

static uint8_t spi_tf_exchange(uint8_t tx) {
    uint8_t rx;
    uint8_t response[1024];

    if (spi_tf.rx.pos || tx < 0x80)
        spi_buffer_push(&spi_tf.rx, &tx, 1);
    spi_buffer_pop(&spi_tf.tx, &rx, 1);
    
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
                response[3] = 0;
                response[4] = 0;
                response_length = 5;
                break;
            case 12:
                printf("nileswan/spi/tf: stop reading\n");
                spi_tf.reading = false;
                spi_tf.tx.pos = 0;
                response[0] = 0xFF;
                response[1] = 0;
                response_length = 2;
                break;
            case 16:
                printf("nileswan/spi/tf: set block length = %d\n", arg);
                break;
            case 17:
            case 18: {
                printf("nileswan/spi/tf: reading %s @ %08X\n",
                    (cmd & 0x3F) == 18 ? "multiple sectors" : "single sector",
                    arg);
                int data_ofs = 16;
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
            default:
                printf("nileswan/spi/tf: unknown command %d\n", cmd & 0x3F);
                response[0] |= 0x04;                
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
static uint16_t bank_rom0, bank_rom1, bank_romL, bank_ram;
static uint8_t flash_enable;
static uint8_t nile_pow_cnt, nile_irq;
static uint16_t nile_spi_cnt, nile_bank_mask;
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
    if (nile_spi_cnt & NILE_SPI_DEV_TF) {
        return spi_tf_exchange(tx);
    } else {
        return spi_flash_exchange(tx);
    }
}

static void spi_cnt_update(void) {
    if (nile_spi_cnt & NILE_SPI_BUSY) {
        const char *device_name = (nile_spi_cnt & NILE_SPI_DEV_TF) ? "TF card" : "onboard flash";

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
        printf("nileswan/spi: %s %d bytes %s %s\n",
            mode_reads ? (mode_writes ? "exchanging" : "reading") : (mode_writes ? "writing" : "???"),
            length,
            mode_reads ? (mode_writes ? "with" : "from") : (mode_writes ? "to" : "with"),
            device_name
        );
        for (; pos < length; pos++) {
            uint8_t rx = spi_exchange(mode_writes ? tx_buffer[pos] : 0xFF);
            if (mode_reads) rx_buffer[pos] = rx;
        }
        nile_spi_cnt = nile_spi_cnt & ~NILE_SPI_BUSY;
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
            nile_spi_cnt = (nile_spi_cnt & 0xFF00) | value;
            // spi_cnt_update();
            break;
        case IO_NILE_SPI_CNT + 1:
            nile_spi_cnt = (nile_spi_cnt & 0xFF) | (value << 8);
            spi_cnt_update();
            break;
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
