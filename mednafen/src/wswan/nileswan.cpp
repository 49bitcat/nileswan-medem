#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
            break;
        case IO_NILE_SPI_CNT + 1:
            nile_spi_cnt = (nile_spi_cnt & 0xFF) | (value << 8);
            break;
    }
}

static inline uint32_t get_spi_bank_offset(bool is_swan) {
    bool is_back_buffer = (nile_spi_cnt & NILE_SPI_BUFFER_IDX) != 0;
    return is_back_buffer ? (is_swan ? 0 : SPI_BUFFER_SIZE_BYTES) : (is_swan ? SPI_BUFFER_SIZE_BYTES : 0);
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
        } else if (physical_address == NILE_SEG_ROM_RX && (!write || is_debugger)) {
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
