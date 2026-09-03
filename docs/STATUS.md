# Project Status

Last updated: 2026-09-01

## Implemented

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
