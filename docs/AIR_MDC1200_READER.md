# Air Radio — MDC1200 Reader

Target branch: `feature/mdc1200-reader`

This feature is for a real MDC1200-capable reader on the Quansheng/BK4819 platform. It must use the BK4819 FSK RX path and decode MDC1200 frames rather than treating DTMF digits as MDC IDs.

## Required MDC1200 core
- Real BK4819 FSK MDC1200 RX decode
- Decode unit ID, opcode and argument
- Recognize PTT ID / post ID
- Recognize Call Alert
- Recognize Emergency
- Recognize Radio Check
- Recognize Status / Message packets
- Unknown-opcode fallback showing raw OP/ARG
- Duplicate packet suppression
- RX timeout/reset handling
- Enable MDC receive alongside normal FM receive without breaking squelch/CTCSS/DCS

## Contact / save-ID workflow
When a new/unknown MDC ID is received:
1. Show the ID on screen.
2. Press MENU while the MDC result is displayed to save the ID.
3. Enter/edit a short contact name from the radio keypad.
4. Press MENU to commit the contact to EEPROM.
5. EXIT cancels without writing.

Saved IDs display name first and unit ID second. Existing contacts can be edited, reviewed and deleted. Unknown IDs remain visible even if not saved.

## Event/history reader
Maintain a rolling history of recent MDC events with:
- unit ID
- event type
- channel/VFO context
- sequence/age
- known contact name if available

Include duplicate filtering and a missed/unread alert counter. Emergency and Call Alert events get distinct UI treatment.

## 101 behavior
Dialing 101 triggers the configured call-alert action. In MDC mode, 101 must use the MDC call-alert path rather than a DTMF imitation.

## Display examples

PTT:
```
MDC 1201
MOSHE
PTT ID
```

Unknown:
```
MDC UNKNOWN
ID 1201
MENU=SAVE
```

Call Alert:
```
CALL ALERT
MOSHE 1201
MENU=SAVE/EDIT
```

Emergency:
```
EMERGENCY
MOSHE 1201
ACK/EXIT
```

## Preserve existing Air-radio behavior
- channel naming
- 2/1 and 2/3 operational modes
- Shabbos-mode screen/light behavior
- current DTMF calling unless explicitly in MDC mode
- normal VHF/UHF analog RX/TX
- memory
- scan
- keypad
- CTCSS/DCS

FM broadcast, spectrum and other non-core extras may be disabled if needed to fit flash.

## Build acceptance
- CI produces `firmware.packed.bin`
- binary fits the UV-K5 flash limit
- no compiler warnings/errors
- packed firmware artifact is retained
- exact flashing and rollback instructions are documented
