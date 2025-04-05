# nileswan-medem

This is a fork of the [Mednafen](https://mednafen.github.io/) emulator based on [wf-mednafen](https://github.com/WonderfulToolchain/wf-mednafen/), which is in turn based on [mednafenPceDev](https://github.com/pce-devel/mednafenPceDev).

It is intended to be used as a device emulator for developing software for the nileswan cartridge. It also features improvements to console emulation accuracy, as well as the improved "happyeyes" debugger laoyut.

## Building

Use this configure command:

    ./configure --disable-apple2 --disable-gb --disable-gba --disable-lynx --disable-md --disable-nes --disable-ngp \
        --disable-pce --disable-pce-fast --disable-pcfx --disable-psx --disable-sasplay --disable-sms --disable-snes --disable-snes-faust --disable-ss \
        --disable-ssfplay --disable-vb --disable-fancy-scalers
