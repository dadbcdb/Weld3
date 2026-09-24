#ifndef WAVE_TEST_H
#define WAVE_TEST_H
#include "WeldData.h"

/* Open-loop bench envelope. Current fields define relative heights only. */
static inline uint32_t WaveTest_Duration(const WeldSettings *s)
{
  uint32_t total = s->squeeze_ms + s->cool_ms[0] + s->cool_ms[1];
  for (unsigned i = 0; i < 3; ++i)
    total += s->stage_up_ms[i] + s->stage_time_ms[i] + s->stage_down_ms[i];
  return total;
}
static inline float WaveTest_Target(const WeldSettings *s, uint32_t ms)
{
  if (ms < s->squeeze_ms) return 0.0f;
  ms -= s->squeeze_ms;
  for (unsigned i = 0; i < 3; ++i)
  {
    if (ms < s->stage_up_ms[i])
      return s->stage_current_a[i] * (float)ms / (float)s->stage_up_ms[i];
    ms -= s->stage_up_ms[i];
    if (ms < s->stage_time_ms[i]) return s->stage_current_a[i];
    ms -= s->stage_time_ms[i];
    if (ms < s->stage_down_ms[i])
      return s->stage_current_a[i] * (1.0f - (float)ms / (float)s->stage_down_ms[i]);
    ms -= s->stage_down_ms[i];
    if (i < 2) { if (ms < s->cool_ms[i]) return 0.0f; ms -= s->cool_ms[i]; }
  }
  return 0.0f;
}
typedef struct {
  uint32_t time_ms, target_milli, duty_permille;
  uint32_t adc_mean, adc_min, adc_max;
  uint32_t primary_filtered_micro, secondary_adc, secondary_filtered_micro;
} WaveTestSample;
typedef struct {
  uint32_t id, active, reason, elapsed_ms, duration_ms, count;
  uint32_t duty_permille, adc_mean;
  uint32_t secondary_adc, secondary_filtered_micro, primary_filtered_micro;
} WaveTestStatus;
typedef struct { float kp, ki, kd, target; uint8_t enabled; } WavePidConfig;
/* reason: 0=idle, 1=completed, 2=STOP, 3=disconnect/lease, 4=acquisition */
#ifdef __cplusplus
extern "C" {
#endif
int WaveTest_Start(const WeldSettings *s, uint32_t duty_percent);
void WaveTest_Stop(uint32_t reason);
void WaveTest_KeepAlive(void);
void WaveTest_GetStatus(WaveTestStatus *status);
int WaveTest_GetSample(uint32_t id, uint32_t index, WaveTestSample *sample);
void WaveTest_GetPid(WavePidConfig *config);
int WaveTest_SetPid(const WavePidConfig *config);
#ifdef __cplusplus
}
#endif
#endif
