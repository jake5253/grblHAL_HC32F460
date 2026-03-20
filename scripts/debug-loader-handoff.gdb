target extended-remote localhost:3333
monitor reset halt
set {unsigned int}0xE000ED08 = 0x0000C000
set $sp = *((unsigned int *)0x0000C000)
set $pc = *((unsigned int *)0x0000C004)
hbreak main
hbreak driver_init
hbreak HardFault_Handler
hbreak MemManage_Handler
hbreak BusFault_Handler
hbreak UsageFault_Handler
continue
bt
info registers
x/8i $pc
quit
