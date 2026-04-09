/*
  flash.c - HC32F460 NVS backend helpers

  Part of grblHAL
*/

#include "driver.h"
#include "grbl/hal.h"
#include "grbl/crc.h"

#define RAM_FUNC_NOINLINE __RAM_FUNC __attribute__((noinline))
#define FLASH_WP_DISABLED_START 0u
#define FLASH_WP_DISABLED_END   0u
#define EFM_FAPRT_REG  (*(volatile uint32_t *)0x40010400UL)
#define EFM_FSTP_REG   (*(volatile uint32_t *)0x40010404UL)
#define EFM_FRMC_REG   (*(volatile uint32_t *)0x40010408UL)
#define EFM_FWMC_REG   (*(volatile uint32_t *)0x4001040CUL)
#define EFM_FSR_REG    (*(volatile uint32_t *)0x40010410UL)
#define EFM_FSCLR_REG  (*(volatile uint32_t *)0x40010414UL)
#define EFM_FSWP_REG   (*(volatile uint32_t *)0x4001041CUL)
#define EFM_FPMTSW_REG (*(volatile uint32_t *)0x40010420UL)
#define EFM_FPMTEW_REG (*(volatile uint32_t *)0x40010424UL)
#define EFM_FSTP_ENABLE  0u
#define EFM_FSTP_DISABLE 1u
#define EFM_FRMC_FLWT_MASK  (0x0FUL << 4)
#define EFM_FRMC_CACHE_MASK (1UL << 16)

#if EEPROM_ENABLE
#define EEPROM_DEVICE_ADDRESS  0xA0u
#define EEPROM_PAGE_SIZE       16u
#define EEPROM_SIZE_BYTES      2048u
#define EEPROM_WRITE_TIMEOUT_US 10000u

static bool eeprom_ready;

static void eeprom_delay (uint32_t us)
{
    uint32_t cycles_per_us = SystemCoreClock / 3000000u;

    if(cycles_per_us == 0u)
        cycles_per_us = 1u;

    while(us--) {
        for(volatile uint32_t i = 0; i < cycles_per_us; i++)
            __NOP();
    }
}

static void eeprom_sda_mode (bool output)
{
    stc_port_init_t cfg = {0};

    cfg.enPinMode = output ? Pin_Mode_Out : Pin_Mode_In;
    cfg.enPinDrv = Pin_Drv_H;
    cfg.enPinOType = Pin_OType_Od;
    cfg.enPullUp = Enable;
    cfg.enExInt = Disable;
    cfg.enPinSubFunc = Disable;

    PORT_SetFunc(EEPROM_SDA_PORT, EEPROM_SDA_PIN, Func_Gpio, Disable);
    PORT_Init(EEPROM_SDA_PORT, EEPROM_SDA_PIN, &cfg);
}

static void eeprom_scl_mode (void)
{
    stc_port_init_t cfg = {0};

    cfg.enPinMode = Pin_Mode_Out;
    cfg.enPinDrv = Pin_Drv_H;
    cfg.enPinOType = Pin_OType_Od;
    cfg.enPullUp = Enable;
    cfg.enExInt = Disable;
    cfg.enPinSubFunc = Disable;

    PORT_SetFunc(EEPROM_SCL_PORT, EEPROM_SCL_PIN, Func_Gpio, Disable);
    PORT_Init(EEPROM_SCL_PORT, EEPROM_SCL_PIN, &cfg);
}

static inline void eeprom_sda_write (bool high)
{
    hc32_gpio_write(EEPROM_SDA_PORT, EEPROM_SDA_PIN, high);
}

static inline void eeprom_scl_write (bool high)
{
    hc32_gpio_write(EEPROM_SCL_PORT, EEPROM_SCL_PIN, high);
}

static inline bool eeprom_sda_read (void)
{
    return hc32_gpio_read(EEPROM_SDA_PORT, EEPROM_SDA_PIN);
}

static void eeprom_init (void)
{
    if(eeprom_ready)
        return;

    eeprom_sda_mode(true);
    eeprom_scl_mode();
    eeprom_sda_write(true);
    eeprom_scl_write(true);
    eeprom_ready = true;
}

static void eeprom_start (void)
{
    eeprom_sda_mode(true);
    eeprom_sda_write(true);
    eeprom_scl_write(true);
    eeprom_delay(4);
    eeprom_sda_write(false);
    eeprom_delay(4);
    eeprom_scl_write(false);
}

static void eeprom_stop (void)
{
    eeprom_sda_mode(true);
    eeprom_scl_write(false);
    eeprom_sda_write(false);
    eeprom_delay(4);
    eeprom_scl_write(true);
    eeprom_sda_write(true);
    eeprom_delay(4);
}

static void eeprom_send_byte (uint8_t value)
{
    eeprom_sda_mode(true);
    eeprom_scl_write(false);

    for(uint_fast8_t bit = 0; bit < 8u; bit++) {
        eeprom_sda_write((value & 0x80u) != 0u);
        value <<= 1;
        eeprom_delay(2);
        eeprom_scl_write(true);
        eeprom_delay(2);
        eeprom_scl_write(false);
        eeprom_delay(2);
    }
}

static bool eeprom_wait_ack (void)
{
    uint32_t timeout = 0;

    eeprom_sda_mode(false);
    eeprom_sda_write(true);
    eeprom_delay(1);
    eeprom_scl_write(true);
    eeprom_delay(1);

    while(eeprom_sda_read()) {
        if(++timeout > 250u) {
            eeprom_stop();
            return false;
        }
    }

    eeprom_scl_write(false);
    eeprom_sda_mode(true);

    return true;
}

static void eeprom_ack (bool ack)
{
    eeprom_scl_write(false);
    eeprom_sda_mode(true);
    eeprom_sda_write(!ack);
    eeprom_delay(2);
    eeprom_scl_write(true);
    eeprom_delay(2);
    eeprom_scl_write(false);
}

static uint8_t eeprom_read_byte_raw (bool ack)
{
    uint8_t value = 0;

    eeprom_sda_mode(false);

    for(uint_fast8_t bit = 0; bit < 8u; bit++) {
        eeprom_scl_write(false);
        eeprom_delay(2);
        eeprom_scl_write(true);
        value <<= 1;
        if(eeprom_sda_read())
            value++;
        eeprom_delay(1);
    }

    eeprom_ack(ack);

    return value;
}

static uint8_t eeprom_devaddr (uint16_t addr, bool read)
{
    return (uint8_t)(EEPROM_DEVICE_ADDRESS + (((addr >> 8) & 0x07u) << 1) + (read ? 1u : 0u));
}

static bool eeprom_read (uint16_t addr, uint8_t *data, uint16_t size)
{
    if((uint32_t)addr + size > EEPROM_SIZE_BYTES)
        return false;

    eeprom_init();

    eeprom_start();
    eeprom_send_byte(eeprom_devaddr(addr, false));
    if(!eeprom_wait_ack())
        return false;
    eeprom_send_byte((uint8_t)(addr & 0xFFu));
    if(!eeprom_wait_ack())
        return false;
    eeprom_start();
    eeprom_send_byte(eeprom_devaddr(addr, true));
    if(!eeprom_wait_ack())
        return false;

    while(size) {
        size--;
        *data++ = eeprom_read_byte_raw(size != 0u);
    }

    eeprom_stop();

    return true;
}

static bool eeprom_wait_ready (uint16_t addr)
{
    uint32_t timeout = EEPROM_WRITE_TIMEOUT_US;

    while(timeout--) {
        eeprom_start();
        eeprom_send_byte(eeprom_devaddr(addr, false));

        if(eeprom_wait_ack()) {
            eeprom_stop();
            return true;
        }

        eeprom_delay(1u);
    }

    return false;
}

static bool eeprom_write_page (uint16_t addr, const uint8_t *data, uint16_t size)
{
    eeprom_start();
    eeprom_send_byte(eeprom_devaddr(addr, false));
    if(!eeprom_wait_ack())
        return false;
    eeprom_send_byte((uint8_t)(addr & 0xFFu));
    if(!eeprom_wait_ack())
        return false;

    while(size--) {
        eeprom_send_byte(*data++);
        if(!eeprom_wait_ack())
            return false;
    }

    eeprom_stop();

    return eeprom_wait_ready(addr);
}

static bool eeprom_write (uint16_t addr, const uint8_t *data, uint16_t size)
{
    if((uint32_t)addr + size > EEPROM_SIZE_BYTES)
        return false;

    eeprom_init();

    while(size) {
        uint16_t page_space = EEPROM_PAGE_SIZE - (addr % EEPROM_PAGE_SIZE);
        uint16_t chunk = size < page_space ? size : page_space;

        if(!eeprom_write_page(addr, data, chunk))
            return false;

        addr += chunk;
        data += chunk;
        size -= chunk;
    }

    return true;
}

bool memcpy_from_flash (uint8_t *dest)
{
    return eeprom_read(0u, dest, (uint16_t)hal.nvs.size);
}

bool memcpy_to_flash (uint8_t *source)
{
    return eeprom_write(0u, source, (uint16_t)hal.nvs.size);
}

uint8_t nvsGetByte (uint32_t addr)
{
    uint8_t value = 0xFFu;

    if(addr < hal.nvs.size)
        (void)eeprom_read((uint16_t)addr, &value, 1u);

    return value;
}

void nvsPutByte (uint32_t addr, uint8_t new_value)
{
    if(addr < hal.nvs.size && nvsGetByte(addr) != new_value)
        (void)eeprom_write((uint16_t)addr, &new_value, 1u);
}

bool nvsRead (uint8_t *dest, uint32_t source, uint32_t size, bool with_checksum)
{
    uint32_t total = size + (with_checksum ? NVS_CRC_BYTES : 0u);

    if((source + total) > hal.nvs.size)
        return false;

    if(!eeprom_read((uint16_t)source, dest, (uint16_t)size))
        return false;

    if(!with_checksum)
        return true;

#if NVS_CRC_BYTES > 1
    uint16_t stored;

    if(!eeprom_read((uint16_t)(source + size), (uint8_t *)&stored, sizeof(stored)))
        return false;

    return calc_checksum(dest, size) == stored;
#else
    uint8_t stored;

    if(!eeprom_read((uint16_t)(source + size), &stored, sizeof(stored)))
        return false;

    return calc_checksum(dest, size) == stored;
#endif
}

bool nvsWrite (uint32_t dest, uint8_t *source, uint32_t size, bool with_checksum)
{
    uint32_t total = size + (with_checksum ? NVS_CRC_BYTES : 0u);
    uint8_t buffer[size + NVS_CRC_BYTES];

    if((dest + total) > hal.nvs.size)
        return false;

    memcpy(buffer, source, size);

    if(with_checksum) {
#if NVS_CRC_BYTES > 1
        uint16_t checksum = calc_checksum(source, size);
        memcpy(&buffer[size], &checksum, sizeof(checksum));
#else
        buffer[size] = calc_checksum(source, size);
#endif
    }

    return eeprom_write((uint16_t)dest, buffer, (uint16_t)total);
}

#else
// ---------------------------------------------------------------------------
// Flash NVS — HC32F460
// Erase/program sequences match the on-chip bootloader exactly:
//   - unlock → clear stop → wait RDY → clear flags → disable cache
//   - PEMODE and PEMOD written as separate RMW bitfield writes
//   - PEMOD=4 for sector erase, PEMOD=1 for single-word program
//   - PEMOD=0 / PEMODE=0 torn down after every operation
//   - no FPMTSW / FPMTEW / FSWP interaction
//   - no BUSHLDCTL
//   - re-stop and re-lock after every operation
// ---------------------------------------------------------------------------

#define FLASH_OP_TIMEOUT    0x1000u

// FSR bits
#define FSR_RDY         (1u << 8)
#define FSR_EOP         (1u << 4)
#define FSR_ERR_MASK    (0x0000002Fu)   // WRPERR|PEPRTERR|PGSZERR|PGMISMTCH|RWERR

// FRMC bits
#define FRMC_CACHE_BIT  (1u << 16)
#define FRMC_FLWT_MASK  (0x000000F0u)
#define FRMC_LATENCY_4  (4u)

// FWMC bits
#define FWMC_PEMODE_BIT (1u << 0)
#define FWMC_PEMOD_MASK (0x07u << 4)
#define FWMC_PEMOD_SINGLE_PROGRAM  (1u << 4)
#define FWMC_PEMOD_SECTOR_ERASE    (4u << 4)

// ---------------------------------------------------------------------------
// Internal RAM helpers
// ---------------------------------------------------------------------------

static RAM_FUNC_NOINLINE bool efm_wait_rdy (void)
{
    uint32_t timeout = FLASH_OP_TIMEOUT;

    while ((EFM_FSR_REG & FSR_RDY) == 0u) {
        if (timeout-- == 0u)
            return false;
    }
    return true;
}

// Unlock EFM registers — two-word sequence, exactly as bootloader sub_1804
static RAM_FUNC_NOINLINE void efm_unlock (void)
{
    EFM_FAPRT_REG = 0x0123u;
    EFM_FAPRT_REG = 0x3210u;
}

// Lock EFM registers
static RAM_FUNC_NOINLINE void efm_lock (void)
{
    EFM_FAPRT_REG = 0x3210u;
    EFM_FAPRT_REG = 0x3210u;
}

// Clear Flash stop mode (enable operations) — bootloader sub_1660 with arg=1
static RAM_FUNC_NOINLINE void efm_cmd_enable (void)
{
    EFM_FSTP_REG = EFM_FSTP_ENABLE;
}

// ---------------------------------------------------------------------------
// Sector erase — mirrors bootloader sub_16e8 exactly
// addr must be the base address of the 8 KB sector (aligned to 0x2000)
// ---------------------------------------------------------------------------
static RAM_FUNC_NOINLINE bool efm_sector_erase (uint32_t addr)
{
    // Save and reconfigure FRMC: set latency=4, disable cache
    uint32_t frmc_saved = EFM_FRMC_REG;
    uint32_t frmc = frmc_saved & ~(FRMC_FLWT_MASK | FRMC_CACHE_BIT);
    frmc |= (FRMC_LATENCY_4 << 4);
    EFM_FRMC_REG = frmc;

    // Clear all sticky flags
    EFM_FSCLR_REG = 0x3Fu;

    // Enable P/E mode, then set erase mode — separate writes, as bootloader does
    EFM_FWMC_REG = (EFM_FWMC_REG & ~FWMC_PEMODE_BIT) | FWMC_PEMODE_BIT;
    EFM_FWMC_REG = (EFM_FWMC_REG & ~FWMC_PEMOD_MASK) | FWMC_PEMOD_SECTOR_ERASE;

    // Trigger erase — write any value to any aligned address in the sector
    *(volatile uint32_t *)addr = 0x12345678u;

    // Wait for completion
    bool ok = efm_wait_rdy();

    // Check for errors
    if (ok && (EFM_FSR_REG & FSR_ERR_MASK) != 0u)
        ok = false;

    // Tear down — clear EOP, then PEMOD=0, PEMODE=0, exactly as bootloader
    EFM_FSCLR_REG = FSR_EOP;
    EFM_FWMC_REG = (EFM_FWMC_REG & ~FWMC_PEMOD_MASK);
    EFM_FWMC_REG = (EFM_FWMC_REG & ~FWMC_PEMODE_BIT);

    // Restore FRMC
    EFM_FRMC_REG = frmc_saved;

    return ok;
}

// ---------------------------------------------------------------------------
// Single word program — mirrors bootloader sub_1788 exactly
// addr must be 4-byte aligned
// ---------------------------------------------------------------------------
static RAM_FUNC_NOINLINE bool efm_program_word (uint32_t addr, uint32_t value)
{
    // Save and reconfigure FRMC: set latency=4, disable cache
    uint32_t frmc_saved = EFM_FRMC_REG;
    uint32_t frmc = frmc_saved & ~(FRMC_FLWT_MASK | FRMC_CACHE_BIT);
    frmc |= (FRMC_LATENCY_4 << 4);
    EFM_FRMC_REG = frmc;

    // Clear all sticky flags
    EFM_FSCLR_REG = 0x3Fu;

    // Enable P/E mode, then set single-program mode — separate writes, as bootloader
    EFM_FWMC_REG = (EFM_FWMC_REG & ~FWMC_PEMODE_BIT) | FWMC_PEMODE_BIT;
    EFM_FWMC_REG = (EFM_FWMC_REG & ~FWMC_PEMOD_MASK) | FWMC_PEMOD_SINGLE_PROGRAM;

    // Write the word
    *(volatile uint32_t *)addr = value;

    // Wait for completion
    bool ok = efm_wait_rdy();

    // Check error flags
    if (ok && (EFM_FSR_REG & FSR_ERR_MASK) != 0u)
        ok = false;

    // Tear down — clear EOP, then PEMOD=0, PEMODE=0
    EFM_FSCLR_REG = FSR_EOP;
    EFM_FWMC_REG = (EFM_FWMC_REG & ~FWMC_PEMOD_MASK);
    EFM_FWMC_REG = (EFM_FWMC_REG & ~FWMC_PEMODE_BIT);

    // Restore FRMC
    EFM_FRMC_REG = frmc_saved;

    return ok;
}

// ---------------------------------------------------------------------------
// Session wrapper — unlock/enable once around a whole erase + rewrite cycle.
// This matches the bootloader flow more closely than reopening flash for every
// single programmed word.
// ---------------------------------------------------------------------------
static RAM_FUNC_NOINLINE bool efm_begin_session (uint32_t *primask)
{
    *primask = __get_PRIMASK();

    __disable_irq();
    __DSB();
    __ISB();

    efm_unlock();
    efm_cmd_enable();

    return efm_wait_rdy();
}

static RAM_FUNC_NOINLINE void efm_end_session (uint32_t primask)
{
    efm_lock();        // lock registers first — doesn't touch FSTP

    __DSB();
    __ISB();

    if (primask == 0u)
        __enable_irq();

    // DO NOT call efm_cmd_disable() here.
    // FSTP is the Flash stop bit. Setting it while about to return
    // into Flash execution will stop the bus before the first fetch.
    // The bootloader's top-level wrappers (sub_1818/sub_1842) do NOT
    // re-engage stop mode after programming — leave it cleared.
}

// ---------------------------------------------------------------------------
// NVS size helper
// ---------------------------------------------------------------------------
static uint32_t flash_nvs_size (void)
{
    uint32_t size = ((hal.nvs.size - 1u) | 0x03u) + 1u;

    return size <= HC32_FLASH_NVS_SIZE ? size : 0u;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Erase the NVS sector then write source data back word by word.
static bool flash_nvs_store (uint8_t const *source)
{
    uint32_t size = flash_nvs_size();
    uint32_t primask;
    bool ok = false;

    if (size == 0u)
        return false;

    if(!efm_begin_session(&primask))
        return false;

    do {
        if(!efm_sector_erase(HC32_FLASH_NVS_BASE))
            break;

        ok = true;

        // Program one word at a time — pad the last partial word with 0xFF
        for(uint32_t offset = 0u; offset < size; offset += 4u) {
            uint32_t word = 0xFFFFFFFFu;
            uint32_t chunk = size - offset;

            if(chunk > 4u)
                chunk = 4u;

            for(uint32_t b = 0u; b < chunk; b++)
                ((uint8_t *)&word)[b] = source[offset + b];

            if(!efm_program_word(HC32_FLASH_NVS_BASE + offset, word)) {
                ok = false;
                break;
            }
        }
    } while(false);

    efm_end_session(primask);

    return ok;
}

bool memcpy_from_flash (uint8_t *dest)
{
    uint32_t size = flash_nvs_size();

    if (size == 0u)
        return false;

    memcpy(dest, (void const *)HC32_FLASH_NVS_BASE, size);
    return true;
}

bool memcpy_to_flash (uint8_t *source)
{
    return flash_nvs_store(source);
}

uint8_t nvsGetByte (uint32_t addr)
{
    uint32_t size = flash_nvs_size();

    return addr < size ? *((uint8_t const *)(HC32_FLASH_NVS_BASE + addr)) : 0xFFu;
}

void nvsPutByte (uint32_t addr, uint8_t new_value)
{
    // Byte-granular writes are not supported on Flash without a full erase cycle.
    // Use nvsWrite() to update a region atomically.
    (void)addr;
    (void)new_value;
}

bool flash_nvs_is_valid (void)
{
    uint32_t size = flash_nvs_size();
    settings_t stored;
    uint16_t stored_checksum;
    uint16_t computed_checksum;

    if (size < (NVS_ADDR_GLOBAL + sizeof(settings_t) + NVS_CRC_BYTES))
        return false;

    if (*((uint8_t const *)HC32_FLASH_NVS_BASE) != SETTINGS_VERSION)
        return false;

    memcpy(&stored,
           (void const *)(HC32_FLASH_NVS_BASE + NVS_ADDR_GLOBAL),
           sizeof(settings_t));
    memcpy(&stored_checksum,
           (void const *)(HC32_FLASH_NVS_BASE + NVS_ADDR_GLOBAL + sizeof(settings_t)),
           sizeof(stored_checksum));

    computed_checksum = calc_checksum((uint8_t const *)&stored, sizeof(settings_t));

    return stored.version.id == SETTINGS_VERSION && stored_checksum == computed_checksum;
}

bool nvsRead (uint8_t *dest, uint32_t source, uint32_t size, bool with_checksum)
{
    uint32_t total = size + (with_checksum ? NVS_CRC_BYTES : 0u);

    if ((source + total) > hal.nvs.size)
        return false;

    memcpy(dest, (void const *)(HC32_FLASH_NVS_BASE + source), size);

    if (!with_checksum)
        return true;

#if NVS_CRC_BYTES > 1
    uint16_t stored;
    memcpy(&stored,
           (void const *)(HC32_FLASH_NVS_BASE + source + size),
           sizeof(stored));
    return calc_checksum(dest, size) == stored;
#else
    return calc_checksum(dest, size) ==
           *((uint8_t const *)(HC32_FLASH_NVS_BASE + source + size));
#endif
}

bool nvsWrite (uint32_t dest, uint8_t *source, uint32_t size, bool with_checksum)
{
    uint32_t total    = size + (with_checksum ? NVS_CRC_BYTES : 0u);
    uint32_t nvs_size = flash_nvs_size();

    if (nvs_size == 0u || (dest + total) > nvs_size)
        return false;

    // Shadow the entire NVS sector in a stack buffer, patch the target region,
    // then erase-and-rewrite the whole sector.
    uint8_t buffer[nvs_size];

    memcpy(buffer, (void const *)HC32_FLASH_NVS_BASE, nvs_size);
    memcpy(&buffer[dest], source, size);

    if (with_checksum) {
#if NVS_CRC_BYTES > 1
        uint16_t checksum = calc_checksum(source, size);
        memcpy(&buffer[dest + size], &checksum, sizeof(checksum));
#else
        buffer[dest + size] = calc_checksum(source, size);
#endif
    }

    return flash_nvs_store(buffer);
}

#endif
