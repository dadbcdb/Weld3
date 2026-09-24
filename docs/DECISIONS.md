# Design Decisions

## 2026-09-24 — Stage values are direct ADC targets

User requested replacing stage current entries with ADC codes. Accept 0..65535
in WinApp, protocol validation and the single-shot sequencer. PID error is
(profile ADC target / 65535) minus normalized filtered ADC1. Legacy PID target
field remains in the protocol but is unused; remove its UI control. GET PID
reports target_mode=stage_adc and WinApp verifies this before starting a test.
Protocol/profile field names currentN_a and stage_current_a remain for storage
compatibility: existing numbers are NOT converted; 100 now means ADC 100.
Open-loop continues proportional profile-height-to-duty mapping. This change
does not calibrate amperes or update the separate embedded LCD's unit labels.

## 2026-09-24 — Separate Rogowski channel and ADC plot scaling

At user request, change runtime ADC2 rank 1 to channel 1 (PA1_C/A3); ADC1
stays channel 0 (PA0_C/A2). Preserve open analogue switches, PWM waveform,
trigger timing and filtering. Generated init/.ioc is not changed; retain the
USER CODE override when regenerating. ADC plots independently choose a zero-based
upper bound from raw/filtered peaks with 10% headroom, rounded and capped at
65535, minimum span 100 codes. Both lines within each plot share that scale.

## 2026-09-24 — ADC graph trace

Use device single-shot records for separate ADC graphs rather than sparse host
polls. Append three secondary/filter fields to TEST TRACE rows; new WinApp accepts
6 or 9 fields. Older WinApp requires an update for 9-field traces. Sampling,
PWM timing and controller math are unchanged; static trace storage grows 24 KiB.

## 2026-09-24 — Secondary telemetry

Expose ADC2 raw mean and normalized filtered value in both status replies.
Use integer millionths to avoid embedded printf float support. WinApp labels
the raw/normalized units and existing shared PA0_C input explicitly; no ampere
calibration or independent Rogowski channel mapping is inferred.

## 2026-09-24 — Secondary/Rogowski-path feedback filter

- Add independent 1-ms first-order IIR to ADC2 half-block means, updated for
  every valid published DMA block even while no waveform test is active.
  Seed from first valid sample after acquisition init to avoid startup bias.
- g_secondaryCurrentFiltered exposes normalized 0..1 feedback; raw mean/min/max
  remain unchanged. This filter does not perform Rogowski integration or current
  calibration and is not connected to a secondary compensation loop.
- Existing bench ADC1/ADC2 channel-0 duplicate PA0_C mapping is retained.
  No PWM/pin/trigger/protection changes; expected PWM timing remains unchanged.

## 2026-09-24 — PID feedback low-pass trial

- User requested filtering. Apply a first-order IIR to normalized ADC half-block
  means before PID P/I/D: y += alpha*(x-y), alpha=dt/(0.001+dt).
  Compute alpha at start; seed state from the current raw mean. At nominal
  119.16-us updates alpha is about .1065. Step response takes about 3.2 ms
  to reach 95%; this is added measurement lag requiring bench retuning.
- g_wavePidFiltered exposes the filtered 0..1 value to the debugger. Raw ADC
  diagnostics/CSV remain unfiltered. No UI coefficient adjustment is added.
- No duty slew limit, pin/phase/sample timing change or delayed stop is added.
  Zero-envelope immediately requests zero; STOP/fault still forces pins LOW.
  Expect smoother duty corrections rather than a forced duty ramp; actual
  stability, IRQ execution time and scope response remain target tests.

## 2026-09-23 — Normalized ADC PID single-shot bench mode

- TEST START now uses the stored PID enabled flag. WinApp explicitly sets and
  reads back PID parameters before starting. Open-loop checkbox takes priority;
  unchecked plus PID enabled selects closed-loop; both unchecked keeps dry-run.
- Feedback is ADC1 half-block mean / 65535 on PA0_C, not amperes or diode-OR
  occupancy. Target is configured peak target times relative profile height.
  PID runs once per 32 pairs (nominal 119.16 us), with measurement derivative,
  conditional integration and per-phase duty cap. The initially added 1.0 duty/s
  upward slew was removed at user request because it masked PID step response.
  Duty now follows the bounded PID result at each acquisition callback; normal
  compare-latch latency remains. With zero feedback, Kp=.2, target=.5 and a 5%
  cap, expect a step to 5% rather than a 50-ms ramp. Pin mapping is unchanged.
  Zero target resets controller; start resets history. Stops/lease remain active.
- PB7/PD15 polarity, timer phase centers and sample locations are unchanged.
  Expect equal A/B widths, centers T/2 apart, varying within selected duty cap;
  SQ/COOL/end remain LOW. This is uncalibrated bench control, not verified
  power-stage current regulation. No flashing or energized test performed.

## 2026-09-15 — WinApp waveform as an explicit open-loop single-shot test

- User requested using the WinApp three-stage waveform for test output after
  clarifying existing production SG3525 hardware and hardware protection.
  Keep original START as timing-only dry-run; add TEST START duty_percent=N,
  TEST STATUS, TEST TRACE and STOP. No implicit power action occurs on connect,
  SET, profile load, boot or an ordinary START.
- Default PWM_WAVE_TEST_ENABLE=1 replaces boot OR-PI activity with zero output.
  Snapshot and independently validate all stage settings at TEST START. Map
  the highest stage current to a separate per-phase duty cap (UI default 5%,
  accepted software range 1..45%); preserve relative stage heights and times.
  Values in amperes define shape only, not a calibrated current-to-duty law.
- DMA processing at priority 4 advances the finite sequence from sample-pair
  counts, independent of host polling and RTOS task scheduling. Envelope values
  use millisecond resolution. Comparisons are still staged at safe alternating
  timer extrema. Allow three acquisition halves for zero compares to settle
  before restoring timer pin function for a new run. There is no auto-repeat.
- HAL TIM23 tick independently stops an active run when ADC processing has
  been absent for more than 3 ms or TEST STATUS lease has expired for more than
  1000 ms. Detected TCP disconnect and STOP also force outputs LOW. Existing
  acquisition faults remain latched; normal completion/STOP can be restarted.
  CPU/global interrupt halt is not covered by these software guards.
- Keep up to 2048 trace records in RAM, with adaptive 1..5-ms logging intervals
  over the permitted profile (up to 8994 ms), including start/final records.
  Trace has time, profile target, commanded per-phase duty and raw ADC mean/
  min/max. It is not a full ADC capture or a measurement of gate pulse widths;
  short features can fall between retained samples on long profiles.
- Trace retrieval is allowed after completion, with run-id/count validation.
  Active output rejects settings/storage/capture/new-start commands; STOP and
  status stay available. WinApp sends the currently displayed settings before
  starting, locks test controls, retrieves the completed trace and can save CSV.
  A separate duty graph avoids presenting ADC values as actual current.
- All main.c runtime changes remain within USER CODE. No .ioc timer/pin
  reassignment is introduced. Retain these overrides across regeneration.
- ARM incremental link, .NET Release build, 12 host tests and an offscreen WPF
  simulation/render check passed. The host test executes the actual sequencer
  functions with register stubs for envelope timing, finite completion, STOP,
  lease expiry, ADC-loss stop, fault rejection, restart and trace bounds. No
  board download, gate-drive test or energized operation was performed.


## 2026-09-14 — User-requested zero-input PI response test

- Add test-only PWM_OR_BENCH_ALLOW_ZERO=1: all-LOW feedback runs PI rather
  than entering constant-input fallback. InputInvalid=1 remains diagnostic,
  but Updates advances and command approaches the existing .90 occupancy
  limit (.45 per phase), with the existing .025/window slew and anti-windup.
- PA0_C/A2 remains selected. All-HIGH policy and acquisition fault shutdown
  remain unchanged. Both bench macros must be 0 to restore strict input-fault
  shutdown. This is not an approved missing-current-sensor policy for welding.
- ARM build/link and C-controller host tests passed, including prolonged zero
  feedback, slew/bounds, saturation and recovery. No target flash performed.

## 2026-09-14 — Move bench feedback to PA0_C at user request

- Select ADC1 regular rank 1 channel 0 in the runtime USER CODE override,
  moving OR feedback from PA1_C/A3 to PA0_C/A2. ADC2 already selects channel
  0 and now supplies duplicate samples of the same pin, not a second sensor.
- Preserve open PA0 analogue switch, waveform, sampling, PID and diagnostic
  hold policy. Generated ADC1 init/.ioc still selects channel 1; regeneration
  must retain CurrentSense_Init's channel-0 override.
- ARM incremental build/link passed; target wiring and response on A2 remain
  to be checked. Prior A3 screenshot showed valid input and 7070 PID updates
  with measured occupancy about .47168; it does not validate the new A2 wiring.

## 2026-09-14 — Correct OR feedback to confirmed PA1_C wiring

- User explicitly confirmed PA1_C (Arduino A3), rather than literal PA0 or
  PA1. Change the runtime ADC1 rank-1 selection from channel 16 to channel 1.
  Existing MSP leaves the PA1 analogue switch OPEN; preserve Ethernet on PA1.
- PWM waveform, sample timing, PID parameters, debug freeze and test-only
  invalid-input hold remain unchanged. This USER CODE override survives
  regeneration and matches the generated ADC1 channel-1 selection.
- ARM incremental compile/link and eight existing host tests passed. Actual
  input levels and closed-loop response on this corrected channel await target
  retest; the agent did not flash the board.


## 2026-09-14 — Freeze the synchronized timers during debugger halt

- User reports PAUSE then RUN removes PWM. The shown pre-resume snapshot has
  zero acquisition errors, 30786 blocks, inputInvalid=1 and no PID updates.
  It establishes constant-below-threshold feedback, not the post-resume fault.
- TIM3/TIM4 previously continued during CPU halt, allowing DMA to overwrite
  unprocessed halves. Set both DBGMCU APB1LFZ1 freeze bits before startup so
  debugger halt stops the PWM and sample trigger clocks together. Keep fault
  checks and their reset-required latch; do not auto-clear faults on resume.
- This is for disconnected-power-stage debugging. Timer freeze retains the
  instantaneous pin level, possibly HIGH, and an in-flight ADC/DMA transaction
  can still finish. It is not a safe power-output pause. Normal run timing is
  unchanged; arbitrary breakpoints inside acquisition code still need target
  validation. No claim that every halt/resume edge case has been eliminated.


## 2026-09-14 — Temporary bench waveform hold for invalid OR input

- User's snapshot shows SD/ADC/DMA/acquisition errors zero, fault detail zero,
  and 32 published DMA halves. This matches the first 1024-sample PID window
  and strongly suggests the constant-input stop; g_pwmOrFault was not shown,
  so neither this cause nor a physical wiring fault is target-confirmed.
- For disconnected-power-stage testing only, PWM_OR_BENCH_HOLD_ON_INVALID=1
  now replaces constant-input shutdown with fixed 25% OR occupancy (12.5%
  per pin). PID integration is reset/frozen on invalid input. Valid mixed
  HIGH/LOW input resumes PID. This is a temporary diagnostic fallback, not a
  correction of the analogue feedback or a production protection policy.
- g_pwmOrInputInvalid reports 1=all LOW, 2=all HIGH, 0=mixed or not yet assessed;
  g_pwmOrInvalidWindows counts such windows. Set the macro to 0 to retain
  strict fault=2 shutdown. ADC/DMA/order/deadline and preload faults still stop
  PWM in both modes. Pins, sample timing and threshold are unchanged.
- ARM build/link and eight host tests passed, including executing the C
  controller in both modes and prolonged-invalid/recovery cases. Board retest
  remains required; the agent did not flash or energize hardware.


## 2026-09-14 — Diagnose missing bench PWM before altering stop conditions

- User reports no PWM following the OR/PID build. Cause is not yet established.
  Constant input stops the outputs after about 4 ms; acquisition/preload-window
  faults and initialization errors can also stop or prevent startup.
- Preserve first-stop TIM4/TIM3 registers, DMA remaining count, input range,
  accumulated HIGH/total samples and existing fault codes in g_pwmStopDetail.
  g_pwmStartupStage distinguishes acquisition init from successful PWM start;
  g_pwmErrorCaller preserves Error_Handler's caller for ELF address resolution.
- Diagnostic only: no threshold, PWM waveform, sample timing or shutdown
  condition is bypassed. Target fault values are required before selecting a fix.


## 2026-09-14 — PA0 diode-OR bench feedback and equal A/B pulse widths

- User clarified that A/B are diode-ORed PWM signals and requested PID testing,
  not current regulation. Supersedes the Sep 8 same-CCR inversion: use TIM4
  center-aligned PWM1/PWM2 with CCR2=w and CCR4=ARR-w, keeping PB7/PD15 HIGH
  polarity and full-period 65536 ticks. Both widths shrink together; centers
  remain T/2 apart. Initial occupancy is 25%, target 50%, limits 10..90%.
- Count ADC1 samples >=20000 codes on literal PA0 / ADC1_INP16. Retain dual
  packed DMA and ADC2 diagnostics. Restore external TIM3 triggering, circular
  DMA and simultaneous dual mode explicitly in CurrentSense_Init: generated
  MX_ADC initialization in the working tree had reverted to software-triggered
  independent mode. ADC12 uses DIV2 and 32.5-cycle acquisition.
- Update PID every 1024 samples (16 full periods, ~3.813 ms). Kp=.20,
  Ki=15/s, Kd=0 by default due to quantized occupancy; derivative-on-measurement
  is implemented. Conditional anti-windup and a 2.5 percentage-point/update
  command slew limit are applied. Gains/threshold/target are compile-time bench
  settings, not calibrated or tuned current-loop values.
- Each CCR is staged after its own pulse center and loads at the opposite
  timer extremum, when that pin is LOW. This avoids splitting a pulse at its
  center. A/B adopt changed commands in successive half periods. A direction/
  count check rejects missed preload windows (fault-detail reason 6).
  Never force a running update event or alter the TIM3 sample divider.
- Constant LOW/HIGH over a control window latches a test fault; acquisition
  faults and Error_Handler stop timers and drive PB7/PD15 LOW via GPIO.
  Reset is required to restart. There is no hardware watchdog/interlock for
  silent loss of all interrupts, and these changes are not power-stage safety.
- All firmware edits are in USER CODE regions. weld3.ioc still describes the
  old up-counter, TIM5 PA0 output, and generated ADC setup. Regeneration must
  preserve these runtime overrides; do not enable TIM5 on PA0. The original
  CURRENT_CONTROL_ENABLE compile-time prohibition remains for welding control.
- Expected scope waveforms and bench wiring are in HARDWARE.md. No flashing,
  energized power-stage operation, or target waveform validation was performed.


## 2026-09-08 — TIM4 PWM B inverse phase

- TIM4 CH2/PB7 is PWM A and TIM4 CH4/PD15 is PWM B. Both use the same compare
  value; CH4 is configured as `PWM2` in the preserved `USER CODE` block so B is
  the inverse of A at the 50% test duty.
- TIM4 is not an advanced timer and inserts no dead time. This change is for
  phase verification only; keep the power stage disconnected until polarity,
  driver behavior and required dead time are confirmed on the oscilloscope.

## 2026-09-01 — Raw LCD capture over TCP

- `CAPTURE SCREEN` snapshots the currently displayed 480 x 272 RGB888 buffer
  into reserved HyperRAM at `0x700C0000`, then sends a size/hash header followed
  by 391,680 raw bytes. Snapshotting avoids transmitting a framebuffer while
  TouchGFX reuses it for drawing.
- WinApp verifies the FNV-1a hash and encodes the BGR byte stream as PNG on the
  PC. PNG compression is deliberately kept off the MCU. This is a diagnostic
  operation only and does not alter PWM, ADC, gate, profile or weld behavior.

## 2026-09-01 — WinApp SET loads the selected device profile into TouchGFX

- WinApp `SET` transfers the selected zero-based file/number and then issues
  `LOAD PROFILE` for that current selector. The controller therefore applies
  the OSPI-resident slot rather than copying potentially stale host grid data.
  After a successful load, WinApp reads `GET SETTINGS` back and the TouchGFX
  grid shows the same current, SQ/COOL, UP, weld-time and DOWN values.
- This does not save the profile to OSPI and does not start welding. Device
  save retains its existing explicit meaning. The values remain UI setpoints
  and are not connected to power-output sequencing.

## 2026-08-30 — OSPI-backed 16 x 16 profile library

- Use zero-based `File 00..15` and `Number 00..15` selectors, providing 256
  controller-resident preset slots in the MX25LM51245G OSPI NOR. This supersedes
  the briefly implemented 99 x 99 WeldRev3 extension and matches the reference
  project's `F_UNIT=16` organization.
- Reserve the last 64 KiB. Each slot is a versioned 128-byte record with file,
  number, complete `WeldSettings`, and CRC. Saving preserves the other 31
  records in the affected 4-KiB erase sector and verifies the rewritten record.
- `SAVE PROFILE` stores the current validated controller setting. `LOAD PROFILE`
  validates CRC and setting ranges before making it current. Both reject an
  active dry-run. The WinApp simulation keeps temporary in-memory slots only.
- The UI separates four operations: file load/save exchanges one JSON setting
  library with the PC filesystem; device load/save exchanges the entire sparse
  OSPI library (all 256 addressable slots, with empty slots omitted on read).
  `SET PROFILE file=N number=N` explicitly transfers the zero-based selector
  values to the controller. The previous current-value read/apply buttons were
  removed from the UI.
- Changing either zero-based selector immediately stores the outgoing grid in
  the WinApp library and displays the newly selected host profile (zero-filled
  when unused). This does not contact the controller. Only the adjacent `SET`
  button changes the firmware's current file/number selection.
- Whole-device load uses the streamed `DUMP PROFILES` response. Whole-device
  save first erases the complete used profile partition, then programs only the
  populated host records with `SAVE PROFILE FAST`; this prevents stale slots
  and avoids repeated 4-KiB erase cycles. Loss of power during whole-device save
  can leave a partial library, so the operation must be allowed to complete.
- Every TCP command now retires an expired dry-run before evaluating `ERR BUSY`;
  this no longer depends on an intervening `GET STATUS`. WinApp disables SET,
  START and device transfer buttons for the duration of a whole-library
  transfer, preventing a new dry-run from racing an OSPI operation.
- The reference WELD5000A source uses the same 16 x 16 `F_UNIT` layout.
- The generated OCTOSPI1 `DeviceSize` and `ClockPrescaler` lines were corrected
  to 26 and 2 and the `.ioc` was synchronized. CubeMX regeneration must retain
  these values and the final-2-MiB partition reservation.

## 2026-08-29 — Three-stage weld setting model

- Follow the referenced WELD5000A concept of independent weld current and time
  for stages 1, 2 and 3. A stage is disabled when both values are zero; a
  current/time mismatch is rejected by both the WinApp and firmware.
- Extend `WeldSettings` and the ASCII TCP protocol with `current1_a/time1_ms`,
  `current2_a/time2_ms` and `current3_a/time3_ms`. The prior single
  `current_a/duration_ms` SET syntax is superseded, so WinApp and firmware must
  be updated together.
- Preserve the existing provisional 0..1000 A validation limit rather than
  copying the reference product's 5000 A limit. WeldRev3 maximum safe current,
  sensor scaling and protection thresholds remain undefined.
- These are stored host/UI setpoints only. They are deliberately not connected
  to PWM duty, gate enable or a timed weld sequencer. Implementing execution
  requires verified polarity/dead time, current calibration, limits,
  interlocks, shutdown behavior and an expected oscilloscope waveform.
- WeldRev3 is treated as a current-controlled spot welder for this interface;
  remove voltage and wire-speed settings/telemetry from the TCP payload and
  WinApp. The app separates monitoring and weld settings into tabs and renders
  a current-versus-time preview from the three stored stages.
- Align each stage more closely with the reference firmware: ramp-up (0..500
  ms), current hold (0..999 ms), and ramp-down (0..500 ms). The WinApp uses one
  combined settings/result screen. It clears measured points on the rising edge
  of `status.welding`, captures current versus `weld_time_ms` during that one
  cycle, and freezes the overlay when welding returns false.
- Preserve the reference `cl`/`cl2` intervals as `cool1_ms` and `cool2_ms`
  (0..999 ms): zero-current hold after stage 1 and stage 2 before the next
  ramp begins. There is no `cool3_ms` because stage 3 is the final stage.
- The capture UI depends on the controller publishing meaningful `WeldStatus`
  transitions and current samples. That producer is not yet implemented; no
  claim is made that the present power-control firmware supplies a weld trace.
- Add a `START` command and WinApp `1회 START` button for timing-only dry-run
  verification. It transitions `welding` for exactly the configured SQ/COOL,
  UP, Weld Time and DOWN total and advances `weld_time_ms`. It explicitly keeps
  reported actual current at 0 A and does not start PWM, enable a gate, alter
  duty, or operate the power stage. `ERR BUSY` rejects overlapping starts.

## 2026-08-29 — TCP default task stack margin

- A WinApp connection reached the TCP server and then triggered the FreeRTOS
  stack-overflow hook for `defaultTask`. The connection itself was successful;
  the firmware stopped before completing the greeting/command exchange.
- Increase the CubeMX default task allocation from 512 to 1024 words (2 KiB to
  4 KiB on this target) and move the single-client 512-byte receive buffer to
  static storage. The `.ioc` and generated `main.c` value are both updated, so
  CubeMX regeneration should retain the allocation.
- This changes only TCP task memory allocation. It does not change PWM, ADC,
  gate output, protection behavior, setpoint ranges, or add a weld command.
- Host compilation validates memory/link fit. Target stack margin and repeated
  connect/poll/disconnect behavior still require a free-running board test.

## 2026-08-29 — DAC sine test source on PA5

- User requested sine output. Default bench signal is 1 kHz, 2048 +/-1241
  DAC codes: nominal 1.65 V offset and 2 Vpp at VREF+=3.3 V.
- Use the existing PA5/DAC1_OUT2 mapping; PA4 remains LCD VSYNC. The PA5
  connector/access point must be checked against the board schematic.
- TIM6 update TRGO clocks DAC channel 2 via DMA1 Stream1, DAC1_CH2 request.
  64 samples/cycle, timer PSC=0 and rounded ARR=4296 at 275 MHz yields
  999.97091 Hz. This is independent of TIM3/TIM4 and ADC DMA1 Stream0.
- DMA source is a 32-byte-aligned RAM_D1 table, cache-cleaned before start.
  DMA HT/TC interrupts are disabled after arm; errors/underruns use priority 6
  and stop TIM6. No voltage is guaranteed on error; this is a test source only.
- Configuration overrides are in USER CODE, leaving generated DAC init and
  weld3.ioc unchanged. Regeneration must retain PA5 analogue output and USER
  CODE. Do not assign TIM6, DMA1 Stream1 or their vectors to another function.
- DAC_SINE_TEST_ENABLE=1 starts output after LCD startup; set to 0 to disable
  for a production build. Disconnect sensor outputs before DAC-to-ADC wiring.
  No power output configuration, current compensation or protection is changed.
- ARM incremental build/link and static contract tests passed. Analogue voltage,
  loading, DAC settling and ADC loopback have not been verified on the board.

## 2026-08-29 — Acquisition IRQ above the RTOS masking boundary

The first-fault snapshot shows reason=2, offset=expectedOffset=32, NDTR=25,
elapsedCycles=134, budget=65536, warmupBlocks=1 and publishedBlocks=0. A full
callback arrived while DMA was writing the second half again; the record does
not demonstrate slow arithmetic (134 cycles is only entry/check time).
At the configured rate NDTR=25 corresponds to 39 pairs into the next buffer,
about 145 us after the preceding full-buffer boundary, modulo whole buffers.

DMA1 Stream0 and ADC shared IRQ were priority 5, equal to LTDC/DMA2D and to
FreeRTOS configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY. They can be delayed by
graphics handlers and BASEPRI critical sections. Raise only ADC/DMA to 4,
with a compile-time boundary check. Every callback reachable from these IRQs
must remain RTOS-free, bounded and nonblocking; even FromISR RTOS APIs are
forbidden at this priority. Keep all ownership/fault checks intact. This removes
an identified latency risk; the exact historical blocker (including possible
debugger halt/global interrupt disable) and target result remain unverified.
No sampling rate, gate/PWM configuration or fault bypass is changed.

## 2026-08-29 — Preserve first acquisition-fault evidence

The observed fault=1 with zero published blocks cannot distinguish a late IRQ,
buffer reuse during processing, callback order, or exceeded cycle budget.
Add g_currentFaultDetail at the existing failure branches without changing
thresholds, DMA timing, PG3 meaning, PWM outputs or fault stop behavior. Capture
the first snapshot before stopping TIM3; inspect it after a free-running test,
not by stepping through DMA callbacks. A debugger halt while timers continue
can itself cause lost DMA half ownership. This patch is diagnostic, not a
claimed fix for PG3 absence.

## 2026-08-29 — Half-period acquisition and honest processing marker

- Supersedes TIM6 free-running acquisition and the TIM4-generated PG3 marker.
  TIM4 PWM pin assignment, polarity, ARR=65535, PSC=0 and 50% test duty are
  preserved. No new gate output or duty update is enabled.
- TIM3 PSC=0, ARR=1023, CCR4=512, PWM2 internal OC4REF/TRGO supplies 64
  equally spaced sample pairs per PWM period, at sample-interval midpoints.
  TIM3 waits in slave trigger mode for TIM4 TRGO=ENABLE; it is NOT started
  in software. Both timers share APB1, so the exact 64:1 divider avoids drift.
  No per-period reset/update trigger is used (avoids extra ADC conversions).
- TIM3 ITR3 is selected for TIM4 TRGO. This agrees with ST's H7 RM0433
  connection table, but the H735-specific RM0468 PDF download failed during
  this task. Do not treat this route or its start delay as target-verified.
- A 64-word (256-byte) DMA buffer has 32-pair (128-byte, cache-line-aligned)
  halves. Half callback is fixed PWM half 0; full callback is half 1. The
  first PWM period is discarded for startup/preload settling.
- Each half retains raw mean/min/max for both sensors. Signed zero-offset
  removal, calibrated amperes, edge blanking, PI and duty actuation remain
  unimplemented. Raw bipolar mean is NOT a current-magnitude feedback value.
  CURRENT_CONTROL_ENABLE=1 now fails compilation rather than pretending to
  enable an empty controller. Changing duty from 50% requires redesign of
  sample selection; fixed halves are not generally ON intervals.
- PG3 brackets actual processing. DWT current/max cycle counts measure this
  code path (not the entire DMA ISR). The LCD delay no longer resets DWT.
- Late/coalesced callbacks are checked using expected half order, DMA NDTR
  before/after reading and a half-period cycle budget. ADC/DMA errors latch
  a fault. Faults stop TIM3 acquisition only, NOT the power stage. These are
  diagnostics, not a substitute for an independent gate shutdown path.
- ADC1/2 async prescaler is DIV4 instead of DIV1; sample time is 16.5 cycles
  instead of 1.5. Both ADCs receive offset/linearity calibration before arm.
  Actual ADC clock (including the device's internal divider), input settling
  and trigger-to-completion delay still require target validation.
- Existing generated ADC initialization lines were edited (clock, sampling,
  trigger). TIM3 configuration and TIM4 master override are in USER CODE.
  weld3.ioc still does not encode this complete runtime setup; regeneration
  MUST be followed by rechecking ADC1/2, DMA, IRQs and TIM3/TIM4 settings.
- No firmware was flashed and no power-stage operation was performed.

## 2026-08-26 — 200 kS/s dual-current acquisition scaffold

Historical: superseded by the 2026-08-29 configuration above.

- ADC1/Arduino A3 is treated as the primary Hall-current input and
  ADC2/Arduino A2 as the secondary Rogowski-integrator input. This electrical
  assignment is provisional until the interface schematic is verified.
- ADC1 and ADC2 use regular simultaneous dual mode. TIM6 update events request
  conversions at approximately 200 kS/s and DMA1 Stream0 transfers packed
  32-bit sample pairs into a 128-entry circular buffer.
- ADC1 conversion data management is explicitly circular DMA; configuring only
  DMA1 Stream0 as circular does not enable continuous ADC DMA requests.
- DMA half/full callbacks calculate raw mean, minimum and maximum codes for
  each 64-pair block. They do not modify PWM duty.
- `CURRENT_CONTROL_ENABLE` remains zero. Sensor scale, zero offset, polarity,
  current limits and safe duty bounds must be verified before closed-loop duty
  compensation is implemented or enabled.
- ADC1 trigger and multimode settings were changed in generated initialization
  code. CubeMX regeneration can overwrite these changes; the `.ioc` does not
  yet describe DMA1 Stream0, TIM6, or the dual-ADC runtime configuration.

## 2026-08-25 — PWM A/B pin assignment

- PWM A uses PB7 / Arduino D9 / TIM4_CH2 in PWM1 mode.
- PWM B uses PD15 / Arduino D6 / TIM4_CH4 in PWM2 mode.
- At the current 50% duty setting these outputs are inverse phase signals.
- This arrangement does not insert dead time. It must not be assumed safe for
  direct half-bridge gate drive until the external driver behavior is known.

## 2026-08-25 — ADC trigger moved off the B-phase output

Superseded for current acquisition on 2026-08-26 by the TIM6-triggered dual-ADC
DMA path described above. TIM4_CH3 remains configured but is no longer selected
by ADC1.

PD15/TIM4_CH4 was initially used as a visible ADC trigger signal. It was later
required as PWM B, so ADC timing moved to the unused TIM4_CH3 OC reference.
TIM4 TRGO now selects OC3REF.

The current compare calculation triggers ADC once, at the center of PWM A's ON
interval:

```text
CCR_A       = (ARR + 1) * duty_percent / 100
CCR_ADC     = CCR_A / 2
```

Sampling both A and B ON centers would require two events per period. Options
under consideration are timer compare interrupts with software ADC start,
regular plus injected ADC conversions, or a synchronized auxiliary timer.

## 2026-08-25 — ADC completion observability

Superseded again on 2026-08-26: a DMA-buffer marker was slower than PWM and was
not useful as a control timing reference. PG3 is now set at TIM4 update and
reset at TIM4_CH3 compare, so its repetition rate matches PWM.

ADC1 runs with conversion-complete interrupts. `PG3 / Arduino D2` toggles in
`HAL_ADC_ConvCpltCallback`, and the ADC result is copied to `g_adc1Value`.
Because the GPIO toggles once per conversion, a 4.196 kHz conversion stream
produces an approximately 2.098 kHz square wave on PG3.

## 2026-08-25 — SD initialization diagnosis

The debugger stopped in `Error_Handler()` because `HAL_SD_Init()` returned an
SD command-response timeout before TIM4 initialization and PWM startup. A
manual reset subsequently worked. A startup delay/retry is proposed, but the
root cause and final recovery policy remain unverified.

## 2026-09-01 — Preserve the ADC1 DMA handle across CubeMX generation

- The current `.ioc` does not cause CubeMX to emit the `hdma_adc1` storage
  definition, while preserved ADC1 MSP, IRQ, and current-sense code still use
  that handle.
- Define `hdma_adc1` in `main.c` inside `USER CODE BEGIN PV` so regeneration
  preserves it. This restores the pre-generation linkage and does not change
  DMA routing, ADC timing, or power-output behavior.
- This remains a regeneration-risk indicator: the `.ioc` and the preserved
  runtime ADC/DMA configuration are not yet fully synchronized.

## 2026-09-23 — WinApp connection references across async waits

- Commands capture the writer/stream before waiting for the command lock and
  reject a disconnected or replaced connection after acquiring it. A queued
  command must not silently migrate to a new connection.
- Line and exact-length reads retain a local stream reference across awaits.
  Disposing the connection can still produce an ordinary transport exception,
  but clearing the shared field cannot cause a null dereference in these reads.
- Apply the same post-lock writer identity check to the profile dump.
