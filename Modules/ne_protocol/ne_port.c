/**
 * @file ne_port.c
 * @brief Транспортный слой NeModbus для USART1.
 *
 * Переносит функциональность Modbus portserial.c на новый протокол.
 * HAL-колбэки для USART1 (NeModbus) и USART3 (TJC) живут здесь,
 * чтобы избежать дублирования.
 */

#include "main.h"
#include "stm32f1xx_hal.h"
#include "tjc_usart_hmi.h"
#include "ne_port.h"
#include <string.h>

/* ================================================================== */
/*  Ring buffer приёма USART1                                          */
/* ================================================================== */
typedef struct {
    uint16_t Head;
    uint16_t Tail;
    uint16_t Length;
    uint8_t  Data[NE_PORT_RX_RING_SIZE];
} NeRing_t;

static NeRing_t rx_ring;
static uint8_t rx_buf_1;

static volatile uint32_t rx_overflow_count;
static volatile uint32_t tx_error_count;

/* Критическая секция: вложенная, по PRIMASK */
static volatile uint32_t lock_cnt;
static uint32_t saved_primask;

static uint32_t ne_irq_save(void)
{
    const uint32_t p = __get_PRIMASK();
    __disable_irq();
    return p;
}

static void ne_irq_restore(uint32_t p)
{
    __set_PRIMASK(p);
}

void EnterCriticalSection(void)
{
    const uint32_t p = __get_PRIMASK();
    __disable_irq();
    if (lock_cnt++ == 0U) {
        saved_primask = p;
    }
}

void ExitCriticalSection(void)
{
    if (lock_cnt == 0U) {
        Error_Handler();
    }
    lock_cnt--;
    if (lock_cnt == 0U) {
        __set_PRIMASK(saved_primask);
    }
}

static void ring_reset(void)
{
    const uint32_t p = ne_irq_save();
    rx_ring.Head = 0U;
    rx_ring.Tail = 0U;
    rx_ring.Length = 0U;
    ne_irq_restore(p);
}

static void ring_push(uint8_t b)
{
    const uint32_t p = ne_irq_save();
    if (rx_ring.Length < NE_PORT_RX_RING_SIZE) {
        rx_ring.Data[rx_ring.Tail] = b;
        rx_ring.Tail = (uint16_t)((rx_ring.Tail + 1U) % NE_PORT_RX_RING_SIZE);
        rx_ring.Length++;
    } else {
        rx_overflow_count++;
    }
    ne_irq_restore(p);
}

static uint16_t ring_len(void)
{
    const uint32_t p = ne_irq_save();
    uint16_t l = rx_ring.Length;
    ne_irq_restore(p);
    return l;
}

static uint8_t ring_pop(void)
{
    const uint32_t p = ne_irq_save();
    uint8_t b = 0U;
    if (rx_ring.Length > 0U) {
        b = rx_ring.Data[rx_ring.Head];
        rx_ring.Head = (uint16_t)((rx_ring.Head + 1U) % NE_PORT_RX_RING_SIZE);
        rx_ring.Length--;
    }
    ne_irq_restore(p);
    return b;
}

/* ================================================================== */
/*  Транспортный слой                                                   */
/* ================================================================== */
static UART_HandleTypeDef *ne_uart;
static ne_packet_handler_t pkt_handler;

/* Буферы */
static uint8_t tx_buf[NE_PORT_TX_BUF_SIZE];
static volatile uint16_t tx_len;
static volatile uint8_t  tx_busy;

/* Накопительный буфер приёма (не пересекается с TX).
   Пакет приходит по одному байту и может не собраться за один вызов
   ne_port_poll(); неразобранный остаток живёт здесь до следующего вызова. */
static uint8_t  rx_acc[NE_PORT_RX_RING_SIZE];
static uint16_t rx_acc_len;

void ne_port_init(UART_HandleTypeDef *u, ne_packet_handler_t h)
{
    ne_uart = u;
    pkt_handler = h;

    ring_reset();
    rx_acc_len = 0U;
    tx_len = 0U;
    tx_busy = 0U;
    rx_overflow_count = 0U;
    tx_error_count = 0U;

    /* Запуск приёма по 1 байту */
    if (HAL_UART_Receive_IT(ne_uart, &rx_buf_1, 1U) != HAL_OK) {
        Error_Handler();
    }
}

static void rx_acc_fill_from_ring(void)
{
    while (rx_acc_len < (uint16_t)sizeof(rx_acc)) {
        if (ring_len() == 0U) {
            return;
        }
        rx_acc[rx_acc_len++] = ring_pop();
    }
}

static void rx_acc_drop(uint16_t count)
{
    if (count >= rx_acc_len) {
        rx_acc_len = 0U;
        return;
    }
    memmove(rx_acc, rx_acc + count, (size_t)(rx_acc_len - count));
    rx_acc_len = (uint16_t)(rx_acc_len - count);
}

void ne_port_poll(void)
{
    if ((rx_acc_len == 0U) && (ring_len() == 0U)) {
        return;
    }

    /* Если буфер полон, а пакет ещё не разобран, лишние байты остаются
       в ring buffer и заберутся следующим вызовом после rx_acc_drop(). */
    rx_acc_fill_from_ring();

    uint16_t pos = 0U;

    while (pos < rx_acc_len) {
        ne_packet_t pkt;
        size_t consumed = 0U;
        const int status = ne_parse_packet(rx_acc + pos,
                                           (size_t)(rx_acc_len - pos),
                                           &pkt, &consumed);

        if (status == NE_OK) {
            if (pkt_handler != NULL) {
                pkt_handler(&pkt);
            }
            pos = (uint16_t)(pos + (uint16_t)consumed);
        } else if (status == NE_ERR_INCOMPLETE) {
            /* Пакет ещё не принят целиком: остаток сохраняется в rx_acc
               до следующего вызова. */
            break;
        } else {
            /* Мусор или битый пакет: искать следующее стартовое слово. */
            const int off = ne_find_start(rx_acc + pos + 1U,
                                          (size_t)(rx_acc_len - pos - 1U));
            if (off >= 0) {
                pos = (uint16_t)(pos + 1U + (uint16_t)off);
            } else {
                /* Стартового слова больше нет. Последний байт может быть
                   первой половиной 0xA55A — его оставляем. */
                const uint16_t keep =
                    (rx_acc[rx_acc_len - 1U] == 0xA5U) ? 1U : 0U;
                pos = (uint16_t)(rx_acc_len - keep);
                break;
            }
        }
    }

    if (pos > 0U) {
        rx_acc_drop(pos);
    }
}

int ne_port_send(ne_cmd_t cmd, const uint8_t *data, uint16_t data_len)
{
    if (tx_busy) {
        return NE_ERR_NO_SPACE;
    }

    int len = ne_build_packet(tx_buf, sizeof(tx_buf), cmd, data, data_len);
    if (len < 0) {
        return len;
    }

    tx_len = (uint16_t)len;
    tx_busy = 1U;

    if (HAL_UART_Transmit_IT(ne_uart, tx_buf, (uint16_t)len) != HAL_OK) {
        tx_busy = 0U;
        tx_len = 0U;
        tx_error_count++;
        return NE_ERR_NO_SPACE;
    }

    return len;
}

int ne_port_send_raw(const uint8_t *buf, uint16_t len)
{
    if (tx_busy || len == 0U || len > sizeof(tx_buf)) {
        return NE_ERR_NO_SPACE;
    }

    if (buf != tx_buf) {
        memcpy(tx_buf, buf, len);
    }
    tx_len = len;
    tx_busy = 1U;

    if (HAL_UART_Transmit_IT(ne_uart, tx_buf, len) != HAL_OK) {
        tx_busy = 0U;
        tx_len = 0U;
        tx_error_count++;
        return NE_ERR_NO_SPACE;
    }

    return (int)len;
}

uint32_t ne_port_rx_overflow_count(void)
{
    const uint32_t p = ne_irq_save();
    uint32_t c = rx_overflow_count;
    ne_irq_restore(p);
    return c;
}

uint32_t ne_port_crc_error_count(void)
{
    /* Счётчик не ведётся: при ошибке CRC пакет просто отбрасывается. */
    return 0U;
}

uint32_t ne_port_tx_error_count(void)
{
    const uint32_t p = ne_irq_save();
    uint32_t c = tx_error_count;
    ne_irq_restore(p);
    return c;
}

/* ================================================================== */
/*  HAL-колбэки (единственный экземпляр)                               */
/* ================================================================== */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((ne_uart != NULL) && (huart->Instance == ne_uart->Instance)) {
        tx_busy = 0U;
        tx_len = 0U;
    } else if (huart->Instance == TJC_UART_INS) {
        tjc_uart_tx_complete_isr();
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((ne_uart != NULL) && (huart->Instance == ne_uart->Instance)) {
        ring_push(rx_buf_1);
        if (HAL_UART_Receive_IT(ne_uart, &rx_buf_1, 1U) != HAL_OK) {
            Error_Handler();
        }
    } else if (huart->Instance == TJC_UART_INS) {
        tjc_uart_rx_isr(RxBuffer[0]);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((ne_uart != NULL) && (huart->Instance == ne_uart->Instance)) {
        (void)HAL_UART_AbortReceive_IT(ne_uart);
        if (HAL_UART_Receive_IT(ne_uart, &rx_buf_1, 1U) != HAL_OK) {
            Error_Handler();
        }
    } else if (huart->Instance == TJC_UART_INS) {
        tjc_uart_error_isr();
    }
}
