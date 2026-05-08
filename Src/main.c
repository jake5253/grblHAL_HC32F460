/*
  main.c - HC32F460 grblHAL driver entry point

  Part of grblHAL
*/

#include "main.h"
#include "grbl/grbllib.h"

int main (void)
{
    SystemCoreClockUpdate();

    SCB->VTOR = HC32_FLASH_APP_START;
    __DSB();
    __ISB();

    if(!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }

    return grbl_enter();
}

void Error_Handler (void)
{
    __disable_irq();

    for(;;) {
    }
}
