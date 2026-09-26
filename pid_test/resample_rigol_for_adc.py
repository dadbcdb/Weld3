#!/usr/bin/env python3
"""Resample Rigol CSV captures onto the WeldRev3 ADC sampling grid.

The oscilloscope timestamps are printed with too few digits and therefore
repeat.  The source interval is reconstructed from the first/last timestamps
and row count, then each channel is linearly interpolated at the firmware ADC
rate.  Voltages are deliberately not converted to ADC codes or amperes because
the Rogowski analogue offset/gain and ampere calibration are not yet verified.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path

import numpy as np


ADC_SAMPLE_RATE_HZ = 268_554.6875


def read_capture(path: Path) -> tuple[list[str], list[float], list[list[float]]]:
    with path.open("r", newline="", encoding="utf-8-sig") as source:
        rows = csv.reader(source)
        header = next(rows)
        if len(header) < 2 or header[0].strip() != "Time(s)":
            raise ValueError(f"{path}: expected Time(s) and at least one channel")

        times: list[float] = []
        channels: list[list[float]] = [[] for _ in header[1:]]
        for line_number, row in enumerate(rows, start=2):
            if not row:
                continue
            if len(row) != len(header):
                raise ValueError(f"{path}:{line_number}: expected {len(header)} fields")
            times.append(float(row[0]))
            for channel, value in zip(channels, row[1:]):
                channel.append(float(value))

    if len(times) < 2:
        raise ValueError(f"{path}: capture needs at least two samples")
    if times[-1] <= times[0]:
        raise ValueError(f"{path}: invalid capture time range")
    return header, times, channels


def resample(path: Path, output_dir: Path, adc_rate_hz: float) -> tuple[Path, Path]:
    header, displayed_times, channels = read_capture(path)
    source_count = len(displayed_times)
    start_s = displayed_times[0]
    displayed_end_s = displayed_times[-1]

    # Rigol rounds displayed timestamps, so reconstruct the uniform source grid
    # from the complete capture rather than using repeated displayed values.
    # The Rigol record covers an exact acquisition window.  Its last printed
    # timestamp rounds the final sub-microsecond position up to the window end,
    # so divide by the record count (not count - 1).  This recovers the nominal
    # 2 MHz / 5 MHz rates of these captures instead of 1,999,998 / 4,999,995 Hz.
    source_dt_s = (displayed_end_s - start_s) / source_count
    reconstructed_last_s = start_s + (source_count - 1) * source_dt_s
    source_rate_hz = 1.0 / source_dt_s
    adc_dt_s = 1.0 / adc_rate_hz
    output_count = math.floor((reconstructed_last_s - start_s) / adc_dt_s) + 1

    # Point-picking a 2/5 MHz Rogowski capture at 268.55 kS/s aliases coil
    # spikes and scope noise into the ADC band. Apply a zero-phase windowed-sinc
    # anti-alias FIR before interpolation. The conservative 0.4 * output-rate
    # cutoff leaves the 4.196-kHz welding waveform and useful harmonics intact.
    fir_taps = max(129, math.ceil(source_rate_hz / adc_rate_hz * 80))
    if fir_taps % 2 == 0:
        fir_taps += 1
    half = fir_taps // 2
    fir_cutoff_hz = adc_rate_hz * 0.4
    offsets = np.arange(fir_taps, dtype=np.float64) - half
    fir = (2.0 * fir_cutoff_hz / source_rate_hz) * np.sinc(
        2.0 * fir_cutoff_hz / source_rate_hz * offsets
    )
    fir *= np.blackman(fir_taps)
    fir /= fir.sum()
    filtered_channels: list[np.ndarray] = []
    for channel in channels:
        padded = np.pad(np.asarray(channel, dtype=np.float64), half, mode="reflect")
        filtered_channels.append(np.convolve(padded, fir, mode="valid"))

    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / f"{path.stem}_adc_{adc_rate_hz:.4f}Hz.csv"
    metadata_path = output_path.with_suffix(".json")

    with output_path.open("w", newline="", encoding="utf-8") as destination:
        writer = csv.writer(destination, lineterminator="\n")
        writer.writerow(["sample_index", "time_s", "time_ms", *header[1:]])
        for sample_index in range(output_count):
            target_s = start_s + sample_index * adc_dt_s
            source_position = (target_s - start_s) / source_dt_s
            left = min(int(source_position), source_count - 2)
            fraction = source_position - left
            values = [channel[left] + (channel[left + 1] - channel[left]) * fraction
                      for channel in filtered_channels]
            writer.writerow(
                [
                    sample_index,
                    f"{target_s:.12f}",
                    f"{target_s * 1000.0:.9f}",
                    *(f"{value:.9e}" for value in values),
                ]
            )

    metadata = {
        "source_file": path.name,
        "source_rows": source_count,
        "source_start_s": start_s,
        "source_displayed_end_s": displayed_end_s,
        "source_reconstructed_last_sample_s": reconstructed_last_s,
        "reconstructed_source_sample_rate_hz": source_rate_hz,
        "adc_sample_rate_hz": adc_rate_hz,
        "adc_sample_interval_s": adc_dt_s,
        "output_rows": output_count,
        "method": "zero-phase Blackman-windowed sinc anti-alias FIR, then linear interpolation on reconstructed uniform Rigol time grid",
        "anti_alias_fir_taps": fir_taps,
        "anti_alias_cutoff_hz": fir_cutoff_hz,
        "units": {name: "V" for name in header[1:]},
        "conversion_status": "Voltage only; ADC-code and ampere calibration not applied",
    }
    metadata_path.write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return output_path, metadata_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--output-dir", type=Path, default=Path("adc_resampled"))
    parser.add_argument("--adc-rate", type=float, default=ADC_SAMPLE_RATE_HZ)
    args = parser.parse_args()
    if not math.isfinite(args.adc_rate) or args.adc_rate <= 0:
        parser.error("--adc-rate must be a positive finite number")

    for input_path in args.inputs:
        output_path, metadata_path = resample(input_path, args.output_dir, args.adc_rate)
        print(f"saved {output_path} and {metadata_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
