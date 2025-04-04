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

#define IO_BANK_2003_ROM_LINEAR 0xCF
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
#define NILE_SPI_DEV_NONE        0x0000
#define NILE_SPI_DEV_TF          0x1000
#define NILE_SPI_DEV_FLASH       0x2000
#define NILE_SPI_DEV_MCU         0x3000
#define NILE_SPI_DEV_MASK        0x3000
#define NILE_SPI_BUFFER_IDX      0x4000
#define NILE_SPI_START           0x8000
#define NILE_SPI_BUSY            0x8000
#define IO_NILE_SPI_CNT    0xE0

#define NILE_POW_CLOCK     0x01
#define NILE_POW_TF        0x02
#define NILE_POW_IO_NILE   0x04
#define NILE_POW_IO_2001   0x08
#define NILE_POW_IO_2003   0x10
#define NILE_POW_MCU_BOOT0 0x20
#define NILE_POW_SRAM      0x40
#define NILE_POW_MCU_RESET 0x80
#define NILE_POW_UNLOCK    0xFD
#define IO_NILE_POW_CNT    0xE2

#define IO_NILE_WARMBOOT_CNT 0xE3

#define NILE_SEG_RAM_MASK  0xF
#define NILE_SEG_RAM_SHIFT 12
#define NILE_SEG_ROM_MASK  0x1FF
#define NILE_SEG_ROM_SHIFT 0
#define NILE_SEG_ROM0_LOCK (1 << 9)
#define NILE_SEG_ROM1_LOCK (1 << 10)
#define NILE_SEG_SRAM_LOCK (1 << 11)
#define IO_NILE_SEG_MASK   0xE4

#define NILE_SEG_RAM_IPC       14
#define NILE_SEG_RAM_TX        15
#define NILE_SEG_ROM_BOOT_PCV2 500
#define NILE_SEG_ROM_RX        510
#define NILE_SEG_ROM_BOOT      511

#define NILE_EMU_EEPROM_MASK 0x3
#define NILE_EMU_EEPROM_128B 0x0
#define NILE_EMU_EEPROM_1KB  0x1
#define NILE_EMU_EEPROM_2KB  0x2
#define IO_NILE_EMU_CNT      0xE6

/* SPI flash defines */

#define NILE_FLASH_ID_W25Q16JV_IQ 0xEF4015
#define NILE_FLASH_ID_W25Q16JV_IM 0xEF7015

#define NILE_FLASH_SR1_BUSY      (1 << 0) ///< Erase/Write in Progress
#define NILE_FLASH_SR1_WEL       (1 << 1) ///< Write Enable Latch
#define NILE_FLASH_SR1_BP0       (1 << 2) ///< Block Protect 0
#define NILE_FLASH_SR1_BP1       (1 << 3) ///< Block Protect 1
#define NILE_FLASH_SR1_BP2       (1 << 4) ///< Block Protect 2
#define NILE_FLASH_SR1_TB        (1 << 5) ///< Top/Bottom Block Protect
#define NILE_FLASH_SR1_SEC       (1 << 6) ///< Sector/Block Protect
#define NILE_FLASH_SR1_CMP       (1 << 7) ///< Complement Protect

#define NILE_FLASH_SR2_SRL       (1 << 0) ///< Status Register Lock
#define NILE_FLASH_SR2_LB1       (1 << 3) ///< Security Register 1 Lock
#define NILE_FLASH_SR2_LB2       (1 << 4) ///< Security Register 2 Lock
#define NILE_FLASH_SR2_LB3       (1 << 5) ///< Security Register 3 Lock
#define NILE_FLASH_SR2_CMP       (1 << 6) ///< Complement Protect
#define NILE_FLASH_SR2_SUS       (1 << 7) ///< Suspend Status

#define NILE_FLASH_SR3_WPS       (1 << 2) ///< Write Protect Selection
#define NILE_FLASH_SR3_DRV_100   (0)      ///< Output Driver Strength 100%
#define NILE_FLASH_SR3_DRV_75    (1 << 5) ///< Output Driver Strength 75%
#define NILE_FLASH_SR3_DRV_50    (2 << 5) ///< Output Driver Strength 50%
#define NILE_FLASH_SR3_DRV_25    (3 << 5) ///< Output Driver Strength 25%
#define NILE_FLASH_SR3_DRV_MASK  (3 << 5) ///< Output Driver Strength Mask

#define NILE_FLASH_CMD_WRSR1     0x01 ///< Write Status Register 1
#define NILE_FLASH_CMD_WRITE     0x02 ///< Write Data
#define NILE_FLASH_CMD_READ      0x03 ///< Read Data
#define NILE_FLASH_CMD_WRDI      0x04 ///< Write Disable
#define NILE_FLASH_CMD_RDSR1     0x05 ///< Read Status Register 1
#define NILE_FLASH_CMD_WREN      0x06 ///< Write Enable
#define NILE_FLASH_CMD_WRSR3     0x11 ///< Write Status Register 3
#define NILE_FLASH_CMD_RDSR3     0x15 ///< Read Status Register 3
#define NILE_FLASH_CMD_ERASE_4K  0x20 ///< Sector Erase (4K)
#define NILE_FLASH_CMD_WRSR2     0x31 ///< Write Status Register 2
#define NILE_FLASH_CMD_RDSR2     0x35 ///< Read Status Register 2
#define NILE_FLASH_CMD_BLOCK_LOCK   0x36 ///< Individual block lock
#define NILE_FLASH_CMD_BLOCK_UNLOCK 0x39 ///< Individual block unlock
#define NILE_FLASH_CMD_BLOCK_RDLOCK 0x3D ///< Read block lock
#define NILE_FLASH_CMD_SEC_WRITE 0x42 ///< Write security registers
#define NILE_FLASH_CMD_SEC_ERASE 0x44 ///< Erase security registers
#define NILE_FLASH_CMD_SEC_READ  0x48 ///< Read security registers
#define NILE_FLASH_CMD_RDUUID    0x4B ///< Read 64-bit Unique ID
#define NILE_FLASH_CMD_ERASE_32K 0x52 ///< Block Erase (32K)
#define NILE_FLASH_CMD_RDSFPD    0x5A ///< Read SFDP
#define NILE_FLASH_CMD_RESET_EN  0x66 ///< Enable reset
#define NILE_FLASH_CMD_SUSPEND   0x75 ///< Erase/Program Suspend
#define NILE_FLASH_CMD_RESUME    0x7A ///< Erase/Program Resume
#define NILE_FLASH_CMD_LOCK      0x7E ///< Global lock
#define NILE_FLASH_CMD_MFR_ID    0x90 ///< Read Manufacturer / Device ID
#define NILE_FLASH_CMD_UNLOCK    0x98 ///< Global unlock
#define NILE_FLASH_CMD_RESET     0x99 ///< Reset device
#define NILE_FLASH_CMD_RDID      0x9F ///< Read JEDEC ID
#define NILE_FLASH_CMD_WAKE_ID   0xAB ///< Release Power-down / Device ID
#define NILE_FLASH_CMD_SLEEP     0xB9 ///< Power-down
#define NILE_FLASH_CMD_ERASE_ALL 0xC7 ///< Chip Erase. Not advisable
#define NILE_FLASH_CMD_ERASE_64K 0xD8 ///< Block Erase (64K)

/* SPI MCU defines */

#define NILE_MCU_BOOT_ACK          0x79
#define NILE_MCU_BOOT_NACK         0x1F
#define NILE_MCU_BOOT_START        0x5A
#define NILE_MCU_BOOT_GET          0x00
#define NILE_MCU_BOOT_GET_VERSION  0x01
#define NILE_MCU_BOOT_GET_ID       0x02
#define NILE_MCU_BOOT_READ_MEMORY  0x11
#define NILE_MCU_BOOT_JUMP         0x21
#define NILE_MCU_BOOT_WRITE_MEMORY 0x31
#define NILE_MCU_BOOT_ERASE_MEMORY 0x44
#define NILE_MCU_BOOT_SPECIAL      0x50
#define NILE_MCU_BOOT_EXT_SPECIAL  0x51
#define NILE_MCU_BOOT_WRITE_LOCK   0x63
#define NILE_MCU_BOOT_WRITE_UNLOCK 0x73
#define NILE_MCU_BOOT_READ_LOCK    0x82
#define NILE_MCU_BOOT_READ_UNLOCK  0x92
#define NILE_MCU_BOOT_GET_CRC      0xA1

#define NILE_MCU_BOOT_ERASE_ALL_SECTORS 0xFFFF

#define NILE_MCU_BOOT_FLAG_SIZE     0x01
#define NILE_MCU_BOOT_FLAG_CHECKSUM 0x02

#define NILE_MCU_FLASH_START 0x08000000
#define NILE_MCU_FLASH_PAGE_SIZE 128
#define NILE_MCU_FLASH_SECTOR_SIZE 4096

typedef enum {
    MCU_SPI_CMD_ECHO = 0x00,
    MCU_SPI_CMD_MODE = 0x01,
    MCU_SPI_CMD_FREQ = 0x02,
    MCU_SPI_CMD_ID = 0x03,
    MCU_SPI_CMD_EEPROM_MODE = 0x10,
    MCU_SPI_CMD_EEPROM_ERASE = 0x11,
    MCU_SPI_CMD_EEPROM_READ = 0x12,
    MCU_SPI_CMD_EEPROM_WRITE = 0x13,
    MCU_SPI_CMD_RTC_COMMAND = 0x14,
    MCU_SPI_CMD_EEPROM_GET_MODE = 0x15,
    MCU_SPI_CMD_SET_SAVE_ID = 0x16,
    MCU_SPI_CMD_GET_SAVE_ID = 0x17,
    MCU_SPI_CMD_USB_CDC_READ = 0x40,
    MCU_SPI_CMD_USB_CDC_WRITE = 0x41,
    MCU_SPI_CMD_USB_HID_WRITE = 0x42,
    MCU_SPI_CMD_USB_CDC_AVAILABLE = 0x43,
} mcu_spi_cmd_t;

#endif
