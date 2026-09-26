# Hardware Reference

## 2026-09-26 selectable 1/2-kHz single-shot PWM timing

- The current single-shot build no longer offers 4 kHz. WinApp selects a
  nominal 1 kHz or 2 kHz before `TEST START`; boot/default is 2 kHz.
- TIM4 remains center-aligned with ARR=34368. TIM4 PSC=1 gives approximately
  2000.407 Hz and PSC=3 gives approximately 1000.204 Hz at the verified
  275-MHz APB1 timer clock. A remains centered at CNT=0 and B at CNT=ARR.
- TIM3 always receives the same prescaler as TIM4 and retains 1074 timer ticks
  per ADC slot. Both modes therefore retain exactly 64 simultaneous ADC pairs
  per PWM period: approximately 128.026 kS/s at 2 kHz and 64.013 kS/s at
  1 kHz. The two PWM pulse centers remain exactly half a period apart.
- Frequency changes are accepted only while the single-shot is inactive. Both
  output pins are forced GPIO LOW, TIM3/TIM4 and ADC DMA are stopped, partial
  acquisition is discarded, and acquisition is restarted before output arming.
- The TIM4 override remains in the `USER CODE BEGIN TIM4_Init 2` region, but
  `weld3.ioc` still contains generated legacy timer values. CubeMX regeneration
  must preserve/review this override and must not be treated as validation.

## 2026-09-24 active ADC mapping (supersedes shared-input bench mapping)

- ADC1_INP0: PA0_C / A2, primary feedback.
- ADC2_INP1: PA1_C / A3, user-requested conditioned Rogowski input.
- Both analogue switches remain OPEN; PA1 Ethernet REF_CLK is isolated from
  PA1_C. ADC2 channel-1 override is inside CurrentSense_Init USER CODE.
- Actual wiring, integrator and ampere calibration remain unverified.

## Current single-shot test configuration (2026-09-15)

Supersedes automatic OR-feedback PWM startup described in the historical
bench section below. Hardware is an existing SG3525-controlled production
product per the user. The supplied partial schematic shows downstream HC14/
RC/diode signal shaping and comparator/latch/control logic. Protection exists
per the user, but exact MCU injection point, logic levels and end-to-end gate
shutdown path have not been electrically verified by the agent.

- PB7/D9 is A and PD15/D6 is B, active HIGH; timer frequency and centered
  A/B pulse relationship are retained. Boot output is LOW. TIM4/TIM3 and ADC
  acquisition keep running internally while idle, with zero-OFF compares.
- `PWM_WAVE_TEST_ENABLE=1` selects explicit single-shot output; the earlier
  OR PI and zero-input/fallback behavior are not executed in this mode.
- PA0_C/A2 remains ADC1/ADC2 channel 0. Both report the same physical input.
  Logged mean/min/max are uncalibrated ADC codes, not measured amperes.
- WinApp test limit is per-phase duty, 1..45%, default 5%. This is a software
  range, not a verified safe rating for the power stage. Largest configured
  stage current maps to this duty; other stage heights scale proportionally.
- SQ and COOL request zero output. UP/HOLD/DOWN follow the stored millisecond
  durations. Each timer pulse center stays T/2 apart. Normal compare changes
  take effect in successive half periods; allow up to one PWM period of
  compare-latch latency in addition to the 1-ms envelope resolution.
- Completion/STOP/fault overrides PB7/PD15 to GPIO LOW, which may truncate
  the final pulse. Confirm transient transformer behavior and downstream gate
  polarity before energized operation. No independent gate-enable pin or
  calibrated current protection has been added by this firmware.
- Debug PAUSE still freezes PWM at its instantaneous level, possibly HIGH.
  Never use PAUSE/single-stepping while this output drives an energized stage.


## Active bench OR/PID configuration (2026-09-14)

This section supersedes the older PWM/ADC assignments below for this test build.
Power-stage operation is not authorized or validated.

Debugger PAUSE freezes TIM3 and TIM4 together. PWM holds its instantaneous
level (possibly HIGH) for the halt duration; RUN is expected to continue the
paused waveform. This creates a stretched pulse/gap on the scope, not a safe
gate shutdown. One already-triggered ADC conversion may finish during halt.

The current TEST-ONLY build uses PWM_OR_BENCH_ALLOW_ZERO=1: all-LOW input
continues PI and increases commanded OR occupancy to its 90% limit (45% per
phase). The LOW diagnostic remains visible. All-HIGH input still holds fixed
12.5% per phase via PWM_OR_BENCH_HOLD_ON_INVALID=1. Set both macros to 0 for
strict constant-input shutdown. Acquisition faults still stop both pins.
For a deterministic zero-input test, disconnect the PWM source from A2 and
pull A2 to signal GND; never ground a driven PWM output. An unplugged input
may float. This test configuration must not drive an energized power stage.

- PWM A: PB7 / Arduino D9 / TIM4_CH2, active HIGH PWM1.
- PWM B: PD15 / Arduino D6 / TIM4_CH4, active HIGH PWM2 with a different CCR.
- TIM4 center-aligned, PSC=0, ARR=32768: full period 65536 ticks,
  nominal 4.196167 kHz at 275 MHz. A is centered at CNT=0; B at CNT=ARR.
  CCR2=w, CCR4=32768-w. Each pulse is approximately 2w ticks wide and
  centers are 119.156 us apart. Initial w=4096 (12.5% each, 25% OR);
  target OR occupancy 50% means approximately 25% per phase.
- User requested moving OR feedback to PA0_C / Arduino A2. ADC1_INP0 is
  now the feedback input (DMA low halfword). ADC2_INP0 samples the same pin
  for duplicate diagnostics, not an independent sensor. PA0 analogue switch
  remains OPEN, isolating PA0 / Arduino D3; its TIM5 override leaves it ANALOG.
  PA1_C / A3 is no longer sampled. PA1 stays Ethernet REF_CLK.
  Input mapping is supported by
  [ST DS13312](https://www.st.com/resource/en/datasheet/stm32h735ag.pdf);
  physical wiring has not been checked on the board.
- ADC12 prescaler DIV2, nominal 40 MHz from current PLL2P=80 MHz;
  32.5-cycle sample time, 16-bit conversions, 64 simultaneous pairs/period.
  PG3 remains a DMA processing marker twice per full PWM period.
- Scope startup includes a shortened first A pulse because counting begins at
  A's center. Two DMA halves are discarded. After settling, observe equal A/B
  widths separated by half a period and two OR pulses per period. At the 50%
  OR target, each width and each intervening LOW gap is about 59.58 us.
- OR occupancy is bounded to 10..90%, leaving nominal gaps of at least 11.92 us.
  This is waveform spacing, not a verified IGBT gate dead-time specification.
- The supplied diagram labels R1 as 100 ohms. At a nominal 2.6 V HIGH this
  would draw about 26 mA from the driving GPIO. Use a suitably light load
  (e.g. 10 kilohms for this logic test), and verify HIGH/LOW levels and edges
  with the scope. The 1N4001 waveform at this timing has not been verified.
  Keep PA0_C within the board's ADC voltage range and share signal GND.


## Controller

- MCU: STM32H735IGK6
- Board configuration indicates STM32H735G-DK-style resources.
- Firmware project: `Firmware/weld3`
- Main configuration: `Firmware/weld3/weld3.ioc`

## TIM4 signal mapping

| Function | MCU pin | Connector label | Timer channel | Current role |
|---|---|---|---|---|
| PWM A phase | PB7 | Arduino D9 | TIM4_CH2 | PWM1 output |
| Legacy ADC timing reference | PD14 | STMOD#14-PWM | TIM4_CH3 | No longer used as the ADC trigger |
| PWM B phase | PD15 | Arduino D6 | TIM4_CH4 | PWM2 output |
| ADC processing marker | PG3 | Arduino D2 | GPIO | HIGH during DMA half-buffer processing |

TIM4 is currently configured as an up-counter with:

- Prescaler: 0
- Auto-reload value: 65535
- Observed PWM frequency: approximately 4.196 kHz
- Test duty: 50%

TIM4 does not provide the advanced-timer complementary-output dead-time
generator used by TIM1/TIM8. The present PWM1/PWM2 relationship therefore has
no hardware-inserted dead time.

## ADC mapping

- ADC1 regular channel: ADC1_IN1 on PA1_C
- Resolution: 16 bits
- Runtime trigger: TIM3 OC4REF through TRGO, nominal 268.5546875 kS/s per ADC
  with the current 275 MHz APB1 timer clock.
- ADC1/ADC2 regular simultaneous mode uses DMA1 Stream0 in circular mode.
- PG3 is HIGH only while the DMA half/full callback processes 32 sample pairs.
  Expected pulse repetition is twice PWM frequency (about 8.392 kHz); pulse
  width measures processing, not hardware conversion time or total IRQ latency.

### Provisional dual-current assignment (not electrically verified)

| Measurement | MCU input | Connector | Acquisition role |
|---|---|---|---|
| Primary Hall current | ADC1_IN1 / PA1_C | Arduino A3 | Dual-mode master, low 16 bits |
| Secondary Rogowski integrator | ADC2_IN0 / PA0_C | Arduino A2 | Dual-mode slave, high 16 bits |

Firmware now targets 64 paired samples per PWM period from TIM3 with packed
ADC1/ADC2 DMA samples. TIM4 TRGO=ENABLE starts TIM3 via ITR3 trigger mode;
the device-specific routing and start latency still require verification.
TIM3 has no enabled output pin. Do not connect a raw Rogowski coil directly: A2 requires a
protected, biased and bandwidth-limited integrator output within the ADC input
range. The assignments and analogue scaling remain provisional.

## DAC test output

- PA5 / DAC1_OUT2: buffered 12-bit sine test output. PA4 stays LCD VSYNC.
- Nominal 1 kHz, about 0.65 to 2.65 V at VREF+=3.3 V; actual output depends
  on reference, load and DAC accuracy. TIM6/DMA1 Stream1, 64 samples/cycle.
- Inspect with a high-impedance scope input, not 50-ohm termination.
- Board connector location for PA5 is not verified. For loopback, disconnect
  power stage and sensor output, then connect PA5 to A3 or A2 with common GND.
  Do not join two driven analogue outputs.

## SD card

- SDMMC1 card-detect input: PF5 (`uSD_Detect`), treated as active-low.
- A captured initialization failure reported `hsd1.ErrorCode == 4`, meaning
  command-response timeout.
- Reset-button startup succeeded after the first failure, suggesting SD card
  power-up/readiness timing as a likely contributor. This is not yet proven.

## OSPI NOR profile storage

- OCTOSPI1 is routed to the STM32H735G-DK MX25LM51245G, a 512-Mbit (64-MiB)
  NOR flash. OCTOSPI2 is the separate HyperRAM interface.
- Weld profiles reserve byte offsets `0x03FF0000..0x03FFFFFF`, the final 64 KiB.
  Do not place TouchGFX/external-loader assets in this range. The 256 records
  occupy the first 32 KiB of that partition. Current TouchGFX
  application configuration selects internal flash.
- OCTOSPI1 device size is 26 address bits and its clock prescaler is 2, matching
  the board BSP configuration. Electrical operation and retention still need a
  target save/power-cycle/load test.

## LCD diagnostic capture

- TouchGFX uses RGB888 double framebuffers at `0x70000000` and `0x70060000`.
- `0x700C0000..0x7011F9FF` in OCTOSPI2 HyperRAM is reserved as a temporary
  480 x 272 x 3-byte LCD capture snapshot. It is not persistent storage and
  must not be assigned to another framebuffer or asset without revisiting the
  `CAPTURE SCREEN` implementation.
