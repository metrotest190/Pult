#include "port.h"
#include "mb.h"
#include "mbport.h"
#include "stm32f1xx_hal.h"
#include "main.h"
#include "stm32f1xx_hal_uart.h"
#include <limits.h>
#include "tjc_usart_hmi.h"

/* ----------------------- Static functions ---------------------------------*/
static void prvvUARTTxReadyISR(void);
static void prvvUARTRxISR(void);
static uint32_t irq_save_disable(void);
static void irq_restore(uint32_t primask);

/* ----------------------- Variables ----------------------------------------*/
extern UART_HandleTypeDef *modbusUart;

static uint8_t txByte;
static uint8_t rxByte;

#define TJC_TX_BUF_SIZE 512U
static uint8_t tjc_tx_buf_a[TJC_TX_BUF_SIZE];
static uint8_t tjc_tx_buf_b[TJC_TX_BUF_SIZE];
static uint8_t *tjc_active_buf = tjc_tx_buf_a;
static volatile uint16_t tjc_tx_len;
static volatile uint8_t tjc_tx_busy;
static volatile uint8_t tjc_tx_overflow;
volatile uint32_t tjc_tx_error_count;

/* TJC RX is produced in USART3 IRQ and consumed in the foreground. */
typedef struct
{
    uint16_t Head;
    uint16_t Tail;
    uint16_t Length;
    uint8_t Ring_data[RINGBUFFER_LEN];
} RingBuffer_t;

static RingBuffer_t ringBuffer;
uint8_t RxBuffer[1];

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

void intToStr(int num, char *str)
{
    if (str == NULL) {
        return;
    }

    int i = 0;
    const int isNegative = (num < 0);
    unsigned int absNum;

    if (isNegative) {
        /* This expression is also safe for INT_MIN. */
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

/*
 * Commands are accumulated in the inactive software buffer. If it overflows,
 * the whole pending batch is discarded by tjc_flush_tx() rather than sending a
 * partial TJC command without its terminating 0xFF bytes.
 */
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

/*
 * Starts a transfer only when USART3 is idle. It never waits and never changes
 * UART_HandleTypeDef fields. A failed start drops the complete pending batch;
 * this is preferable to mixing its bytes with commands generated on a retry.
 */
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

    /* The caller keeps its state and formats a new complete batch on retry.
       Never retain an untransmittable partial batch beside future commands. */
    tjc_tx_len = 0U;
    tjc_tx_error_count++;
    irq_restore(primask);
    return 0U;
}

uint8_t tjc_tx_is_busy(void)
{
    return tjc_tx_busy;
}

/* Drop only bytes queued in the inactive software buffer. The active UART
   transfer, if any, is never modified. It is used before an addt sequence,
   where raw graph data must directly follow the display's 0xFE response. */
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

/* ----------------------- FreeModbus serial port ---------------------------*/
void vMBPortSerialEnable(BOOL xRxEnable, BOOL xTxEnable)
{
    if (modbusUart == NULL) {
        return;
    }

    if (xRxEnable == FALSE) {
        (void)HAL_UART_AbortReceive_IT(modbusUart);
    } else {
        (void)HAL_UART_Receive_IT(modbusUart, &rxByte, 1U);
    }

    if (xTxEnable == FALSE) {
        (void)HAL_UART_AbortTransmit_IT(modbusUart);
    } else if (modbusUart->gState == HAL_UART_STATE_READY) {
        prvvUARTTxReadyISR();
    }
}

BOOL xMBPortSerialInit(UCHAR ucPORT, ULONG ulBaudRate, UCHAR ucDataBits,
                       eMBParity eParity)
{
    (void)ucPORT;

    if (modbusUart == NULL) {
        return FALSE;
    }

    if ((modbusUart->Init.BaudRate != ulBaudRate) ||
        (ucDataBits != 8U) ||
        (eParity != MB_PAR_NONE)) {
        /* USART1 is configured by CubeMX as 8N1. Do not claim that a
           different runtime request was applied when it was not. */
        return FALSE;
    }

    return TRUE;
}

BOOL xMBPortSerialPutByte(CHAR ucByte)
{
    if (modbusUart == NULL) {
        return FALSE;
    }

    txByte = (uint8_t)ucByte;
    return (HAL_UART_Transmit_IT(modbusUart, &txByte, 1U) == HAL_OK) ? TRUE : FALSE;
}

BOOL xMBPortSerialGetByte(CHAR *pucByte)
{
    if (pucByte == NULL) {
        return FALSE;
    }

    *pucByte = (CHAR)rxByte;
    return TRUE;
}

static void prvvUARTTxReadyISR(void)
{
    (void)pxMBFrameCBTransmitterEmpty();
}

static void prvvUARTRxISR(void)
{
    (void)pxMBFrameCBByteReceived();
}

/* ----------------------- TJC RX ring buffer -------------------------------*/
void initRingBuffer(void)
{
    const uint32_t primask = irq_save_disable();
    ringBuffer.Head = 0U;
    ringBuffer.Tail = 0U;
    ringBuffer.Length = 0U;
    irq_restore(primask);
}

void write1ByteToRingBuffer(uint8_t data)
{
    const uint32_t primask = irq_save_disable();

    if (ringBuffer.Length < RINGBUFFER_LEN) {
        ringBuffer.Ring_data[ringBuffer.Tail] = data;
        ringBuffer.Tail = (uint16_t)((ringBuffer.Tail + 1U) % RINGBUFFER_LEN);
        ringBuffer.Length++;
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

/* ----------------------- HAL callbacks ------------------------------------*/
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((modbusUart != NULL) && (huart->Instance == modbusUart->Instance)) {
        prvvUARTTxReadyISR();
    } else if (huart->Instance == TJC_UART_INS) {
        tjc_tx_busy = 0U;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((modbusUart != NULL) && (huart->Instance == modbusUart->Instance)) {
        prvvUARTRxISR();
        (void)HAL_UART_Receive_IT(modbusUart, &rxByte, 1U);
    } else if (huart->Instance == TJC_UART_INS) {
        write1ByteToRingBuffer(RxBuffer[0]);
        (void)HAL_UART_Receive_IT(&TJC_UART, RxBuffer, 1U);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((modbusUart != NULL) && (huart->Instance == modbusUart->Instance)) {
        (void)HAL_UART_AbortReceive_IT(modbusUart);
        (void)HAL_UART_Receive_IT(modbusUart, &rxByte, 1U);
    } else if (huart->Instance == TJC_UART_INS) {
        /* A receive error must not release a still active TJC transmission. */
        (void)HAL_UART_AbortReceive_IT(&TJC_UART);
        (void)HAL_UART_Receive_IT(&TJC_UART, RxBuffer, 1U);

        /* If HAL has returned the transmitter to READY after an error, no
           completion callback will arrive, so permit a queued retry. */
        if ((tjc_tx_busy != 0U) && (huart->gState == HAL_UART_STATE_READY)) {
            tjc_tx_busy = 0U;
            tjc_tx_error_count++;
        }
    }
}
