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
slot. C32 receives critical lifecycle/config snapshots while PRIMARY is healthy,
not each 5-minute checkpoint. It never receives new reminders, history, notes or
nonessential logs. Backup failure does not reject a successfully committed running
checkpoint on primary; standby unavailability/pending critical repair is degraded.

## Standby policy

`BackupSyncReason` separates normal checkpoints from critical synchronization.
There is **no periodic C32 safety timer**. The normal checkpoint path, health
checks, unchanged config saves and repeated boots perform no standby write just
because elapsed time, checkpoint/last-turn epoch, direction or turn counts changed.

| Reason | When C32 may be written |
| --- | --- |
| BatchStart | Durable START / wasRunning becomes 1 / new batch identity |
| BatchStop | STOP/cancel / wasRunning becomes 0; keep NVS until both copies verify STOP |
| CriticalConfig | A changed packed recovery config has been committed on primary |
| ResumeState | One explicit controller resume; repair pending critical data or a reinserted standby with incompatible lifecycle/config or a newer foreign generation |
| Migration | Initial critical journal publication when no compatible standby exists |
| Failover | Latest authoritative RAM snapshot must be committed and verified before BACKUP routing; reinserted active backup also requires verified RAM publication |

An identical already-published critical snapshot is reread/verified without a new
write. Pending critical requests survive in RAM until a readable backup returns;
boot detects outstanding lifecycle/config differences from the persisted copies.
Ordinary standby age in elapsed/turn counters is intentional. After failback C32
returns to this policy immediately. During BACKUP mode it is active critical
storage and receives the 5-minute checkpoint until C512 has recovered.

I2C addresses remain C512 `0x56`, C32 `0x57`. Boot reports each configured device
separately as OK / NO ACK / SCAN FAILED / SCHEMA UNSUPPORTED. ACK does not identify
the EEPROM model, capacity or page size: geometry is configured, with journal
reads/CRC and write readback validation; confirm physical chip markings/straps on
the PCB. Firmware does not auto-detect or swap chip identities by ACK.

## Publication, migration and wear

For each append: invalidate destination commit byte and read it back, write the
body, read/compare the complete body and CRC, then write/read the commit byte.
The previous slot is untouched. Boot scans slots; there is no persisted current
index. Partial/unreadable scans cannot authorize writes. Sequence is shared across
devices and recovery uses max observed generation + 1, with live RAM authoritative
during runtime reconnection.

The 5-minute checkpoint remains unchanged. Each primary slot is reused after
96 checkpoints (8 hours), distributing writes across 48 times as many locations
as the previous two-slot batch scheme. Unchanged durable snapshots are skipped;
there is no persisted current-slot pointer. See the 20-year calculation below.

Legacy schema decoders are retained. On first boot without new journals, read old
C32 config/batch, copy reminders to C512 and publish the critical snapshot without
altering legacy critical slots. If primary is initially absent, reminder migration
retries when it returns without overwriting valid primary reminders. A one-time fence prevents falling back to an
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
failure stays suspect even when ACK probes succeed. A failed running checkpoint
is retained as authoritative RAM for subsequent failover/repair; a failed config
or START is not applied into controller state. Stuck SDA/SCL, lock contention
or correlated RTC/LCD failures suppress EEPROM failover evidence and let the bus
supervisor recover the shared bus. The supervisor tracks the two EEPROMs separately.
Failover publishes RAM critical data to C32 with readback before changing route.

Failback requires four successful, spaced probe/read checks, a new journal scan,
generation comparison and a verified RAM-to-C512 append before enabling primary
optional traffic. C32 continues serving critical writes throughout recovery.
No old C512 config is applied into a running controller. Persistent stop intent
in NVS is cleared only after primary and backup both contain a verified stopped
batch, including fresh record reads before the STOP acknowledgement. It stays set
while either chip is unavailable. At boot, an existing NVS tombstone always requests
worker STOP reconciliation, even if the one readable copy already says STOP; boot
cannot clear it from that copy alone. An old running C32 therefore cannot resurrect
a stopped batch on a later boot with only C32 available.

## Wear estimate over 20 years

Assumptions: 365.25 days/year, continuous operation, no failed-write retries,
no corruption/replacement loops. These are **wear distribution estimates, not a
20-year lifetime guarantee**. Temperature, voltage, actual device endurance/data
retention specifications and time at temperature must be checked for the PCB.

- C512 normal checkpoints: `20 × 365.25 × 24 × 12 = 2,103,840` journal appends.
  Over 96 slots: `21,915` appends/slot. The current packed critical record is
  **166 bytes**, independently extracted from production payloads in host tests.
  With 128-byte pages and the 126-byte Wire transfer limit, one append uses three
  body write cycles plus two marker writes: five physical cycles across its pages.
  The busiest body/marker pages see two cycles per reuse: about **43,830 cycles
  per busiest journal page** from checkpoints, plus lifecycle/config/recovery
  appends. Total journal write cycles across the chip are 10,519,200; that total
  is not the wear on any one page/cell.
- History is separate: 288 samples/day across nine 128-byte pages gives 32 page
  write cycles/day, or **233,760 cycles/history page** in 20 continuous years.
  Reminders use their existing A/B region and depend on edit frequency. Their
  wear must not be confused with the journal's lower per-page estimate.
- C32 normal PRIMARY mode: **zero periodic 5-minute writes**. Let `E` be actual
  nonduplicate START/STOP/config/resume/failover/migration/standby-repair snapshots,
  and `D` be days spent with C32 active. The estimate is `N32 = E + 288 × D`
  appends. Two alternating slots give `ceil(N32 / 2)` maximum appends/slot.
  A 166-byte body on 32-byte pages needs six body cycles plus two marker writes,
  so eight cycles/append across the chip; the busiest marker page sees
  `2 × ceil(N32 / 2)` cycles. One-time migration fences are separate, not an index.
- Example assumptions, **not a predicted failure rate**: per year 12 batches
  (24 START/STOP events), 12 config changes, 2 resumes, 1 failover and 1 standby
  repair; plus one initial migration. This gives `E = 801` in 20 years.
  With no active-backup days: at most 401 appends/slot and **802 marker-page cycles**.
  With 20 total active-backup days: `N32 = 6,561`, at most 3,281 appends/slot and
  **6,562 marker-page cycles**. Long BACKUP residence increases wear proportionally
  and calls for repair; there is no promise of 20-year operation on backup.

Retries/failed publications can add marker/body wear without a committed snapshot.
Host instrumentation counts byte writes, write calls, valid publications and the
actual driver's page-program cycles. It asserts 1,000 normal PRIMARY checkpoints
produce 1,000 C512 publications and **zero C32 writes**, including intervening
health service and ordinary reboot. Physical write latency/endurance remain bench
and datasheet matters.

## Validation

`python3 tools/test_storage.py --sanitize` compiles the actual journal, driver,
backend and async facade against fault-injection HALs. Tests cover byte-by-byte
power cuts on fresh/reused slots, migration and failback; CRC/readback corruption;
single and sustained NACK; shared-bus failure; backup-newer boot; STOP versus old
checkpoint acknowledgement; migration fences; geometry/page/end-of-capacity bounds;
bounded retry; no EEPROM I/O from the async caller; START/STOP/config/resume policy;
standby outages/repair; normal-checkpoint write counts; and NVS STOP across single-
copy reboots and byte-level global power cuts. CI runs the suite with
ASan/UBSan, existing reliability/protocol/runtime tests, and both firmware builds.

Hardware validation is still required: remove/restore each EEPROM while running,
cut supply during a write, force shared SDA low, verify heater/turning timing and
storage worker stack high-water on the actual board. Host fault injection and
successful builds do not constitute physical power-cut or timing measurements.

Geometry reference: [Microchip AT24C512C page write](https://onlinedocs.microchip.com/oxy/GUID-98E3BD38-CAB5-45CF-A328-676E2788AC4D-en-US-2/GUID-A5A8BD8E-49CA-44EE-8192-4BD4D6CAF6E4.html),
[AT24C32E](https://www.microchip.com/en-us/product/AT24C32E).
