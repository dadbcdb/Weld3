# Project Status

## 2026-09-25 — User-settable WinApp integration offset

Added an automatic/manual offset control to the Rogowski waveform tab. Auto
remains the default and displays the detected guarded pre-PWM mean. Clearing
the checkbox enables a voltage entry; `오프셋 적용` validates a finite number
and recomputes both the integrated trace and RMS view using that one fixed
value. Release build completed with zero warnings/errors and the
`PWM_SingleShot` package was refreshed.

## 2026-09-25 — Synthetic 100-ms square-current CSV

Added `pid_test/synthetic/SquareCurrent_100ms.csv` as an ideal relative-current
reference and `SquareCurrent_100ms_Rogowski.csv` as its direct-coil derivative
with -0.155 V offset, 200-us rise/fall and 268,554.6875-S/s ADC-grid timing.
The actual WinApp parser/integrator reproduced 1.000000002 relative amplitude
at 50 ms and returned to 1.4e-9 after 100.3 ms. The generator script makes both
fixtures reproducible. Added `BipolarSquareVoltage_100ms.csv` to demonstrate
the textbook square-voltage integral: +1 V for 50 ms followed by -1 V for
50 ms. Fixed pre-trigger offset integration gives 49.9954 V*ms at the apex and
-0.0037 V*ms after the full 100-ms cycle (ADC-grid boundary quantization).

## 2026-09-25 — Measurement-valid fixed offset restored

Supersedes the PWM-state offset entry below. Adaptive/ON/post offsets were
removed because they can erase real low-frequency current and therefore are
not valid measurement. WinApp again freezes one arithmetic-mean offset from
the guarded pre-PWM zero-current interval and applies only the trapezoidal
cumulative sum. Any PWM-active slope remains visible for electrical diagnosis.
Release build and package publish passed.

## 2026-09-25 — PWM-state Rogowski offsets

A single pre-PWM offset still produced the reported active-window slope because
the measured coil channel mean changes when PWM is present (for example DS1 is
about -0.155 V OFF versus -0.145 V ON). WinApp now keeps the same cumulative
integration formula but uses separately measured OFF-before, PWM-ON and
OFF-after means. The ON mean is the trapezoidal mean over the detected complete
finite burst, which makes its net coil area zero without reshaping samples.
The UI reports all three offsets. Release build and package publish passed.

## 2026-09-25 — PWM-pretrigger mean offset

Corrected Rogowski offset detection from a pre-trigger median to the arithmetic
mean before the known t=0 PWM start, excluding the final 0.5 ms as an edge
guard. DS1/DS2/DS3 offsets are approximately -0.155/-0.157/-0.152 V and the
integral is flat through the guarded pre-PWM interval. The value is frozen for
the whole record; no post-PWM or endpoint forcing is applied. Release build and
package publish passed.

## 2026-09-25 — Plain offset-subtracted Rogowski integration

At user direction, removed endpoint, slow-centre and per-cycle corrections from
the authoritative integrated waveform. WinApp now detects one pre-trigger
offset and computes only the trapezoidal cumulative sum of `(V-offset)`
over the original sample intervals. DS1..DS3 each detected -0.1632962 V. Any
remaining slope or end error is shown as measured rather than silently forced
away. `적분 원파형` is now the default selector. Release build and package
publish passed.

## 2026-09-25 — Direct-coil RMS current envelope

Supersedes both the earlier endpoint correction and 2-ms slow-centre removal.
Native DS1..DS3 analysis found a dominant 16.783-kHz repetition, four groups
per 4.196-kHz bridge period. Integration now removes the trapezoidal mean and
integrates independently in exact 59.58-us intervals, preventing long-record
drift without creating a V-shaped centre. The RMS view uses bucket averages
instead of peak-envelope filling. Selecting `적분 원파형` automatically opens
a 2-ms window around the trigger so individual switching cycles are visible.
Release build and package publish passed.

## 2026-09-25 — Native Rigol integration in WinApp

The Rogowski tab now directly loads `RigolDS0~3.csv` native Rigol format as
well as ADC-grid CSV. It reconstructs repeated rounded scope timestamps as a
uniform 2/5-MHz grid and performs the relative-current integration at the full
native rate. The actual WinApp parser/integrator processed all four one-million
row originals; DS0 exposed both channels, DS1..DS3 produced finite traces and
returned within 0.02 V·ms of zero. Release build and packaged publish passed.

## 2026-09-25 — Rigol ADC resampling anti-alias correction

The original resampler used linear interpolation without a prefilter, causing
aliasing of the direct Rogowski coil spikes. It now applies a zero-phase
Blackman-windowed sinc FIR at 107,421.875 Hz before conversion to
268,554.6875 S/s. All four ADC-grid CSV/JSON pairs were regenerated. Against
the old point-sampled result, DS1..DS3 changed by 0.28..0.39 V RMS with peak
corrections of 3.71..6.62 V, confirming that aliasing was material. The WinApp
integrator accepted all regenerated files and returned end levels within
0.02 V·ms of zero. Reload the CSV in an already-running app to use new data.

## 2026-09-25 — Rogowski waveform rendering and integration correction

Fixed the zoomed chart's disconnected vertical bars by drawing a continuous
time-ordered path while retaining per-pixel extrema. Added a display selector:
the default `적분 상대전류` detects the active interval from 1-ms RMS blocks,
interpolates pre/post quiet baselines, trapezoidally integrates voltage, and
removes residual linear drift across that active interval so the finite pulse
returns to zero. `원본 전압` shows the captured samples.
The integrated axis is V·ms because amperes calibration is unavailable. All
three single-channel captures passed the actual WinApp parser/integrator with
53,711 finite output samples each and end levels within 0.02 V·ms of zero.
Release build and packaged publish passed.

## 2026-09-25 — Rogowski waveform zoom and pan

The imported Rogowski chart now supports cursor-centred mouse-wheel time zoom,
left-button horizontal drag/pan, double-click reset, and explicit zoom in/out/
full-view buttons. The minimum view is 32 ADC samples. Y-axis scaling follows
the visible time window and the displayed zoom multiplier reports the current
time magnification. Release build/publish passed with zero warnings/errors and
the updated controls were visually verified in the packaged WinApp.

## 2026-09-25 — WinApp imported Rogowski waveform

WinApp now has a `로고스키 실측 파형` tab. It loads the resampled CSV format,
validates monotonic time and finite channel values, offers CH1/CH2 selection,
and displays signed voltage with an automatic axis and zero reference. Large
captures are rendered as per-pixel min/max envelopes to retain narrow peaks.
All four generated Rigol files passed the actual WinApp parser (134,278 rows
for DS0 and 53,711 rows each for DS1..DS3). .NET Release build/publish passed
with zero warnings and errors; `PWM_SingleShot` contains the updated app.
No ADC-code or ampere calibration is claimed and no firmware/power output was
changed.

## 2026-09-25 — Rigol Rogowski captures resampled for ADC playback

`pid_test/RigolDS0.csv` through `RigolDS3.csv` were resampled onto the firmware
ADC grid of 268,554.6875 samples/s (3.723636 us/sample) and saved under
`pid_test/adc_resampled`. Repeated rounded Rigol timestamps are not used as
individual sample times; the uniform 2 MHz or 5 MHz source grid is reconstructed
from each complete 1,000,000-point acquisition, then linearly interpolated.
Each output has a JSON sidecar recording the rates, row counts and method.
Channel data remains in volts. ADC-code and ampere conversion is intentionally
deferred until the Rogowski analogue offset/gain and current calibration are
verified. `pid_test/resample_rigol_for_adc.py` makes the conversion reproducible.

## Stage ADC target entry

WinApp stage column now accepts ADC targets 0..65535; PID directly tracks those
values including UP/DOWN envelopes. Separate normalized target box removed.
Existing file/profile numbers are preserved and interpreted as ADC codes.
New app checks firmware target_mode before test output; matching firmware is
required. Legacy stored field names remain; onboard LCD units are not migrated.
ARM firmware and WinApp Release builds passed; all 12 host tests passed,
including direct 10000/20000 target response and 65535/65536 boundary checks.
No device flash or live output test was performed.

## 2026-09-24 — PA1_C Rogowski and automatic graph scale

ADC2 now uses PA1_C/A3/channel 1; ADC1 stays PA0_C/A2/channel 0.
ADC plot Y axes automatically fit each channel's raw and filtered peaks.
Earlier shared-input notes below are historical. Physical wiring is unverified.

## ADC graph window

Supersedes numeric-only window: two ADC/time plots overlay raw half-block means
and filtered values in ADC-code units. Completed TEST TRACE data supplies plots,
including when opening the window after a test. Trace rows append primary filter
millionths, secondary mean and secondary filter millionths (9 fields total).
Legacy 6-field rows show primary raw only. CSV headers match either format.
Existing adaptive 1..5-ms trace interval and 2048-point limit remain; these are
sampled snapshots, not every DMA block. Primary filter is meaningful during PID.

## ADC measurement window

WinApp ADC button opens a separate owned numeric window for primary/secondary
raw and filtered values, all in ADC-code units. Polling is approximately 250ms,
not waveform capture. Primary PID filter updates only during PID execution.
Status telemetry adds primary filtered millionths and primary raw idle readout.
Shared PA0_C mapping and uncalibrated units are explicitly labeled.

## 2026-09-24 — WinApp secondary input display

Live status and test status include secondary_adc and secondary_filtered_micro
(normalized filter value scaled by 1,000,000; integer serialization).
WinApp displays both below the current readout and labels the uncalibrated,
shared PA0_C bench mapping. This is a live numeric display, not trace capture.
ARM/Release builds and 12 host tests passed. No target flash performed.

## 2026-09-24 — Secondary feedback filter

ADC2 block means now feed an independent 1-ms low-pass filter continuously.
Watch g_secondaryCurrentFiltered (0..1); g_secondaryCurrentAdc remains raw.
Both ADCs still sample PA0_C in this bench build. Actual Rogowski mapping,
integration/current calibration and secondary compensation remain unimplemented.
ARM incremental build/link and 12 host tests passed, including secondary filter
initialization, step response, alternating input and reset. No board flash or
updated target processing-time measurement was performed.

## 2026-09-24 — PID feedback filtering

Added a 1-ms time-constant first-order filter to normalized ADC feedback only.
Debugger: g_wavePidFiltered (0..1). ADC logs remain raw; no output ramp added.
State is seeded per run; coefficient follows the configured sample rate.
Hardware filter response and loop stability have not been verified.
ARM incremental build/link and all 12 host tests passed, including filter step
response, alternating-input attenuation, run-state reset and zero-target output.
Firmware was not downloaded to the board.

## WinApp grid edit transaction fix

- Active text editors retain Delete/Backspace and clipboard key handling.
- Bulk clear/paste commits cell and row edits before mutation/Refresh; invalid
  edits leave data untouched and show a correction message. START also checks
  commit success before reading settings.
- Release build passed with zero warnings/errors; live editor retest pending.

## WinApp source formatting

C# blocks/statements and XAML elements/attributes were expanded for readability.
Added local .editorconfig to prevent single-line C# blocks/statements.
No behavior change intended. Release build passed with zero warnings/errors.

## 2026-09-23 PID bench update

PID parameters now drive the finite waveform test using ADC1 mean/65535.
Uncheck PWM single-shot (PID OFF), check PID use, then START. Parameters are
sent and read back before output. Both unchecked retains timing-only START.
Feedback is uncalibrated and differs from the older thresholded OR occupancy.
Duty cap, reset, zero-envelope and all existing stop paths apply.
The extra output slew limit was removed at user request: a step can reach the
duty cap on its first control update. UP/DOWN shape the target only.
Target execution and loop tuning remain unverified.

Validation: ARM incremental build/link and WinApp Release build passed.
All 12 host tests passed, including added PID output cap, high-feedback
reduction, STOP, restart state reset and active-run configuration rejection.
No firmware download or live power-output operation was performed.

Last updated: 2026-09-15

## Current implementation: WinApp PWM single-shot test

- WinApp now has an explicit PWM single-shot checkbox (PID OFF), per-phase
  duty cap (default 5%, range 1..45%), STOP, a duty-result tab and CSV export.
  It transfers current UI settings before starting. Current setpoints define
  relative heights; no real-current calibration is claimed.
- Firmware boots LOW and runs a finite SQ/UP/HOLD/DOWN/COOL profile only on
  TEST START. Completion/STOP, detected disconnect, expired 1-second status
  lease, ADC processing loss and acquisition faults force pins LOW. Timers/
  acquisition continue internally when normally idle. Existing START remains
  dry-run. Legacy continuous OR PI is disabled by PWM_WAVE_TEST_ENABLE=1.
- Results retain up to 2048 time/target/duty/raw-ADC records. CSV is from the
  device trace, not sparse WinApp polling. Logged duty is the commanded value,
  not oscilloscope feedback. Long-profile traces are decimated (up to 5 ms).
- Verification: ARM incremental build/link; WinApp .NET Release build; 12
  host tests; offscreen WPF simulation through START, completion, graph and
  CSV-button activation with rendered layout inspection. Production GPIO/
  gate injection and real-time workload still require board validation.
- Local TCP mock integration also passed: current UI settings transfer,
  explicit test command, retrieval of a cycle completed before the next poll,
  and control unlock after complete trace reception. See WAVEFORM_TEST.md.
- WinApp now exposes PID 사용, Kp, Ki, Kd, 목표 and PID 적용 controls. Firmware
  supports GET PID/SET PID with bounded values and rejects changes during a test.
  PID remains disabled by default and is not connected to calibrated current
  control yet; this prevents raw ADC codes from being treated as amperes.
- Deliverable app: WinApp/WeldApp/PWM_SingleShot/WeldApp.exe. Firmware:
  Firmware/weld3/Debug/weld3.elf. Agent has not flashed or operated the board.
- Important: debugger PAUSE can hold an output HIGH. No energized pause or
  single-stepping; this software is not a replacement for hardware protection.


## Latest bench test implementation — supersedes old PWM/ADC entries below

- Latest requested test: all-LOW feedback now runs PI toward the 90% OR
  command limit (45% per phase), selected by PWM_OR_BENCH_ALLOW_ZERO=1.
  InputInvalid=1 still indicates all LOW; it no longer implies frozen PID.
  Input stays PA0_C/A2. ARM build and controller host tests passed; target
  test pending. All-HIGH hold and acquisition-fault stops remain active.

- Current input: PA0_C / Arduino A2, changed from A3 at user request. ADC1
  channel 0 drives PID; ADC2 channel 0 is duplicate diagnostic acquisition.
  ARM incremental compile/link passed. No board flash performed; A2 retest
  pending. Prior A3 snapshot showed valid feedback, 7070 PID updates and
  measured occupancy .47168, with no acquisition errors.

- Latest correction: user confirmed OR signal is connected to PA1_C / Arduino
  A3. Runtime ADC1 feedback now selects channel 1 instead of PA0/channel 16.
  ARM incremental build and eight host tests pass. After flashing/resetting,
  check inputInvalid=0, increasing pwmOrUpdates and measured occupancy near
  0.5. Target verification is pending; all older PA0 feedback entries below
  describe the superseded configuration.

- PAUSE/RUN investigation: added joint TIM3/TIM4 debug freeze before startup,
  preventing continuous ADC trigger/PWM progression while the CPU is halted.
  Normal waveform/sample timing and fault checks are unchanged. User snapshot
  shows inputInvalid=1, measured=0, updates=0: fixed test PWM only, not PID.
  ARM incremental build and host tests pass; target pause/resume retest pending.


- Latest diagnostic build: snapshot with 32 blocks and no ADC/DMA fault suggests
  first-window constant-input shutdown, not a failed ADC start. For TEST ONLY,
  PWM_OR_BENCH_HOLD_ON_INVALID=1 keeps fixed 12.5% A/B pulses while feedback
  is invalid and resets/freezes PID. Mixed input resumes PID. Strict shutdown
  is restored with macro=0; all acquisition fault stops remain enabled.
  Watch g_pwmOrInputInvalid (1=all LOW, 2=all HIGH, 0=mixed/unassessed) and
  g_pwmOrInvalidWindows. ARM link and eight host tests passed, testing both
  fallback and strict modes. Actual PA0 levels/wiring and board output remain
  unverified; this fallback does not repair a missing feedback signal.

- Follow-up: user reports PWM absent. Not yet resolved on target. Added
  g_pwmStopDetail (first-stop register/input snapshot), g_pwmStartupStage
  (0=before acquisition init, 1=initializing, 2=armed, 3=PWM started), and
  g_pwmErrorCaller (resolve against this build's ELF). ARM incremental build
  and the existing eight host tests pass with diagnostics. Await target fault
  values; stop conditions remain active and no board was flashed by the agent.

- PA0 / Arduino D3 / ADC1_INP16 now measures diode-OR PWM occupancy. A/B retain
  PB7/PD15 pins and HIGH polarity but use equal center-aligned pulse widths,
  with centers T/2 apart. Initial 12.5% per phase, target 25% per phase (50% OR).
- Restored synchronized ADC dual circular DMA configuration after generated
  initialization had reverted. Sample rate remains nominal 268.555 kS/s,
  64 pairs per PWM period. ADC clock DIV2 and sample time 32.5 cycles.
- Bench PID runs every 16 periods with output limits, conditional anti-windup,
  slew limiting, stuck-level detection and LOW pin stop on detected faults.
  Default Kd=0; this is a PI starting configuration, not tuned welding control.
- Observe g_pwmOrMeasured, g_pwmOrCommand (0..1 OR occupancy), g_pwmOrUpdates,
  g_pwmOrFault (0=none, 1=acquisition, 2=constant input), g_currentSenseFault
  and g_currentFaultDetail. Goal/threshold/gains are PWM_OR_* defines in main.c.
- ARM incremental compile/link passed. Six acquisition contract tests and two
  new bench tests passed, including executing the actual extracted C PID on
  the host for quantized-loop convergence, bounds, slew, saturation and stuck
  inputs. These do not validate HAL behavior, trigger phase or electrical I/O.
- Board not flashed. Scope validation, ADC threshold verification, actual
  interrupt deadlines and waveform shape remain pending. Read HARDWARE.md
  before wiring; supplied 100-ohm pull-down is a substantial GPIO load.


## Implemented

- TIM4 PWM B (PD15/CH4) is configured in PWM2 mode to be inverse phase to PWM A
  (PB7/CH2) at the shared 50% test duty. No hardware dead time is provided;
  bench polarity/dead-time verification remains required.

- Screen1 용접 파형 그래프의 X축 그리드는 TouchGFX Designer 설정으로 관리한다.
  `.touchgfx`에서 81개 점(80개 구간), 주요선/라벨 10 샘플, 보조선 5 샘플로
  설정해 주요선 8칸과 중간 보조선을 표시한다. 별도의 수동 경계선과
  런타임 그리드 덮어쓰기는 사용하지 않는다. `GraphWrapAndClear`의 누적 데이터 개수를
  별도로 추적하고 X축 오프셋으로 상쇄하여 파형을 다시 그릴 때마다 시간축이
  0 ms부터 시작한다. 샘플당 시간의 소수 정밀도를 보존하기 위한 그래프 Scale
  설정만 런타임에 둔다.

- WinApp `LCD 캡처` and firmware `CAPTURE SCREEN`: transfers a hashed RGB888
  snapshot of the live LCD framebuffer and saves it as PNG on the PC.

- The controller's `SET PROFILE file=N number=N` selection is now shared with
  the TouchGFX model. Screen1 overlays two-digit file and number values on the
  Designer-created `boxWithBorderFile` and `boxWithBorderPage` boxes and updates
  them on the next GUI tick after WinApp SET succeeds. This changes UI state
  only; it does not load a profile or affect power output.
  WinApp SET then loads the selected OSPI device slot and reads the applied
  settings back; Screen1 fills all three grid rows from those controller values.
  TouchGFX also derives the active stage count from the loaded second/third
  stage current/time pairs and rebuilds the graph when controller data changes.


- WinApp and firmware OSPI profile library: File 00..15 x Number 00..15 (256
  slots), with explicit controller save/load controls. Firmware reserves the
  final 64 KiB of MX25LM51245G, uses CRC/versioned records, sector-preserving
  individual updates, full-library streaming and read-back verification. The .NET Release build and ARM firmware
  link passed. Actual NOR ID/read/write/retention remain to be tested on-board.

- WinApp `1회 START` and TCP `START` timing-only dry run: one configured cycle
  drives host-visible welding/time state for graph-capture workflow. It does
  not enable PWM/gates or claim measured current; actual current remains 0 A.

- Three-stage WinApp/TCP setting model based on the referenced WELD5000A UI:
  independent current and time for 1st, 2nd and 3rd stages, with zero/zero
  disabling a stage and mismatched pairs rejected. Host WinApp and ARM firmware
  builds pass. These settings are not connected to power-output sequencing.
- WinApp now has separate Monitoring and Weld Settings screens. The settings
  and result views are now combined into one screen. It includes three-stage
  ramp-up/hold/ramp-down settings and overlays one completed weld's measured
  current on the configured waveform. Stage 1 and 2 also include the reference
  firmware's post-ramp zero-current hold (`cl`/`cl2`, shown as 하강유지). Voltage and
  wire-speed controls, telemetry and graph series were removed from the host
  protocol/UI because this welder's requested control variable is current.

- TCP `defaultTask` stack increased from 2 KiB to 4 KiB and its 512-byte
  single-client command buffer moved to static storage after a WinApp
  connection reproduced the FreeRTOS stack-overflow hook. CubeMX `.ioc` is
  synchronized. Target retest is pending.

- Windows WPF monitor at `WinApp/WeldApp`: connects to the existing Weld3 TCP
  server at `192.168.0.100:5000`, reads settings/status, validates and writes
  accepted settings, and plots measured current trends. An
  offline simulation mode supports UI testing without hardware. The app does
  not provide a weld-start, gate-enable or power-output command.
- Host build of the WinApp with .NET 10 completed with zero warnings/errors on
  2026-08-29. Live Ethernet communication with the target is not yet verified.

- PA5 DAC1 channel 2 sine test source: nominal 1 kHz / 2 Vpp / 1.65 V offset
  at VREF+=3.3 V, enabled by DAC_SINE_TEST_ENABLE. TIM6 + DMA1 Stream1;
  no sample/period interrupts during normal output, error flags in
  g_dacSineError and g_dacSineDmaError. No change to ADC/PWM timing.
- DAC patch ARM incremental compile/link passed (text=399890, data=1428,
  bss=99267 bytes). Static waveform/resource tests added; target output still
  requires measurement. Agent did not flash or operate the board.

- ADC/DMA IRQ priority raised from 5 to 4 (above FreeRTOS masking and priority-5
  graphics handlers); callbacks must not use any RTOS APIs. Fault checks remain.

- Provisional simultaneous acquisition of primary Hall current on A3/ADC1 and
  secondary Rogowski-integrator output on A2/ADC2.
- TIM3 internal OC4REF acquisition target: 64 sample pairs per PWM period,
  nominal 268.555 kS/s per sensor. TIM4 ENABLE/ITR3 starts the slave timer.
- DMA1 Stream0 circular buffer with 32-pair half-buffer processing and raw
  mean/minimum/maximum diagnostics stored separately for each PWM half.
- First two callbacks discarded for startup; ADC offset/linearity calibration
  and longer sampling time added. Sensor zero/gain calibration is still absent.
- DMA ownership/order checks, ADC/DMA error latch, processing cycle current/max
  and deadline counters added. No fault here shuts down the power stage.
- D-cache maintenance around the aligned DMA buffer.
- ADC1 continuous DMA request mode corrected after PG3 showed no completed
  buffer activity with conversion data management left in data-register mode.
- Closed-loop current compensation is compile-time disabled and no new duty
  update path has been enabled.

- TIM4 PWM A on PB7 / Arduino D9 / CH2.
- TIM4 PWM B on PD15 / Arduino D6 / CH4.
- A/B test output at approximately 4.196 kHz and 50% duty.
- Legacy TIM4_CH3 center trigger configuration remains in TIM4 but is no
  longer selected by ADC1.
- PG3 / Arduino D2 is HIGH during actual DMA block processing only. TIM4
  update/CC3 marker interrupts are no longer enabled.

## Host verification (2026-08-29)

- LCD capture diagnostics now log request, active LTDC framebuffer address,
  snapshot/hash completion, TCP header/data errors with offsets, and successful
  byte count over the USART3 ST-LINK VCP at 921600 8-N-1. The WinApp resets its
  TCP connection after any capture failure so a partially received binary frame
  cannot corrupt later line-based commands. ARM firmware and an alternate-output
  WinApp Release build passed on 2026-09-01; target capture remains to be tested.

- Six timing/configuration contract tests pass (including the IRQ boundary):
  `python -B tests/test_current_sense_contract.py`.
- Tests check 64:1 division, 32 pairs per half, cache alignment, ownership
  predicates, trigger-only startup and disabled control. They do NOT execute
  firmware or validate hardware routing/interrupt timing.
- ARM GCC 14.3.rel1 was located under C:/root/ST/STM32CubeIDE_2.2.0.
  Incremental `make -j4 all` rebuilt affected sources and linked weld3.elf
  successfully after both diagnostics and the priority-4 change. This is not a clean rebuild or
  a target execution test; no firmware was flashed by the agent.
- Official RM0468 download failed; TIM3 ITR3 route remains to be confirmed
  specifically for H735. Existing H7 reference material is not board proof.

## Bench observations

- User debugger screenshot: g_currentSenseFault=1, g_currentDmaBlocks=0,
  g_currentAdcError=0, g_currentDmaError=0, hsd1.ErrorCode=0. Thus processing
  rejected a block before any valid publication; not evidence that ADC/DMA
  never started. Exact failed predicate was not recorded in that build.
- New g_currentFaultDetail preserves the first reason, callback offset,
  expected offset, entry/exit NDTR, elapsed/budget cycles, warmup count, DMA
  flags and PWM count. Reasons: 1 order, 2 entry ownership, 3 exit ownership,
  4 processing deadline, 5 publication deadline. Existing stop behavior is
  retained.
- Follow-up snapshot: reason=2, offset=32, expectedOffset=32, entry/exit NDTR=25,
  elapsed=134 cycles, budget=65536, warmup=1, published=0, DMA flags=0 and
  PWM CNT=40231. This identifies stale second-half ownership at callback entry,
  not excessive processing time. The exact interrupt-delay source is not
  captured. Priority-4 mitigation now requires a no-breakpoint bench retest.

- PB7 and PD15 PWM waveforms were observed at approximately 4.196 kHz.
- An earlier PD15 waveform correctly represented the old period-center trigger,
  but that design was superseded when PD15 became PWM B.
- SD initialization once stopped boot with `HAL_SD_ERROR_CMD_RSP_TIMEOUT`;
  pressing reset allowed operation.

## Not yet verified

- Target execution of the synchronized dual-ADC DMA path and diagnosis of
  the observed fault=1.
- Actual TIM3 start/trigger phase, simultaneous ADC packing order and absence of DMA
  overruns under the full RTOS/TouchGFX workload.
- Electrical confirmation that A3 is the primary Hall input and A2 is the
  conditioned Rogowski input.
- Hall/Rogowski zero offsets, gains, polarities, bandwidth and safe ADC limits.

- Clean full rebuild of all translation units (incremental compile/link passed).
- Actual ADC trigger-to-conversion-complete latency on the oscilloscope.
- Absence of missed ADC conversions under full RTOS/TouchGFX workload.
- Correct current-sense analog input and scaling.
- Safe gate-driver polarity and required A/B dead time.
- Sampling at both A-phase and B-phase ON centers.
- Robust cold-start SD initialization and recovery behavior.

## Recommended next steps

1. With the power stage disconnected, capture PB7, PD15, and PG3 together.
   Expect PG3 pulses every about 119.16 us (8.392 kHz). The last sample in each
   half is nominally 1.862 us before its boundary; PG3 follows ADC conversion
   and IRQ latency, so the pulse need not start exactly at the PWM edge.
   Verify all 32 samples stay within the intended half and measure worst-case
   processing/IRQ latency with TouchGFX active. A PWM-looking PG3 is not proof.
2. Confirm the gate-driver schematic, active polarity, and dead-time demand.
3. Verify g_currentSenseFault=0, separate half statistics, data order and
   processing cycles. For 550 MHz, cycles / 550 gives microseconds; 65536
   cycles is the nominal half-period budget. Do not use breakpoints on a live
   power stage; halted CPU/DMA behavior is not a protection mechanism.
4. Add SD startup delay/retry or make SD failure nonfatal according to product
   requirements.
5. Build and archive a clean compiler result before power-stage testing.

## 2026-09-23 — WinApp Command NullReferenceException fix

- Diagnosed a race in MainWindow.Command: Disconnect can clear writer while
  Command awaits commandLock, after its original null check.
- Added post-lock connection identity validation and stable stream references.
- Validation: .NET 10 build passed with 0 warnings and 0 errors (output:
  WinApp/WeldApp/WeldApp/bin_test/NullConnectionFix).
- A temporary console harness using the actual Command/read method source
  passed queued-disconnect, queued-connection-replacement, normal PING/PONG,
  and semaphore-release checks using loopback TCP only.
- No device or power-stage operation performed; real-device UI retest pending.
