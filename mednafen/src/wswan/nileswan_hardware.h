#ifndef __NILESWAN_HARDWARE_H__
#define __NILESWAN_HARDWARE_H__

#define IO_BANK_RAM 0xC1
#define IO_BANK_ROM0 0xC2
#define IO_BANK_ROM1 0xC3
#define IO_BANK_ROM_LINEAR 0xC0

#define IO_CART_EEP_DATA 0xC4
#define IO_CART_EEP_CMD  0xC6
#define IO_CART_EEP_CTRL 0xC8

#define IO_CART_RTC_CTRL 0xCA
#define CART_RTC_READY  0x80
#define CART_RTC_ACTIVE 0x10
#define CART_RTC_READ   0x00
#define CART_RTC_WRITE  0x01
#define CART_RTC_CMD_RESET    0x00
#define CART_RTC_CMD_STATUS   0x02
#define CART_RTC_CMD_DATETIME 0x04
#define CART_RTC_CMD_TIME     0x06
#define CART_RTC_CMD_INTCFG   0x08
#define CART_RTC_CMD_NOP      0x0A

#define IO_CART_RTC_DATA 0xCB

#define IO_CART_GPO_CTRL 0xCC
#define IO_CART_GPO_DATA 0xCD
#define CART_GPO_ENABLE(n) (1 << (n))
#define CART_GPO_MASK(n)   (1 << (n))

#define IO_CART_FLASH 0xCE
#define CART_FLASH_ENABLE  0x01
#define CART_FLASH_DISABLE 0x00

#define IO_BANK_2003_RAM 0xD0
#define IO_BANK_2003_ROM0 0xD2
#define IO_BANK_2003_ROM1 0xD4

#define IO_CART_KARNAK_TIMER 0xD6
#define CART_KARNAK_TIMER_ENABLE 0x80

#define IO_CART_KARNAK_ADPCM_INPUT 0xD8
#define IO_CART_KARNAK_ADPCM_OUTPUT 0xD9
#define NILE_SPI_MODE_WRITE      0x0000
#define NILE_SPI_MODE_READ       0x0200
#define NILE_SPI_MODE_EXCH       0x0400
#define NILE_SPI_MODE_WAIT_READ  0x0600
#define NILE_SPI_MODE_MASK       0x0600
#define NILE_SPI_390KHZ          0x0800
#define NILE_SPI_25MHZ           0x0000
#define NILE_SPI_CS_HIGH         0x0000
#define NILE_SPI_CS_LOW          0x1000
#define NILE_SPI_CS              0x1000
#define NILE_SPI_DEV_FLASH       0x0000
#define NILE_SPI_DEV_TF          0x2000
#define NILE_SPI_BUFFER_IDX      0x4000
#define NILE_SPI_START           0x8000
#define NILE_SPI_BUSY            0x8000
#define IO_NILE_SPI_CNT    0xE0

#define NILE_POW_CLOCK     0x01
#define NILE_POW_TF        0x02
#define IO_NILE_POW_CNT    0xE2

#define NILE_IRQ_ENABLE    0x01
#define NILE_IRQ_SPI       0x02
#define IO_NILE_IRQ        0xE3

#define IO_NILE_SEG_MASK   0xE4

#define NILE_SEG_RAM_TX    15
#define NILE_SEG_ROM_RX    510
#define NILE_SEG_ROM_BOOT  511

#endif
