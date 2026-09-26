#ifndef WELD_DATA_H
#define WELD_DATA_H

#include <stdint.h>

#define WELD_STAGE_TARGET_MAX 10000.0f

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  uint8_t welding;
  uint8_t fault;
  uint16_t fault_code;
  float actual_current_a;
  uint32_t weld_time_ms;
} WeldStatus;

typedef struct
{
  uint32_t squeeze_ms;
  float stage_current_a[3];
  uint32_t stage_up_ms[3];
  uint32_t stage_time_ms[3];
  uint32_t stage_down_ms[3];
  uint32_t cool_ms[2];
} WeldSettings;

void WeldData_GetStatus(WeldStatus *status);
void WeldData_SetStatus(const WeldStatus *status);
void WeldData_GetSettings(WeldSettings *settings);
int WeldData_SetSettings(const WeldSettings *settings);
void WeldData_GetProfileSelection(uint8_t *file, uint8_t *number);
void WeldData_SetProfileSelection(uint8_t file, uint8_t number);

#ifdef __cplusplus
}
#endif

#endif
