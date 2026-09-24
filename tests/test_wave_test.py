"""Run the actual single-shot firmware functions with host register stubs."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / 'Firmware/weld3/Core/Src'


class WaveTest(unittest.TestCase):
    def test_single_shot(self):
        compiler = list(pathlib.Path('C:/Program Files/Microsoft Visual Studio').glob(
            '*/Community/VC/Auxiliary/Build/vcvars64.bat'))
        if not compiler:
            self.skipTest('MSVC unavailable')
        source = (SRC / 'main.c').read_text(encoding='utf-8')
        functions = source.split('static void WaveTest_OutputsOff(void)\n{')[1].split('/* USER CODE END 4 */')[0]
        globals_ = source.split('static WeldSettings waveSettings;')[1].split('static void WaveTest_Dma(void);')[0]
        program = r'''
#include <assert.h>
#include <math.h>
#include <string.h>
#include "WaveTest.h"
#include "CurrentFeedbackFilter.h"
#define PWM_WAVE_TEST_ENABLE 1
#define CURRENT_DMA_HALF 32U
#define TIM_CCER_CC2E 0x10
#define TIM_CCER_CC4E 0x1000
#define GPIO_PIN_7 128
#define GPIO_PIN_15 32768
#define CLEAR_BIT(r,m) ((r)&=~(m))
#define SET_BIT(r,m) ((r)|=(m))
#define MODIFY_REG(r,m,v) ((r)=((r)&~(m))|(v))
#define __DMB() ((void)0)
static uint32_t irq_mask, tick;
static uint32_t __get_PRIMASK(void){return irq_mask;}
static void __disable_irq(void){irq_mask=1;}
static void __set_PRIMASK(uint32_t x){irq_mask=x;}
static uint32_t HAL_GetTick(void){return tick;}
static struct {uint32_t CCER, CCR2, CCR4, ARR;} timer={0,0,0,32768};
static struct {uint32_t BSRR, MODER;} gb,gd;
#define TIM4 (&timer)
#define GPIOB (&gb)
#define GPIOD (&gd)
static uint32_t g_primaryCurrentAdc=100, g_primaryCurrentMin=0, g_primaryCurrentMax=200;
static uint32_t g_secondaryCurrentAdc=32;
static float g_secondaryCurrentFiltered=0.5f;
static uint32_t g_currentSenseFault, g_pwmOrFault, g_currentDmaBlocks=10;
static uint32_t g_currentSampleRateHz=32000; /* one modeled DMA half per ms */
static float g_pwmOrCommand;
static void PwmOr_Process(uint32_t h,uint32_t c){(void)h;(void)c;assert(0);}
static WeldSettings waveSettings;
''' + globals_ + '\nstatic void WaveTest_OutputsOff(void)\n{' + functions + r'''
static void advance(uint32_t ms, int keepalive) {
  while(ms--){++tick;WaveTest_Dma();if(keepalive)WaveTest_KeepAlive();WaveTest_Tick();}
}
int main(void){
  CurrentFeedbackFilter secondary;
  CurrentFeedbackFilter_Init(&secondary,0.001f,0.001f);
  assert(CurrentFeedbackFilter_Update(&secondary,65535)==1.0f);
  assert(CurrentFeedbackFilter_Update(&secondary,0)==0.5f);
  assert(CurrentFeedbackFilter_Update(&secondary,0)==0.25f);
  CurrentFeedbackFilter_Init(&secondary,0.001f,0.001f);
  assert(CurrentFeedbackFilter_Update(&secondary,0)==0.0f);
  for(int i=0;i<20;i++) CurrentFeedbackFilter_Update(&secondary,(i%2)?65535:0);
  assert(secondary.value>0.65f && secondary.value<0.68f);
  WeldSettings s={10,{100,50,0},{10,0,0},{20,10,0},{10,0,0},{5,0}};
  assert(WaveTest_Duration(&s)==65);
  assert(WaveTest_Target(&s,9)==0);
  assert(WaveTest_Target(&s,15)==50);
  assert(WaveTest_Target(&s,20)==100);
  assert(WaveTest_Target(&s,45)==50);
  assert(WaveTest_Target(&s,50)==0);
  assert(WaveTest_Target(&s,55)==50);
  assert(WaveTest_Target(&s,65)==0);
  assert(!WaveTest_Start(&s,0) && !WaveTest_Start(&s,46));
  assert(WaveTest_Start(&s,10));
  assert(!WaveTest_Start(&s,10));
  assert(TIM4->CCER==0 && g_pwmOrCommand==0);
  advance(3,1); assert(waveArmHalves==0 && TIM4->CCER!=0);
  advance(15,1); assert(fabsf(g_pwmOrCommand-.1f)<.00001f);
  advance(50,1);
  assert(waveStatus.active==0 && waveStatus.reason==1 && waveStatus.elapsed_ms==65);
  assert(TIM4->CCER==0 && g_pwmOrCommand==0 && waveStatus.count>0);
  uint32_t id=waveStatus.id;
  WaveTestSample p;
  assert(WaveTest_GetSample(id,0,&p));
  assert(!WaveTest_GetSample(id+1,0,&p));
  advance(100,1);assert(!waveStatus.active); /* no automatic repeat */
  assert(WaveTest_Start(&s,10));advance(3,1);WaveTest_Stop(2);
  assert(!waveStatus.active && waveStatus.reason==2 && TIM4->CCER==0);
  s.stage_time_ms[0]=999;s.stage_time_ms[1]=999;
  assert(WaveTest_Start(&s,45));advance(1002,0);
  assert(!waveStatus.active && waveStatus.reason==3 && g_pwmOrCommand==0);
  assert(WaveTest_Start(&s,45));advance(3,1);tick+=4;WaveTest_Tick();
  assert(!waveStatus.active && waveStatus.reason==4);
  WaveTest_Dma();g_currentSenseFault=1;assert(!WaveTest_Start(&s,5));g_currentSenseFault=0;
  s.stage_current_a[0]=NAN;assert(!WaveTest_Start(&s,5));
  s=(WeldSettings){999,{100,200,300},{500,500,500},{999,999,999},{500,500,500},{999,999}};
  assert(WaveTest_Start(&s,5));advance(9005,1);
  assert(!waveStatus.active && waveStatus.reason==1 && waveStatus.count<=2048);
  assert(WaveTest_GetSample(waveStatus.id,waveStatus.count-1,&p));
  assert(p.time_ms==WaveTest_Duration(&s) && p.duty_permille==0);
  WavePidConfig pid={0.2f,15.0f,0.0f,0.5f,1};
  assert(WaveTest_SetPid(&pid));
  s=(WeldSettings){0,{32768,0,0},{0,0,0},{999,0,0},{0,0,0},{0,0}};
  g_primaryCurrentAdc=0;
  assert(WaveTest_Start(&s,5));
  assert(!WaveTest_SetPid(&pid));
  advance(3,1); /* Arming completes: P term alone exceeds the 5% cap. */
  assert(waveStatus.duty_permille==50); /* No artificial output ramp. */
  advance(103,1);
  assert(waveStatus.duty_permille>0 && waveStatus.duty_permille<=50);
  g_primaryCurrentAdc=65535;advance(10,1);
  assert(waveStatus.duty_permille==0);
  WaveTest_Stop(2);assert(g_pwmOrCommand==0);
  assert(WaveTest_Start(&s,5));assert(waveIntegral==0 && waveOutput==0);
  WaveTest_Stop(2);
  /* A 1-ms host update and 1-ms time constant produce alpha=0.5.
     Verify startup seeding, feedback smoothing and immediate zero target. */
  g_primaryCurrentAdc=0;
  assert(WaveTest_Start(&s,5));
  assert(g_wavePidFiltered==0);
  g_primaryCurrentAdc=65535;
  assert(WaveTest_Control(0)==0);
  assert(fabsf(g_wavePidFiltered-0.5f)<0.00001f);
  assert(WaveTest_Control(0)==0);
  assert(fabsf(g_wavePidFiltered-0.75f)<0.00001f);
  for(int i=0;i<20;i++) {
    g_primaryCurrentAdc=(i%2)?65535:0;
    WaveTest_Control(0);
  }
  assert(g_wavePidFiltered>0.65f && g_wavePidFiltered<0.68f);
  WaveTest_Stop(2);
  assert(g_pwmOrCommand==0);
  assert(WaveTest_Start(&s,5));
  assert(g_wavePidFiltered==1); /* No stale filter state across runs. */
  WaveTest_Stop(2);
  pid.kp=NAN;assert(!WaveTest_SetPid(&pid));
  pid=(WavePidConfig){0.2f,0,0,1,1};
  assert(WaveTest_SetPid(&pid));
  g_primaryCurrentAdc=0;
  assert(WaveTest_Start(&s,45));
  assert(fabsf(WaveTest_Control(10000)-100.0f*0.2f*10000/65535)<0.0001f);
  assert(fabsf(WaveTest_Control(20000)-100.0f*0.2f*20000/65535)<0.0001f);
  WaveTest_Stop(2);
  s.stage_current_a[0]=65535;assert(WaveTest_Start(&s,45));WaveTest_Stop(2);
  s.stage_current_a[0]=65536;assert(!WaveTest_Start(&s,45));
  irq_mask=1;WaveTest_GetStatus(&waveStatus);assert(irq_mask==1);
  assert(waveStatus.secondary_adc==32 && waveStatus.secondary_filtered_micro==500000);
  waveStatus.count=0;
  g_wavePidFiltered=0.25f;
  WaveTest_Log(0);
  assert(waveSamples[0].primary_filtered_micro==250000);
  assert(waveSamples[0].secondary_adc==32);
  assert(waveSamples[0].secondary_filtered_micro==500000);
  return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='wave-test-') as directory:
            work = pathlib.Path(directory)
            (work / 'test.c').write_text(program, encoding='utf-8')
            (work / 'run.cmd').write_text(
                f'@echo off\ncall "{compiler[0]}" >nul\n'
                f'cl /nologo /TC /I"{SRC}" test.c /Fe:test.exe\n'
                'if errorlevel 1 exit /b 1\ntest.exe\n', encoding='utf-8')
            result = subprocess.run(['cmd', '/c', str(work / 'run.cmd')], cwd=work,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
