/*
  serial.h - HC32F460 serial interface

  Part of grblHAL
*/

#pragma once

#include <stdint.h>

#include "grbl/stream.h"

const io_stream_t *serialInit (uint32_t baud_rate);
void serialEnableRxInterrupt (void);
