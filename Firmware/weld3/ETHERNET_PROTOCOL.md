# Weld3 Ethernet protocol

The controller is a TCP server at `192.168.0.100:5000` using IPv4,
netmask `255.255.255.0`, and gateway `192.168.0.1`.

Commands are ASCII and must end with LF (`\n`). Responses end with CRLF.
Only one client is serviced at a time.

## Commands

### OSPI weld profiles

- `SET PROFILE file=0 number=0` selects the active OSPI slot. Both indices are
  zero-based, 0..15.
- `SAVE PROFILE` stores the current validated settings in the selected slot.
- `LOAD PROFILE` validates and makes the selected slot's settings current.
  Follow with `GET SETTINGS` to read the loaded values.
- `DUMP PROFILES` scans all 256 slots and streams one JSON line per valid
  profile, followed by `{"type":"profiles_end","count":N}`. Empty slots are
  omitted. `ERR STORAGE` terminates the stream on an OSPI access failure.
- `CLEAR PROFILES` erases the 8 sectors occupied by all 256 records. WinApp
  uses it before an entire-library upload so deleted/empty host slots do not
  leave stale controller records. This operation can take tens of seconds and
  must not be interrupted.
- `SAVE PROFILE FAST` programs and verifies the selected 128-byte record without
  a sector erase and fails unless that record is erased. It is reserved for the
  WinApp whole-library upload immediately following `CLEAR PROFILES`, avoiding
  repeated erase cycles for multiple records in the same sector.
- Replies: `OK`, `ERR RANGE`, `ERR NOT_FOUND`, `ERR STORAGE`, or `ERR BUSY`.
- Slots occupy a dedicated 64 KiB partition at byte offset `0x03FF0000` in the
  64 MiB MX25LM51245G. Each record has a format version and CRC. Saving performs
  a read/modify/erase/write/verify of one 4 KiB sector and is rejected during a
  dry-run cycle.

- `PING`
- `GET STATUS`
- `GET SETTINGS`
- `START` (one timing-only dry run; does not enable PWM/gate output)
- `SET SETTINGS squeeze_ms=100 current1_a=100.0 up1_ms=50 time1_ms=100 down1_ms=50 cool1_ms=0 current2_a=0 up2_ms=0 time2_ms=0 down2_ms=0 cool2_ms=0 current3_a=0 up3_ms=0 time3_ms=0 down3_ms=0`

`GET` responses are one-line JSON objects. A successful `SET` returns `OK`.
Invalid commands and values return `ERR COMMAND` or `ERR RANGE`.

Each of the three weld stages has an independent current, ramp-up, hold and
ramp-down time. A stage is
disabled only when both values are zero; a nonzero current with zero time (or
the reverse) is rejected. These values are currently configuration storage for
the host/UI and are not yet connected to a verified power-stage sequencer.

The application updates live measurements with `WeldData_SetStatus()` and reads
accepted setpoints with `WeldData_GetSettings()` from `Core/Src/WeldData.h`.
