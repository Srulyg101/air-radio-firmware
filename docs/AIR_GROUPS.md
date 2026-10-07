# Air Radio Groups

Air Radio already has a native group engine in `air_groups.c`.

## EEPROM layout

- Base: `0x1E00`
- 16 profiles
- 8 bytes per profile
- Used range: `0x1E00-0x1E7F`
- `0x1EC0` and above is calibration data, so the group table stays below calibration.

Each profile is:

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 1 | Magic `0x47` |
| 1 | 1 | Enabled |
| 2 | 1 | Memory channel, zero-based |
| 3 | 1 | Reserved |
| 4 | 2 | Group ID |
| 6 | 2 | Reserved |

A memory channel may belong to more than one group.

## On-radio programming codes

Enter these from the normal radio DTMF/code entry workflow:

- `71GGGG` — assign group GGGG to the current memory channel
- `70GGGG` — remove group GGGG from the current memory channel
- `72GGGG` — open group GGGG over MDC
- `73GGGG` — close group GGGG over MDC
- `75CCCGGGG` — assign group GGGG to explicit memory channel CCC
- `76CCCGGGG` — remove group GGGG from explicit memory channel CCC

Example:

- `711201` assigns group 1201 to the channel currently selected.
- `750021201` assigns group 1201 to memory channel 002.
- `721201` opens group 1201.
- `731201` closes group 1201.

## Browser programmer

The Air Radio Programmer should read the 16 records directly using the radio's existing EEPROM read command and write them using the existing EEPROM write command.

The UI should show each profile as:

- Group label (browser-side friendly label)
- Group ID
- Memory channel
- Channel name/frequency resolved from the normal channel table
- Enabled
- Open/closed live state when connected

Friendly labels are intentionally separate from the radio's 8-byte group profile so no calibration or channel-name storage is sacrificed.
