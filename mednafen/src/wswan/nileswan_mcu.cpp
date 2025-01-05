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
    bool boot_mode;
    bool boot_started;
    bool boot_waiting_ack;
    uint8_t boot_cmd;
    uint8_t boot_step;
    uint16_t boot_erase_count;
    uint32_t boot_dest_address;
} spi_mcu;

static bool spi_mcu_persistent_initialized = false;
struct {
    uint8_t eeprom_mode;
    uint16_t eeprom_data[1024];
} spi_mcu_persistent;

static void spi_mcu_send_response(uint16_t len, const void *buffer) {
    uint16_t key = len << 1;
    spi_buffer_push(&spi_mcu.tx, (const uint8_t*) &key, 2);
    spi_buffer_push(&spi_mcu.tx, (const uint8_t*) buffer, len);
}

static void spi_mcu_boot_send_ack(bool is_ack) {
    uint8_t r = 0;
    spi_buffer_push(&spi_mcu.tx, &r, 1); // empty byte
    r = is_ack ? NILE_MCU_BOOT_ACK : NILE_MCU_BOOT_NACK;
    spi_buffer_push(&spi_mcu.tx, &r, 1); // ACK
    spi_mcu.boot_waiting_ack = true;
}

uint8_t nile_spi_mcu_boot_exchange(uint8_t tx) {
    uint8_t rx = NILE_MCU_BOOT_NACK;
    if (spi_mcu.tx.pos && spi_buffer_pop(&spi_mcu.tx, &rx, 1))
        return rx;
    spi_buffer_push(&spi_mcu.rx, &tx, 1);

    if (spi_mcu.rx.pos) {
        if ((spi_mcu.boot_waiting_ack || !spi_mcu.boot_step) && spi_mcu.rx.data[0] == NILE_MCU_BOOT_ACK) {
            printf("nileswan/spi/mcu/boot: ack\n");
            spi_mcu.boot_waiting_ack = false;
            spi_buffer_pop(&spi_mcu.rx, NULL, 1);
            if (spi_mcu.boot_cmd == NILE_MCU_BOOT_JUMP && spi_mcu.boot_step == 2) {
                printf("nileswan/spi/mcu/boot: stub: jump to %08X\n", spi_mcu.boot_dest_address);
                spi_mcu.boot_mode = false;
            }
        } else if (spi_mcu.boot_waiting_ack) {
            spi_buffer_pop(&spi_mcu.rx, NULL, 1);
            return rx;
        } else if (spi_mcu.boot_step) {
            switch (spi_mcu.boot_cmd) {
                case NILE_MCU_BOOT_JUMP:
                case NILE_MCU_BOOT_WRITE_MEMORY: {
                    if (spi_mcu.boot_step == 1) {
                        if (spi_mcu.rx.pos < 5) return rx;
                        spi_mcu.boot_dest_address = (spi_mcu.rx.data[0] << 24)
				| (spi_mcu.rx.data[1] << 16)
				| (spi_mcu.rx.data[2] << 8)
				| spi_mcu.rx.data[3];
                        spi_mcu.boot_step = 2;
                        spi_buffer_pop(&spi_mcu.rx, NULL, 5);
                        spi_mcu_boot_send_ack(true);
                    }
                    if (spi_mcu.boot_step == 2) {
                        if (spi_mcu.rx.pos < 1) return rx;
                        int len = spi_mcu.rx.data[0] + 1;
                        if (spi_mcu.rx.pos < len + 2) return rx;
                        printf("nileswan/spi/mcu/boot: stub: write %d bytes to %08X\n", len, spi_mcu.boot_dest_address);
                        spi_mcu.boot_step = 0;
                        spi_buffer_pop(&spi_mcu.rx, NULL, len + 2);
                        spi_mcu_boot_send_ack(true);
                    }
                } break;
                case NILE_MCU_BOOT_ERASE_MEMORY: {
                    if (spi_mcu.boot_step == 1) {
                        if (spi_mcu.rx.pos < 3) return rx;
                        spi_mcu.boot_erase_count = (spi_mcu.rx.data[0] << 8) | spi_mcu.rx.data[1];
                        spi_mcu.boot_step = 2;
                        spi_buffer_pop(&spi_mcu.rx, NULL, 3);
                        spi_mcu_boot_send_ack(true);
                    }
                    if (spi_mcu.boot_step == 2) {
                        if (spi_mcu.rx.pos < spi_mcu.boot_erase_count*2+3) return rx;
                        printf("nileswan/spi/mcu/boot: stub: erase %d sectors\n", spi_mcu.boot_erase_count);
                        spi_mcu.boot_step = 0;
                        spi_buffer_pop(&spi_mcu.rx, NULL, spi_mcu.boot_erase_count*2+3);
                        spi_mcu_boot_send_ack(true);
                    }
                } break;
            }
        } else if (spi_mcu.rx.data[0] == NILE_MCU_BOOT_START) {
            if (!spi_mcu.boot_started) {
                printf("nileswan/spi/mcu/boot: start\n");
                spi_mcu_boot_send_ack(true);
                spi_mcu.boot_started = true;
                spi_buffer_pop(&spi_mcu.rx, NULL, 1);
                return rx;
            }

            if (spi_mcu.rx.pos < 3) return rx;
            if ((spi_mcu.rx.data[1] ^ 0xFF) != spi_mcu.rx.data[2]) {
                printf("nileswan/spi/mcu/boot: command ID transfer error (%02X %02X)\n", spi_mcu.rx.data[1], spi_mcu.rx.data[2]);
                spi_buffer_pop(&spi_mcu.rx, NULL, 3);
                return rx;
            }

            spi_mcu.boot_cmd = spi_mcu.rx.data[1];
            switch (spi_mcu.rx.data[1]) {
                case NILE_MCU_BOOT_START: {
                    printf("nileswan/spi/mcu/boot: start\n");
                    spi_mcu_boot_send_ack(true);
                } break;
                case NILE_MCU_BOOT_ERASE_MEMORY: {
                    printf("nileswan/spi/mcu/boot: erase memory\n");
                    spi_mcu.boot_step = 1;
                    spi_mcu_boot_send_ack(true);
                } break;
                case NILE_MCU_BOOT_WRITE_MEMORY: {
                    printf("nileswan/spi/mcu/boot: write memory\n");
                    spi_mcu.boot_step = 1;
                    spi_mcu_boot_send_ack(true);
                } break;
                case NILE_MCU_BOOT_JUMP: {
                    printf("nileswan/spi/mcu/boot: jump\n");
                    spi_mcu.boot_step = 1;
                    spi_mcu_boot_send_ack(true);
                } break;
                default: {
                    printf("nileswan/spi/mcu/boot: unknown command %02X\n", spi_mcu.rx.data[1]);
                    spi_mcu_boot_send_ack(false);
                } break;
            }
            spi_buffer_pop(&spi_mcu.rx, NULL, 3);
        } else {
            spi_buffer_pop(&spi_mcu.rx, NULL, 1);
        }
    }

    return rx;
}

uint8_t nile_spi_mcu_exchange(uint8_t tx) {
    if (spi_mcu.boot_mode) {
        return nile_spi_mcu_boot_exchange(tx);
    }

    uint8_t rx = 0xFF;
    uint8_t response[512];
    if (spi_buffer_pop(&spi_mcu.tx, &rx, 1))
        return rx;
    spi_buffer_push(&spi_mcu.rx, &tx, 1);

    if (spi_mcu.rx.pos) {
        // synchronize to command
        if (spi_mcu.rx.data[0] == 0xFF) {
            spi_buffer_pop(&spi_mcu.rx, NULL, 1);
            return rx;
        }
        if (spi_mcu.rx.pos < 2) {
            return rx;
        }

        uint16_t cmd = spi_mcu.rx.data[0] & 0x7F;
        uint16_t arg = (spi_mcu.rx.data[0] >> 7) | spi_mcu.rx.data[1] << 1;
        switch (cmd) {
            case MCU_SPI_CMD_EEPROM_MODE: {
                printf("nileswan/spi/mcu: set EEPROM mode to %d\n", arg);
                spi_mcu_persistent.eeprom_mode = arg;
                spi_buffer_pop(&spi_mcu.rx, NULL, 2);
                response[0] = 1;
                spi_mcu_send_response(1, response);
            } break;
            case MCU_SPI_CMD_EEPROM_READ: {
                if (arg == 0) arg = 512;
                if (spi_mcu.rx.pos < 4) break;
                uint16_t address = spi_mcu.rx.data[2] | (spi_mcu.rx.data[3] << 8);
                printf("nileswan/spi/mcu: read %d words from EEPROM address %04X\n", arg, address);
                spi_buffer_pop(&spi_mcu.rx, NULL, 4);
                spi_mcu_send_response(2 * arg, spi_mcu_persistent.eeprom_data + address);
            } break;
            case MCU_SPI_CMD_EEPROM_GET_MODE: {
                printf("nileswan/spi/mcu: get EEPROM mode (%d)\n", spi_mcu_persistent.eeprom_mode);
                spi_buffer_pop(&spi_mcu.rx, NULL, 2);
                response[0] = spi_mcu_persistent.eeprom_mode;
                spi_mcu_send_response(1, response);
            } break;
            case MCU_SPI_CMD_USB_CDC_READ: {
                if (arg == 0) arg = 512;
                if (arg > MCU_MAX_PER_USB_CDC_PACKET) arg = MCU_MAX_PER_USB_CDC_PACKET;
                spi_buffer_pop(&spi_mcu.rx, NULL, 2);
                uint16_t len = 0;
                for (; len < arg; len++) {
                    if (!Comm_RecvByte(response + len))
                        break;
                }
                printf("nileswan/spi/mcu: USB read %d bytes, found %d\n", arg, len);
                spi_mcu_send_response(len, response);
            } break;
            case MCU_SPI_CMD_USB_CDC_WRITE: {
                if (arg == 0) arg = 512;
                if (spi_mcu.rx.pos < 2+arg) break;
                spi_buffer_pop(&spi_mcu.rx, NULL, 2);
                printf("nileswan/spi/mcu: USB write %d bytes\n", arg);
                int len = 0;
                for (; len < arg; len++) {
                    if (!Comm_SendByte(spi_mcu.rx.data[len]))
                        break;
                }
                spi_buffer_pop(&spi_mcu.rx, NULL, arg);
                spi_mcu_send_response(2, &len);
            } break;
            case MCU_SPI_CMD_ECHO: {
                if (arg == 0) arg = 512;
                if (spi_mcu.rx.pos < 2+arg) break;
                spi_buffer_pop(&spi_mcu.rx, NULL, 2);
                memcpy(response, spi_mcu.rx.data, arg);
                spi_buffer_pop(&spi_mcu.rx, NULL, arg);
                spi_mcu_send_response(arg, response);
            } break;
            default: {
                printf("nileswan/spi/mcu: unknown command %02X %04X\n", cmd, arg);
                spi_buffer_pop(&spi_mcu.rx, NULL, 2);
            } break;
        }
    }

    return rx;
}

void nile_spi_mcu_reset(bool full, bool boot_mode) {
    if (!spi_mcu_persistent_initialized) {
        memset(&spi_mcu_persistent, 0, sizeof(spi_mcu_persistent));
        spi_mcu_persistent_initialized = true;
    }
    if (full) {
        memset(&spi_mcu, 0, sizeof(spi_mcu));
    }
    spi_mcu.boot_mode = boot_mode;
}
