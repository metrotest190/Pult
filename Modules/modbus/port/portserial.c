/* ... (Оставляем ваши комментарии и инклуды без изменений) ... */
#include "port.h"
#include "mb.h"
#include "mbport.h"
#include "stm32f1xx_hal.h"
#include "main.h"
#include "stm32f1xx_hal_uart.h"
#include <stdio.h>
#include <limits.h>
#include "tjc_usart_hmi.h"

/* ----------------------- Static functions ---------------------------------*/
static void prvvUARTTxReadyISR( void );
static void prvvUARTRxISR( void );
static uint32_t mb_irq_save_disable(void);
static void mb_irq_restore(uint32_t primask);

/* ----------------------- Variables ----------------------------------------*/
extern UART_HandleTypeDef* modbusUart;
uint8_t txByte = 0x00;
uint8_t rxByte = 0x00;

#define TJC_TX_BUF_SIZE 512
static uint8_t tjc_tx_buf_a[TJC_TX_BUF_SIZE];
static uint8_t tjc_tx_buf_b[TJC_TX_BUF_SIZE];
static uint8_t* tjc_active_buf = tjc_tx_buf_a;
static volatile uint16_t tjc_tx_len = 0;
static volatile uint8_t  tjc_tx_busy = 0;

typedef struct
{
    uint16_t Head;
    uint16_t Tail;
    uint16_t Length;
    uint8_t  Ring_data[RINGBUFFER_LEN];
} RingBuffer_t;

RingBuffer_t ringBuffer;
uint8_t RxBuffer[1];

static uint32_t mb_irq_save_disable(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void mb_irq_restore(uint32_t primask)
{
    __set_PRIMASK(primask);
}

void intToStr(int num, char* str) {
    if (str == NULL) return;
    int i = 0;
    int isNegative = 0;
    unsigned int absNum = 0;

    if (num < 0) {
        isNegative = 1;
        absNum = (unsigned int)(-(num + 1)) + 1U;
    } else {
        absNum = (unsigned int)num;
    }

    do {
        str[i++] = (char)((absNum % 10U) + '0');
        absNum /= 10U;
    } while (absNum != 0U);

    if (isNegative) str[i++] = '-';
    str[i] = '\0';

    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

void uart_send_char(char ch)
{
    // Добавляем защиту от переполнения. Если буфер полон, ждем освобождения (с таймаутом)
    uint32_t timeout = HAL_GetTick() + 10;
    while (tjc_tx_len >= TJC_TX_BUF_SIZE && HAL_GetTick() < timeout) {
        // Буфер полон, ждем пока DMA/IT не отправит данные.
        // Это предотвращает потерю байт завершения команды 0xFF.
    }
    if (tjc_tx_len < TJC_TX_BUF_SIZE) {
        tjc_active_buf[tjc_tx_len++] = (uint8_t)ch;
    }
}

void uart_send_string(char* str)
{
    while(str != NULL && *str != 0) {
        uart_send_char(*str++);
    }
}

void tjc_flush_tx(void)
{
    if (tjc_tx_len == 0) return;

    // Ждем завершения предыдущей передачи, но с таймаутом, чтобы не зависнуть навсегда
    uint32_t timeout = HAL_GetTick() + 100;
    while (tjc_tx_busy && HAL_GetTick() < timeout) {}

    uint8_t* send_buf = tjc_active_buf;
    uint16_t send_len = tjc_tx_len;

    if (send_buf == tjc_tx_buf_a) {
        tjc_active_buf = tjc_tx_buf_b;
    } else {
        tjc_active_buf = tjc_tx_buf_a;
    }
    tjc_tx_len = 0;
    tjc_tx_busy = 1;

    // Если передача не стартовала (ошибка UART), снимаем флаг занятости, чтобы не зависнуть
    if (HAL_UART_Transmit_IT(&TJC_UART, send_buf, send_len) != HAL_OK) {
        tjc_tx_busy = 0;
        TJC_UART.gState = HAL_UART_STATE_READY; // Принудительно сбрасываем состояние HAL
    }
}

uint8_t tjc_tx_is_busy(void)
{
    return tjc_tx_busy;
}

void tjc_send_string(char* str) {
    while(str != NULL && *str != 0) {
        uart_send_char(*str++);
    }
    uart_send_char(0xff); uart_send_char(0xff); uart_send_char(0xff);
}

void tjc_send_txt(char* objname, char* attribute, char* txt) {
    uart_send_string(objname);
    uart_send_char('.');
    uart_send_string(attribute);
    uart_send_string("=\"");
    uart_send_string(txt);
    uart_send_char('\"');
    uart_send_char(0xff); uart_send_char(0xff); uart_send_char(0xff);
}

void tjc_send_val(char* objname, char* attribute, int val) {
    uart_send_string(objname);
    uart_send_char('.');
    uart_send_string(attribute);
    uart_send_char('=');
    char txt[12]="";
    intToStr(val, txt);
    uart_send_string(txt);
    uart_send_char(0xff); uart_send_char(0xff); uart_send_char(0xff);
}

void tjc_send_nstring(char* str, unsigned char str_length) {
    for (int var = 0; var < str_length; ++var) {
        uart_send_char(*str++);
    }
    uart_send_char(0xff); uart_send_char(0xff); uart_send_char(0xff);
}

/* ----------------------- Start implementation -----------------------------*/

void vMBPortSerialEnable(BOOL xRxEnable, BOOL xTxEnable)
{
  if (xRxEnable == FALSE) {
    HAL_UART_AbortReceive_IT(modbusUart);
  } else {
    HAL_UART_Receive_IT(modbusUart, &rxByte, 1);
  }

  if (xTxEnable == FALSE) {
    HAL_UART_AbortTransmit_IT(modbusUart);
  } else {
    if (modbusUart->gState == HAL_UART_STATE_READY) {
      prvvUARTTxReadyISR();
    }
  }
}

BOOL xMBPortSerialInit(UCHAR ucPORT, ULONG ulBaudRate, UCHAR ucDataBits, eMBParity eParity) {
    return TRUE;
}

BOOL xMBPortSerialPutByte(CHAR ucByte) {
  txByte = ucByte;
  HAL_UART_Transmit_IT(modbusUart, &txByte, 1);
  return TRUE;
}

BOOL xMBPortSerialGetByte(CHAR * pucByte) {
  *pucByte = rxByte;
  return TRUE;
}

static void prvvUARTTxReadyISR(void) { pxMBFrameCBTransmitterEmpty(); }
static void prvvUARTRxISR(void) { pxMBFrameCBByteReceived(); }

void initRingBuffer(void) {
    uint32_t primask = mb_irq_save_disable();
    ringBuffer.Head = 0;
    ringBuffer.Tail = 0;
    ringBuffer.Length = 0;
    mb_irq_restore(primask);
}

void write1ByteToRingBuffer(uint8_t data) {
    uint32_t primask = mb_irq_save_disable();
    if(ringBuffer.Length >= RINGBUFFER_LEN) {
        mb_irq_restore(primask);
        return;
    }
    ringBuffer.Ring_data[ringBuffer.Tail] = data;
    ringBuffer.Tail = (ringBuffer.Tail + 1) % RINGBUFFER_LEN;
    ringBuffer.Length++;
    mb_irq_restore(primask);
}

void deleteRingBuffer(uint16_t size) {
    uint32_t primask = mb_irq_save_disable();
    if(size >= ringBuffer.Length) {
        ringBuffer.Head = 0;
        ringBuffer.Tail = 0;
        ringBuffer.Length = 0;
    } else {
        for(int i = 0; i < size; i++) {
            ringBuffer.Head = (ringBuffer.Head + 1) % RINGBUFFER_LEN;
            ringBuffer.Length--;
        }
    }
    mb_irq_restore(primask);
}

uint8_t read1ByteFromRingBuffer(uint16_t position) {
    uint32_t primask = mb_irq_save_disable();
    uint8_t value = 0;
    // Добавлена проверка: нельзя читать данные, которых нет в буфере
    if (position < ringBuffer.Length) {
        uint16_t realPosition = (ringBuffer.Head + position) % RINGBUFFER_LEN;
        value = ringBuffer.Ring_data[realPosition];
    }
    mb_irq_restore(primask);
    return value;
}

uint16_t getRingBufferLength() {
    uint32_t primask = mb_irq_save_disable();
    uint16_t length = ringBuffer.Length;
    mb_irq_restore(primask);
    return length;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == modbusUart->Instance) {
    prvvUARTTxReadyISR();
  } else if (huart->Instance == TJC_UART_INS) {
    tjc_tx_busy = 0;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == modbusUart->Instance) {
    prvvUARTRxISR();
    HAL_UART_Receive_IT(modbusUart, &rxByte, 1);
  } else if(huart->Instance == TJC_UART_INS) {
    write1ByteToRingBuffer(RxBuffer[0]);
    HAL_UART_Receive_IT(&TJC_UART, RxBuffer, 1);
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == modbusUart->Instance) {
    HAL_UART_AbortReceive_IT(modbusUart);
    HAL_UART_Receive_IT(modbusUart, &rxByte, 1);
  } else if (huart->Instance == TJC_UART_INS) {
    // При ошибке UART (например, Overrun) сбрасываем флаги, чтобы не зависнуть
    tjc_tx_busy = 0;
    HAL_UART_AbortReceive_IT(&TJC_UART);
    HAL_UART_Receive_IT(&TJC_UART, RxBuffer, 1);
  }
}
