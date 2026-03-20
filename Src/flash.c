/*
  flash.c - HC32F460 external EEPROM-backed NVS

  Part of grblHAL
*/

#include "driver.h"
#include "grbl/hal.h"
#include "grbl/crc.h"
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
