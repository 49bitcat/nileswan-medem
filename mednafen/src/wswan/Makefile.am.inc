mednafen_SOURCES 	+= wswan/gfx.cpp wswan/main.cpp wswan/memory.cpp wswan/comm.cpp wswan/v30mz.cpp wswan/sound.cpp wswan/tcache.cpp wswan/interrupt.cpp wswan/eeprom.cpp wswan/rtc.cpp
mednafen_SOURCES	+= wswan/nileswan.cpp wswan/nileswan_tf.cpp wswan/nileswan_mcu.cpp wswan/nileswan_flash.cpp

if WANT_DEBUGGER
mednafen_SOURCES	+= wswan/debug.cpp wswan/dis/dis_decode.cpp wswan/dis/dis_groups.cpp wswan/dis/resolve.cpp wswan/dis/syntax.cpp
endif

