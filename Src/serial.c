/*
  serial.c - HC32F460 UART stream implementation

  Part of grblHAL
*/

#include "driver.h"
#include "serial.h"

#include "grbl/hal.h"
#include "grbl/protocol.h"
#include "grbl/stream.h"
#include "main.h"

static stream_rx_buffer_t rxbuffer = {0};
static stream_tx_buffer_t txbuffer = {0};
static enqueue_realtime_command_ptr enqueue_realtime_command = protocol_enqueue_realtime_command;
static volatile bool tx_active = false;

static inline uint16_t serialRxCount (void)
{
    uint_fast16_t head = rxbuffer.head, tail = rxbuffer.tail;

    return BUFCOUNT(head, tail, RX_BUFFER_SIZE);
}

static uint16_t serialRxFree (void)
{
    return (RX_BUFFER_SIZE - 1u) - serialRxCount();
}

static int32_t serialGetC (void)
{
    uint_fast16_t tail = rxbuffer.tail;

    if(tail == rxbuffer.head)
        return SERIAL_NO_DATA;

    uint8_t data = rxbuffer.data[tail];
    rxbuffer.tail = (tail + 1u) & (RX_BUFFER_SIZE - 1u);

    return (int16_t)data;
}

static void serialRxFlush (void)
{
    rxbuffer.tail = rxbuffer.head;
    rxbuffer.overflow = false;
}

static void serialRxCancel (void)
{
    serialRxFlush();
    rxbuffer.data[rxbuffer.head] = ASCII_CAN;
    rxbuffer.head = (rxbuffer.head + 1u) & (RX_BUFFER_SIZE - 1u);
}

static bool serialPutC (const uint8_t c)
{
    uint_fast16_t next_head = (txbuffer.head + 1u) & (TX_BUFFER_SIZE - 1u);

    while(txbuffer.tail == next_head) {
        if(!hal.stream_blocking_callback())
            return false;

        next_head = (txbuffer.head + 1u) & (TX_BUFFER_SIZE - 1u);
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    txbuffer.data[txbuffer.head] = c;
    txbuffer.head = next_head;

    if(!tx_active) {
        tx_active = true;
        USART_FuncCmd(SERIAL_PORT_USART, UsartTxCmpltInt, Disable);
        USART_FuncCmd(SERIAL_PORT_USART, UsartTxAndTxEmptyInt, Enable);
    }

    if(!primask)
        __enable_irq();

    return true;
}

static void serialWriteS (const char *s)
{
    while(*s)
        serialPutC(*s++);
}

static void serialWrite (const uint8_t *s, uint16_t length)
{
    while(length--)
        serialPutC(*s++);
}

static uint16_t serialTxCount (void)
{
    uint_fast16_t head = txbuffer.head, tail = txbuffer.tail;

    return BUFCOUNT(head, tail, TX_BUFFER_SIZE) + (tx_active ? 1u : 0u);
}

static void serialTxFlush (void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    USART_FuncCmd(SERIAL_PORT_USART, UsartTxEmptyInt, Disable);
    USART_FuncCmd(SERIAL_PORT_USART, UsartTxCmpltInt, Disable);
    USART_FuncCmd(SERIAL_PORT_USART, UsartTx, Disable);
    txbuffer.tail = txbuffer.head;
    tx_active = false;

    if(!primask)
        __enable_irq();
}

static bool serialDisable (bool disable)
{
    USART_FuncCmd(SERIAL_PORT_USART, UsartRxInt, disable ? Disable : Enable);

    return true;
}

static bool serialSuspendInput (bool await)
{
    return stream_rx_suspend(&rxbuffer, await);
}

static bool serialSetBaudRate (uint32_t baud_rate)
{
    return USART_SetBaudrate(SERIAL_PORT_USART, baud_rate) == Ok;
}

static bool serialEnqueueRtCommand (uint8_t c)
{
    return enqueue_realtime_command(c);
}

static enqueue_realtime_command_ptr serialSetRtHandler (enqueue_realtime_command_ptr handler)
{
    enqueue_realtime_command_ptr previous = enqueue_realtime_command;

    enqueue_realtime_command = handler ? handler : protocol_enqueue_realtime_command;

    return previous;
}

static void serialRxIRQ (void)
{
    uint8_t data = (uint8_t)USART_RecData(SERIAL_PORT_USART);

    if(data == CMD_RESET || data == CMD_STOP ||
       data == CMD_STATUS_REPORT || data == CMD_STATUS_REPORT_LEGACY ||
       data == CMD_CYCLE_START || data == CMD_CYCLE_START_LEGACY ||
       data == CMD_FEED_HOLD || data == CMD_FEED_HOLD_LEGACY ||
       (data >= 0x80u && data <= 0xBFu)) {
        enqueue_realtime_command(data);
        return;
    }

    uint_fast16_t next_head = (rxbuffer.head + 1u) & (RX_BUFFER_SIZE - 1u);

    if(next_head == rxbuffer.tail) {
        rxbuffer.overflow = true;
        return;
    }

    rxbuffer.data[rxbuffer.head] = data;
    rxbuffer.head = next_head;
}

static void serialErrorIRQ (void)
{
    if(USART_GetStatus(SERIAL_PORT_USART, UsartFrameErr) == Set)
        USART_ClearStatus(SERIAL_PORT_USART, UsartFrameErr);

    if(USART_GetStatus(SERIAL_PORT_USART, UsartOverrunErr) == Set)
        USART_ClearStatus(SERIAL_PORT_USART, UsartOverrunErr);
}

static void serialTxIRQ (void)
{
    uint_fast16_t tail = txbuffer.tail;

    if(txbuffer.head != tail) {
        USART_SendData(SERIAL_PORT_USART, txbuffer.data[tail]);
        txbuffer.tail = (tail + 1u) & (TX_BUFFER_SIZE - 1u);
    } else {
        USART_FuncCmd(SERIAL_PORT_USART, UsartTxCmpltInt, Enable);
        USART_FuncCmd(SERIAL_PORT_USART, UsartTxEmptyInt, Disable);
    }
}

static void serialTxCompleteIRQ (void)
{
    USART_FuncCmd(SERIAL_PORT_USART, UsartTx, Disable);
    USART_FuncCmd(SERIAL_PORT_USART, UsartTxCmpltInt, Disable);
    tx_active = false;
}

void serialEnableRxInterrupt (void)
{
    USART_FuncCmd(SERIAL_PORT_USART, UsartRx, Enable);
    USART_FuncCmd(SERIAL_PORT_USART, UsartRxInt, Enable);
}

const io_stream_t *serialInit (uint32_t baud_rate)
{
    static const io_stream_t stream = {
        .type = StreamType_Serial,
        .instance = 0,
        .is_connected = stream_connected,
        .get_rx_buffer_free = serialRxFree,
        .write = serialWriteS,
        .write_all = serialWriteS,
        .write_char = serialPutC,
        .enqueue_rt_command = serialEnqueueRtCommand,
        .read = serialGetC,
        .reset_read_buffer = serialRxFlush,
        .cancel_read_buffer = serialRxCancel,
        .set_enqueue_rt_handler = serialSetRtHandler,
        .suspend_read = serialSuspendInput,
        .write_n = serialWrite,
        .disable_rx = serialDisable,
        .get_rx_buffer_count = serialRxCount,
        .get_tx_buffer_count = serialTxCount,
        .reset_write_buffer = serialTxFlush,
        .set_baud_rate = serialSetBaudRate
    };

    PWC_Fcg1PeriphClockCmd(SERIAL_PORT_CLOCKS, Enable);

    PORT_SetFunc(SERIAL_PORT_RX, SERIAL_PORT_RX_PIN, SERIAL_PORT_RX_FUNC, Disable);
    PORT_SetFunc(SERIAL_PORT_TX, SERIAL_PORT_TX_PIN, SERIAL_PORT_TX_FUNC, Disable);

    USART_DeInit(SERIAL_PORT_USART);
    SERIAL_PORT_USART->CR1 = 0u;
    SERIAL_PORT_USART->CR2 = 0u;
    SERIAL_PORT_USART->CR3 = 0u;
    SERIAL_PORT_USART->PR = 0u;
    txbuffer.head = txbuffer.tail = 0u;
    tx_active = false;

    USART_SetBaudrate(SERIAL_PORT_USART, baud_rate);

    static const hc32_irq_registration_t rx_irq = {
        .source = SERIAL_PORT_RI,
        .irq = Int000_IRQn,
        .handler = serialRxIRQ,
        .priority = DDL_IRQ_PRIORITY_05
    };
    static const hc32_irq_registration_t err_irq = {
        .source = SERIAL_PORT_EI,
        .irq = Int001_IRQn,
        .handler = serialErrorIRQ,
        .priority = DDL_IRQ_PRIORITY_05
    };
    static const hc32_irq_registration_t tx_irq = {
        .source = SERIAL_PORT_TI,
        .irq = Int002_IRQn,
        .handler = serialTxIRQ,
        .priority = DDL_IRQ_PRIORITY_05
    };
    static const hc32_irq_registration_t txc_irq = {
        .source = SERIAL_PORT_TCI,
        .irq = Int003_IRQn,
        .handler = serialTxCompleteIRQ,
        .priority = DDL_IRQ_PRIORITY_05
    };

    hc32_irq_register(rx_irq);
    hc32_irq_register(err_irq);
    hc32_irq_register(tx_irq);
    hc32_irq_register(txc_irq);

    return &stream;
}
