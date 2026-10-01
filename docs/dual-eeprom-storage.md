# C512 primary / C32 critical backup

Base audited: `main` at `61e7c27`. The previous implementation used C32 fixed
config/batch A/B slots and direct history I2C calls in the control/network tasks.
The existing flash batch logger is disabled; it remains disabled. NVS safety
intent/reset handling is deliberately preserved as an independent safety layer.

## Map and wiring

Addresses are centralized in `config.h`: C512 PRIMARY `0x56`, C32 BACKUP `0x57`,
DS3231 `0x68`. Set the physical A0/A1/A2 straps to match. Driver capacity uses
32-bit arithmetic so 65,536 bytes does not wrap to zero. Geometry is separate:
C512 65,536 bytes / 128-byte pages; C32 4,096 bytes / 32-byte pages.

| Device | Range | Purpose |
| --- | --- | --- |
| C512 | 0000–02FF | One-time migration fence at 0000; otherwise reserved |
| C512 | 0300–06FF, 0700–0AFF | Existing reminder schema, two verified copies |
| C512 | 0B00–0F7F | 288 four-byte temperature samples, 24 hours / 5 minutes |
| C512 | 0F80–0FFF | Alignment reserve |
| C512 | 1000–CFFF | 96 × 512-byte critical journal slots |
| C512 | D000–FFFF | 12 KiB reserved for future Notes/log implementation |
| C32 | 0000–0AFF | Legacy config, batch and reminders retained for migration |
| C32 | 0B00 | One-time migration fence (old history is no longer written) |
| C32 | 0C00–0DFF, 0E00–0FFF | Two critical snapshot slots |

The new snapshot contains config + batch with their existing payload schema
versions, a 64-bit generation, record CRC32 and a commit byte at the end of its
slot. C32 receives only this critical snapshot, including when primary is healthy;
it never receives new reminders, history, notes or nonessential logs. Backup
failure does not reject a successfully committed running checkpoint on primary.

## Publication, migration and wear

For each append: invalidate destination commit byte and read it back, write the
body, read/compare the complete body and CRC, then write/read the commit byte.
The previous slot is untouched. Boot scans slots; there is no persisted current
index. Partial/unreadable scans cannot authorize writes. Sequence is shared across
devices and recovery uses max observed generation + 1, with live RAM authoritative
during runtime reconnection.

The 5-minute checkpoint remains unchanged. Each primary slot is reused after
96 checkpoints (8 hours), distributing writes across 48 times as many locations
as the previous two-slot batch scheme. This is a distribution factor, not a
guaranteed lifetime multiplier: page write amplification, extra saves and
temperature still matter. C32's two slots retain critical recovery redundancy;
its endurance remains a separate limiting factor. Unchanged snapshots are skipped.

Legacy schema decoders are retained. On first boot without new journals, read old
C32 config/batch, copy reminders to C512 and publish the critical snapshot without
altering legacy critical slots. A one-time fence prevents falling back to an
ancient legacy running record if both new journals are later lost. Unknown payload
schema versions fail closed. Neither journal format nor migration promises safe
downgrade to old firmware; restore/export data before downgrading.

## Health and realtime

The low-priority static storage worker owns EEPROM writes, retries, health checks,
scan/recovery and history cache population. The control task submits bounded RAM
mailboxes; HMI/MQTT save success and batch start wait for verified completion.
MQTT history reads use RAM only. Every I2C transfer is bounded; page and 126-byte
transport limits are respected. The shared bus mutex is released during internal
EEPROM write-cycle waits. RTC bus admission is nonblocking in the control task;
contention retries after 20 ms without incrementing device-error evidence. Two attempts per operation, 2 ms retry gap, 20 ms write
cycle deadline; health/recovery attempts are spaced 5 seconds apart.

Three failed service checks spanning at least 10 seconds confirm primary failure.
Single transaction retries do not count as separate health evidence. Write/readback
failure stays suspect even when ACK probes succeed. Stuck SDA/SCL, lock contention
or correlated RTC/LCD failures suppress EEPROM failover evidence and let the bus
supervisor recover the shared bus. The supervisor tracks the two EEPROMs separately.
Failover publishes RAM critical data to C32 with readback before changing route.

Failback requires four successful, spaced probe/read checks, a new journal scan,
generation comparison and a verified RAM-to-C512 append before enabling primary
optional traffic. C32 continues serving critical writes throughout recovery.
No old C512 config is applied into a running controller. Persistent stop intent
in NVS is cleared only after primary and backup both contain a verified stopped
batch. It stays set while either chip is unavailable, so an old running record
cannot resurrect a stopped batch on a later boot with only that chip available.

## Validation

`python3 tools/test_storage.py --sanitize` compiles the actual journal, driver,
backend and async facade against fault-injection HALs. Tests cover byte-by-byte
power cuts on fresh/reused slots, migration and failback; CRC/readback corruption;
single and sustained NACK; shared-bus failure; backup-newer boot; STOP versus old
checkpoint acknowledgement; migration fences; geometry/page/end-of-capacity bounds;
bounded retry; and no EEPROM I/O from the async caller. CI runs the suite with
ASan/UBSan, existing reliability/protocol/runtime tests, and both firmware builds.

Hardware validation is still required: remove/restore each EEPROM while running,
cut supply during a write, force shared SDA low, verify heater/turning timing and
storage worker stack high-water on the actual board. Host fault injection and
successful builds do not constitute physical power-cut or timing measurements.

Geometry reference: [Microchip AT24C512C page write](https://onlinedocs.microchip.com/oxy/GUID-98E3BD38-CAB5-45CF-A328-676E2788AC4D-en-US-2/GUID-A5A8BD8E-49CA-44EE-8192-4BD4D6CAF6E4.html),
[AT24C32E](https://www.microchip.com/en-us/product/AT24C32E).
