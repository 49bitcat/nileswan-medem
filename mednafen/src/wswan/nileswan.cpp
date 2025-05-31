#include "wswan.h"
#include "memory.h"
#include "comm.h"
#include "rtc.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "nileswan.h"
#include "nileswan_hardware.h"

#define PSRAM_MAX_BANK 255
#define SRAM_MAX_BANK 7
#define NILE_EMULATED_BOARD_REVISION 1

namespace MDFN_IEN_WSWAN
{
extern uint8_t *wsCartROM;
extern uint32_t rom_size;
}
using namespace MDFN_IEN_WSWAN;

extern FILE *file_flash;
extern FILE *file_tf;

static uint16_t bank_rom0, bank_rom1, bank_romL, bank_ram;
static uint8_t flash_enable;
static uint8_t nile_pow_cnt, nile_emu_cnt;
static uint16_t nile_spi_cnt, nile_bank_mask;
static int8_t nile_fpga_core;

enum
{
 WW_STATE_READ = 0,
 WW_STATE_UNLOCK_1,
 WW_STATE_UNLOCK_2,
 WW_STATE_FAST,
 WW_STATE_FAST_WRITE,
 WW_STATE_WRITE,
 WW_STATE_ERASE
};

static uint8_t nile_ww_state;

void spi_buffer_push(nile_spi_device_buffer_t *buffer, const uint8_t *data, uint32_t length) {
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

bool spi_buffer_pop(nile_spi_device_buffer_t *buffer, uint8_t *data, uint32_t length) {
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

/* === Constants and initialization === */

#define PSRAM_SIZE_BYTES ((PSRAM_MAX_BANK + 1) * 0x10000)
#define SRAM_SIZE_BYTES ((SRAM_MAX_BANK + 1) * 0x10000)
#define PSRAM_MASK_BYTES (PSRAM_SIZE_BYTES - 1)
#define SRAM_MASK_BYTES (SRAM_SIZE_BYTES - 1)
#define SPI_BUFFER_SIZE_BYTES 512
#define SPI_BUFFER_MASK_BYTES (SPI_BUFFER_SIZE_BYTES - 1)

static uint8_t *nile_psram, *nile_sram;
static bool nileswan_initialized = false;

uint8_t nile_ipc[NILE_IPC_SIZE];
static uint8_t nile_spi_rx[SPI_BUFFER_SIZE_BYTES * 2];
static uint8_t nile_spi_tx[SPI_BUFFER_SIZE_BYTES * 2];

bool nileswan_is_active(void) {
    return nileswan_initialized;
}

bool nileswan_is_tf_powered(void) {
    return nile_pow_cnt & NILE_POW_TF;
}

void nile_fpga_reset(void) {
    bank_rom0 = 0xFFFF;
    bank_rom1 = 0xFFFF;
    bank_romL = 0x00FF;
    bank_ram = 0xFFFF;
    flash_enable = 0;
    nile_spi_cnt = 0;
    nile_pow_cnt = NILE_POW_UNLOCK;
    nile_bank_mask = 0xFFFF;
    nile_emu_cnt = 0;
    nile_ww_state = 0;

    memset(&nile_ipc, 0, sizeof(nile_ipc));
}

bool nileswan_init(void) {
    nile_fpga_core = -1;
    nile_fpga_reset();
    nile_spi_mcu_reset(true, false);
    nile_spi_flash_reset(true);
    nile_spi_tf_reset(true);

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
        return nile_spi_tf_exchange(tx);
    } else if ((nile_spi_cnt & NILE_SPI_DEV_MASK) == NILE_SPI_DEV_FLASH) {
        return nile_spi_flash_exchange(tx);
    } else if ((nile_spi_cnt & NILE_SPI_DEV_MASK) == NILE_SPI_DEV_MCU) {
        return nile_spi_mcu_exchange(tx);
    } else {
        return 0xFF;
    }
}

static void spi_cnt_update(uint16_t prev_spi_cnt) {
    if (!(nile_spi_cnt & NILE_SPI_390KHZ) && !(nile_pow_cnt & NILE_POW_CLOCK))
        return;

    if ((nile_spi_cnt & NILE_SPI_DEV_MASK) != NILE_SPI_DEV_FLASH) {
        nile_spi_flash_reset(false);
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
    fflush(stdout);
}

static void pow_cnt_update(uint8_t new_value) {
    uint8_t old_value = nile_pow_cnt;
    nile_pow_cnt = new_value;
    if (!(nile_pow_cnt & NILE_POW_TF)) {
        nile_spi_tf_reset(true);
    }
    if (!(old_value & NILE_POW_MCU_RESET) && (new_value & NILE_POW_MCU_RESET)) {
        printf("nileswan/mcu: reset\n");
        nile_spi_mcu_reset(true, (new_value & NILE_POW_MCU_BOOT0) != 0);
    }
}

/* === Cartridge memory/IO routing === */

uint8_t nileswan_io_read(uint32_t index, bool is_debugger) {
    if(index == 0xCA || index == 0xCB)
        return RTC_Read(index);

    switch (index) {
        case IO_CART_FLASH:
            return flash_enable;
        case IO_BANK_ROM_LINEAR:
            return bank_romL;
        case IO_BANK_2003_ROM_LINEAR:
            return bank_romL;
        case IO_BANK_RAM:
            return bank_ram;
        case IO_BANK_2003_RAM:
            return bank_ram;
        case IO_BANK_2003_RAM+1:
            return bank_ram >> 8;
        case IO_BANK_ROM0:
            return bank_rom0;
        case IO_BANK_2003_ROM0:
            return bank_rom0;
        case IO_BANK_2003_ROM0+1:
            return bank_rom0 >> 8;
        case IO_BANK_ROM1:
            return bank_rom1;
        case IO_BANK_2003_ROM1:
            return bank_rom1;
        case IO_BANK_2003_ROM1+1:
            return bank_rom1 >> 8;
        case IO_NILE_POW_CNT:
            return nile_pow_cnt;
        case IO_NILE_SEG_MASK:
            return nile_bank_mask;
        case IO_NILE_SEG_MASK + 1:
            return nile_bank_mask >> 8;
        case IO_NILE_SPI_CNT:
            return nile_spi_cnt;
        case IO_NILE_SPI_CNT + 1:
            return nile_spi_cnt >> 8;
        case IO_NILE_EMU_CNT:
            return nile_emu_cnt;
        case IO_NILE_BOARD_REVISION:
            return NILE_EMULATED_BOARD_REVISION;
    }
    return 0x00;
}

void nileswan_io_write(uint32_t index, uint8_t value) {
    if((index == 0xCA || index == 0xCB) && (nile_pow_cnt & NILE_POW_IO_2003))
        RTC_Write(index, value);

    switch (index) {
        case IO_CART_FLASH:
            if(!(nile_pow_cnt & NILE_POW_IO_2003)) break;
            flash_enable = value & 0x01;
            break;
        case IO_BANK_ROM_LINEAR:
            bank_romL = value;
            break;
        case IO_BANK_2003_ROM_LINEAR:
            if(!(nile_pow_cnt & NILE_POW_IO_2003)) break;
            bank_romL = value;
            break;
        case IO_BANK_RAM:
            bank_ram = (bank_ram & 0xFF00) | value;
            break;
        case IO_BANK_2003_RAM:
            if(!(nile_pow_cnt & NILE_POW_IO_2003)) break;
            bank_ram = (bank_ram & 0xFF00) | value;
            break;
        case IO_BANK_2003_RAM+1:
            if(!(nile_pow_cnt & NILE_POW_IO_2003)) break;
            bank_ram = (bank_ram & 0xFF) | (value << 8);
            break;
        case IO_BANK_ROM0:
            bank_rom0 = (bank_rom0 & 0xFF00) | value;
            break;
        case IO_BANK_2003_ROM0:
            if(!(nile_pow_cnt & NILE_POW_IO_2003)) break;
            bank_rom0 = (bank_rom0 & 0xFF00) | value;
            break;
        case IO_BANK_2003_ROM0+1:
            if(!(nile_pow_cnt & NILE_POW_IO_2003)) break;
            bank_rom0 = (bank_rom0 & 0xFF) | (value << 8);
            break;
        case IO_BANK_ROM1:
            bank_rom1 = (bank_rom1 & 0xFF00) | value;
            break;
        case IO_BANK_2003_ROM1:
            if(!(nile_pow_cnt & NILE_POW_IO_2003)) break;
            bank_rom1 = (bank_rom1 & 0xFF00) | value;
            break;
        case IO_BANK_2003_ROM1+1:
            if(!(nile_pow_cnt & NILE_POW_IO_2003)) break;
            bank_rom1 = (bank_rom1 & 0xFF) | (value << 8);
            break;
        case IO_NILE_POW_CNT:
            if(!(nile_pow_cnt & NILE_POW_IO_NILE) && value != NILE_POW_UNLOCK) break;
            pow_cnt_update(value);
            break;
        case IO_NILE_WARMBOOT_CNT:
            nile_fpga_core = value & 0x3;
            printf("nileswan/fpga: warmboot to core %d\n", nile_fpga_core);
            nile_fpga_reset();
            break;
        case IO_NILE_SEG_MASK:
            if(!(nile_pow_cnt & NILE_POW_IO_NILE)) break;
            nile_bank_mask = (nile_bank_mask & 0xFF00) | value;
            break;
        case IO_NILE_SEG_MASK + 1:
            if(!(nile_pow_cnt & NILE_POW_IO_NILE)) break;
            nile_bank_mask = (nile_bank_mask & 0xFF) | (value << 8);
            break;
        case IO_NILE_SPI_CNT: {
            if(!(nile_pow_cnt & NILE_POW_IO_NILE)) break;
            uint16_t new_nile_spi_cnt = (nile_spi_cnt & 0xFF00) | value;
            if (nile_spi_cnt & NILE_SPI_BUSY) {
                printf("nileswan/spi: BUG trying to write to SPI control while transfer active (control %04X => %04X)\n",
                    nile_spi_cnt, new_nile_spi_cnt);
                break;
            }
            nile_spi_cnt = new_nile_spi_cnt;
        } break;
        case IO_NILE_SPI_CNT + 1: {
            if(!(nile_pow_cnt & NILE_POW_IO_NILE)) break;
            uint16_t new_nile_spi_cnt = (nile_spi_cnt & 0xFF) | (value << 8);
            if (nile_spi_cnt != new_nile_spi_cnt && (new_nile_spi_cnt | NILE_SPI_BUSY) == nile_spi_cnt) {
                printf("nileswan/spi: abort\n");
            } else if (nile_spi_cnt & NILE_SPI_BUSY) {
                printf("nileswan/spi: BUG trying to write to SPI control while transfer active (control %04X => %04X)\n",
                    nile_spi_cnt, new_nile_spi_cnt);
                break;
            }
            uint16_t old_spi_cnt = nile_spi_cnt;
            nile_spi_cnt = new_nile_spi_cnt;
            printf("nileswan/spi: control = %04X\n", nile_spi_cnt);
            spi_cnt_update(old_spi_cnt);
        } break;
        case IO_NILE_EMU_CNT:
            if(!(nile_pow_cnt & NILE_POW_IO_NILE)) break;
            nile_emu_cnt = value & 0x1F;
            break;
    }
}

static inline void resolve_bank(uint32_t address, uint8_t **buffer, bool write, bool is_debugger) {
    uint8_t cpu_bank = (address >> 16) & 0xF;
    bool is_ram = (cpu_bank == 1) && !flash_enable;

    uint32_t physical_bank;
    uint16_t mask_bit;
    if (cpu_bank == 1) {
        physical_bank = bank_ram;
        mask_bit = NILE_SEG_SRAM_LOCK;
    } else if (cpu_bank == 2) {
        physical_bank = bank_rom0;
        mask_bit = NILE_SEG_ROM0_LOCK;
    } else if (cpu_bank == 3) {
        physical_bank = bank_rom1;
        mask_bit = NILE_SEG_ROM1_LOCK;
    } else {
        physical_bank = (bank_romL << 4) | cpu_bank;
        mask_bit = 0;
    }
    *buffer = NULL;
    if (is_ram) {
	if (!mask_bit || (nile_bank_mask & mask_bit))
            physical_bank &= (nile_bank_mask >> 12);
        else
            physical_bank &= 0xF;
        uint32_t physical_address = (physical_bank << 16) | (address & 0xFFFF);

        if (physical_bank <= SRAM_MAX_BANK) {
            if (nile_pow_cnt & NILE_POW_SRAM) {
                if (nile_emu_cnt & NILE_EMU_SRAM_32KB) {
                    physical_address &= ~0x8000;
                }
                *buffer = nile_sram + physical_address;
            }
        } else if (physical_bank == NILE_SEG_RAM_IPC) {
            *buffer = nile_ipc + (physical_address & (NILE_IPC_SIZE - 1));
        } else if (physical_bank == NILE_SEG_RAM_TX && (write || is_debugger)) {
            *buffer = nile_spi_tx + (physical_address & SPI_BUFFER_MASK_BYTES) + get_spi_bank_offset(true);
        }
    } else {
	if (!mask_bit || (nile_bank_mask & mask_bit))
            physical_bank &= (nile_bank_mask & 0x1FF);
        else
            physical_bank &= 0x1FF;
        uint32_t physical_address = (physical_bank << 16) | (address & 0xFFFF);

        if (physical_bank <= PSRAM_MAX_BANK) {
            *buffer = nile_psram + physical_address;
        } else if (physical_bank == NILE_SEG_ROM_BOOT || physical_bank == NILE_SEG_ROM_BOOT_PCV2) {
            *buffer = wsCartROM + (rom_size - 512) + (physical_address & 511);
        } else if (physical_bank == NILE_SEG_ROM_RX && (!write || is_debugger)) {
            *buffer = nile_spi_rx + (physical_address & SPI_BUFFER_MASK_BYTES) + get_spi_bank_offset(true);
        }
    }
}

uint8_t nileswan_cart_read(uint32_t index, bool is_debugger) {
    uint8_t *buffer;
    resolve_bank(index, &buffer, false, is_debugger);
    if ((nile_emu_cnt & NILE_EMU_FLASH_FSM) && flash_enable && (index & 0xF0000) == 0x10000) {
      if (nile_ww_state == WW_STATE_FAST)
        return 0x00;
      if (nile_ww_state == WW_STATE_ERASE)
        return 0xFF;
    }
    if (buffer != NULL) {
        return *buffer;
    } else {
        return 0xFF;
    }
}

void nileswan_cart_write(uint32_t index, uint8_t value) {
    uint8_t *buffer;
    resolve_bank(index, &buffer, true, false);
    if ((nile_emu_cnt & NILE_EMU_FLASH_FSM) && flash_enable && (index & 0xF0000) == 0x10000) {
      if (nile_ww_state == WW_STATE_READ) {
        if (value == 0xAA) nile_ww_state = WW_STATE_UNLOCK_1;
        else nile_ww_state = WW_STATE_READ;
      }
      else if (nile_ww_state == WW_STATE_UNLOCK_1) {
        if (value == 0x55) nile_ww_state = WW_STATE_UNLOCK_2;
        else nile_ww_state = WW_STATE_READ;
      }
      else if (nile_ww_state == WW_STATE_UNLOCK_2) {
        if (value == 0x20) nile_ww_state = WW_STATE_FAST;
        else if (value == 0xA0) nile_ww_state = WW_STATE_WRITE;
        else if (value == 0x10) nile_ww_state = WW_STATE_ERASE;
        else if (value == 0x30) nile_ww_state = WW_STATE_ERASE;
        else nile_ww_state = WW_STATE_READ;
      }
      else if (nile_ww_state == WW_STATE_FAST) {
        if (value == 0xA0) nile_ww_state = WW_STATE_FAST_WRITE;
        else if (value == 0x90) nile_ww_state = WW_STATE_READ; /* Reset mode */
        else nile_ww_state = WW_STATE_FAST;
      }
      else if (nile_ww_state == WW_STATE_FAST_WRITE) {
        *buffer = value;
        nile_ww_state = WW_STATE_FAST;
      }
      else if (nile_ww_state == WW_STATE_WRITE) {
        *buffer = value;
        nile_ww_state = WW_STATE_READ;
      }
      else if (nile_ww_state == WW_STATE_ERASE) {
        if (value == 0xAA) nile_ww_state = WW_STATE_UNLOCK_1;
        else nile_ww_state = WW_STATE_READ;
      }
      return;
    }
    if (buffer != NULL) {
        *buffer = value;
    }
}
