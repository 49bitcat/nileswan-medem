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
FILE *file_tf;

uint8_t nile_spi_tf_exchange(uint8_t tx) {
    uint8_t rx;
    uint8_t response[1024];

    if (!nileswan_is_tf_powered())
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

void nile_spi_tf_reset(bool full) {
    if (full) {
        memset(&spi_tf, 0, sizeof(spi_tf));
    }
}
