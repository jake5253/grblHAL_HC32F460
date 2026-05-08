/*
  irq.c - HC32F460 dynamic IRQ routing

  Part of grblHAL
*/

#include "driver.h"

static func_ptr_t irq_handlers[32] = {0};

bool hc32_irq_register (hc32_irq_registration_t registration)
{
    if(registration.irq < Int000_IRQn || registration.irq > Int031_IRQn)
        return false;

    stc_intc_sel_field_t *selector = (stc_intc_sel_field_t *)((uint32_t)(&M4_INTC->SEL0) + (4u * registration.irq));

    selector->INTSEL = registration.source;
    irq_handlers[registration.irq] = registration.handler;

    NVIC_SetPriority(registration.irq, registration.priority);
    NVIC_ClearPendingIRQ(registration.irq);
    NVIC_EnableIRQ(registration.irq);

    return true;
}

#define DEFINE_IRQ_HANDLER(n) \
    void IRQ##n##_Handler (void) { if(irq_handlers[Int##n##_IRQn]) irq_handlers[Int##n##_IRQn](); }

DEFINE_IRQ_HANDLER(000)
DEFINE_IRQ_HANDLER(001)
DEFINE_IRQ_HANDLER(002)
DEFINE_IRQ_HANDLER(003)
DEFINE_IRQ_HANDLER(004)
DEFINE_IRQ_HANDLER(005)
DEFINE_IRQ_HANDLER(006)
DEFINE_IRQ_HANDLER(007)
DEFINE_IRQ_HANDLER(008)
DEFINE_IRQ_HANDLER(009)
DEFINE_IRQ_HANDLER(010)
DEFINE_IRQ_HANDLER(011)
DEFINE_IRQ_HANDLER(012)
DEFINE_IRQ_HANDLER(013)
DEFINE_IRQ_HANDLER(014)
DEFINE_IRQ_HANDLER(015)
DEFINE_IRQ_HANDLER(016)
DEFINE_IRQ_HANDLER(017)
DEFINE_IRQ_HANDLER(018)
DEFINE_IRQ_HANDLER(019)
DEFINE_IRQ_HANDLER(020)
DEFINE_IRQ_HANDLER(021)
DEFINE_IRQ_HANDLER(022)
DEFINE_IRQ_HANDLER(023)
DEFINE_IRQ_HANDLER(024)
DEFINE_IRQ_HANDLER(025)
DEFINE_IRQ_HANDLER(026)
DEFINE_IRQ_HANDLER(027)
DEFINE_IRQ_HANDLER(028)
DEFINE_IRQ_HANDLER(029)
DEFINE_IRQ_HANDLER(030)
DEFINE_IRQ_HANDLER(031)
