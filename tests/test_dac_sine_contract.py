"""Static waveform/configuration checks; not analogue output verification."""
import math
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "Firmware/weld3/Core/Src/main.c").read_text(encoding="utf-8")


class DacSineContract(unittest.TestCase):
    def test_table(self):
        body = re.search(r"dacSineTable\[DAC_SINE_SAMPLES\] = \{(.*?)\};", SOURCE, re.S)[1]
        values = [int(v) for v in re.findall(r"(\d+)U", body)]
        self.assertEqual(len(values), 64)
        self.assertEqual(min(values), 807)
        self.assertEqual(max(values), 3289)
        self.assertEqual(values[-1], 2048)
        self.assertEqual(sum(values) / len(values), 2048)
        for i, v in enumerate(values):
            self.assertLessEqual(abs(v - (2048 + 1241 * math.sin(2 * math.pi * (i + 1) / 64))), 0.5)

    def test_frequency(self):
        ticks = (275_000_000 + 64000 // 2) // 64000
        self.assertEqual(ticks, 4297)
        self.assertAlmostEqual(275_000_000 / ticks / 64, 999.9709099372, places=6)

    def test_independent_resources(self):
        body = SOURCE.split("static void DacSine_Start(void)\n{")[1].split("void DMA1_Stream1_IRQHandler")[0]
        self.assertIn("DMA_REQUEST_DAC1_CH2", body)
        self.assertIn("DMA1_Stream1", body)
        self.assertIn("DAC_TRIGGER_T6_TRGO", body)
        self.assertNotIn("&htim4", body)
        self.assertNotIn("&htim3", body)
        self.assertIn("SCB_CleanDCache_by_Addr", body)
        self.assertIn("DMA_IT_HT | DMA_IT_TC", body)


if __name__ == "__main__":
    unittest.main()
