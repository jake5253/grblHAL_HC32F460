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

#if MPG_ENABLE == 2 || KEYPAD_ENABLE == 2
#define SERIAL_HAS_AUX_UART 1
#else
#define SERIAL_HAS_AUX_UART 0
#endif

typedef struct {
    M4_USART_TypeDef *uart;
    uint32_t clock_gate;
    en_port_t rx_port;
    uint16_t rx_pin;
    en_port_func_t rx_func;
    en_port_t tx_port;
    uint16_t tx_pin;
    en_port_func_t tx_func;
    en_int_src_t ri;
    en_int_src_t ti;
    en_int_src_t ei;
    en_int_src_t tci;
    IRQn_Type rx_irq;
    IRQn_Type err_irq;
    IRQn_Type tx_irq;
    IRQn_Type txc_irq;
    uint8_t instance;
    stream_rx_buffer_t rxbuffer;
    stream_tx_buffer_t txbuffer;
    enqueue_realtime_command_ptr enqueue_realtime_command;
    volatile bool tx_active;
    io_stream_status_t status;
} hc32_serial_port_t;

static hc32_serial_port_t serial0 = {
    .uart = SERIAL_PORT_USART,
    .clock_gate = SERIAL_PORT_CLOCKS,
    .rx_port = SERIAL_PORT_RX,
    .rx_pin = SERIAL_PORT_RX_PIN,
    .rx_func = SERIAL_PORT_RX_FUNC,
    .tx_port = SERIAL_PORT_TX,
    .tx_pin = SERIAL_PORT_TX_PIN,
    .tx_func = SERIAL_PORT_TX_FUNC,
    .ri = SERIAL_PORT_RI,
    .ti = SERIAL_PORT_TI,
    .ei = SERIAL_PORT_EI,
    .tci = SERIAL_PORT_TCI,
    .rx_irq = Int000_IRQn,
    .err_irq = Int001_IRQn,
    .tx_irq = Int002_IRQn,
    .txc_irq = Int003_IRQn,
    .instance = 0
};

#if SERIAL_HAS_AUX_UART
static hc32_serial_port_t serial1 = {
    .uart = SERIAL_AUX_PORT_USART,
    .clock_gate = SERIAL_AUX_PORT_CLOCKS,
    .rx_port = SERIAL_AUX_PORT_RX,
    .rx_pin = SERIAL_AUX_PORT_RX_PIN,
    .rx_func = SERIAL_AUX_PORT_RX_FUNC,
    .tx_port = SERIAL_AUX_PORT_TX,
    .tx_pin = SERIAL_AUX_PORT_TX_PIN,
    .tx_func = SERIAL_AUX_PORT_TX_FUNC,
    .ri = SERIAL_AUX_PORT_RI,
    .ti = SERIAL_AUX_PORT_TI,
    .ei = SERIAL_AUX_PORT_EI,
    .tci = SERIAL_AUX_PORT_TCI,
    .rx_irq = Int004_IRQn,
    .err_irq = Int005_IRQn,
    .tx_irq = Int006_IRQn,
    .txc_irq = Int007_IRQn,
    .instance = AUX_UART_STREAM
};
#endif

static inline uint16_t serial_rx_count (hc32_serial_port_t *serial)
{
    uint_fast16_t head = serial->rxbuffer.head, tail = serial->rxbuffer.tail;

    return BUFCOUNT(head, tail, RX_BUFFER_SIZE);
}

static uint16_t serial_rx_free (hc32_serial_port_t *serial)
{
    return (RX_BUFFER_SIZE - 1u) - serial_rx_count(serial);
}

static int32_t serial_get_c (hc32_serial_port_t *serial)
{
    uint_fast16_t tail = serial->rxbuffer.tail;

    if(tail == serial->rxbuffer.head)
        return SERIAL_NO_DATA;

    uint8_t data = serial->rxbuffer.data[tail];
    serial->rxbuffer.tail = (tail + 1u) & (RX_BUFFER_SIZE - 1u);

    return (int16_t)data;
}

static void serial_rx_flush (hc32_serial_port_t *serial)
{
    serial->rxbuffer.tail = serial->rxbuffer.head;
    serial->rxbuffer.overflow = false;
}

static void serial_rx_cancel (hc32_serial_port_t *serial)
{
    serial_rx_flush(serial);
    serial->rxbuffer.data[serial->rxbuffer.head] = ASCII_CAN;
    serial->rxbuffer.head = (serial->rxbuffer.head + 1u) & (RX_BUFFER_SIZE - 1u);
}

static bool serial_put_c (hc32_serial_port_t *serial, const uint8_t c)
{
    uint_fast16_t next_head = (serial->txbuffer.head + 1u) & (TX_BUFFER_SIZE - 1u);

    while(serial->txbuffer.tail == next_head) {
        if(!hal.stream_blocking_callback())
            return false;

        next_head = (serial->txbuffer.head + 1u) & (TX_BUFFER_SIZE - 1u);
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    serial->txbuffer.data[serial->txbuffer.head] = c;
    serial->txbuffer.head = next_head;

    if(!serial->tx_active) {
        serial->tx_active = true;
        USART_FuncCmd(serial->uart, UsartTxCmpltInt, Disable);
        USART_FuncCmd(serial->uart, UsartTxAndTxEmptyInt, Enable);
    }

    if(!primask)
        __enable_irq();

    return true;
}

static void serial_write_s (hc32_serial_port_t *serial, const char *s)
{
    while(*s)
        serial_put_c(serial, (uint8_t)*s++);
}

static void serial_write (hc32_serial_port_t *serial, const uint8_t *s, uint16_t length)
{
    while(length--)
        serial_put_c(serial, *s++);
}

static uint16_t serial_tx_count (hc32_serial_port_t *serial)
{
    uint_fast16_t head = serial->txbuffer.head, tail = serial->txbuffer.tail;

    return BUFCOUNT(head, tail, TX_BUFFER_SIZE) + (serial->tx_active ? 1u : 0u);
}

static void serial_tx_flush (hc32_serial_port_t *serial)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    USART_FuncCmd(serial->uart, UsartTxEmptyInt, Disable);
    USART_FuncCmd(serial->uart, UsartTxCmpltInt, Disable);
    USART_FuncCmd(serial->uart, UsartTx, Disable);
    serial->txbuffer.tail = serial->txbuffer.head;
    serial->tx_active = false;

    if(!primask)
        __enable_irq();
}

static bool serial_disable (hc32_serial_port_t *serial, bool disable)
{
    USART_FuncCmd(serial->uart, UsartRxInt, disable ? Disable : Enable);

    return true;
}

static bool serial_suspend_input (hc32_serial_port_t *serial, bool await)
{
    return stream_rx_suspend(&serial->rxbuffer, await);
}

static bool serial_set_baud_rate (hc32_serial_port_t *serial, uint32_t baud_rate)
{
    bool ok = USART_SetBaudrate(serial->uart, baud_rate) == Ok;

    if(ok)
        serial->status.baud_rate = baud_rate;

    return ok;
}

static bool serial_enqueue_rt_command (hc32_serial_port_t *serial, uint8_t c)
{
    return serial->enqueue_realtime_command(c);
}

static enqueue_realtime_command_ptr serial_set_rt_handler (hc32_serial_port_t *serial, enqueue_realtime_command_ptr handler)
{
    enqueue_realtime_command_ptr previous = serial->enqueue_realtime_command;

    serial->enqueue_realtime_command = handler ? handler : protocol_enqueue_realtime_command;

    return previous;
}

static void serial_rx_irq (hc32_serial_port_t *serial)
{
    uint8_t data = (uint8_t)USART_RecData(serial->uart);

    if(data == CMD_RESET || data == CMD_STOP ||
       data == CMD_STATUS_REPORT || data == CMD_STATUS_REPORT_LEGACY ||
       data == CMD_CYCLE_START || data == CMD_CYCLE_START_LEGACY ||
       data == CMD_FEED_HOLD || data == CMD_FEED_HOLD_LEGACY ||
       (data >= 0x80u && data <= 0xBFu)) {
        serial->enqueue_realtime_command(data);
        return;
    }

    uint_fast16_t next_head = (serial->rxbuffer.head + 1u) & (RX_BUFFER_SIZE - 1u);

    if(next_head == serial->rxbuffer.tail) {
        serial->rxbuffer.overflow = true;
        return;
    }

    serial->rxbuffer.data[serial->rxbuffer.head] = data;
    serial->rxbuffer.head = next_head;
}

static void serial_error_irq (hc32_serial_port_t *serial)
{
    if(USART_GetStatus(serial->uart, UsartFrameErr) == Set)
        USART_ClearStatus(serial->uart, UsartFrameErr);

    if(USART_GetStatus(serial->uart, UsartOverrunErr) == Set)
        USART_ClearStatus(serial->uart, UsartOverrunErr);
}

static void serial_tx_irq (hc32_serial_port_t *serial)
{
    uint_fast16_t tail = serial->txbuffer.tail;

    if(serial->txbuffer.head != tail) {
        USART_SendData(serial->uart, serial->txbuffer.data[tail]);
        serial->txbuffer.tail = (tail + 1u) & (TX_BUFFER_SIZE - 1u);
    } else {
        USART_FuncCmd(serial->uart, UsartTxCmpltInt, Enable);
        USART_FuncCmd(serial->uart, UsartTxEmptyInt, Disable);
    }
}

static void serial_tx_complete_irq (hc32_serial_port_t *serial)
{
    USART_FuncCmd(serial->uart, UsartTx, Disable);
    USART_FuncCmd(serial->uart, UsartTxCmpltInt, Disable);
    serial->tx_active = false;
}

#define DEFINE_SERIAL_WRAPPERS(N, SERIAL_REF) \
static uint16_t serial##N##RxFree (void) { return serial_rx_free(&(SERIAL_REF)); } \
static int32_t serial##N##GetC (void) { return serial_get_c(&(SERIAL_REF)); } \
static void serial##N##RxFlush (void) { serial_rx_flush(&(SERIAL_REF)); } \
static void serial##N##RxCancel (void) { serial_rx_cancel(&(SERIAL_REF)); } \
static bool serial##N##PutC (const uint8_t c) { return serial_put_c(&(SERIAL_REF), c); } \
static void serial##N##WriteS (const char *s) { serial_write_s(&(SERIAL_REF), s); } \
static void serial##N##Write (const uint8_t *s, uint16_t length) { serial_write(&(SERIAL_REF), s, length); } \
static uint16_t serial##N##TxCount (void) { return serial_tx_count(&(SERIAL_REF)); } \
static void serial##N##TxFlush (void) { serial_tx_flush(&(SERIAL_REF)); } \
static bool serial##N##Disable (bool disable) { return serial_disable(&(SERIAL_REF), disable); } \
static bool serial##N##SuspendInput (bool await) { return serial_suspend_input(&(SERIAL_REF), await); } \
static bool serial##N##SetBaudRate (uint32_t baud_rate) { return serial_set_baud_rate(&(SERIAL_REF), baud_rate); } \
static bool serial##N##EnqueueRtCommand (uint8_t c) { return serial_enqueue_rt_command(&(SERIAL_REF), c); } \
static enqueue_realtime_command_ptr serial##N##SetRtHandler (enqueue_realtime_command_ptr handler) { return serial_set_rt_handler(&(SERIAL_REF), handler); } \
static uint16_t serial##N##RxCount (void) { return serial_rx_count(&(SERIAL_REF)); } \
static void serial##N##RxIRQ (void) { serial_rx_irq(&(SERIAL_REF)); } \
static void serial##N##ErrorIRQ (void) { serial_error_irq(&(SERIAL_REF)); } \
static void serial##N##TxIRQ (void) { serial_tx_irq(&(SERIAL_REF)); } \
static void serial##N##TxCompleteIRQ (void) { serial_tx_complete_irq(&(SERIAL_REF)); }

DEFINE_SERIAL_WRAPPERS(0, serial0)
#if SERIAL_HAS_AUX_UART
DEFINE_SERIAL_WRAPPERS(1, serial1)
#endif

static const io_stream_t serial0_stream = {
    .type = StreamType_Serial,
    .instance = 0,
    .is_connected = stream_connected,
    .get_rx_buffer_free = serial0RxFree,
    .write = serial0WriteS,
    .write_all = serial0WriteS,
    .write_char = serial0PutC,
    .enqueue_rt_command = serial0EnqueueRtCommand,
    .read = serial0GetC,
    .reset_read_buffer = serial0RxFlush,
    .cancel_read_buffer = serial0RxCancel,
    .set_enqueue_rt_handler = serial0SetRtHandler,
    .suspend_read = serial0SuspendInput,
    .write_n = serial0Write,
    .disable_rx = serial0Disable,
    .get_rx_buffer_count = serial0RxCount,
    .get_tx_buffer_count = serial0TxCount,
    .reset_write_buffer = serial0TxFlush,
    .set_baud_rate = serial0SetBaudRate
};

#if SERIAL_HAS_AUX_UART
static const io_stream_t serial1_stream = {
    .type = StreamType_Serial,
    .instance = AUX_UART_STREAM,
    .is_connected = stream_connected,
    .get_rx_buffer_free = serial1RxFree,
    .write = serial1WriteS,
    .write_all = serial1WriteS,
    .write_char = serial1PutC,
    .enqueue_rt_command = serial1EnqueueRtCommand,
    .read = serial1GetC,
    .reset_read_buffer = serial1RxFlush,
    .cancel_read_buffer = serial1RxCancel,
    .set_enqueue_rt_handler = serial1SetRtHandler,
    .suspend_read = serial1SuspendInput,
    .write_n = serial1Write,
    .disable_rx = serial1Disable,
    .get_rx_buffer_count = serial1RxCount,
    .get_tx_buffer_count = serial1TxCount,
    .reset_write_buffer = serial1TxFlush,
    .set_baud_rate = serial1SetBaudRate
};

static io_stream_properties_t serial_streams[] = {
    {
        .type = StreamType_Serial,
        .instance = AUX_UART_STREAM,
        .flags.claimable = On,
        .flags.claimed = Off,
        .flags.can_set_baud = On,
        .flags.modbus_ready = On,
    }
};
#endif

static const io_stream_t *serial_init_port (hc32_serial_port_t *serial, const io_stream_t *stream, uint32_t baud_rate,
                                            void (*rx_handler)(void), void (*err_handler)(void),
                                            void (*tx_handler)(void), void (*txc_handler)(void))
{
    PWC_Fcg1PeriphClockCmd(serial->clock_gate, Enable);

    PORT_SetFunc(serial->rx_port, serial->rx_pin, serial->rx_func, Disable);
    PORT_SetFunc(serial->tx_port, serial->tx_pin, serial->tx_func, Disable);

    USART_DeInit(serial->uart);
    serial->uart->CR1 = 0u;
    serial->uart->CR2 = 0u;
    serial->uart->CR3 = 0u;
    serial->uart->PR = 0u;

    serial->rxbuffer.head = serial->rxbuffer.tail = 0u;
    serial->rxbuffer.overflow = false;
    serial->txbuffer.head = serial->txbuffer.tail = 0u;
    serial->tx_active = false;
    serial->enqueue_realtime_command = protocol_enqueue_realtime_command;
    serial->status.baud_rate = baud_rate;
    serial->status.flags.can_set_baud = On;
    serial->status.flags.modbus_ready = On;

    USART_SetBaudrate(serial->uart, baud_rate);

    hc32_irq_register((hc32_irq_registration_t){
        .source = serial->ri,
        .irq = serial->rx_irq,
        .handler = rx_handler,
        .priority = DDL_IRQ_PRIORITY_05
    });
    hc32_irq_register((hc32_irq_registration_t){
        .source = serial->ei,
        .irq = serial->err_irq,
        .handler = err_handler,
        .priority = DDL_IRQ_PRIORITY_05
    });
    hc32_irq_register((hc32_irq_registration_t){
        .source = serial->ti,
        .irq = serial->tx_irq,
        .handler = tx_handler,
        .priority = DDL_IRQ_PRIORITY_05
    });
    hc32_irq_register((hc32_irq_registration_t){
        .source = serial->tci,
        .irq = serial->txc_irq,
        .handler = txc_handler,
        .priority = DDL_IRQ_PRIORITY_05
    });

    return stream;
}

const io_stream_t *serialInit (uint32_t baud_rate)
{
    return serial_init_port(&serial0, &serial0_stream, baud_rate,
                            serial0RxIRQ, serial0ErrorIRQ, serial0TxIRQ, serial0TxCompleteIRQ);
}

void serialEnableRxInterrupt (void)
{
    USART_FuncCmd(serial0.uart, UsartRx, Enable);
    USART_FuncCmd(serial0.uart, UsartRxInt, Enable);
}

#if SERIAL_HAS_AUX_UART
static const io_stream_t *serial1Init (uint32_t baud_rate)
{
    serial_streams[0].flags.claimed = On;

    return serial_init_port(&serial1, &serial1_stream, baud_rate,
                            serial1RxIRQ, serial1ErrorIRQ, serial1TxIRQ, serial1TxCompleteIRQ);
}

static bool serialRelease (uint8_t instance)
{
    if(instance == AUX_UART_STREAM && serial_streams[0].flags.claimed) {
        serial_streams[0].flags.claimed = Off;
        return true;
    }

    return false;
}

static const io_stream_status_t *serialGetStatus (uint8_t instance)
{
    return instance == AUX_UART_STREAM ? &serial1.status : NULL;
}

void serialRegisterStreams (void)
{
    static io_stream_details_t details = {
        .n_streams = sizeof(serial_streams) / sizeof(serial_streams[0]),
        .streams = serial_streams
    };

    serial_streams[0].claim = serial1Init;
    serial_streams[0].release = serialRelease;
    serial_streams[0].get_status = serialGetStatus;

    stream_register_streams(&details);
}
#else
void serialRegisterStreams (void)
{
}
#endif
