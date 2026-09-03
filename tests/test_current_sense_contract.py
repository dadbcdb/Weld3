"""Host-side timing/configuration contracts, NOT a target/HAL execution test."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "Firmware/weld3/Core/Src/main.c").read_text(encoding="utf-8")


class CurrentSenseContract(unittest.TestCase):
    def test_irq_above_rtos_mask(self):
        header = (ROOT / "Firmware/weld3/Core/Inc/main.h").read_text(encoding="utf-8")
        config = (ROOT / "Firmware/weld3/Core/Inc/FreeRTOSConfig.h").read_text(encoding="utf-8")
        msp = (ROOT / "Firmware/weld3/Core/Src/stm32h7xx_hal_msp.c").read_text(encoding="utf-8")
        priority = int(re.search(r"#define CURRENT_SENSE_IRQ_PRIORITY\s+(\d+)U", header)[1])
        boundary = int(re.search(r"#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY\s+(\d+)", config)[1])
        self.assertGreater(priority, 0)
        self.assertLess(priority, boundary)
        for irq in ("DMA1_Stream0_IRQn", "ADC_IRQn"):
            self.assertIn(f"HAL_NVIC_SetPriority({irq}, CURRENT_SENSE_IRQ_PRIORITY, 0)", msp)

    def test_buffer_and_cache_boundaries(self):
        pairs = int(re.search(r"#define CURRENT_DMA_SAMPLES\s+(\d+)U", SOURCE)[1])
        self.assertEqual(pairs, 64)
        self.assertEqual((pairs // 2 * 4) % 32, 0)
        self.assertIn("aligned(32)", SOURCE)

    def test_exact_ratio_and_midpoint_samples(self):
        pwm_ticks, interval = 65536, 1024
        events = list(range(interval // 2, pwm_ticks, interval))
        self.assertEqual(len(events), 64)
        self.assertEqual(sum(t < pwm_ticks // 2 for t in events), 32)
        self.assertEqual(events[31], 32256)
        self.assertEqual(events[63], 65024)
        self.assertEqual(events[63] - events[31], pwm_ticks // 2)
        self.assertTrue(all(t not in (0, 32768, 65536) for t in events))
        # At the current 275 MHz timer clock: 268554.6875 pairs/s.
        self.assertAlmostEqual(275_000_000 / interval, 268554.6875)

    def test_ownership_predicates(self):
        for remaining in range(65):
            half_safe = remaining != 0 and remaining <= 32
            full_safe = remaining > 32
            self.assertFalse(half_safe and full_safe)
            self.assertEqual(half_safe or full_safe, remaining != 0)

    def test_no_software_start_or_pwm_marker(self):
        self.assertNotIn("HAL_TIM_Base_Start(&htim3)", SOURCE)
        self.assertNotIn("HAL_TIM_PWM_Start(&htim3", SOURCE)
        self.assertNotIn("HAL_TIM_OC_DelayElapsedCallback", SOURCE)
        self.assertNotIn("__HAL_TIM_ENABLE_IT(&htim4", SOURCE)
        self.assertIn("slave.SlaveMode = TIM_SLAVEMODE_TRIGGER", SOURCE)
        self.assertIn("master.MasterOutputTrigger = TIM_TRGO_ENABLE", SOURCE)
        self.assertIn("ADC_EXTERNALTRIG_T3_TRGO", SOURCE)

    def test_control_stays_disabled(self):
        self.assertRegex(SOURCE, r"#define CURRENT_CONTROL_ENABLE\s+0U")
        processing = SOURCE.split("static void CurrentSense_ProcessBlock(uint32_t offset, uint32_t count)\n{")[1]
        processing = processing.split("static void CurrentSense_Init(void)")[0]
        self.assertNotIn("SET_COMPARE", processing)
        self.assertNotIn("HAL_TIM_PWM_Start", processing)
        self.assertIn("g_currentProcessingCycles", processing)
        self.assertIn("g_currentSenseFault", processing)


if __name__ == "__main__":
    unittest.main()
