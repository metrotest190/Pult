/**
 * @file tjc_usart_hmi.c
 * @brief Драйвер TJC USART HMI (USART3).
 *
 * Вынесено из Modules/modbus/port/portserial.c при переходе на протокол
 * NeModbus: TJC-часть не должна зависеть от стека Modbus.
 *
 * Содержит:
 *  - ring buffer приёма USART3;
 *  - буферизованную отправку TJC-команд с двойным буфером;
 *  - хелперы tjc_send_*;
 *  - ISR-хуки, которые вызывает транспортный слой (ne_port.c).
 */

#include "main.h"
#include "stm32f1xx_hal.h"
#include "tjc_usart_hmi.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Локальные критические секции                                      */
/* ------------------------------------------------------------------ */
static uint32_t irq_save_disable(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void irq_restore(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/* ------------------------------------------------------------------ */
/*  Переменные                                                        */
/* ------------------------------------------------------------------ */
#define TJC_TX_BUF_SIZE 512U

static uint8_t tjc_tx_buf_a[TJC_TX_BUF_SIZE];
static uint8_t tjc_tx_buf_b[TJC_TX_BUF_SIZE];
static uint8_t *tjc_active_buf = tjc_tx_buf_a;
static volatile uint16_t tjc_tx_len;
static volatile uint8_t  tjc_tx_busy;
static volatile uint8_t  tjc_tx_overflow;
volatile uint32_t tjc_tx_error_count;

/* Диагностика переполнения TJC RX ring buffer. */
static volatile uint8_t  tjc_ring_overflow_flag;
volatile uint32_t tjc_ring_overflow_count;

typedef struct
{
    uint16_t Head;
    uint16_t Tail;
    uint16_t Length;
    uint8_t  Ring_data[RINGBUFFER_LEN];
} RingBuffer_t;

static RingBuffer_t ringBuffer;
uint8_t RxBuffer[1];

/* ------------------------------------------------------------------ */
/*  Утилиты                                                           */
/* ------------------------------------------------------------------ */
void intToStr(int num, char *str)
{
    if (str == NULL) {
        return;
    }

    int i = 0;
    const int isNegative = (num < 0);
    unsigned int absNum;

    if (isNegative) {
        absNum = (unsigned int)(-(num + 1)) + 1U;
    } else {
        absNum = (unsigned int)num;
    }

    do {
        str[i++] = (char)((absNum % 10U) + (unsigned int)'0');
        absNum /= 10U;
    } while (absNum != 0U);

    if (isNegative) {
        str[i++] = '-';
    }
    str[i] = '\0';

    for (int start = 0, end = i - 1; start < end; ++start, --end) {
        const char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
    }
}

/* ------------------------------------------------------------------ */
/*  Отправка                                                          */
/* ------------------------------------------------------------------ */
void uart_send_char(char ch)
{
    const uint32_t primask = irq_save_disable();

    if (tjc_tx_overflow == 0U) {
        if (tjc_tx_len < TJC_TX_BUF_SIZE) {
            tjc_active_buf[tjc_tx_len++] = (uint8_t)ch;
        } else {
            tjc_tx_overflow = 1U;
            tjc_tx_error_count++;
        }
    }

    irq_restore(primask);
}

void uart_send_string(char *str)
{
    while ((str != NULL) && (*str != '\0')) {
        uart_send_char(*str++);
    }
}

uint8_t tjc_flush_tx(void)
{
    const uint32_t primask = irq_save_disable();

    if (tjc_tx_overflow != 0U) {
        tjc_tx_len = 0U;
        tjc_tx_overflow = 0U;
        irq_restore(primask);
        return 0U;
    }

    if ((tjc_tx_busy != 0U) || (tjc_tx_len == 0U)) {
        irq_restore(primask);
        return 0U;
    }

    uint8_t *const send_buf = tjc_active_buf;
    const uint16_t send_len = tjc_tx_len;

    if (HAL_UART_Transmit_IT(&TJC_UART, send_buf, send_len) == HAL_OK) {
        tjc_active_buf = (send_buf == tjc_tx_buf_a) ? tjc_tx_buf_b : tjc_tx_buf_a;
        tjc_tx_len = 0U;
        tjc_tx_busy = 1U;
        irq_restore(primask);
        return 1U;
    }

    tjc_tx_len = 0U;
    tjc_tx_error_count++;
    irq_restore(primask);
    return 0U;
}

uint8_t tjc_tx_is_busy(void)
{
    return tjc_tx_busy;
}

uint16_t tjc_diag_status(void)
{
    uint16_t status = 0U;
    if (tjc_tx_busy != 0U) {
        status |= 0x0001U;
    }
    if (tjc_tx_overflow != 0U) {
        status |= 0x0002U;
    }
    if (tjc_ring_overflow_flag != 0U) {
        status |= 0x0004U;
    }
    if (tjc_tx_error_count > 255U) {
        status |= 0xFF00U;
    } else {
        status |= (uint16_t)((tjc_tx_error_count & 0xFFU) << 8);
    }
    return status;
}

void tjc_discard_pending_tx(void)
{
    const uint32_t primask = irq_save_disable();
    tjc_tx_len = 0U;
    tjc_tx_overflow = 0U;
    irq_restore(primask);
}

void tjc_send_string(char *str)
{
    uart_send_string(str);
    uart_send_char((char)0xFF);
    uart_send_char((char)0xFF);
    uart_send_char((char)0xFF);
}

void tjc_send_txt(char *objname, char *attribute, char *txt)
{
    uart_send_string(objname);
    uart_send_char('.');
    uart_send_string(attribute);
    uart_send_string("=\"");
    uart_send_string(txt);
    uart_send_char('\"');
    uart_send_char((char)0xFF);
    uart_send_char((char)0xFF);
    uart_send_char((char)0xFF);
}

void tjc_send_val(char *objname, char *attribute, int val)
{
    char txt[12];

    uart_send_string(objname);
    uart_send_char('.');
    uart_send_string(attribute);
    uart_send_char('=');
    intToStr(val, txt);
    uart_send_string(txt);
    uart_send_char((char)0xFF);
    uart_send_char((char)0xFF);
    uart_send_char((char)0xFF);
}

void tjc_send_nstring(char *str, unsigned char str_length)
{
    if (str == NULL) {
        return;
    }

    for (unsigned char index = 0U; index < str_length; ++index) {
        uart_send_char(str[index]);
    }
    uart_send_char((char)0xFF);
    uart_send_char((char)0xFF);
    uart_send_char((char)0xFF);
}

/* ------------------------------------------------------------------ */
/*  RX ring buffer                                                    */
/* ------------------------------------------------------------------ */
void initRingBuffer(void)
{
    const uint32_t primask = irq_save_disable();
    ringBuffer.Head = 0U;
    ringBuffer.Tail = 0U;
    ringBuffer.Length = 0U;
    tjc_ring_overflow_flag = 0U;
    tjc_ring_overflow_count = 0U;
    irq_restore(primask);
}

void write1ByteToRingBuffer(uint8_t data)
{
    const uint32_t primask = irq_save_disable();

    if (ringBuffer.Length < RINGBUFFER_LEN) {
        ringBuffer.Ring_data[ringBuffer.Tail] = data;
        ringBuffer.Tail = (uint16_t)((ringBuffer.Tail + 1U) % RINGBUFFER_LEN);
        ringBuffer.Length++;
    } else {
        tjc_ring_overflow_flag = 1U;
        if (tjc_ring_overflow_count < 0xFFFFFFFFU) {
            tjc_ring_overflow_count++;
        }
    }

    irq_restore(primask);
}

void deleteRingBuffer(uint16_t size)
{
    const uint32_t primask = irq_save_disable();

    if (size >= ringBuffer.Length) {
        ringBuffer.Head = 0U;
        ringBuffer.Tail = 0U;
        ringBuffer.Length = 0U;
    } else {
        ringBuffer.Head = (uint16_t)((ringBuffer.Head + size) % RINGBUFFER_LEN);
        ringBuffer.Length = (uint16_t)(ringBuffer.Length - size);
    }

    irq_restore(primask);
}

uint8_t read1ByteFromRingBuffer(uint16_t position)
{
    const uint32_t primask = irq_save_disable();
    uint8_t value = 0U;

    if (position < ringBuffer.Length) {
        const uint16_t realPosition =
            (uint16_t)((ringBuffer.Head + position) % RINGBUFFER_LEN);
        value = ringBuffer.Ring_data[realPosition];
    }

    irq_restore(primask);
    return value;
}

uint16_t getRingBufferLength(void)
{
    const uint32_t primask = irq_save_disable();
    const uint16_t length = ringBuffer.Length;
    irq_restore(primask);
    return length;
}

/* ------------------------------------------------------------------ */
/*  ISR-хуки (вызываются из HAL-колбэков в ne_port.c)                 */
/* ------------------------------------------------------------------ */
void tjc_uart_rx_isr(uint8_t byte)
{
    write1ByteToRingBuffer(byte);
    (void)HAL_UART_Receive_IT(&TJC_UART, RxBuffer, 1U);
}

void tjc_uart_tx_complete_isr(void)
{
    tjc_tx_busy = 0U;
}

void tjc_uart_error_isr(void)
{
    (void)HAL_UART_AbortReceive_IT(&TJC_UART);
    (void)HAL_UART_Receive_IT(&TJC_UART, RxBuffer, 1U);

    /* Если HAL вернул передатчик в READY после ошибки, колбэк завершения
       не придёт — разрешаем повтор из очереди. */
    if ((tjc_tx_busy != 0U) && (TJC_UART.gState == HAL_UART_STATE_READY)) {
        tjc_tx_busy = 0U;
        tjc_tx_error_count++;
    }
}
