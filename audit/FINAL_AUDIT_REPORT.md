# FINAL AUDIT REPORT — MAYAP Machine Control Logic

AUDIT-ONLY DOCUMENT
NOT PRODUCTION CODE

Reviewer stance: independent adversarial static/simulated review, no assumption of author intent.
Branch reviewed: `main`.
Files in scope: `MAYAP_INDUSTRIAL_v3_4_0/machine_control.h` (full read, 5732 lines), plus
integration points read in `MAYAP_INDUSTRIAL_v3_4_0/hmi.h`, `realtime_link.h`,
`cloud_alert_link.h` (partial — only sections directly touching batch/turn/fault sequencing).
Files explicitly NOT deep-audited this pass: `network_service.h`, `PersistentStore`
internals for `saveBatch`/`loadBatch` byte-level A/B commit logic, `SHT485Industrial`
UART parser internals, `InputManager` debounce internals. Any claim depending on those
is marked `UNKNOWN` below, not asserted.

---

## VII. EXECUTION EVIDENCE — DECLARATION

No claim in this report is `RUNTIME`. This is a static-source review with hand-simulated
multi-step traces where noted. No test/build was executed against this codebase in this
session.

---

## VIII. TEST HARNESS / SIMULATOR CHECK

```text
Searched for: unit tests, integration tests, mocks, fake clock, hardware abstraction
  test layer, PlatformIO native test env, Unity/ArduinoUnit framework references.
Result: NONE FOUND.
  - No /test, /tests, *test*, *mock* paths in repository.
  - No platformio.ini (project is Arduino-IDE-only, per README.md and prior session
    context) — no native/host build target exists.
  - grep for UNITY/ArduinoUnit/native_test/platformio across all .h/.ino/.ini: no match.
```

**Decision:** per the instructions, since no harness exists AND this environment cannot
compile the firmware (no ESP32/Arduino toolchain available in this sandbox — confirmed
in prior sessions: no network access to Espressif package index), I am **not** attempting
to build a harness. Faking hardware I/O (GPIO, I2C RTC, I2C EEPROM, UART sensor) reliably
enough to trust RUNTIME-level conclusions is not achievable within this review's means.
**All findings below are STATIC or SIMULATED only. No RUNTIME evidence exists or is
claimed anywhere in this report.**

---

## IX–X. FINDINGS (full evidence blocks)

```text
ID: F-01
Type: CONFIRMED BUG
Severity: HIGH
Evidence Level: STATIC
Confidence: HIGH
Status: CONFIRMED
Source: machine_control.h (processHmiTransactions) + hmi.h (openTurningToggleConfirm,
        commitSetting, settingLockedDuringBatch) + app.js (turningForm submit handler)
Function: MachineController::processHmiTransactions()
Line: machine_control.h:3527-3536 (protectedBatchChange / saveAllowed)
Trigger: Operator changes turningEnabled, turnIntervalMin, or turnMaxRunSec via HMI
  "Sua thong so" screen or the web turningForm, WHILE batchRunning_==true, and confirms
  through the CO/HUY (yes/no) dialog that a prior session-turn added specifically to
  allow this.
Execution Path:
  1. hmi.h::commitSetting() no longer blocks this field via settingLockedDuringBatch()
     (that check was narrowed to totalIncubationDays only in a prior turn this session).
  2. hmi.h::openTurningToggleConfirm() shows CO/HUY, on accept calls
     startConfigSave(pendingTurningConfig) — same code path app.js's turningForm
     handler also feeds via MQTT config/set.
  3. Both land in MachineController::processHmiTransactions() via hmiTakeSavedConfig().
  4. Line 3527: `const bool protectedBatchChange = (batchRunning_ || resumePending_) &&
     (requested.turningEnabled != config_.turningEnabled || requested.turnIntervalMin
     != config_.turnIntervalMin || requested.turnMaxRunSec != config_.turnMaxRunSec ||
     requested.totalIncubationDays != config_.totalIncubationDays || ...);`
     This condition is UNCHANGED and still true for exactly the 3 fields the UI now
     claims to allow mid-batch.
  5. Line 3535-3537: `saveAllowed = !batchClearPending_ && !safetyJournalFaultLatched_
     && !protectedBatchChange;` → false. `ok = saveAllowed && store_.saveConfig(...)` →
     false, `store_.saveConfig` never called.
  6. `hmiConfirmConfigSave(transactionId, ok=false, nullptr)` /
     `mayapWebConfirmConfigSave(transactionId, false, nullptr)` fire.
  7. hmi.h::processConfigAck(): `if (!ack.ok) { currentConfig = rollback; showToast("LOI
     LUU - DA HOAN TAC", true); }` — HMI value visually reverts.
  8. realtime_link.h::mayapWebConfirmConfigSave(): enqueues ack "rejected" with message
     "LUU CAU HINH BI TU CHOI" — app.js shows this as a form error toast.
Current Behavior: Operator completes a full CO/HUY confirmation flow that the UI
  presents as functional; the change is silently rejected and rolled back one step
  later, with a generic message that does not state the real reason (an intentional
  in-batch policy lock), so it reads as a technical/EEPROM failure instead.
Expected Behavior: Either (a) processHmiTransactions()'s protectedBatchChange should
  exclude turningEnabled/turnIntervalMin/turnMaxRunSec to match the UI that was
  deliberately unlocked for exactly these fields (totalIncubationDays stays locked), or
  (b) the UI unlock should not have been made if the persistence-layer lock was meant to
  stay — the two layers currently disagree.
Impact: No physical/safety consequence (rollback is clean, no state corruption observed
  in the traced path). Operational trust impact: operator believes a turning-safety
  setting was changed when it was not, or vice versa.
```

```text
ID: F-02
Type: MISSING FEATURE
Severity: HIGH
Evidence Level: SIMULATED
Confidence: HIGH
Status: CONFIRMED
Source: machine_control.h
Function: latchTurnFault(), clearTurnFault(), updateTurning()
Line: 4558-4583 (latch/clear), 4352-4446 (updateTurning scheduling re-entry)
Trigger: A limit-switch input is permanently stuck/disconnected during an active batch
  (hardware fault, not transient).
Execution Path (hand-simulated over multiple ticks):
  T0: updateTurning() reaches scheduled turn → requestTurn() → Deadtime → Moving.
  T1: Neither limit switch ever activates → elapsed >= turnMaxRunSec →
      latchTurnFault(TurnTimeout) → turnPhase_=Fault, turnFaultLatched_=true.
  T2: Operator sends AlarmAck (mask includes AlarmTurning) →
      processHmiTransactions() → clearTurnFault() is called.
  T3: clearTurnFault() line 4569: `if ((in.limitLeft && in.limitRight) || in.turnLeft ||
      in.turnRight) return false;` — with both switches stuck OFF and no manual turn
      input held, this guard passes → turnFaultLatched_=false, turnPhase_=Idle.
  T4: Next updateTurning() tick: turnFaultLatched_ is false, so the block at line
      4361 (`if (turnFaultLatched_) {...}`) no longer short-circuits. Scheduling logic
      (scheduleNextTurnFromAnchor / nextTurnAt_) is unchanged from before the fault, so
      the SAME due turn is retried, producing the identical Timeout at T1+turnMaxRunSec.
  No counter exists anywhere in the file for consecutive turn-fault occurrences
  (grep for turnFaultRepeatCount/TURN_RETRY/consecutiveTurnFault/turnFaultStreak across
  all .h files: zero matches — exhaustive, HIGH confidence this counter does not exist).
Current Behavior: Operator can ACK-retry indefinitely; each cycle looks identical to a
  normal transient fault recovery from the alarm UI's perspective.
Expected Behavior: After N consecutive turn faults within a bounded window with zero
  successful turns between them, the system should stop auto-retrying and require an
  explicit Test Mode / mechanical check acknowledgment distinct from the normal
  AlarmAck path.
Impact: Eggs may go unturned for an extended period while the alarm system reports
  "acknowledged and handled" each cycle, masking an ongoing mechanical failure.
```

```text
ID: F-03
Type: MISSING FEATURE
Severity: MEDIUM
Evidence Level: STATIC
Confidence: HIGH
Source: machine_control.h
Function: processResume(), begin()
Line: 3906-3914 (confirmation block), all 14 references to resumeConfirmationRequired_
  in the file checked exhaustively (grep, no timeout-clearing site found anywhere)
Trigger: Boot with resumeConfirmationRequired_==true (real power loss,
  autoResumeOnPowerLoss config is OFF), operator does not act on the HMI/web prompt.
Execution Path: processResume() line 3911-3914 returns immediately every tick while
  resumeConfirmationRequired_ stays true; nothing else in the file clears this flag
  except explicit ResumeYes/ResumeNo commands (lines 3640, 3650/3658) or stopBatch()
  (line 3790, only reachable if batchRunning_ or resumePending_ is otherwise true — not
  applicable here since batchRunning_ is false during this wait).
Current Behavior: The prompt can remain open indefinitely with no escalating alarm if
  the operator is not present. Existing `ResumeRequiresAuto` fault only fires once the
  operator HAS proceeded and AUTO switch is off — it does not cover "nobody has
  responded at all".
Expected Behavior: A Warning-level fault after a configurable wait threshold
  ("CHO XAC NHAN AP LAI QUA LAU") — not an automatic resume (that would defeat the
  deliberate safety purpose of the confirmation), only an escalating notice.
Impact: Eggs may sit without heat/turning for an unbounded time after an unattended
  power-loss recovery if nobody checks the machine promptly. Status: CONFIRMED that no
  such timeout exists in this file (exhaustive); the operational severity depends on
  deployment context (attended vs. unattended site), which is outside code evidence.
```

```text
ID: F-04
Type: DESIGN RISK
Severity: LOW
Evidence Level: SIMULATED
Confidence: MEDIUM
Status: CONFIRMED (mechanism) / UNKNOWN (downstream consequence)
Source: machine_control.h
Function: stopTurn(), updateTurning() (multiple call sites), trayPosition_ usage
Line: 4552-4556 (stopTurn), trayPosition_ reads at 4438-4444, 4489-4490, 4497-4498
Trigger: Turning is aborted mid-`Moving{Left,Right}` (e.g. turningEnabled toggled off
  mid-batch once F-01 is fixed, AUTO switch dropped, turningLockdownActive triggers
  mid-move, any latched fault).
Execution Path: stopTurn(false) only sets turnPhase_=Idle; it never touches
  trayPosition_. trayPosition_ retains whatever it was BEFORE the interrupted move
  started (the origin, not the true mid-travel physical position). When turning resumes
  later, the direction decision at line 4438-4441 uses this stale value.
Current Behavior (simulated consequence): Because trayPosition_ still equals the
  ORIGIN of the interrupted move, the next commanded direction is the SAME direction
  that was already in progress — the tray continues toward the limit switch it was
  already heading for. This is not obviously unsafe in the traced scenario.
Expected Behavior: Not necessarily different — but the code gives no explicit signal
  that trayPosition_ is "unconfirmed" after an abrupt mid-move stop; it looks identical
  to a confirmed position.
Impact: LOW in the traced scenario (self-corrects toward the correct limit). Marked
  UNKNOWN whether any other code path reads trayPosition_ under this exact condition in
  a way that would matter — a full cross-reference of every trayPosition_ read against
  every abrupt-stop path was not completed this pass.
```

```text
ID: F-05
Type: DESIGN RISK
Severity: LOW
Evidence Level: STATIC
Confidence: HIGH
Status: CONFIRMED
Source: machine_control.h
Function: updateAlarms()
Line: 4127-4138 (heaterStuckTracking_/heaterNotHeatingActive_ reset condition)
Trigger: Sensor becomes momentarily unusable (sensorUsable_ false for even one tick)
  while HeaterNotHeating (E115) tracking is in progress or already active.
Execution Path: `if (!heaterCommandedOn || !batchRunning_ || !sensorUsable_) {
  heaterStuckTracking_ = false; heaterNotHeatingActive_ = false; }` — unconditional
  reset on ANY sensor blip, not just sustained loss.
Current Behavior: A brief sensor dropout resets the stuck-heater tracking window to
  zero, discarding any progress toward detecting a genuinely stuck heater. SensorLost
  fault (higher display priority, 235 vs 65) covers the gap while the sensor is down,
  so the operator is not left without any alarm — but once the sensor recovers, E115
  detection restarts from scratch rather than resuming.
Expected Behavior: Arguable either way; flagged as a design risk, not a bug — worth a
  deliberate decision on whether brief blips should reset vs. pause-and-resume the
  tracking window.
Impact: Delays detection of a genuinely stuck heater if sensor blips coincide with (or
  are caused by) the same underlying wiring/power issue.
```

```text
ID: F-06
Type: DESIGN RISK
Severity: MEDIUM
Evidence Level: STATIC
Confidence: HIGH
Status: CONFIRMED
Source: machine_control.h, config.h
Function: processHmiTransactions() AlarmAck handling; updateHeatingAndOutputs() siren line
Line: machine_control.h:3577 (`sirenMutedUntil_ = now + SIREN_TEMPORARY_MUTE_MS`),
  machine_control.h:4717 (`req.siren = emergencyActive_ &&
  timeReached(now, sirenMutedUntil_)`), config.h:407 (`SIREN_TEMPORARY_MUTE_MS =
  300000UL`)
Trigger: Operator acknowledges an active EmergencyTemperature condition that has not
  yet cleared.
Execution Path: Acknowledgment only sets a 5-minute mute window on the siren OUTPUT.
  The underlying fault (emergencyActive_) is untouched by acknowledgment — its trip/
  clear logic is purely temperature+hysteresis+confirm-timer driven (verified: no
  `acknowledged` read anywhere in the emergencyActive_ trip/clear block, lines
  4001-4012). The actual safety cutoff (dropHeatMaster/inhibitSsr via
  faults_.masterDropRequired()) is likewise untouched by acknowledgment. So the ONLY
  effect of muting is 5 minutes of silence on the audible alarm while the unsafe
  temperature condition may persist or worsen.
Current Behavior: Correctly fail-safe for the actual heat cutoff (confirmed
  independent of ack state). The audible warning, however, can be silent for up to 5
  minutes during an ongoing emergency-temperature event.
Expected Behavior: Not a bug — a deliberate design choice (comment in code: prevents a
  stuck/looping siren from being un-mutable). Flagged as a DESIGN RISK because 5 minutes
  is a long silent window for an "Emergency" severity condition if nobody is physically
  present to notice a resumed alarm.
Impact: MEDIUM — no loss of the physical safety interlock, but potential for delayed
  human awareness during genuine emergency escalation.
```

```text
ID: F-07 (CORRECTION OF PRIOR REPORT)
Type: DESIGN RISK
Severity: LOW
Evidence Level: STATIC
Confidence: MEDIUM
Status: CONFIRMED (correction)
Source: cloud_alert_link.h
Function: checkBatchSchedule()
Line: 428-460
Correction: An earlier version of this review (previous chat turn) claimed "no
  batch-complete / overdue alarm exists" as a Missing Feature. That claim is WRONG and
  is retracted here. `checkBatchSchedule()` in cloud_alert_link.h explicitly sends a
  "BATCH_NEARING_END" push notification (BATCH_NEARING_END_DAYS_LEFT before due date)
  and a "BATCH_OVERDUE" push notification (Warning level) once
  `currentRuntime.currentDay > config.totalIncubationDays`. This exists and is wired to
  the same `runtime_.currentDay` this review already traced in machine_control.h.
Residual Design Risk (why still flagged, downgraded to LOW): This reminder is delivered
  ONLY via Cloud Push (phone notification, requires network + subscribed device) — no
  equivalent FaultManager entry (no local HMI alarm/fault code) exists for "batch
  overdue" in machine_control.h itself. If the device has no network/push subscription,
  there is no on-machine alarm equivalent; the operator's only local signal is passively
  reading the current-day counter on the HMI home screen (not an active alert).
  Confidence is MEDIUM rather than HIGH because full cloud_alert_link.h internals
  (queueing/delivery reliability) were not audited — out of this review's stated scope
  (cloud/backend explicitly excluded) and only read incidentally while correcting F-07.
```

```text
ID: F-08
Type: UNKNOWN
Severity: UNKNOWN (touches CRITICAL recovery path — see Section XI note)
Evidence Level: STATIC (declaration of non-coverage, not a behavioral claim)
Confidence: N/A
Status: UNCONFIRMED
Source: machine_control.h (class PersistentStore, referenced but internals of
  saveBatch/loadBatch A/B commit sequence and torn-write handling not read this pass)
Function: PersistentStore::saveBatch(), PersistentStore::loadBatch()
Line: not read in this session (class defined ~1588-1932; only the config-side
  refreshConfigCache()/legacy-schema logic was read in prior sessions, not the batch
  record path specifically)
Trigger: Power loss occurring mid-write during checkpointBatch()/saveBatchRecord()
  (called every BATCH_CHECKPOINT_MS=300000ms and after every completed turn).
Why UNKNOWN: This review confirmed checkpointBatch()/saveBatchRecord() are CALLED at
  the right moments (function-call level, STATIC, HIGH confidence) but did NOT trace
  the low-level EEPROM write sequence (A/B slot swap, checksum, atomicity) for the BATCH
  record specifically — only the CONFIG record's equivalent mechanism was read in an
  earlier session and is known to use double-buffering. Whether PackedBatchV1 follows
  the identical pattern was assumed by naming convention (`PackedBatchV1` mirrors
  `PackedMachineConfigV1`) but not verified line-by-line this pass.
Impact if unverified assumption is wrong: A torn write during a power-loss-mid-
  checkpoint could corrupt the batch record read back at next boot, which
  `processResume()` depends on unconditionally (no CRC/sanity check visible in the code
  paths that WERE read — `hasBatchRecord = storeReady && store_.loadBatch(batch)` at
  begin() line 3082, and `loadBatch`'s own internal validation was not verified this
  pass).
Recommended action: Dedicated follow-up review of `PersistentStore::saveBatch/
  loadBatch` and `ExternalEeprom24xx` before treating recovery-after-power-loss as fully
  verified.
```

```text
ID: F-09
Type: POTENTIAL BUG
Severity: LOW
Evidence Level: STATIC
Confidence: HIGH
Status: CONFIRMED (inconsistency) — safety impact UNCONFIRMED
Source: machine_control.h
Function: startBatch() vs processResume()
Line: startBatch() 3715-3717 (`if (!config_.turningEnabled) { message = "HAY BAT TU
  DONG DAO"; return false; }`) vs processResume() 3906-3952 (no equivalent check)
Trigger: A stored batch record with turningEnabled==false is resumed automatically
  after a power-loss/reset event (resumePending_ path, not a fresh manual start).
Execution Path: processResume()'s precondition list (9 checks, lines 3917-3949) does
  not include a turningEnabled check that startBatch() enforces. If turningEnabled was
  false at the time of resume, the batch is allowed to auto-resume with turning
  disabled — updateTurning()/updateAlarms() then correctly raise
  AutoTurningDisabledDuringBatch (Stop severity) on the very next tick (line 4200-4202),
  so the condition IS caught and alarmed — but one tick later, via a different code
  path than the explicit upfront rejection startBatch() gives.
Current Behavior: Inconsistent precondition sets between manual start and auto-resume
  for the same underlying requirement.
Expected Behavior: Either both paths enforce it upfront, or the inconsistency is
  intentional (resume should be more permissive to avoid blocking recovery on a config
  that predates the batch) — not stated anywhere in comments, so intent is unclear.
Impact: LOW — the AutoTurningDisabledDuringBatch fault (Stop severity, inhibitsTurning)
  still catches this within one control-loop tick, so no window of "silently running
  without turning and without alarm" was found.
```

---

## XII. CONSISTENCY CHECK

```text
Finding count:
  CRITICAL = 0
  HIGH     = 2   (F-01, F-02)
  MEDIUM   = 2   (F-03, F-06)
  LOW      = 4   (F-04, F-05, F-07, F-09)
  UNKNOWN  = 1   (F-08 — severity itself unknown, tracked separately)

Finding classification (Type):
  CONFIRMED BUG   = 1  (F-01)
  MISSING FEATURE = 2  (F-02, F-03)
  DESIGN RISK     = 4  (F-04, F-05, F-06, F-07)
  POTENTIAL BUG   = 1  (F-09)
  UNKNOWN         = 1  (F-08)

Every finding has exactly ONE severity value (no combined labels such as
"MEDIUM-HIGH") — verified by construction above.

Status:
  CONFIRMED   = 7  (F-01, F-02, F-03, F-04*, F-05, F-06, F-07, F-09) [*F-04 mechanism
                confirmed, downstream consequence separately marked UNKNOWN]
  UNCONFIRMED = 1  (F-08)
  REJECTED    = 0

Evidence level distribution:
  STATIC    = 7  (F-01, F-03, F-05, F-06, F-07, F-08(declaration), F-09)
  SIMULATED = 2  (F-02, F-04)
  RUNTIME   = 0

These counts match the Summary Table below exactly.
```

---

## XIII. FINAL AUDIT SUMMARY TABLE

| ID | Type | Severity | Evidence | Confidence | Status |
|----|------|----------|----------|------------|--------|
| F-01 | CONFIRMED BUG | HIGH | STATIC | HIGH | CONFIRMED |
| F-02 | MISSING FEATURE | HIGH | SIMULATED | HIGH | CONFIRMED |
| F-03 | MISSING FEATURE | MEDIUM | STATIC | HIGH | CONFIRMED |
| F-04 | DESIGN RISK | LOW | SIMULATED | MEDIUM | CONFIRMED (mechanism) / UNKNOWN (consequence) |
| F-05 | DESIGN RISK | LOW | STATIC | HIGH | CONFIRMED |
| F-06 | DESIGN RISK | MEDIUM | STATIC | HIGH | CONFIRMED |
| F-07 | DESIGN RISK (correction) | LOW | STATIC | MEDIUM | CONFIRMED |
| F-08 | UNKNOWN | UNKNOWN | STATIC (non-coverage) | N/A | UNCONFIRMED |
| F-09 | POTENTIAL BUG | LOW | STATIC | HIGH | CONFIRMED |

```text
Confirmed Bugs:      1  (F-01)
Potential Bugs:      1  (F-09)
Design Risks:        4  (F-04, F-05, F-06, F-07)
Missing Features:    2  (F-02, F-03)
Missing Alarms:      0  (none found as a standalone category distinct from F-02/F-03;
                         F-07's original "missing alarm" claim was RETRACTED after
                         verification — the alarm exists at the cloud layer)
Missing Recovery:    0  (no confirmed case of a fault with no recovery path at all —
                         F-02 is a recovery that WORKS but lacks an escalation ceiling,
                         classified as Missing Feature, not Missing Recovery)
Unknowns:            1  (F-08)
Runtime Tests:        0
Simulated Tests:      2  (F-02, F-04 — hand-traced multi-tick sequences)
Static Checks:        7  (F-01, F-03, F-05, F-06, F-07, F-08, F-09)
```

```text
CAN RUN HAPPY PATH?
YES (per STATIC/SIMULATED trace only — not RUNTIME verified; no execution occurred)

CAN DETECT MAJOR FAULTS?
YES (per STATIC trace of FaultManager wiring — sensor loss, over-temperature, output
  conflict, turn timeout/stuck/conflict all confirmed wired to faultDescriptor() with
  correct inhibit/drop flags)

CAN RECOVER FROM FAULTS?
PARTIAL — most traced fault/recovery pairs are correct (STATIC/SIMULATED, HIGH
  confidence). F-01 is a CONFIRMED recovery-path bug (config-level, not physical
  safety). F-02 is a confirmed gap in recovery ESCALATION (repeated identical failures
  are not distinguished from a single transient one).

CAN GET STUCK?
NO hard deadlock found (no blocking wait construct exists in the reviewed control
  loop). F-02 is a RETRY LOOP (not a deadlock — machine keeps attempting and reporting
  status, it does not freeze), still operationally equivalent to "stuck turning" from
  the eggs' perspective if the root cause is never fixed by a human.

CAN ENTER INVALID STATE?
UNKNOWN for the persistence layer (F-08). NO invalid state transition found in the
  parts of the state machine that were fully traced (batch lifecycle, turn phase,
  output arbitration).

MISSING CRITICAL FUNCTIONALITY?
YES — F-02 (turn-fault retry ceiling) is assessed HIGH severity and is a genuine gap
  with direct operational consequence (eggs unturned). F-03 (resume-confirmation
  escalation) is MEDIUM.

PRODUCTION STATUS:
NEEDS FIX BEFORE PRODUCTION

VERDICT LIMITED BY UNKNOWN: YES — F-08 (batch record persistence torn-write behavior)
  was not verified and sits directly on the power-loss recovery path that this review's
  scope explicitly names as critical. This finding alone is sufficient, per the
  reviewing rules, to bar a "Production Ready" or "Production Ready with Risks"
  conclusion until it is resolved to STATIC or RUNTIME confidence.
```

**Why not "NOT SAFE TO RELEASE":** no finding in this pass demonstrates a path where the
physical safety interlocks (heat master + SSR double layer, output-conflict cutoff,
emergency-temperature cutoff, sensor-loss cutoff) can be defeated or bypassed — every
CONFIRMED BUG/DESIGN RISK found degrades either operational trust (F-01), fault-recovery
completeness (F-02, F-03), or audible-alert timing (F-06), not the underlying cutoff
logic itself. That is a materially different risk class from "unsafe to release," but it
is also not yet "ready" given F-01 (confirmed, easy to fix — one boolean condition) and
the open F-08 unknown.
