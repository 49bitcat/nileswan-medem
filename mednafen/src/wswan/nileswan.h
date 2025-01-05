#ifndef __NILESWAN_H__
#define __NILESWAN_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Configuration */
#define MCU_MAX_PER_USB_CDC_PACKET 128

#define TF_STOP_TRANSFER_BUSY_DELAY_BYTES 8
#define TF_DATA_BLOCK_READ_DELAY_BYTES 16

#define SPI_DEVICE_BUFFER_SIZE_BYTES 4096

typedef struct {
    uint8_t data[SPI_DEVICE_BUFFER_SIZE_BYTES];
    uint32_t pos;
} nile_spi_device_buffer_t;

#define NILE_IPC_SIZE 512
extern uint8_t nile_ipc[NILE_IPC_SIZE];

bool nileswan_is_active(void);
bool nileswan_init(void);
void nileswan_quit(void);
void nileswan_open_spi(const char *path);
void nileswan_open_tf(const char *path);
uint8_t nileswan_io_read(uint32_t index, bool is_debugger);
void nileswan_io_write(uint32_t index, uint8_t value);
uint8_t nileswan_cart_read(uint32_t index, bool is_debugger);
void nileswan_cart_write(uint32_t index, uint8_t value);
bool nileswan_is_tf_powered(void);

void spi_buffer_push(nile_spi_device_buffer_t *buffer, const uint8_t *data, uint32_t length);
bool spi_buffer_pop(nile_spi_device_buffer_t *buffer, uint8_t *data, uint32_t length);

uint8_t nile_spi_mcu_exchange(uint8_t tx);
void nile_spi_mcu_reset(bool full);
uint8_t nile_spi_flash_exchange(uint8_t tx);
void nile_spi_flash_reset(bool full);
uint8_t nile_spi_tf_exchange(uint8_t tx);
void nile_spi_tf_reset(bool full);

#endif /* __NILESWAN_H__ */
