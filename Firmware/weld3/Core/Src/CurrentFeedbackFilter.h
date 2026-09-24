#ifndef CURRENT_FEEDBACK_FILTER_H
#define CURRENT_FEEDBACK_FILTER_H

#include <stdint.h>

typedef struct {
  float value;
  float alpha;
  uint8_t initialized;
} CurrentFeedbackFilter;

static inline void CurrentFeedbackFilter_Init(CurrentFeedbackFilter *filter,
                                              float dt, float tau)
{
  filter->value = 0.0f;
  filter->alpha = dt / (tau + dt);
  filter->initialized = 0U;
}

static inline float CurrentFeedbackFilter_Update(CurrentFeedbackFilter *filter,
                                                 uint16_t adc)
{
  float input = (float)adc / 65535.0f;
  if (!filter->initialized)
  {
    filter->value = input;
    filter->initialized = 1U;
  }
  else
  {
    filter->value += filter->alpha * (input - filter->value);
  }
  return filter->value;
}

#endif
