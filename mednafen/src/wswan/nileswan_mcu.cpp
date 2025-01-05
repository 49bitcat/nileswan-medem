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
    bool bootloader_mode;
} spi_mcu;

static void spi_mcu_send_response(uint16_t len, const void *buffer) {
    uint16_t key = len << 1;
    spi_buffer_push(&spi_mcu.tx, (const uint8_t*) &key, 2);
    spi_buffer_push(&spi_mcu.tx, (const uint8_t*) buffer, len);
}

uint8_t nile_spi_mcu_boot_exchange(uint8_t tx) {
    return 0x1F;
}

uint8_t nile_spi_mcu_exchange(uint8_t tx) {
    if (spi_mcu.bootloader_mode) {
        return nile_spi_mcu_boot_exchange(tx);
    }

    uint8_t rx = 0xFF;
    uint8_t response[512];
    if (spi_buffer_pop(&spi_mcu.tx, &rx, 1))
        return rx;
    spi_buffer_push(&spi_mcu.rx, &tx, 1);

    if (spi_mcu.rx.pos >= 2) {
        uint16_t cmd = spi_mcu.rx.data[0] & 0x7F;
        uint16_t arg = (spi_mcu.rx.data[0] >> 7) | spi_mcu.rx.data[1] << 1;
        switch (cmd) {
            case 0x7F:
                spi_buffer_pop(&spi_mcu.rx, NULL, 1);
                break;
            case 0x40: {
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
            case 0x41: {
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
            case 0x00: {
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

void nile_spi_mcu_reset(bool full, bool bootloader_mode) {
    if (full) {
        memset(&spi_mcu, 0, sizeof(spi_mcu));
    }
    spi_mcu.bootloader_mode = bootloader_mode;
}
