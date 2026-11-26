/**
 * @file ne_port.h
 * @brief Транспортный слой NeModbus для USART1 (связь с ПК).
 *
 * Раньше на USART1 работал Modbus RTU (slave 0x0A, 115200 8N1).
 * Теперь этот же порт используется для нового протокола NeModbus.
 *
 * Слой отвечает за:
 *  - приём байт USART1 по прерываниям в ring buffer;
 *  - сборку пакетов и вызов обработчика команд;
 *  - отправку пакетов;
 *  - глобальные HAL-колбэки для USART1 (NeModbus) и USART3 (TJC).
 */

#ifndef NE_PORT_H
#define NE_PORT_H

#include <stdint.h>
#include "stm32f1xx_hal.h"
#include "ne_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Обработчик принятого пакета. Вызывается из ne_port_poll(). */
typedef void (*ne_packet_handler_t)(const ne_packet_t *pkt);

/* Размер кольцевого буфера приёма USART1. */
#ifndef NE_PORT_RX_RING_SIZE
#define NE_PORT_RX_RING_SIZE   512u
#endif

/* Размер буфера отправки (максимальный пакет). */
#ifndef NE_PORT_TX_BUF_SIZE
#define NE_PORT_TX_BUF_SIZE    (NE_MAX_DATA_LEN + NE_MIN_PACKET_SIZE)
#endif

/**
 * @brief Инициализировать транспорт.
 *
 * UART должен быть уже сконфигурирован CubeMX (USART1, 115200 8N1).
 * Включает приём по прерываниям.
 *
 * @param uart     указатель на huart1
 * @param handler  обработчик пакетов (может быть NULL)
 */
void ne_port_init(UART_HandleTypeDef *uart, ne_packet_handler_t handler);

/**
 * @brief Обработать принятые байты и разобрать пакеты.
 * Вызывать в главном цикле как можно чаще.
 */
void ne_port_poll(void);

/* Пауза внутри кадра дольше этого числа миллисекунд означает, что кадр
   недособран: накопительный буфер сбрасывается, запрос повторяют. */
#ifndef NE_PORT_RX_IDLE_TIMEOUT_MS
#define NE_PORT_RX_IDLE_TIMEOUT_MS    100u
#endif

/**
 * @brief Отправить пакет в USART1.
 *
 * @return длина отправленного пакета или отрицательный ne_status_t.
 */
int ne_port_send(ne_cmd_t cmd, const uint8_t *data, uint16_t data_len);

/**
 * @brief Отправить уже собранный буфер (для ответов).
 */
int ne_port_send_raw(const uint8_t *buf, uint16_t len);

/* ------------------------------------------------------------------ */
/*  Диагностика                                                        */
/* ------------------------------------------------------------------ */
uint32_t ne_port_rx_overflow_count(void);
uint32_t ne_port_crc_error_count(void);
uint32_t ne_port_rx_timeout_count(void);
uint32_t ne_port_frame_error_count(void);
uint32_t ne_port_tx_error_count(void);

/* ------------------------------------------------------------------ */
/*  ISR-хуки TJC (реализованы в Core/Src/tjc_usart_hmi.c)              */
/*  Объявлены здесь, чтобы HAL-колбэки жили в одном месте.            */
/* ------------------------------------------------------------------ */
void tjc_uart_rx_isr(uint8_t byte);
void tjc_uart_tx_complete_isr(void);
void tjc_uart_error_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* NE_PORT_H */
