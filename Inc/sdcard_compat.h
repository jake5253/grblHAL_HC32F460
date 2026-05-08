#pragma once

#include "hc32_ddl.h"

#ifndef PWC_FCG0_PERIPH_AOS
#define PWC_FCG0_PERIPH_AOS ((uint32_t)0x00020000u)
#endif

#ifndef SdiocModeSD
typedef enum en_sdioc_mode
{
    SdiocModeSD = 0u,
    SdiocModeIO = 1u,
} en_sdioc_mode_t;

__STATIC_INLINE void SDIOC_SetMode (const M4_SDIOC_TypeDef *SDIOCx, en_sdioc_mode_t enMode)
{
    (void)SDIOCx;
    (void)enMode;
}
#endif
