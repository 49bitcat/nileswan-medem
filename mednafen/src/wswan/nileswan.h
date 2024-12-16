#ifndef __NILESWAN_H__
#define __NILESWAN_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

#endif /* __NILESWAN_H__ */
