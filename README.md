# Apple ][ Emulator — ESP32 VGA

Fully functional Apple ][ emulator for LilyGO TTGO VGA32 and ROBGO_RG boards, ported using Claude.ai

Based on the [original project](https://github.com/codesafe/ESP32-VGA_AppleII_Emulator) written for the **ESP32-S3**, ported to the **classic ESP32** (dual-core Xtensa, 240 MHz, PSRAM).

The original port drove video through the `LCD_CAM` peripheral, which only exists on the S3. Here the video layer was rewritten on top of [FabGL](https://github.com/fdivitto/FabGL) (I2S + scanline callback), with the framebuffer living in internal RAM.

---

## Supported hardware

| Board | Status |
|---|---|
| **LilyGO TTGO VGA32** | fully supported |
| **ROBGO_RG** | fully supported |

You'll need: a VGA monitor, a PS/2 keyboard and an SD card.

---

## Features

- **~60 FPS** at the Apple II's real speed (1.023 MHz)
- **Disk emulation** — `.dsk` and `.nib` images read straight from the SD card
- **Sound** — the Apple II's 1-bit speaker (`$C030`) through the DAC
- **PS/2 keyboard**
- **Disk browser** with folder navigation on the card
- **Hot disk swap** — no reset, just like changing a real floppy
- TEXT, LORES, HIRES and MIXED video modes
- Color or green monochrome monitor

---


## Keys

| Key | Action |
|---|---|
| `F1` | Help |
| `F2` | Insert / swap disk |
| `F11` | Toggle color / green monitor |
| `F12` | Reset (same as Ctrl-Reset) |
| Arrows | Apple ][ arrow keys |
| `ESC` | ESC |
| `Ctrl`+letter | Control codes |

In the disk browser: arrows move, `RETURN` enters a folder or boots, `ESC` goes up one level.

---

## Disks

Copy `.dsk` or `.nib` files anywhere on the SD card. The browser starts at the root.

`.dsk` images (raw sectors) are nibblized on load — 6&2 encoding with the DOS 3.3 2:1 interleave. `.nib` images are used as-is.

Multi-disk games work: when the game asks for the next disk, press `F2`, pick the image and carry on. The machine keeps running.

---

## Credits

- Original ESP32-S3 project: [codesafe/ESP32-VGA_AppleII_Emulator](https://github.com/codesafe/ESP32-VGA_AppleII_Emulator)
- [FabGL](https://github.com/fdivitto/FabGL) — Fabrizio Di Vittorio
