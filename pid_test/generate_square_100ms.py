#!/usr/bin/env python3
"""Generate an ideal 100-ms current pulse and its direct-Rogowski voltage."""

import csv
from pathlib import Path


SAMPLE_RATE_HZ = 268_554.6875
DT_S = 1.0 / SAMPLE_RATE_HZ
START_S = -0.020
END_S = 0.120
PULSE_START_S = 0.0
PULSE_END_S = 0.100
EDGE_S = 0.0002
OFFSET_V = -0.155


def current(t: float) -> float:
    if t < PULSE_START_S or t >= PULSE_END_S:
        return 0.0
    if t < PULSE_START_S + EDGE_S:
        return (t - PULSE_START_S) / EDGE_S
    if t >= PULSE_END_S - EDGE_S:
        return (PULSE_END_S - t) / EDGE_S
    return 1.0


def main() -> None:
    output = Path(__file__).resolve().parent / "synthetic"
    output.mkdir(exist_ok=True)
    count = int((END_S - START_S) / DT_S) + 1
    times = [START_S + i * DT_S for i in range(count)]
    currents = [current(t) for t in times]

    current_path = output / "SquareCurrent_100ms.csv"
    with current_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(["sample_index", "time_s", "time_ms", "current_relative"])
        for i, (t, value) in enumerate(zip(times, currents)):
            writer.writerow([i, f"{t:.12f}", f"{t * 1000:.9f}", f"{value:.9f}"])

    # Vcoil = offset + dI/dt. The scale is chosen so integrating volts over
    # milliseconds produces the same numeric relative-current amplitude.
    coil = [OFFSET_V]
    for i in range(1, count):
        derivative_per_ms = (currents[i] - currents[i - 1]) / (DT_S * 1000.0)
        coil.append(OFFSET_V + derivative_per_ms)

    coil_path = output / "SquareCurrent_100ms_Rogowski.csv"
    with coil_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(["sample_index", "time_s", "time_ms", "CH1(V)"])
        for i, (t, value) in enumerate(zip(times, coil)):
            writer.writerow([i, f"{t:.12f}", f"{t * 1000:.9f}", f"{value:.9f}"])

    # A separate bipolar square-voltage fixture demonstrates the textbook
    # square-to-triangle integral: +1 V for 50 ms, then -1 V for 50 ms.
    bipolar_path = output / "BipolarSquareVoltage_100ms.csv"
    with bipolar_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(["sample_index", "time_s", "time_ms", "CH1(V)"])
        for i, t in enumerate(times):
            signal = 1.0 if 0 <= t < 0.050 else -1.0 if 0.050 <= t < 0.100 else 0.0
            writer.writerow([i, f"{t:.12f}", f"{t * 1000:.9f}", f"{OFFSET_V + signal:.9f}"])

    print(current_path)
    print(coil_path)
    print(bipolar_path)


if __name__ == "__main__":
    main()
