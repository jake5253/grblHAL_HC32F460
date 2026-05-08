#pragma once

#include "driver.h"

#if SDCARD_ENABLE

#include "sdcard.h"

#if SDCARD_SDIO

char *hc32_sdcard_mount (FATFS **fs);
bool hc32_sdcard_unmount (FATFS **fs);
bool hc32_sdcard_is_inserted (void);

#endif
#endif
