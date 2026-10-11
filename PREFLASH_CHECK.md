# Air Radio pre-flash audit — 2026-10-11

Base: feature/mdc1200-reader, 2e25174af63d46c5cb4dc1c47d692065edb5ef69.
Target: original DP32G030 Quansheng UV-K5 family with BK4819.
Status: software test candidate, NOT hardware/RF certified. Do not flash a different hardware revision.

## Findings fixed
- MDC overlay swallowed PTT events in MAIN_ProcessKeys, including release. The MDC handler now explicitly lets PTT through.
- Incoming MDC replaced the menu display and captured its keys. Overlay render/input are now restricted to the main display.
- UP remained latched after releasing a held key, affecting the next 2/3 PTT. Clear it on every release.
- Decoder handled only one mark/space polarity and detected but ignored inverted differential sync. Normalize sync and try both polarities with mandatory CRC validation.
- Contact edits could save to a newly received caller instead of the caller being edited. Capture the edit ID.
- Contact loading truncated the tenth name character; keep all ten and terminate the caller's output buffer.
- MENU-next did not request an editor redraw. Request it.

## Verification
Run `python tests/preflash.py` with host GCC. Tests execute actual extracted decoder/key functions and the real air_nv.c with an emulated EEPROM.
- 65,534 unit IDs, two polarities: 131,068 encode/decode checks, plus truncated-packet rejection.
- Independent OneOfEleven reference packet: opcode 01, argument 80, ID 0740.
- PTT press/release passes through both log and edit modes; editor next/save/exit.
- EEPROM writes preserve all bytes outside reserved channel-name tails, including calibration.
- Main-screen-only overlay routing.
- ARM GCC 12.3.Rel1 build with repository defaults and -Werror; packed output produced.
These are software tests, not live radio tests or proof of Motorola interoperability.

## What you can operate on the radio
- Normal MENU: existing settings, including ChName, retained. Exit the MDC log with EXIT before opening normal MENU.
- F+9 or long 9: opens MDC history if there are entries; otherwise retains the original call-channel shortcut.
- In MDC history: UP/DOWN browse; MENU edits the caller name; EXIT closes.
- Name editor: 2–9 letters, UP/DOWN cycle characters, MENU advances, hold MENU saves, EXIT cancels.
- Air modes and own MDC ID still use local keypad commands, not dedicated menu rows. The Roger menu's “MDC” option is not a full MDC configuration menu.

## Remaining hardware acceptance / limitations
1. Confirm radio hardware revision and current firmware/bootloader before selecting a flasher. This build targets DP32G030, not PY32 variants.
2. Read and preserve the full original EEPROM/calibration and channel backup before flashing. Generic programmers may overwrite the unused name-tail bytes used for Air settings/contacts/groups.
3. On a matching radio, test boot, normal MENU navigation and ChName save/reboot first.
4. For the first receive test, use one fixed FM channel, battery save OFF and dual watch OFF. Receive a known MDC1200 transmitter with a known ID. Confirm displayed ID/type and repeated packets. Bench decoding cannot test BK4819 synchronization, FIFO/interrupt behavior, RF levels or timing.
5. Test PTT press/release while an MDC popup appears, with suitable test equipment; confirm TX ends on release. Then test 2/1 and 2/3 channel selection with and without UP held, and after releasing UP.
6. Test a new incoming ID while editing a contact; confirm the saved name belongs to the original ID after reboot.
7. Test dual watch/scan only after fixed-channel reception works. A single receiver can miss short bursts while listening elsewhere.
8. Call alert 101, group signaling and named event labels are experimental. A label for remote monitor/radio check does not implement that remote function. No hardware-confirmed interoperability claim is made.

Reference decoder/vector: https://github.com/OneOfEleven/uv-k5-firmware-custom/blob/main/mdc1200.c
