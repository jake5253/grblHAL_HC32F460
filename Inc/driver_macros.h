/*
  driver_macros.h - HAL abstraction macros for HC32F460 grblHAL driver

  Part of grblHAL
*/

#ifndef _DRIVER_MACROS_H_
#define _DRIVER_MACROS_H_

#define timer(t)    M4_TMRA ## t
#define timerINT(t) INT_TMRA ## t

#define usart(t)    M4_USART ## t
#define usartINT(t) INT_USART ## t

#define DIGITAL_IN(port, pin)      hc32_gpio_read(port, pin)
#define DIGITAL_OUT(port, pin, on) hc32_gpio_write(port, pin, on)

#endif /* _DRIVER_MACROS_H_ */
