/*
 * MainProc.cpp
 *
 *  Created on: 2026. 8. 20.
 *      Author: dadbc
 */



#include "MainProc.h"
#include "main.h"
#include "WeldData.h"
#include "WaveTest.h"
#include "xprintf.h"

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "lwip.h"
#include "lwip/api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define WELD_TCP_PORT 5000
#define COMMAND_BUFFER_SIZE 512
#define LCD_CAPTURE_WIDTH 480U
#define LCD_CAPTURE_HEIGHT 272U
#define LCD_CAPTURE_BYTES (LCD_CAPTURE_WIDTH * LCD_CAPTURE_HEIGHT * 3U)
#define LCD_CAPTURE_BUFFER_ADDRESS 0x700C0000UL

/* Only one client is served at a time. Keep the receive buffer out of the
 * defaultTask stack because LwIP/netconn and response formatting also need
 * stack concurrently. */
static char commandBuffer[COMMAND_BUFFER_SIZE];

static WeldStatus weldStatus = {};
static WeldSettings weldSettings = { 100U, { 100.0f, 0.0f, 0.0f }, { 50U, 0U, 0U }, { 100U, 0U, 0U }, { 50U, 0U, 0U }, { 0U, 0U } };
static void LockData(void);
static void UnlockData(void);
static uint32_t dryRunStartTick;
static uint32_t dryRunDurationMs;
static uint8_t dryRunActive;
static bool WaveTest_IsActive(void)
{
  WaveTestStatus s;
  WaveTest_GetStatus(&s);
  return s.active != 0U;
}

/* MX25LM51245G: reserve the final 64 KiB of the 64 MiB OSPI NOR. TouchGFX is
 * currently linked to internal flash, but this partition must remain reserved
 * if external assets are enabled later.  A 4 KiB erase sector contains 32
 * fixed 128-byte profile records. */
#define PROFILE_FLASH_BASE       0x03FF0000UL
#define PROFILE_FLASH_SIZE       0x00010000UL
#define PROFILE_RECORD_SIZE      128UL
#define PROFILE_SECTOR_SIZE      4096UL
#define PROFILE_COUNT_AXIS       16UL
#define PROFILE_MAGIC            0x574C4433UL
#define PROFILE_VERSION          1U

extern OSPI_HandleTypeDef hospi1;
static uint8_t profileSectorBuffer[PROFILE_SECTOR_SIZE];
static int profileFlashState;
static uint8_t currentProfileFile;
static uint8_t currentProfileNumber;

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint8_t file;
  uint8_t number;
  WeldSettings settings;
  uint32_t crc;
  uint8_t reserved[PROFILE_RECORD_SIZE - 72U];
} ProfileRecord;
static_assert(sizeof(ProfileRecord) == PROFILE_RECORD_SIZE, "Profile record size");

static uint32_t ProfileCrc(const ProfileRecord *record)
{
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(record);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < offsetof(ProfileRecord, crc); ++i) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}

static int OspiCommand(uint32_t instruction, uint32_t addressMode, uint32_t address,
                       uint32_t dataMode, uint32_t size, uint32_t dummyCycles)
{
  OSPI_RegularCmdTypeDef command = {};
  command.OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;
  command.FlashId = HAL_OSPI_FLASH_ID_1;
  command.Instruction = instruction;
  command.InstructionMode = HAL_OSPI_INSTRUCTION_1_LINE;
  command.InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;
  command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  command.Address = address;
  command.AddressMode = addressMode;
  command.AddressSize = HAL_OSPI_ADDRESS_32_BITS;
  command.AddressDtrMode = HAL_OSPI_ADDRESS_DTR_DISABLE;
  command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
  command.DataMode = dataMode;
  command.DataDtrMode = HAL_OSPI_DATA_DTR_DISABLE;
  command.NbData = size;
  command.DummyCycles = dummyCycles;
  command.DQSMode = HAL_OSPI_DQS_DISABLE;
  command.SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;
  return HAL_OSPI_Command(&hospi1, &command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) == HAL_OK;
}

static int OspiWaitReady(uint32_t timeoutMs)
{
  uint32_t start = HAL_GetTick();
  uint8_t status;
  do
  {
    if (!OspiCommand(0x05U, HAL_OSPI_ADDRESS_NONE, 0U, HAL_OSPI_DATA_1_LINE, 1U, 0U) ||
        HAL_OSPI_Receive(&hospi1, &status, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) return 0;
    if ((status & 0x01U) == 0U) return 1;
    osDelay(1U);
  } while ((HAL_GetTick() - start) < timeoutMs);
  return 0;
}

static int OspiWriteEnable(void)
{
  return OspiCommand(0x06U, HAL_OSPI_ADDRESS_NONE, 0U, HAL_OSPI_DATA_NONE, 0U, 0U);
}

static int OspiRead(uint32_t address, uint8_t *data, uint32_t size)
{
  return OspiCommand(0x13U, HAL_OSPI_ADDRESS_1_LINE, address, HAL_OSPI_DATA_1_LINE, size, 0U) &&
         HAL_OSPI_Receive(&hospi1, data, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) == HAL_OK;
}

static int OspiProgramPage(uint32_t address, uint8_t *data, uint32_t size)
{
  return OspiWriteEnable() &&
         OspiCommand(0x12U, HAL_OSPI_ADDRESS_1_LINE, address, HAL_OSPI_DATA_1_LINE, size, 0U) &&
         HAL_OSPI_Transmit(&hospi1, data, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) == HAL_OK &&
         OspiWaitReady(1000U);
}

static int OspiErase4K(uint32_t address)
{
  return OspiWriteEnable() &&
         OspiCommand(0x21U, HAL_OSPI_ADDRESS_1_LINE, address, HAL_OSPI_DATA_NONE, 0U, 0U) &&
         OspiWaitReady(5000U);
}

static int ProfileFlashInit(void)
{
  if (profileFlashState != 0) return profileFlashState > 0;
  /* Restore the NOR to its power-on SPI protocol, then verify the JEDEC ID. */
  if (!OspiCommand(0x66U, HAL_OSPI_ADDRESS_NONE, 0U, HAL_OSPI_DATA_NONE, 0U, 0U) ||
      !OspiCommand(0x99U, HAL_OSPI_ADDRESS_NONE, 0U, HAL_OSPI_DATA_NONE, 0U, 0U))
  { profileFlashState = -1; return 0; }
  osDelay(2U);
  uint8_t id[3] = {};
  if (!OspiCommand(0x9FU, HAL_OSPI_ADDRESS_NONE, 0U, HAL_OSPI_DATA_1_LINE, sizeof(id), 0U) ||
      HAL_OSPI_Receive(&hospi1, id, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK || id[0] != 0xC2U)
  { profileFlashState = -1; return 0; }
  profileFlashState = 1;
  return 1;
}

static uint32_t ProfileAddress(uint32_t file, uint32_t number)
{
  return PROFILE_FLASH_BASE + ((file * PROFILE_COUNT_AXIS + number) * PROFILE_RECORD_SIZE);
}

static int ProfileLoad(uint32_t file, uint32_t number, WeldSettings *settings)
{
  if ((file >= PROFILE_COUNT_AXIS) || (number >= PROFILE_COUNT_AXIS) || !ProfileFlashInit()) return -1;
  ProfileRecord record;
  if (!OspiRead(ProfileAddress(file, number), reinterpret_cast<uint8_t *>(&record), sizeof(record))) return -1;
  if ((record.magic != PROFILE_MAGIC) || (record.version != PROFILE_VERSION) ||
      (record.file != file) || (record.number != number) || (record.crc != ProfileCrc(&record))) return 0;
  *settings = record.settings;
  return 1;
}

static int ProfileSave(uint32_t file, uint32_t number, const WeldSettings *settings)
{
  if ((file >= PROFILE_COUNT_AXIS) || (number >= PROFILE_COUNT_AXIS) || !ProfileFlashInit()) return 0;
  uint32_t address = ProfileAddress(file, number);
  uint32_t sectorAddress = address & ~(PROFILE_SECTOR_SIZE - 1U);
  uint32_t offset = address - sectorAddress;
  if ((sectorAddress < PROFILE_FLASH_BASE) || ((sectorAddress + PROFILE_SECTOR_SIZE) > (PROFILE_FLASH_BASE + PROFILE_FLASH_SIZE)) ||
      !OspiRead(sectorAddress, profileSectorBuffer, sizeof(profileSectorBuffer))) return 0;
  ProfileRecord record = {};
  memset(&record, 0xFF, sizeof(record));
  record.magic = PROFILE_MAGIC;
  record.version = PROFILE_VERSION;
  record.file = static_cast<uint8_t>(file);
  record.number = static_cast<uint8_t>(number);
  record.settings = *settings;
  record.crc = ProfileCrc(&record);
  memcpy(&profileSectorBuffer[offset], &record, sizeof(record));
  if (!OspiErase4K(sectorAddress)) return 0;
  for (uint32_t page = 0U; page < PROFILE_SECTOR_SIZE; page += 256U)
    if (!OspiProgramPage(sectorAddress + page, &profileSectorBuffer[page], 256U)) return 0;
  ProfileRecord verify;
  return OspiRead(address, reinterpret_cast<uint8_t *>(&verify), sizeof(verify)) &&
         (memcmp(&record, &verify, sizeof(record)) == 0);
}

static int ProfileEraseAll(void)
{
  if (!ProfileFlashInit()) return 0;
  const uint32_t usedBytes = PROFILE_COUNT_AXIS * PROFILE_COUNT_AXIS * PROFILE_RECORD_SIZE;
  const uint32_t sectorCount = (usedBytes + PROFILE_SECTOR_SIZE - 1U) / PROFILE_SECTOR_SIZE;
  for (uint32_t sector = 0U; sector < sectorCount; ++sector)
    if (!OspiErase4K(PROFILE_FLASH_BASE + sector * PROFILE_SECTOR_SIZE)) return 0;
  return 1;
}

static int ProfileSaveErased(uint32_t file, uint32_t number, const WeldSettings *settings)
{
  if ((file >= PROFILE_COUNT_AXIS) || (number >= PROFILE_COUNT_AXIS) || !ProfileFlashInit()) return 0;
  uint32_t address = ProfileAddress(file, number);
  ProfileRecord record;
  uint8_t existing[PROFILE_RECORD_SIZE];
  if (!OspiRead(address, existing, sizeof(existing))) return 0;
  for (size_t i = 0U; i < sizeof(existing); ++i) if (existing[i] != 0xFFU) return 0;
  memset(&record, 0xFF, sizeof(record));
  record.magic = PROFILE_MAGIC; record.version = PROFILE_VERSION;
  record.file = static_cast<uint8_t>(file); record.number = static_cast<uint8_t>(number);
  record.settings = *settings; record.crc = ProfileCrc(&record);
  if (!OspiProgramPage(address, reinterpret_cast<uint8_t *>(&record), sizeof(record))) return 0;
  ProfileRecord verify;
  return OspiRead(address, reinterpret_cast<uint8_t *>(&verify), sizeof(verify)) && memcmp(&record, &verify, sizeof(record)) == 0;
}

static uint32_t WeldSettings_TotalTimeMs(const WeldSettings *settings)
{
  uint32_t total = settings->squeeze_ms + settings->cool_ms[0] + settings->cool_ms[1];
  for (unsigned int i = 0U; i < 3U; ++i) total += settings->stage_up_ms[i] + settings->stage_time_ms[i] + settings->stage_down_ms[i];
  return total;
}

static void UpdateDryRunStatus(void)
{
  if (dryRunActive == 0U) return;
  uint32_t elapsed = osKernelGetTickCount() - dryRunStartTick;
  LockData();
  weldStatus.weld_time_ms = elapsed;
  weldStatus.welding = (elapsed < dryRunDurationMs) ? 1U : 0U;
  weldStatus.actual_current_a = 0.0f; /* No ADC/current claim in dry-run mode. */
  if (weldStatus.welding == 0U) dryRunActive = 0U;
  UnlockData();
}

static void LockData(void)
{
  taskENTER_CRITICAL();
}

static void UnlockData(void)
{
  taskEXIT_CRITICAL();
}

void WeldData_GetStatus(WeldStatus *status)
{
  if (status == NULL) return;
  LockData();
  *status = weldStatus;
  UnlockData();
}

void WeldData_SetStatus(const WeldStatus *status)
{
  if (status == NULL) return;
  LockData();
  weldStatus = *status;
  UnlockData();
}

void WeldData_GetSettings(WeldSettings *settings)
{
  if (settings == NULL) return;
  LockData();
  *settings = weldSettings;
  UnlockData();
}

int WeldData_SetSettings(const WeldSettings *settings)
{
  if (WaveTest_IsActive() || dryRunActive) return 0;
  if ((settings == NULL) ||
      0)
  {
    return 0;
  }
  if (settings->squeeze_ms > 10000U) return 0;
  for (unsigned int i = 0U; i < 3U; ++i)
  {
    if ((settings->stage_current_a[i] != settings->stage_current_a[i]) ||
        (settings->stage_current_a[i] < 0.0f) ||
        (settings->stage_current_a[i] > WELD_STAGE_TARGET_MAX) ||
        (settings->stage_up_ms[i] > 10000U) || (settings->stage_time_ms[i] > 10000U) ||
        (settings->stage_down_ms[i] > 10000U) ||
        ((settings->stage_current_a[i] == 0.0f) != (settings->stage_time_ms[i] == 0U)) ||
        ((settings->stage_current_a[i] == 0.0f) &&
         ((settings->stage_up_ms[i] != 0U) || (settings->stage_down_ms[i] != 0U))))
    {
      return 0;
    }
  }
  if ((settings->cool_ms[0] > 10000U) || (settings->cool_ms[1] > 10000U)) return 0;
  LockData();
  weldSettings = *settings;
  UnlockData();
  return 1;
}

void WeldData_GetProfileSelection(uint8_t *file, uint8_t *number)
{
  if ((file == NULL) || (number == NULL)) return;
  LockData();
  *file = currentProfileFile;
  *number = currentProfileNumber;
  UnlockData();
}

void WeldData_SetProfileSelection(uint8_t file, uint8_t number)
{
  LockData();
  currentProfileFile = file;
  currentProfileNumber = number;
  UnlockData();
}

static void SendText(struct netconn *client, const char *text)
{
  netconn_write(client, text, strlen(text), NETCONN_COPY);
}

static uint32_t CaptureHash(const uint8_t *data, uint32_t size)
{
  uint32_t hash = 2166136261UL;
  for (uint32_t i = 0U; i < size; ++i) hash = (hash ^ data[i]) * 16777619UL;
  return hash;
}

static err_t SendCaptureData(struct netconn *client, const void *data, size_t size)
{
  return netconn_write(client, data, size, NETCONN_COPY);
}

static void SendScreenCapture(struct netconn *client)
{
  const uint32_t frameAddress = LTDC_Layer1->CFBAR;
  xprintf("[LCD] capture request, CFBAR=%08lX\r\n", (unsigned long)frameAddress);
  if ((frameAddress != 0x70000000UL) && (frameAddress != 0x70060000UL))
  {
    xprintf("[LCD] invalid framebuffer address\r\n");
    SendText(client, "ERR FRAMEBUFFER\r\n");
    return;
  }
  uint8_t *snapshot = reinterpret_cast<uint8_t *>(LCD_CAPTURE_BUFFER_ADDRESS);
  xprintf("[LCD] snapshot copy start: %lu bytes\r\n", (unsigned long)LCD_CAPTURE_BYTES);
  for (uint32_t offset = 0U; offset < LCD_CAPTURE_BYTES; offset += 16384U)
  {
    const uint32_t remaining = LCD_CAPTURE_BYTES - offset;
    const uint32_t chunk = remaining < 16384U ? remaining : 16384U;
    memcpy(snapshot + offset, reinterpret_cast<const uint8_t *>(frameAddress) + offset, chunk);
  }
  const uint32_t hash = CaptureHash(snapshot, LCD_CAPTURE_BYTES);
  xprintf("[LCD] snapshot ready, hash=%08lX\r\n", (unsigned long)hash);
  char header[96];
  snprintf(header, sizeof(header), "SCREEN RGB888 %u %u %lu %08lX\r\n",
           LCD_CAPTURE_WIDTH, LCD_CAPTURE_HEIGHT,
           (unsigned long)LCD_CAPTURE_BYTES, (unsigned long)hash);
  xprintf("[LCD] header send start\r\n");
  err_t sendError = SendCaptureData(client, header, strlen(header));
  if (sendError != ERR_OK)
  {
    xprintf("[LCD] header send failed: %d\r\n", (int)sendError);
    return;
  }
  xprintf("[LCD] header sent\r\n");
  uint32_t nextProgressLog = 65536U;
  for (uint32_t offset = 0U; offset < LCD_CAPTURE_BYTES; offset += TCP_MSS)
  {
    const uint32_t remaining = LCD_CAPTURE_BYTES - offset;
    const uint32_t chunk = remaining < TCP_MSS ? remaining : TCP_MSS;
    err_t error = SendCaptureData(client, snapshot + offset, chunk);
    if (error != ERR_OK)
    {
      xprintf("[LCD] stream failed: offset=%lu error=%d\r\n",
              (unsigned long)offset, (int)error);
      return;
    }
    const uint32_t sent = offset + chunk;
    if ((sent >= nextProgressLog) || (sent == LCD_CAPTURE_BYTES))
    {
      xprintf("[LCD] streaming: %lu/%lu bytes (%lu%%)\r\n",
              (unsigned long)sent, (unsigned long)LCD_CAPTURE_BYTES,
              (unsigned long)((sent * 100U) / LCD_CAPTURE_BYTES));
      nextProgressLog += 65536U;
    }
  }
  xprintf("[LCD] capture sent: %lu bytes\r\n", (unsigned long)LCD_CAPTURE_BYTES);
}

static void FormatFloat2(char *destination, size_t size, float value)
{
  long scaled = (long)((value * 100.0f) + ((value >= 0.0f) ? 0.5f : -0.5f));
  long whole = scaled / 100L;
  long fraction = scaled % 100L;
  if (fraction < 0L) fraction = -fraction;
  snprintf(destination, size, "%ld.%02ld", whole, fraction);
}

/* Keep JSON numeric formatting independent of printf float support in the
 * embedded C library (which is intentionally disabled in the production link).
 */
static void FormatFloat6(char *destination, size_t size, float value)
{
  long scaled = (long)((value * 1000000.0f) + ((value >= 0.0f) ? 0.5f : -0.5f));
  long whole = scaled / 1000000L;
  long fraction = scaled % 1000000L;
  if (fraction < 0L) fraction = -fraction;
  snprintf(destination, size, "%ld.%06ld", whole, fraction);
}

static void SendAllProfiles(struct netconn *client)
{
  char response[384], current1[20], current2[20], current3[20];
  WeldSettings settings;
  uint32_t count = 0U;
  for (uint32_t file = 0U; file < PROFILE_COUNT_AXIS; ++file)
  {
    for (uint32_t number = 0U; number < PROFILE_COUNT_AXIS; ++number)
    {
      int loaded = ProfileLoad(file, number, &settings);
      if (loaded < 0) { SendText(client, "ERR STORAGE\r\n"); return; }
      if (loaded == 0) continue;
      FormatFloat2(current1, sizeof(current1), settings.stage_current_a[0]);
      FormatFloat2(current2, sizeof(current2), settings.stage_current_a[1]);
      FormatFloat2(current3, sizeof(current3), settings.stage_current_a[2]);
      snprintf(response, sizeof(response),
               "{\"type\":\"profile\",\"file\":%lu,\"number\":%lu,\"squeeze_ms\":%lu,\"current1_a\":%s,\"up1_ms\":%lu,\"time1_ms\":%lu,\"down1_ms\":%lu,\"cool1_ms\":%lu,\"current2_a\":%s,\"up2_ms\":%lu,\"time2_ms\":%lu,\"down2_ms\":%lu,\"cool2_ms\":%lu,\"current3_a\":%s,\"up3_ms\":%lu,\"time3_ms\":%lu,\"down3_ms\":%lu}\r\n",
               (unsigned long)file, (unsigned long)number, (unsigned long)settings.squeeze_ms,
               current1, (unsigned long)settings.stage_up_ms[0], (unsigned long)settings.stage_time_ms[0], (unsigned long)settings.stage_down_ms[0], (unsigned long)settings.cool_ms[0],
               current2, (unsigned long)settings.stage_up_ms[1], (unsigned long)settings.stage_time_ms[1], (unsigned long)settings.stage_down_ms[1], (unsigned long)settings.cool_ms[1],
               current3, (unsigned long)settings.stage_up_ms[2], (unsigned long)settings.stage_time_ms[2], (unsigned long)settings.stage_down_ms[2]);
      SendText(client, response);
      ++count;
    }
  }
  snprintf(response, sizeof(response), "{\"type\":\"profiles_end\",\"count\":%lu}\r\n", (unsigned long)count);
  SendText(client, response);
}

static int ParseFloatField(const char **cursor, const char *label, float *value)
{
  size_t length = strlen(label);
  if (strncmp(*cursor, label, length) != 0) return 0;
  char *end;
  const char *start = *cursor + length;
  *value = strtof(start, &end);
  if (end == start) return 0;
  *cursor = end;
  return 1;
}

static int ParseUintField(const char **cursor, const char *label, uint32_t *value)
{
  size_t length = strlen(label);
  if (strncmp(*cursor, label, length) != 0) return 0;
  char *end;
  const char *start = *cursor + length;
  unsigned long parsed = strtoul(start, &end, 10);
  if ((end == start) || (parsed > UINT32_MAX)) return 0;
  *value = (uint32_t)parsed;
  *cursor = end;
  return 1;
}

static int ParseSettings(const char *command, WeldSettings *settings)
{
  const char *cursor = command;
  if ((command == NULL) || (settings == NULL)) return 0;
  if (!ParseUintField(&cursor, "SET SETTINGS squeeze_ms=", &settings->squeeze_ms) ||
      !ParseFloatField(&cursor, " current1_a=", &settings->stage_current_a[0]) ||
      !ParseUintField(&cursor, " up1_ms=", &settings->stage_up_ms[0]) ||
      !ParseUintField(&cursor, " time1_ms=", &settings->stage_time_ms[0]) ||
      !ParseUintField(&cursor, " down1_ms=", &settings->stage_down_ms[0]) ||
      !ParseUintField(&cursor, " cool1_ms=", &settings->cool_ms[0]) ||
      !ParseFloatField(&cursor, " current2_a=", &settings->stage_current_a[1]) ||
      !ParseUintField(&cursor, " up2_ms=", &settings->stage_up_ms[1]) ||
      !ParseUintField(&cursor, " time2_ms=", &settings->stage_time_ms[1]) ||
      !ParseUintField(&cursor, " down2_ms=", &settings->stage_down_ms[1]) ||
      !ParseUintField(&cursor, " cool2_ms=", &settings->cool_ms[1]) ||
      !ParseFloatField(&cursor, " current3_a=", &settings->stage_current_a[2]) ||
      !ParseUintField(&cursor, " up3_ms=", &settings->stage_up_ms[2]) ||
      !ParseUintField(&cursor, " time3_ms=", &settings->stage_time_ms[2]) ||
      !ParseUintField(&cursor, " down3_ms=", &settings->stage_down_ms[2])) return 0;
  return (*cursor == '\0') ? 1 : 0;
}

static int ParseProfileCommand(const char *command, const char *verb, uint32_t *file, uint32_t *number)
{
  size_t verbLength = strlen(verb);
  if (strncmp(command, verb, verbLength) != 0) return 0;
  const char *cursor = command + verbLength;
  return ParseUintField(&cursor, " file=", file) &&
         ParseUintField(&cursor, " number=", number) && (*cursor == '\0');
}

static int ParseCaptureRead(const char *command, uint32_t *offset, uint32_t *size)
{
  const char *cursor = command;
  return ParseUintField(&cursor, "CAPTURE READ offset=", offset) &&
         ParseUintField(&cursor, " size=", size) && (*cursor == '\0');
}

static void ProcessCommand(struct netconn *client, char *command)
{
  char response[384];
  char current1[20], current2[20], current3[20];
  WeldStatus status;
  WeldSettings settings;
  uint32_t profileFile, profileNumber;
  uint32_t captureOffset, captureSize;
  size_t length = strlen(command);

  while ((length > 0U) && ((command[length - 1U] == '\r') || (command[length - 1U] == '\n') || (command[length - 1U] == ' ')))
  {
    command[--length] = '\0';
  }

  /* Do not rely on GET STATUS polling to retire a completed dry-run. Storage
   * commands must see the current timer state even while the WinApp holds the
   * TCP command stream for a long whole-library transfer. */
  UpdateDryRunStatus();

  if (WaveTest_IsActive() && strcmp(command, "STOP") != 0 &&
      strcmp(command, "TEST STATUS") != 0 && strcmp(command, "GET STATUS") != 0 &&
      strcmp(command, "PING") != 0)
  { SendText(client, "ERR BUSY\r\n"); return; }

  if (strcmp(command, "STOP") == 0)
  {
    WaveTest_Stop(2U);
    dryRunActive = 0U;
    LockData(); weldStatus.welding = 0U; UnlockData();
    SendText(client, "OK\r\n"); return;
  }
  if (strcmp(command, "TEST STATUS") == 0)
  {
    WaveTestStatus s;
    WaveTest_KeepAlive();
    WaveTest_GetStatus(&s);
    snprintf(response, sizeof(response),
      "{\"id\":%lu,\"active\":%lu,\"reason\":%lu,\"time_ms\":%lu,\"duration_ms\":%lu,\"count\":%lu,\"duty_permille\":%lu,\"adc_mean\":%lu,\"secondary_adc\":%lu,\"secondary_filtered_micro\":%lu,\"primary_filtered_micro\":%lu,\"pwm_frequency_hz\":%lu,\"start_reject\":%lu,\"current_fault\":%lu,\"pwm_fault\":%lu,\"current_fault_detail\":%lu}\r\n",
      (unsigned long)s.id, (unsigned long)s.active, (unsigned long)s.reason,
      (unsigned long)s.elapsed_ms, (unsigned long)s.duration_ms, (unsigned long)s.count,
      (unsigned long)s.duty_permille, (unsigned long)s.adc_mean,
      (unsigned long)s.secondary_adc, (unsigned long)s.secondary_filtered_micro,
      (unsigned long)s.primary_filtered_micro, (unsigned long)s.pwm_frequency_hz,
      (unsigned long)s.start_reject, (unsigned long)s.current_fault,
      (unsigned long)s.pwm_fault, (unsigned long)s.current_fault_detail);
    SendText(client, response); return;
  }
  if (strcmp(command, "GET PID") == 0)
  {
    WavePidConfig p; char kp[24], ki[24], kd[24], target[24]; WaveTest_GetPid(&p);
    FormatFloat6(kp, sizeof(kp), p.kp); FormatFloat6(ki, sizeof(ki), p.ki);
    FormatFloat6(kd, sizeof(kd), p.kd); FormatFloat6(target, sizeof(target), p.target);
    snprintf(response, sizeof(response), "{\"target_mode\":\"stage_adc\",\"kp\":%s,\"ki\":%s,\"kd\":%s,\"target\":%s,\"enabled\":%u}\r\n", kp,ki,kd,target,p.enabled);
    SendText(client, response); return;
  }
  if (strncmp(command, "SET PID", 7) == 0)
  {
    WavePidConfig p; WaveTest_GetPid(&p); uint32_t enabledValue = 0U;
    const char *cursor = command;
    if (!ParseFloatField(&cursor, "SET PID kp=", &p.kp) ||
        !ParseFloatField(&cursor, " ki=", &p.ki) ||
        !ParseFloatField(&cursor, " kd=", &p.kd) ||
        !ParseFloatField(&cursor, " target=", &p.target) ||
        !ParseUintField(&cursor, " enabled=", &enabledValue) ||
        *cursor != '\0' || enabledValue > 1U)
      SendText(client, "ERR RANGE\r\n");
    else { p.enabled = (uint8_t)enabledValue; SendText(client, WaveTest_SetPid(&p) ? "OK\r\n" : "ERR BUSY\r\n"); }
    return;
  }
  if (strncmp(command, "TEST START", 10) == 0)
  {
    const char *cursor = command;
    uint32_t duty, frequency;
    if (dryRunActive) SendText(client, "ERR BUSY\r\n");
    else if (!ParseUintField(&cursor, "TEST START duty_percent=", &duty) ||
             !ParseUintField(&cursor, " frequency_hz=", &frequency) || *cursor != '\0')
      SendText(client, "ERR RANGE\r\n");
    else
    {
      WeldData_GetSettings(&settings);
      SendText(client, WaveTest_Start(&settings, duty, frequency) ? "OK TEST\r\n" : "ERR TEST_NOT_READY\r\n");
    }
    return;
  }
  if (strcmp(command, "TEST TRACE") == 0)
  {
    WaveTestStatus s;
    WaveTest_GetStatus(&s);
    snprintf(response, sizeof(response), "TRACE %lu %lu\r\n",
             (unsigned long)s.id, (unsigned long)s.count);
    SendText(client, response);
    for (uint32_t i = 0; i < s.count; ++i)
    {
      WaveTestSample p;
      if (!WaveTest_GetSample(s.id, i, &p)) break;
      snprintf(response, sizeof(response), "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\r\n",
        (unsigned long)p.time_ms, (unsigned long)p.target_milli,
        (unsigned long)p.duty_permille, (unsigned long)p.adc_mean,
        (unsigned long)p.adc_min, (unsigned long)p.adc_max,
        (unsigned long)p.primary_filtered_micro, (unsigned long)p.secondary_adc,
        (unsigned long)p.secondary_filtered_micro);
      SendText(client, response);
    }
    SendText(client, "END\r\n"); return;
  }

  if (strcmp(command, "CAPTURE SCREEN") == 0)
  {
    SendScreenCapture(client);
  }
  else if (ParseCaptureRead(command, &captureOffset, &captureSize))
  {
    if ((captureSize == 0U) || (captureSize > 256U) ||
        (captureOffset >= LCD_CAPTURE_BYTES) ||
        (captureSize > (LCD_CAPTURE_BYTES - captureOffset)))
    {
      SendText(client, "ERR CAPTURE_RANGE\r\n");
    }
    else
    {
      if (captureOffset == 0U)
      {
        xprintf("[LCD] first pull request: offset=%lu size=%lu\r\n",
                (unsigned long)captureOffset, (unsigned long)captureSize);
      }
      err_t error = SendCaptureData(client,
          reinterpret_cast<const uint8_t *>(LCD_CAPTURE_BUFFER_ADDRESS) + captureOffset,
          captureSize);
      if (error != ERR_OK)
      {
        xprintf("[LCD] pull failed: offset=%lu size=%lu error=%d\r\n",
                (unsigned long)captureOffset, (unsigned long)captureSize, (int)error);
      }
      else if (((captureOffset + captureSize) % 65536U) < captureSize ||
               (captureOffset + captureSize) == LCD_CAPTURE_BYTES)
      {
        xprintf("[LCD] pulled: %lu/%lu bytes (%lu%%)\r\n",
                (unsigned long)(captureOffset + captureSize),
                (unsigned long)LCD_CAPTURE_BYTES,
                (unsigned long)(((captureOffset + captureSize) * 100U) / LCD_CAPTURE_BYTES));
      }
    }
  }
  else if (strcmp(command, "GET STATUS") == 0)
  {
    WaveTestStatus sense; WaveTest_GetStatus(&sense);
    WeldData_GetStatus(&status);
    FormatFloat2(current1, sizeof(current1), status.actual_current_a);
    snprintf(response, sizeof(response),
             "{\"type\":\"status\",\"welding\":%u,\"fault\":%u,\"fault_code\":%u,\"current_a\":%s,\"weld_time_ms\":%lu,\"adc_mean\":%lu,\"secondary_adc\":%lu,\"secondary_filtered_micro\":%lu,\"primary_filtered_micro\":%lu}\r\n",
             status.welding, status.fault, status.fault_code,
             current1,
             (unsigned long)status.weld_time_ms, (unsigned long)sense.adc_mean,
             (unsigned long)sense.secondary_adc, (unsigned long)sense.secondary_filtered_micro, (unsigned long)sense.primary_filtered_micro);
    SendText(client, response);
  }
  else if (strcmp(command, "DUMP PROFILES") == 0)
  {
    if (dryRunActive != 0U) SendText(client, "ERR BUSY\r\n");
    else SendAllProfiles(client);
  }
  else if (strcmp(command, "CLEAR PROFILES") == 0)
  {
    if (dryRunActive != 0U) SendText(client, "ERR BUSY\r\n");
    else SendText(client, ProfileEraseAll() ? "OK\r\n" : "ERR STORAGE\r\n");
  }
  else if (strcmp(command, "GET SETTINGS") == 0)
  {
    WeldData_GetSettings(&settings);
    FormatFloat2(current1, sizeof(current1), settings.stage_current_a[0]);
    FormatFloat2(current2, sizeof(current2), settings.stage_current_a[1]);
    FormatFloat2(current3, sizeof(current3), settings.stage_current_a[2]);
    snprintf(response, sizeof(response),
             "{\"type\":\"settings\",\"squeeze_ms\":%lu,\"current1_a\":%s,\"up1_ms\":%lu,\"time1_ms\":%lu,\"down1_ms\":%lu,\"cool1_ms\":%lu,\"current2_a\":%s,\"up2_ms\":%lu,\"time2_ms\":%lu,\"down2_ms\":%lu,\"cool2_ms\":%lu,\"current3_a\":%s,\"up3_ms\":%lu,\"time3_ms\":%lu,\"down3_ms\":%lu}\r\n",
             (unsigned long)settings.squeeze_ms,
             current1, (unsigned long)settings.stage_up_ms[0], (unsigned long)settings.stage_time_ms[0], (unsigned long)settings.stage_down_ms[0], (unsigned long)settings.cool_ms[0],
             current2, (unsigned long)settings.stage_up_ms[1], (unsigned long)settings.stage_time_ms[1], (unsigned long)settings.stage_down_ms[1], (unsigned long)settings.cool_ms[1],
             current3, (unsigned long)settings.stage_up_ms[2], (unsigned long)settings.stage_time_ms[2], (unsigned long)settings.stage_down_ms[2]);
    SendText(client, response);
  }
  else if (ParseSettings(command, &settings))
  {
    SendText(client, WeldData_SetSettings(&settings) ? "OK\r\n" : "ERR RANGE\r\n");
  }
  else if (ParseProfileCommand(command, "SET PROFILE", &profileFile, &profileNumber))
  {
    if ((profileFile >= PROFILE_COUNT_AXIS) || (profileNumber >= PROFILE_COUNT_AXIS)) SendText(client, "ERR RANGE\r\n");
    else
    {
      WeldData_SetProfileSelection(static_cast<uint8_t>(profileFile),
                                   static_cast<uint8_t>(profileNumber));
      SendText(client, "OK\r\n");
    }
  }
  else if (strcmp(command, "SAVE PROFILE") == 0)
  {
    if (dryRunActive != 0U) SendText(client, "ERR BUSY\r\n");
    else
    {
      WeldData_GetSettings(&settings);
      SendText(client, ProfileSave(currentProfileFile, currentProfileNumber, &settings) ? "OK\r\n" : "ERR STORAGE\r\n");
    }
  }
  else if (strcmp(command, "SAVE PROFILE FAST") == 0)
  {
    if (dryRunActive != 0U) SendText(client, "ERR BUSY\r\n");
    else
    {
      WeldData_GetSettings(&settings);
      SendText(client, ProfileSaveErased(currentProfileFile, currentProfileNumber, &settings) ? "OK\r\n" : "ERR STORAGE\r\n");
    }
  }
  else if (strcmp(command, "LOAD PROFILE") == 0)
  {
    if (dryRunActive != 0U) SendText(client, "ERR BUSY\r\n");
    else
    {
      int loaded = ProfileLoad(currentProfileFile, currentProfileNumber, &settings);
      if (loaded < 0) SendText(client, "ERR STORAGE\r\n");
      else if (loaded == 0) SendText(client, "ERR NOT_FOUND\r\n");
      else SendText(client, WeldData_SetSettings(&settings) ? "OK\r\n" : "ERR RANGE\r\n");
    }
  }
  else if (ParseProfileCommand(command, "SAVE PROFILE", &profileFile, &profileNumber))
  {
    if (dryRunActive != 0U) SendText(client, "ERR BUSY\r\n");
    else if ((profileFile >= PROFILE_COUNT_AXIS) || (profileNumber >= PROFILE_COUNT_AXIS)) SendText(client, "ERR RANGE\r\n");
    else
    {
      WeldData_GetSettings(&settings);
      SendText(client, ProfileSave(profileFile, profileNumber, &settings) ? "OK\r\n" : "ERR STORAGE\r\n");
    }
  }
  else if (ParseProfileCommand(command, "LOAD PROFILE", &profileFile, &profileNumber))
  {
    if (dryRunActive != 0U) SendText(client, "ERR BUSY\r\n");
    else
    {
      int loaded = ProfileLoad(profileFile, profileNumber, &settings);
      if (loaded < 0) SendText(client, "ERR STORAGE\r\n");
      else if (loaded == 0) SendText(client, "ERR NOT_FOUND\r\n");
      else SendText(client, WeldData_SetSettings(&settings) ? "OK\r\n" : "ERR RANGE\r\n");
    }
  }
  else if (strcmp(command, "START") == 0)
  {
    if (dryRunActive != 0U)
    {
      SendText(client, "ERR BUSY\r\n");
    }
    else
    {
      WeldData_GetSettings(&settings);
      dryRunDurationMs = WeldSettings_TotalTimeMs(&settings);
      if (dryRunDurationMs == 0U)
      {
        SendText(client, "ERR RANGE\r\n");
      }
      else
      {
        LockData();
        weldStatus.welding = 1U;
        weldStatus.weld_time_ms = 0U;
        weldStatus.actual_current_a = 0.0f;
        UnlockData();
        dryRunStartTick = osKernelGetTickCount();
        dryRunActive = 1U;
        SendText(client, "OK DRYRUN\r\n");
      }
    }
  }
  else if (strcmp(command, "PING") == 0)
  {
    SendText(client, "PONG\r\n");
  }
  else
  {
    SendText(client, "ERR COMMAND\r\n");
  }
}

static void ServeClient(struct netconn *client)
{
  struct netbuf *buffer;
  void *data;
  u16_t dataLength;
  size_t used = 0U;

  SendText(client, "WELD3 READY\r\n");
  while (netconn_recv(client, &buffer) == ERR_OK)
  {
    do
    {
      netbuf_data(buffer, &data, &dataLength);
      const char *bytes = static_cast<const char *>(data);
      for (u16_t i = 0; i < dataLength; ++i)
      {
        if (bytes[i] == '\n')
        {
          commandBuffer[used] = '\0';
          ProcessCommand(client, commandBuffer);
          used = 0U;
        }
        else if (used < (sizeof(commandBuffer) - 1U))
        {
          commandBuffer[used++] = bytes[i];
        }
        else
        {
          used = 0U;
          SendText(client, "ERR TOO_LONG\r\n");
        }
      }
    } while (netbuf_next(buffer) >= 0);
    netbuf_delete(buffer);
  }
  WaveTest_Stop(3U);
}

void StartDefaultTask(void *argument)
{
	WeldSettings startupSettings;
	int startupProfile = ProfileLoad(currentProfileFile, currentProfileNumber, &startupSettings);
	if ((startupProfile > 0) && WeldData_SetSettings(&startupSettings))
	{
		xprintf("[PROFILE] startup load %02u/%02u: stages=%u%u%u\r\n",
		        currentProfileFile, currentProfileNumber,
		        startupSettings.stage_time_ms[0] != 0U,
		        startupSettings.stage_time_ms[1] != 0U,
		        startupSettings.stage_time_ms[2] != 0U);
	}
	else if (startupProfile == 0)
	{
		xprintf("[PROFILE] startup %02u/%02u not found; using defaults\r\n",
		        currentProfileFile, currentProfileNumber);
	}
	else
	{
		xprintf("[PROFILE] startup %02u/%02u load failed; using defaults\r\n",
		        currentProfileFile, currentProfileNumber);
	}
	xprintf("[TCP] starting LwIP\r\n");
	MX_LWIP_Init();

	struct netconn *server = netconn_new(NETCONN_TCP);
	if (server == NULL)
	{
		xprintf("[TCP] netconn_new failed\r\n");
		for (;;) osDelay(1000);
	}
	err_t tcpError = netconn_bind(server, IP_ADDR_ANY, WELD_TCP_PORT);
	if (tcpError != ERR_OK)
	{
		xprintf("[TCP] bind port %d failed: %d\r\n", WELD_TCP_PORT, (int)tcpError);
		netconn_delete(server);
		for (;;) osDelay(1000);
	}
	tcpError = netconn_listen(server);
	if (tcpError != ERR_OK)
	{
		xprintf("[TCP] listen failed: %d\r\n", (int)tcpError);
		netconn_delete(server);
		for (;;) osDelay(1000);
	}

	xprintf("[TCP] server 192.168.0.100:%d LISTENING\r\n", WELD_TCP_PORT);
	for (;;)
	{
		struct netconn *client = NULL;
		if (netconn_accept(server, &client) == ERR_OK)
		{
			xprintf("[TCP] client connected\r\n");
			ServeClient(client);
			netconn_close(client);
			netconn_delete(client);
			xprintf("[TCP] client disconnected\r\n");
		}
	}
}
