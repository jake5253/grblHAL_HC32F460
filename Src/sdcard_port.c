#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "sdcard_port.h"
#include "sd_card.h"

#if SDCARD_ENABLE && SDCARD_SDIO

#define HC32_SD_BLOCK_SIZE      512u
#define HC32_SD_IO_TIMEOUT_MS   2000u

static FATFS sd_fs;
static stc_sd_handle_t sd_handle;
static DSTATUS sd_status = STA_NOINIT;
static bool sd_pins_ready = false;
static en_sdioc_bus_width_t sd_bus_width = SdiocBusWidth4Bit;
static en_sdioc_clk_freq_t sd_clk_freq = SdiocClk25M;

static void sdcard_configure_detect_source (void)
{
#if defined(SDIOC_DETECT_VIA_GPIO) && SDIOC_DETECT_VIA_GPIO
    if(sd_handle.SDIOCx == NULL)
        return;

    SDIOC_SetCardDetectSignal(sd_handle.SDIOCx, SdiocCardDetectTestLevel);
    sd_handle.SDIOCx->HOSTCON_f.CDTL = hc32_sdcard_is_inserted() ? 1u : 0u;
#endif
}

static void sdcard_reset (void)
{
    memset(&sd_handle, 0, sizeof(sd_handle));
    sd_status = STA_NOINIT;
}

static void sdioc_init_pins (void)
{
    stc_port_init_t cfg = {0};

    if(sd_pins_ready)
        return;

    cfg.enPinDrv = Pin_Drv_M;
    PORT_Init(SDIOC_CK_PORT, SDIOC_CK_PIN, &cfg);

    PORT_SetFunc(SDIOC_D0_PORT, SDIOC_D0_PIN, Func_Sdio, Disable);
    PORT_SetFunc(SDIOC_D1_PORT, SDIOC_D1_PIN, Func_Sdio, Disable);
    PORT_SetFunc(SDIOC_D2_PORT, SDIOC_D2_PIN, Func_Sdio, Disable);
    PORT_SetFunc(SDIOC_D3_PORT, SDIOC_D3_PIN, Func_Sdio, Disable);
    PORT_SetFunc(SDIOC_CK_PORT, SDIOC_CK_PIN, Func_Sdio, Disable);
    PORT_SetFunc(SDIOC_CMD_PORT, SDIOC_CMD_PIN, Func_Sdio, Disable);

    sd_pins_ready = true;
}

bool hc32_sdcard_is_inserted (void)
{
#ifdef SD_DETECT_PIN
    return !hc32_gpio_read(SD_DETECT_PORT, SD_DETECT_PIN);
#else
    return true;
#endif
}

static en_result_t sdcard_init_host (void)
{
    static const struct {
        en_sdioc_bus_width_t width;
        en_sdioc_clk_freq_t clk;
    } attempts[] = {
        { SdiocBusWidth4Bit, SdiocClk25M },
        { SdiocBusWidth1Bit, SdiocClk25M },
        { SdiocBusWidth1Bit, SdiocClk400K }
    };
    stc_sdcard_init_t card_cfg = {
        .enBusWidth = SdiocBusWidth4Bit,
        .enClkFreq = SdiocClk25M,
        .enSpeedMode = SdiocNormalSpeedMode,
        .pstcInitCfg = NULL
    };
    en_result_t rc = Error;

    if(!hc32_sdcard_is_inserted()) {
        sd_status = STA_NOINIT | STA_NODISK;
        return Error;
    }

    if((sd_status & STA_NOINIT) == 0)
        return Ok;

    sdioc_init_pins();
    PWC_Fcg1PeriphClockCmd(SDIOC_CLOCK, Enable);

    memset(&sd_handle, 0, sizeof(sd_handle));
    for(uint_fast8_t i = 0; i < sizeof(attempts) / sizeof(attempts[0]); i++) {
        memset(&sd_handle, 0, sizeof(sd_handle));
        sd_handle.SDIOCx = SDIOC_UNIT;
        sd_handle.enDevMode = SdCardPollingMode;
        sd_handle.pstcDmaInitCfg = NULL;
        sdcard_configure_detect_source();

        card_cfg.enBusWidth = attempts[i].width;
        card_cfg.enClkFreq = attempts[i].clk;
        sd_bus_width = card_cfg.enBusWidth;
        sd_clk_freq = card_cfg.enClkFreq;

        rc = SDCARD_Init(&sd_handle, &card_cfg);
        if(rc == Ok)
            break;
    }

    if(rc != Ok) {
        sd_status = STA_NOINIT;
        return rc;
    }

    sd_status = 0;

    return Ok;
}

static DRESULT sdcard_transfer (bool write, LBA_t sector, UINT count, uint8_t *buffer)
{
    en_result_t rc;

    if(count == 0u)
        return RES_PARERR;

    if(sdcard_init_host() != Ok)
        return RES_NOTRDY;

    rc = write
        ? SDCARD_WriteBlocks(&sd_handle, (uint32_t)sector, (uint16_t)count, buffer, HC32_SD_IO_TIMEOUT_MS)
        : SDCARD_ReadBlocks(&sd_handle, (uint32_t)sector, (uint16_t)count, buffer, HC32_SD_IO_TIMEOUT_MS);

    return rc == Ok ? RES_OK : RES_ERROR;
}

static DRESULT sdcard_transfer_unaligned (bool write, LBA_t sector, UINT count, BYTE *buffer)
{
    size_t bytes = (size_t)count * HC32_SD_BLOCK_SIZE;
    uint8_t *raw = NULL, *aligned;
    DRESULT result;

    if((((uintptr_t)buffer) & 0x3u) == 0u)
        return sdcard_transfer(write, sector, count, buffer);

    raw = malloc(bytes + 3u);
    if(raw == NULL)
        return RES_ERROR;

    aligned = (uint8_t *)(((uintptr_t)raw + 3u) & ~(uintptr_t)0x3u);

    if(write)
        memcpy(aligned, buffer, bytes);

    result = sdcard_transfer(write, sector, count, aligned);

    if(result == RES_OK && !write)
        memcpy(buffer, aligned, bytes);

    free(raw);

    return result;
}

char *hc32_sdcard_mount (FATFS **fs)
{
    en_result_t init_result;
    FRESULT mount_result = FR_NOT_READY;

    if(fs != NULL)
        *fs = NULL;

    init_result = sdcard_init_host();

    if(init_result == Ok) {
        memset(&sd_fs, 0, sizeof(sd_fs));
        mount_result = f_mount(&sd_fs, "", 1);
        if(mount_result == FR_OK && fs != NULL)
            *fs = &sd_fs;
    }

    return mount_result == FR_OK ? "" : NULL;
}

bool hc32_sdcard_unmount (FATFS **fs)
{
    FRESULT rc = f_unmount("");

    if(fs != NULL)
        *fs = NULL;

    sdcard_reset();

    return rc == FR_OK;
}

DSTATUS disk_initialize (BYTE pdrv)
{
    if(pdrv != 0u)
        return STA_NOINIT;

    (void)sdcard_init_host();

    return sd_status;
}

DSTATUS disk_status (BYTE pdrv)
{
    if(pdrv != 0u)
        return STA_NOINIT;

    if(!hc32_sdcard_is_inserted())
        sd_status = STA_NOINIT | STA_NODISK;

    return sd_status;
}

DRESULT disk_read (BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if(pdrv != 0u || buff == NULL)
        return RES_PARERR;

    if(!hc32_sdcard_is_inserted())
        return RES_NOTRDY;

    return sdcard_transfer_unaligned(false, sector, count, buff);
}

#if FF_FS_READONLY == 0
DRESULT disk_write (BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if(pdrv != 0u || buff == NULL)
        return RES_PARERR;

    if(!hc32_sdcard_is_inserted())
        return RES_NOTRDY;

    return sdcard_transfer_unaligned(true, sector, count, (BYTE *)buff);
}
#endif

DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void *buff)
{
    if(pdrv != 0u)
        return RES_PARERR;

    if(!hc32_sdcard_is_inserted())
        return RES_NOTRDY;

    if(sdcard_init_host() != Ok)
        return RES_NOTRDY;

    switch(cmd) {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_COUNT:
            if(buff == NULL)
                return RES_PARERR;
            *(LBA_t *)buff = (LBA_t)sd_handle.stcSdCardInfo.u32LogBlockNbr;
            return RES_OK;

        case GET_SECTOR_SIZE:
            if(buff == NULL)
                return RES_PARERR;
            *(WORD *)buff = (WORD)sd_handle.stcSdCardInfo.u32LogBlockSize;
            return RES_OK;

        case GET_BLOCK_SIZE:
            if(buff == NULL)
                return RES_PARERR;
            *(DWORD *)buff = 1u;
            return RES_OK;

        default:
            return RES_PARERR;
    }
}

DWORD get_fattime (void)
{
    return ((DWORD)(2026u - 1980u) << 25)
         | ((DWORD)1u << 21)
         | ((DWORD)1u << 16);
}

#endif
