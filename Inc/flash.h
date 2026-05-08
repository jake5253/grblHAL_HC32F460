/*
  flash.h - HC32F460 flash-backed NVS helpers

  Part of grblHAL
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

bool memcpy_from_flash (uint8_t *dest);
bool memcpy_to_flash (uint8_t *source);
uint8_t nvsGetByte (uint32_t addr);
void nvsPutByte (uint32_t addr, uint8_t new_value);
bool nvsRead (uint8_t *dest, uint32_t source, uint32_t size, bool with_checksum);
bool nvsWrite (uint32_t dest, uint8_t *source, uint32_t size, bool with_checksum);
