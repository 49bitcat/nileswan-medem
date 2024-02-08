# wf-mednafen

This is a fork of Mednafen based off [mednafenPceDev](https://github.com/pce-devel/mednafenPceDev), adding the following major changes relevant for users of the Wonderful toolchain:

* Improved debugger layout ("happyeyes"),
* Crucial emulation fixes to allow medium-accuracy debugging of homebrew made with Wonderful.

Downloads are available through the `Releases` page.

## nileswan support

This branch provides bare-bones Nileswan emulation support (current as of February 8th, 2024).

Helpful configure command for WonderSwan support only:

    ./configure --disable-apple2 --disable-gb --disable-gba --disable-lynx --disable-md --disable-nes --disable-ngp \
        --disable-pce --disable-pce-fast --disable-pcfx --disable-psx --disable-sasplay --disable-sms --disable-snes --disable-snes-faust --disable-ss \
        --disable-ssfplay --disable-vb --disable-fancy-scalers

The Nileswan emulation expects the following file layout:

- nileswan.ipl0: IPL0 (512 bytes)
- nileswan.spi: SPI image (currently IPL0 expects IPL1 at offset 131072 in this image)
- nileswan.img: TF card (<= 2GB, I did not implement SDHC)

in the same directory, then run `./mednafen nileswan.ipl0` in that directory.
