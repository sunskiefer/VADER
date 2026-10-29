# LXR-02 Open-Source Firmware 
## A functional port of Sonic Potions LXR Software, version 0.37
### Introduction
The LXR02 is a digital drum synthesizer produced in collaboration with Sonic Potions and Erica Synths. It is based on a 32-bit Cortex-M7 processor. If you want to go straight to the firmware, it is './build/LXRV2_lxr02.img'. Put this in the root directory of the SD card and power on while pressing the encoder, as you would for any firmware update. You can switch between this and the Erica Synths firmware any time with this method. 

This repository ports the original firmware written for the Sonic Potions LXR Drumsynth to the LXR02 hardware. Because the original LXR was based on a dual-processor design using an Atmega644 8-bit processor and a Cortex-M4, much of the underlying code has changed, and most of the hardware drivers are new:
- All hardware read/writes happen on asynchronously draining queues, including LCD refreshes.
- SD card read/writes use https://github.com/thenickdude/asyncfatfs. One of my favorite things about the port. Thanks to Nick for making some of that background magic happen. 
- **USE A FAT32 FORMATTED CARD, MBR partition**. FAT16 also works, but MBR-FAT32 is the recommended cross-compatible format. FAT12 and exFAT are not supported; if one is detected at boot, the firmware shows `Unsupported card` / `use MBR-FAT32` and does not mount or load from it. On my mac this is just MBR partition, MS-DOS(FAT), but I'm including the terminal commands for future-proofing. If you are using an SD >32GB you may need to manually create a partition that is smaller, those options are included below, just remove/edit the <options> for your system and card. In your respective terminal, for disk <X>:
    - Linux: sudo parted -s /dev/sd<X> mklabel msdos mkpart primary fat32 1MiB <32GiB> 100% && sudo mkfs.vfat -F 32 -n "LXR" /dev/sd<X>1
    - Mac: 'diskutil partitionDisk disk3 MBR FAT32 "LXR" <R *if 32G or less, otherwise* 32G>'
    - Win: "select disk <X>", "clean", "convert mbr", "create partition primary <size=32768>", "format fs=fat32 quick label=LXR", "assign" | diskpart
- **The original 0.37 kits and SD card files from here**: http://sonic-potions.com/public/SdCardImage.zip
- Memory mapping is updated for the M7, and there is 1.5MB available for sample storage in flash.
- The mixbus and output buffers use the full 24-bit width of the DACs. 

Other than that, the firmware is designed to work as closely as possible to the Sonic Potions LXR, version 0.37. Kit/pattern/performance/all formats are kept compatible where the hardware allows; legacy 22-byte `glo.cfg`/ALL globals are accepted and migrated, while current globals use the LXR-02 23-byte span. Additions/differences:
- The voice faders work. The log curve can be changed in config.h
- The "STEP" mode button is labeled "LOAD" on the LXR02. Pressing "LOAD" gets you step mode, as per 0.37
- The "< BAR >" buttons trigger the selected voice at 127/64 velocity and can be recorded. 
- The sample loading option is updated. You can add two directories to the root of the SD card: 'samples' and 'loops'. You can have up to 120 44.1kHz/16-bit files between the directories. They will both be loaded into flash when the option is selected, 'samples' are 1-shot and 'loops' play looped, always. The trucated filename will also show when you click in to the waveform parameter with the encoder on the OSC page.
- Global menu changes: the non-functional trigger jack output options are removed. Two new parameters are there: Oscillator Interpolation and a CPU monitor. Oscillator Interpolation interpolates between waveforms when they are automated with an LFO. It works for the main voice oscillators (not the FM oscillators), and there are two dynamically-assigned slots for this. This can be changed in config.h if you like.  

Enjoy! If you find any bugs or have an idea or make some cool music, feel free to join the Discord server: https://discord.gg/sWjGWuavUX

And if you want to support the absurd nonsense I get up to in general: https://patreon.com/voskomm

### Some notes for developers or prospective developers
This repository is designed to be as LLM-friendly as I could make it so that adding features would be easy for everyone. The whole sausage-making process of the hardware trace, bring-up, driver wrangling, and application port is there in the session logs. The idea is that you can start a session with something like:
 "The goal of this session is to implement < some feature >. Read @README.md and @MEMORY.md for project context and any further files as necessary, then write a plan of implementation with possible conflicts and risk factors to the root directory as < some feature >_AUDIT.md." 
And then the LLM will grab the context it needs as required. Then you read the plan, work through it, and write back these files and the logs when you're done. There's verbose logs, a template for that, and a lightweight log index to keep the context sorta-manageable.  
The build requirements are pretty lightweight, too. See 'requirements.txt'. You just need gcc, make, and python 3. If you don't want to deal with that, you can probably just drop the whole zip file into an LLM and make it build an .img for you. LLM-stuff starts below the fold. Have fun!
Brendan
brendanpaulclarke@gmail.com
https://brendanclarke.com
_______

## Repository
**Structure**
- **Working firmware:** primary branch `LXR02Open-prime`; repository root is the working tree root
- **Git history:** project became a git repository after Session 023
- **Original open-source LXR source:** available read-only at `knowledge_files/LXR-master/` — must not be modified
- **Session Logs** see `knowledge_files/log_archive/000_SESSION_INDEX.md`

## Boot Process
1. LXR-02 bootloader (LXRV2) loads from flash
2. Bootloader reads SD card for `LXRV2_lxr02.img`
3. Image format: `[8B magic "LXRV2IMG"][4B payload size LE][4B checksum LE][payload]`
4. App loaded at 0x08008000, SP=0x20080000
5. Boot by holding main encoder button while powering on
6. Packager: `tools/build_lxrv2_img.py`

## Toolchain
```
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
make && make img  → build/LXRV2_lxr02.img
```

## Directory Structure
```
./
├── README.md                        ← this file
├── MEMORY.md                        ← project context, known issues, critical reminders
├── main.c
├── config.h
├── Makefile
├── STM32F765VIHx_FLASH.ld
├── requirements.txt
├── tools/
│   └── build_lxrv2_img.py          ← packages ELF → LXRV2_lxr02.img
├── build/                          ← generated, not in VCS
├── knowledge_files/
│   ├── SESSION_HANDOFF_TEMPLATE.md ← template for writing new session handoff logs
│   ├── ENHANCED_FEATURES.md        ← future enhancement notes
│   ├── MEMORY_AUDIT.md             ← memory region audit notes
│   ├── DSP_AUDIT.md                ← DSP pipeline audit and hot-path notes
│   ├── OSC_INTERP_AUDIT.md         ← oscillator interpolation audit
│   ├── hardware_archive/
│   │   ├── HARDWARE_MAP.md         ← full confirmed pin table, IRQ numbers
│   │   ├── AVR_TO_F765_MIGRATION.md ← architectural notes, sequencer ISR design baseline
│   │   ├── FRONTPANEL_AUDIT.md     ← frontPanel_sendData() elimination audit
│   │   ├── SD_CARD_INVESTIGATION.md ← SD false-positive analysis (PA8, 74HC165)
│   │   └── XP_CONNECTOR_MAPS.md   ← ribbon cable pin mappings
│   └── log_archive/
│       ├── 000_SESSION_INDEX.md    ← index of all sessions with keyword lookup
│       ├── 001_SESSION_HANDOFF_LOG.md
│       ├── 002_SESSION_HANDOFF_LOG.md
│       ├... etc                     ← see 000_SESSION_INDEX.md for current list
└── Core/
    ├── globals.h
    ├── datatypes.h
    ├── Src/
    │   └── startup_stm32f765xx.s
    ├── Hardware/
    │   ├── clocks.c/h               ← sysclk_init(), FPU enable via CPACR
    │   ├── timebase.c/h             ← SysTick 4kHz mainboard tick, TIM6 1kHz counters + 500Hz foreground service, TIM7 5kHz LCD drain
    │   ├── AudioCodecManager.c/h    ← consolidated audio: DMA ISRs, I2S/GPIO/DMA init, SPSC queue
    │   ├── triggerJacks.c/h         ← CLK OUT/IN, RST IN; OUT jack detect is foreground-polled
    │   ├── memtest.c/h              ← flash sector probe (boot-time, MEMTEST_ENABLED gate)
    │   ├── frontPanel/
    │   │   ├── buttonHandler.c/h    ← ISR-safe event ring, main-loop processEvents()
    │   │   ├── lcd.c/h              ← TIM7-driven async queue, 128-entry SPSC ring
    │   │   ├── ledHandler.c/h
    │   │   └── IO/
    │   │       ├── adcPots.c/h      ← sliders RV5-10, ADC1 DMA
    │   │       ├── din.c/h          ← 74HC165×5 buttons, SPI1
    │   │       ├── dout.c/h         ← 74HC595×5 LEDs, SPI1
    │   │       ├── encoder.c/h      ← SW42, TIM1 IC, Dannegger, accel + rebound suppression
    │   │       └── endlessPots.c/h  ← RV1-4, atan2 delta tracking
    │   ├── SD/
    │   │   ├── filesystem.c/h       ← public facade: typed async load/save/name/scan operations
    │   │   ├── kitBrowser.c/h       ← kit-only 128-slot gap-tolerant browser
    │   │   ├── SPI/
    │   │   │   ├── spi_sd.c/h       ← bit-bang SPI: PC12/PD2/PC8/PD0
    │   │   │   └── sd_routines.c/h  ← SD_init() only; blocking read/write superseded
    │   │   └── asyncfatfs/
    │   │       ├── asyncfatfs.c/h   ← Betaflight asyncfatfs (modified for LXR-02)
    │   │       ├── fat_standard.c/h
    │   │       ├── sdcard.h
    │   │       └── sdcard_lxr02.c/h ← SD block-device shim over bit-bang SPI
    │   └── USB/
    │       ├── OTG_Driver/
    │       ├── Device_Library/
    │       └── App/                 ← usb_manager, usb_midi_core, etc.
    ├── Menu/
    │   ├── menu.c/h                 ← full port, all pages, load/save UI
    │   ├── menuPages.h              ← 16-page × 8-subpage table
    │   ├── MenuText.h               ← all label strings
    │   ├── Cc2Text.c                ← modTargets[] 205 entries
    │   ├── CcNr2Text.h
    │   ├── copyClearTools.c/h       ← copy/clear tools; direct seq_* calls wired Session 15
    │   └── screensaver.c/h          ← screensaver with explicit LCD off/on phases
    ├── Preset/
    │   ├── ParameterArray.h/c       ← supersedes Parameters.h; NUM_PARAMS=275
    │   └── presetManager.c/h        ← typed load/save for kit, morph, pattern, performance, all, globals
    ├── MIDI/
    │   ├── Uart.c/h                 ← USART3, 31250 baud, interrupt-driven dual FIFO (realtime + normal)
    │   ├── MidiRealtime.c/h         ← 32-entry timestamped SPSC ring for MIDI_CLOCK/START/CONTINUE/STOP
    │   ├── FIFO.c/h
    │   ├── MidiMessages.h           ← full mainboard version (MIDI_NRPN_* prefix)
    │   ├── MidiNoteNumbers.h
    │   ├── MidiParser.c/h
    │   ├── MidiVoiceControl.c/h
    │   ├── SeqStep.h
    │   ├── frontPanelParser.c/h     ← local dispatcher + sequencer/front-panel bridge
    │   └── valueShaper.h
    ├── SampleRom/
    │   ├── SampleMemory.c/h         ← sample flash metadata/runtime cache, 120 entries, loop flags
    │   └── sampleFlash.c/h          ← guarded F765 sector 6-11 erase/program helpers
    ├── Sequencer/
    │   ├── sequencerTimer.c/h       ← TIM3 4kHz sequencer timing owner (IRQ29, priority 2) — Session 019
    │   ├── sequencer.c/h            ← original LXR sequencer source (driven by TIM3_IRQHandler)
    │   ├── EuklidGenerator.c/h      ← original LXR euclid generator source
    │   ├── SomData.c/h              ← original LXR SOM data tables
    │   ├── SomGenerator.c/h         ← original LXR SOM generator source
    │   ├── clockSync.c/h
    ├── DSPAudio/
    │   ├── random.c/h               ← F765 RNG port (PLL48CLK, bare register)
    │   └── [all DSP voice files]    ← ported; mixer_calcNextSampleBlock wired to AudioCodecManager
    └── compat/
        ├── stm32f4xx.h              ← vestigial-include shim via <stdint.h>
        └── cmsis_intrinsics.h
```

### Where to look for things

| Question | File |
|----------|------|
| Which session introduced a fix? | `knowledge_files/log_archive/000_SESSION_INDEX.md` |
| Full details of a fix or decision? | `knowledge_files/log_archive/0xx_SESSION_HANDOFF_LOG.md` |
| Confirmed pin assignments / IRQs? | `knowledge_files/hardware_archive/HARDWARE_MAP.md` |
| Sequencer / DSP architecture plans? | `knowledge_files/hardware_archive/AVR_TO_F765_MIGRATION.md` |
| Current known issues and reminders? | `MEMORY.md` |

## Confirmed Working Hardware
- LCD 4-bit parallel (PE7-PE12), TIM7 async driver
- LEDs: 74HC595x5 via SPI1
- Buttons: 74HC165x5 via SPI1 (40 inputs, 1kHz poll, event ring)
- SW43 SHIFT/BAR1 button (PB7) and LED (PB8)
- Main encoder SW42 (TIM1 IC, PE13/PE14, Dannegger + acceleration + rebound suppression)
- Endless pots RV1-RV4 (ADC1 DMA, atan2 delta tracking)
- Sliders RV5-RV10 (ADC1 DMA, PA0-PA5)
- Audio DAC1 (CS4344, I2S3, PA15/PB5/PC7/PC10), 24-bit signed payload
- Audio DAC2 (CS4344, I2S2, PB12/PB13/PB15/PC6), 24-bit signed payload
- MIDI DIN RX/TX (USART3, PB10 TX / PB11 RX, 31250 baud, interrupt-driven dual FIFO)
- USB MIDI (OTG_FS, PA11/PA12, enumerates as "Sonic Potions USB MIDI")
- SD card SPI bit-bang (PC12/PD2/PC8/PD0), SDHC confirmed
- CLK OUT jack (PC13)
- CLK IN jack (PD4, GPIO input pull-up, EXTI4 rising edge low-to-high)
- RST IN jack (PD5, GPIO input pull-up, EXTI5 rising edge low-to-high)
- OUT1 L/R jack detect (PD6/PD7, input pull-up, no plug=LOW, plug inserted=HIGH, sampled by foreground service)
- OUT2 L/R jack detect (PB4/PB6, no plug=LOW, plug inserted=HIGH, sampled by foreground service)
- I-Cache enabled (16KB, ICIALLU invalidate)
- D-Cache enabled (16KB) with MPU (WT for SRAM, SO for DMA buffers)
- DMA buffers in `.dma_nocache` linker section (Strongly-Ordered via MPU)
- `audioOutBuffer` in DTCM (INDTCMZ, single-cycle access)
- Flash sector layout probed: sectors 5-11 blank, app in sector 2, single-bank confirmed

### Clock Configuration (confirmed)
- HSE = 16MHz (ZQ1 crystal confirmed)
- SYSCLK = 216MHz: PLLM=16, PLLN=432, PLLP=2
- PCLK1 = 54MHz (APB1/4)
- PCLK2 = 108MHz (APB2/2)
- PLL48CLK = 48MHz (PLLQ=9) — USB
- PLLI2S: N=271, R=2 → 135.5MHz → Fs=44108Hz
- RCC_DCKCFGR2 (0x40023890): CLK48SEL=00 written explicitly
