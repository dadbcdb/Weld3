# Hardware Reference

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
