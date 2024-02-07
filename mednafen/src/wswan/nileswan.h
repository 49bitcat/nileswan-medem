#ifndef __NILESWAN_H__
#define __NILESWAN_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool nileswan_is_active(void);
bool nileswan_init(void);
void nileswan_quit(void);
uint8_t nileswan_io_read(uint32_t index, bool is_debugger);
void nileswan_io_write(uint32_t index, uint8_t value);
uint8_t nileswan_cart_read(uint32_t index, bool is_debugger);
void nileswan_cart_write(uint32_t index, uint8_t value);

#endif /* __NILESWAN_H__ */
