/**
 * @file ne_types.h
 * @brief Типы данных протокола обмена (ядро).
 *
 * Основан на DataChannel.h проекта pultwithgit, переписано на C.
 */

#ifndef NE_TYPES_H
#define NE_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Типы каналов (что передаётся)                                     */
/* ------------------------------------------------------------------ */
typedef enum {
    NE_CH_UNDEFINED      = 0x00,
    NE_CH_POSITION       = 0x01,
    NE_CH_STRAIN         = 0x02,
    NE_CH_DEFORMATION    = 0x03,
    NE_CH_DEFORMATION_2  = 0x04,
    NE_CH_DEFORMATION_3  = 0x05,
    NE_CH_TIME           = 0xF0,
    NE_CH_CYCLE          = 0xF1,
    NE_CH_DIAGNOSTICS    = 0xF2
} ne_channel_type_t;

/* ------------------------------------------------------------------ */
/*  Типы данных в канале                                              */
/* ------------------------------------------------------------------ */
typedef enum {
    NE_DT_FLOAT  = 0x00,
    NE_DT_DOUBLE = 0x01,
    NE_DT_UINT64 = 0x02,
    NE_DT_UINT32 = 0x03,
    NE_DT_UINT16 = 0x04,
    NE_DT_UINT8  = 0x05,
    NE_DT_INT64  = 0x06,
    NE_DT_INT32  = 0x07,
    NE_DT_INT16  = 0x08,
    NE_DT_INT8   = 0x09
} ne_data_type_t;

/* ------------------------------------------------------------------ */
/*  Режимы работы                                                     */
/* ------------------------------------------------------------------ */
typedef enum {
    NE_MODE_CONNECT    = 0,
    NE_MODE_TEST       = 1,
    NE_MODE_PRETEST    = 2,
    NE_MODE_SETTINGS   = 3,
    NE_MODE_DEVELOPER  = 4,
    NE_MODE_PROTECTION = 5
} ne_operating_mode_t;

/* ------------------------------------------------------------------ */
/*  Статусы тестирования                                              */
/* ------------------------------------------------------------------ */
typedef enum {
    NE_TEST_NOT_RUNNING    = 0,
    NE_TEST_DYNAMIC        = 1,
    NE_TEST_BLOCK          = 2,
    NE_TEST_STATIC         = 3
} ne_test_status_t;

/* ------------------------------------------------------------------ */
/*  Состояния рабочего режима                                         */
/* ------------------------------------------------------------------ */
typedef enum {
    NE_STATE_SETTING  = 0,
    NE_STATE_READY    = 1,
    NE_STATE_IN_PROG  = 2
} ne_work_state_t;

#ifdef __cplusplus
}
#endif

#endif /* NE_TYPES_H */
