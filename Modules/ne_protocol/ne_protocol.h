/**
 * @file ne_protocol.h
 * @brief Ядро протокола обмена NeModbus.
 *
 * Формат пакета (версия 1):
 *
 *   | 0xA55A | version | command | data_len | data[data_len] | crc32 |
 *   | 2 байт | 2 байт  | 4 байт  | 2 байта  | N байт         | 4 байта |
 *
 * Все многобайтовые поля передаются big-endian (старший байт первым).
 * CRC32 считается по всем байтам пакета, кроме самого поля CRC
 * (полином 0x04C11DB7, init 0xFFFFFFFF, xorout 0xFFFFFFFF, как в
 * BasicElements::Crc32 проекта pultwithgit).
 *
 * Модуль не зависит от HAL и mbed: чистая логика, пригодная для
 * юнит-тестов на ПК.
 */

#ifndef NE_PROTOCOL_H
#define NE_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#include "ne_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Константы формата                                                  */
/* ------------------------------------------------------------------ */
#define NE_START_WORD              0xA55Au

#define NE_PROTO_VERSION_1         0x0001u
#define NE_PROTO_VERSION_CURRENT   NE_PROTO_VERSION_1

#define NE_HEAD_SIZE               10u  /* start(2)+ver(2)+cmd(4)+len(2) */
#define NE_CRC_SIZE                4u
#define NE_MIN_PACKET_SIZE         (NE_HEAD_SIZE + NE_CRC_SIZE)

/* Максимальный размер данных в одном пакете. Ограничен буфером
   приёмника и требованиями задачи; при необходимости увеличить. */
#ifndef NE_MAX_DATA_LEN
#define NE_MAX_DATA_LEN            128u
#endif

/* ------------------------------------------------------------------ */
/*  Коды ошибок                                                        */
/* ------------------------------------------------------------------ */
typedef enum {
    NE_OK               =  0,
    NE_ERR_NO_START     = -1,   /* нет стартового слова            */
    NE_ERR_VERSION      = -2,   /* неизвестная версия протокола    */
    NE_ERR_TOO_BIG      = -3,   /* данных больше NE_MAX_DATA_LEN   */
    NE_ERR_NO_SPACE     = -4,   /* не хватает места в буфере       */
    NE_ERR_CRC          = -5,   /* неверная CRC32                  */
    NE_ERR_INCOMPLETE   = -6    /* пакет ещё не принят целиком     */
} ne_status_t;

/* ------------------------------------------------------------------ */
/*  Разобранный пакет                                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    uint16_t version;
    ne_cmd_t cmd;
    uint16_t data_len;
    const uint8_t *data;    /* указывает внутрь буфера приёма */
} ne_packet_t;

/* ------------------------------------------------------------------ */
/*  CRC32                                                              */
/* ------------------------------------------------------------------ */
uint32_t ne_crc32(const uint8_t *buf, size_t len);

/* ------------------------------------------------------------------ */
/*  Сборка пакета                                                      */
/* ------------------------------------------------------------------ */
/**
 * @brief Собрать пакет в буфер.
 * @param out       буфер назначения
 * @param out_size  размер буфера
 * @param cmd       код команды
 * @param data      данные (может быть NULL при data_len == 0)
 * @param data_len  длина данных
 * @return длина пакета в байтах или отрицательный ne_status_t
 */
int ne_build_packet(uint8_t *out, size_t out_size, ne_cmd_t cmd,
                    const uint8_t *data, uint16_t data_len);

/* ------------------------------------------------------------------ */
/*  Разбор пакета                                                      */
/* ------------------------------------------------------------------ */
/**
 * @brief Попытаться разобрать пакет из буфера приёма.
 *
 * Если в буфере меньше NE_MIN_PACKET_SIZE байт, возвращает
 * NE_ERR_INCOMPLETE. Если данных в пакете ещё не все — тоже.
 *
 * @param buf        буфер приёма
 * @param len        сколько байт реально принято
 * @param pkt        результат разбора (валиден при NE_OK)
 * @param consumed   сколько байт занял пакет (может быть NULL)
 * @return ne_status_t
 */
int ne_parse_packet(const uint8_t *buf, size_t len,
                    ne_packet_t *pkt, size_t *consumed);

/**
 * @brief Найти стартовое слово в буфере, отбросив мусор.
 * @return смещение до начала пакета или -1, если не найдено.
 */
int ne_find_start(const uint8_t *buf, size_t len);

/* ------------------------------------------------------------------ */
/*  Big-endian помощники                                               */
/* ------------------------------------------------------------------ */
void     ne_put_u16(uint8_t *p, uint16_t v);
void     ne_put_u32(uint8_t *p, uint32_t v);
void     ne_put_u64(uint8_t *p, uint64_t v);
void     ne_put_f32(uint8_t *p, float v);
void     ne_put_f64(uint8_t *p, double v);

uint16_t ne_get_u16(const uint8_t *p);
uint32_t ne_get_u32(const uint8_t *p);
uint64_t ne_get_u64(const uint8_t *p);
float    ne_get_f32(const uint8_t *p);
double   ne_get_f64(const uint8_t *p);

#ifdef __cplusplus
}
#endif

#endif /* NE_PROTOCOL_H */
