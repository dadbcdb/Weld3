"""Execute the firmware PID function on the host; no HAL/board validation."""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'Firmware/weld3/Core/Src/main.c').read_text(encoding='utf-8')


class PwmOrPid(unittest.TestCase):
    def test_firmware_controller(self):
        candidates = list(pathlib.Path('C:/Program Files/Microsoft Visual Studio').glob(
            '*/Community/VC/Auxiliary/Build/vcvars64.bat'))
        if not candidates:
            self.skipTest('MSVC host compiler not installed')
        defines = '\n'.join(line for line in SOURCE.splitlines()
                            if line.startswith('#define PWM_') or
                            line.startswith('#define CURRENT_DMA_'))
        defines = '\n'.join(line for line in defines.splitlines()
                            if not line.startswith('#define PWM_OR_BENCH_HOLD_ON_INVALID'))
        state = SOURCE.split('volatile float g_pwmOrMeasured;')[1].split(
            '__attribute__((aligned(32))) static uint32_t g_currentDmaBuffer')[0]
        function = SOURCE.split('static void PwmOr_Process(')[1].split(
            'static void CurrentSense_RecordFault(')[0]
        program = '''#include <stdint.h>
#include <assert.h>
#include <math.h>
''' + defines + '\nvolatile float g_pwmOrMeasured;\n' + state + '''
static uint32_t g_currentSampleRateHz = 268554U;
static int stopped;
static void PwmOr_Stop(void) { stopped = 1; }
static void PwmOr_Process(''' + function + '''
static void reset(void) {
  stopped = 0; g_pwmOrFault = 0; g_pwmOrUpdates = 0;
  g_pwmOrInputInvalid = g_pwmOrInvalidWindows = 0;
  pwmOrHigh = pwmOrSamples = 0;
  g_pwmOrCommand = pwmOrIntegral = 0.25f; pwmOrPrevious = 0;
}
int main(void) {
  reset();
  /* Exact center-aligned counter model, sample-interval midpoints. */
  for (int cycle = 0; cycle < 200; ++cycle) {
    uint32_t w = (uint32_t)(g_pwmOrCommand * 16384.0f), high = 0;
    for (uint32_t t = 512; t < 65536; t += 1024) {
      uint32_t cnt = t < 32768 ? t : 65536 - t;
      int a = cnt < w, b = cnt >= 32768 - w;
      assert(!(a && b)); high += a || b;
    }
    float previous = g_pwmOrCommand;
    PwmOr_Process(high * 16, 64 * 16);
    assert(!stopped);
    assert(g_pwmOrCommand >= 0.10f && g_pwmOrCommand <= 0.90f);
    assert(fabsf(g_pwmOrCommand - previous) <= 0.02501f);
  }
  assert(fabsf(g_pwmOrMeasured - PWM_OR_TARGET) <= 0.03125f);
  reset(); PwmOr_Process(0, 1024);
#if PWM_OR_BENCH_ALLOW_ZERO
  assert(!stopped && g_pwmOrInputInvalid == 1 && g_pwmOrUpdates == 1);
  assert(g_pwmOrCommand > 0.25f && g_pwmOrCommand <= 0.27501f);
  for (int i = 0; i < 1000; ++i) {
    float old = g_pwmOrCommand;
    PwmOr_Process(0, 1024);
    assert(g_pwmOrCommand >= old && g_pwmOrCommand <= 0.90f);
    assert(g_pwmOrCommand - old <= 0.02501f);
  }
  assert(fabsf(g_pwmOrCommand - 0.90f) < 0.00001f);
  assert(pwmOrIntegral < 1.0f && g_pwmOrInvalidWindows == 1001);
  for (int i = 0; i < 200; ++i) PwmOr_Process(900, 1024);
  assert(g_pwmOrInputInvalid == 0 && g_pwmOrCommand < 0.20f);
#elif PWM_OR_BENCH_HOLD_ON_INVALID
  assert(!stopped && g_pwmOrFault == 0 && g_pwmOrInputInvalid == 1);
  for (int i = 0; i < 1000; ++i) PwmOr_Process(0, 1024);
  assert(g_pwmOrCommand == 0.25f && pwmOrIntegral == 0.25f);
  assert(g_pwmOrUpdates == 0 && g_pwmOrInvalidWindows == 1001);
  PwmOr_Process(256, 1024);
  assert(g_pwmOrInputInvalid == 0 && g_pwmOrUpdates == 1);
  assert(g_pwmOrCommand <= 0.27501f);
#else
  assert(stopped && g_pwmOrFault == 2 && g_pwmOrUpdates == 0);
#endif
  reset(); PwmOr_Process(1024, 1024);
#if PWM_OR_BENCH_HOLD_ON_INVALID
  assert(!stopped && g_pwmOrInputInvalid == 2 && g_pwmOrCommand == 0.25f);
#else
  assert(stopped && g_pwmOrFault == 2 && g_pwmOrUpdates == 0);
#endif
  reset(); PwmOr_Process(8, 32);
  assert(g_pwmOrUpdates == 0 && !stopped);
  for (int i = 0; i < 200; ++i) PwmOr_Process(32, 1024);
  assert(g_pwmOrCommand <= 0.90f && pwmOrIntegral < 1.0f);
  for (int i = 0; i < 200; ++i) PwmOr_Process(900, 1024);
  assert(g_pwmOrCommand >= 0.10f && pwmOrIntegral > -1.0f);
  return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='weld-pid-') as directory:
            work = pathlib.Path(directory)
            (work / 'pid.c').write_text(program, encoding='utf-8')
            for mode in (0, 1):
                (work / 'run.cmd').write_text(
                    f'@echo off\ncall "{candidates[0]}" >nul\n'
                    f'cl /nologo /TC /DPWM_OR_BENCH_HOLD_ON_INVALID={mode} pid.c /Fe:pid.exe\n'
                    'if errorlevel 1 exit /b 1\npid.exe\n', encoding='utf-8')
                result = subprocess.run(['cmd', '/c', str(work / 'run.cmd')],
                                        cwd=work, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_waveform_sweep(self):
        for width in (1638, 4096, 8192, 14745):
            a = b = overlap = 0
            for t in range(65536):
                cnt = t if t < 32768 else 65536 - t
                a += cnt < width
                b += cnt >= 32768 - width
                overlap += (cnt < width) and (cnt >= 32768 - width)
            self.assertEqual(overlap, 0)
            self.assertLessEqual(abs(a - b), 2)  # counter endpoint convention
            self.assertAlmostEqual((a + b) / 65536, width / 16384)


if __name__ == '__main__':
    unittest.main()
