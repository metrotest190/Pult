# Статическая проверка ядра FreeModbus

**Контекст проверки:** STM32F103C8, RTU slave `0x0A`, USART1 115200 8N1, TIM3 с периодом 50 мкс, исправленный bare-metal port. Анализ выполнен только по приложенному исходному коду. Фиксированная карта регистров, отсутствие DE-переключения на 4‑проводной шине, 0-based адресация и возврат `MB_ENOREG` для coils/discrete не считаются дефектами.

> **Итог:** RTU FSM, CRC и базовый порядок событий с FIFO(8) в нормальном режиме согласованы. Найдены один критический дефект FC17, один высокий класс ошибок в проверке длины write-multiple PDU и несколько средних проблем отказоустойчивости и конфигурации.

## Находки

| ID | Место (file:line) | Дефект | Сценарий | Исправление | Приоритет |
|---|---|---|---|---|---|
| **FM-01** | `mbfuncother.c:79–84` | Ответ FC17 не содержит обязательный байт количества следующих данных. Функция копирует Slave ID сразу в `pucFrame[1]`, где должен находиться `byte count`; при старте `usMBSlaveIDLen == 0`, поэтому PDU состоит только из function code. | Мастер запрашивает FC17 и получает невалидный ответ либо интерпретирует первый байт Slave ID как длину, после чего кадр не проходит проверку клиента. | Записать `usMBSlaveIDLen` в `pucFrame[MB_PDU_DATA_OFF]`, копировать данные с `MB_PDU_DATA_OFF + 1` и увеличить `*usLen` на дополнительный байт. До вызова FC17 гарантированно вызвать `eMBSetSlaveID()`. | **Критич.** |
| **FM-02** | `mbfuncholding.c:117–134, 243–265`; `mbfunccoils.c:209–235` | FC16, FC23 и FC15 проверяют минимальную длину и счётчик байтов, но не требуют точного соответствия общей длины PDU величине byte count. | CRC-valid запрос с лишним хвостом проходит в callback. Более опасно: FC15 длиной 6 и FC16/FC23 длиной без payload могут пройти первичную проверку и передать callback указатель на отсутствующие/старые данные. | После чтения `byte count` требовать `*usLen == OFFSET_VALUES + byteCount`; для FC15 первичную проверку изменить на `>= MB_PDU_FUNC_WRITE_MUL_VALUES_OFF`. | **Высокий** |
| **FM-03** | `mbrtu.c:300–305`; `portserial.c:263–271` | `xMBRTUTransmitFSM()` игнорирует `FALSE` от `xMBPortSerialPutByte()`, но всё равно сдвигает указатель и уменьшает остаток. | Если `HAL_UART_Transmit_IT()` не стартовал, байт теряется, следующего TX-complete IRQ нет, FSM может остаться в `STATE_TX_XMIT` навсегда. | Проверять результат. При ошибке прекратить передачу, вернуть RX, перевести TX FSM в idle и увеличить счётчик ошибки. | **Высокий** |
| **FM-04** | `mb.c:358–366`; `mbrtu.c:308–313, 329–350`; `portevent.c:40–55`; `portserial.c:283–291`; `porttimer.c:18–22` | При переполнении FIFO `xMBPortEventPost()` возвращает `FALSE`, но `eMBPoll()` и ISR-обёртки не учитывают/не диагностируют этот исход. В bare-metal порте return `xNeedPoll` намеренно не используется, но потеря события остаётся невидимой. | Если FIFO заполнен, `EV_EXECUTE` или `EV_FRAME_RECEIVED` не попадёт в очередь; мастер увидит timeout. Причина не будет отличима от ошибки линии. | Ввести `volatile`-счётчик отказов публикации в `portevent.c` и инкрементировать/фиксировать ошибку в `mb.c` и RTU FSM. Для `EV_EXECUTE` не выполнять ответ без успешно опубликованного события. | **Средний** |
| **FM-05** | `mbfuncdisc.c:54`; `mbconfig.h:112–125`; `mb.c:122–124` | Реализация FC02 обёрнута в `#if MB_FUNC_READ_COILS_ENABLED`, а не в `MB_FUNC_READ_DISCRETE_INPUTS_ENABLED`. | При отключении coils и включённых discrete `mb.c` зарегистрирует `eMBFuncReadDiscreteInputs`, но функция не будет скомпилирована. В текущем ТЗ оба флага равны 1, поэтому сбой сейчас скрыт. | Заменить guard на `#if MB_FUNC_READ_DISCRETE_INPUTS_ENABLED > 0`. | **Средний** |
| **FM-06** | `mbfuncinput.c:78–80`; `mbfuncholding.c:47,187–188` | FC04 использует `< MB_PDU_FUNC_READ_REGCNT_MAX`, тогда как FC03 использует `<=`. При константе `0x007D` FC04 отклоняет количество 125. | Запрос FC04 на допустимое по заданному лимиту количество `0x007D` возвращает exception `0x03`; FC03 при том же количестве принимается. | Изменить `<` на `<=` в FC04. При необходимости синхронизировать аналогичные границы coils/discrete отдельным решением. | **Средний** |
| **FM-07** | `mbutils.c:120–136`; `mb.h:111–121` | Mapper возвращает `MB_EX_SLAVE_DEVICE_FAILURE` для `MB_EINVAL`. Если callback использует `MB_EINVAL` для некорректного значения, ожидаемый exception `0x03` не выдаётся. Символа `MB_ENORSP`, указанного в ТЗ, в приложенном `eMBErrorCode` нет. | Кастомный callback возвращает `MB_EINVAL`; мастер получает `0x04` вместо `0x03`. | Добавить case `MB_EINVAL → MB_EX_ILLEGAL_DATA_VALUE`. Не добавлять несуществующий `MB_ENORSP`; неподдержанная функция уже обрабатывается в `mb.c:370–397` как `MB_EX_ILLEGAL_FUNCTION`. | **Средний** |
| **FM-08** | `mbfuncother.c:60–69` | Проверка ёмкости FC17 использует строгий `<`, поэтому заполнение буфера ровно до 32 байтов отклоняется; при `usAdditionalLen > 0` не проверяется `pucAdditional != NULL`. | Максимально допустимые 30 additional bytes отвергаются; неверный API-вызов с NULL приводит к `memcpy` по нулевому указателю. | Допустить `usAdditionalLen <= MB_FUNC_OTHER_REP_SLAVEID_BUF - 2U`; для nonzero length проверить `pucAdditional`. | **Средний** |
| **FM-09** | `mbrtu.c:98–114, 233–279, 321–350`; `porttimer.c:39–66, 88–102` | При 115200 RTU использует только fixed timeout T3.5, а межсимвольный T1.5 явно не контролируется. Это типичное упрощение данного FSM, но не строгая проверка разрывов кадра. Комментарий `35 /* 1800us */` неверен для данного порта: 35 × 50 мкс = 1750 мкс. | Кадр с межсимвольной паузой больше T1.5, но меньше 1.75 мс, будет продолжен, а не отброшен как повреждённый. | Если нужна строгая совместимость, добавить отдельный T1.5/измерение периода байтов; иначе документировать ограничение. Исправить комментарий на `1750us`. | **Низкий** |

## Подтверждено корректным

| Пункт ТЗ | Статус | Фактическая проверка |
|---|---|---|
| FIFO(8): порядок нормальных событий | **Корректно при отсутствии переполнения** | `portevent.c:40–55` добавляет событие в tail FIFO, `portevent.c:58–75` снимает из head под критической секцией. RTU публикует `EV_READY`/`EV_FRAME_RECEIVED` после T3.5 (`mbrtu.c:321–350`), `eMBPoll()` снимает `EV_FRAME_RECEIVED` и затем ставит `EV_EXECUTE` (`mb.c:351–366`), TX завершение ставит `EV_FRAME_SENT` (`mbrtu.c:298–313`). |
| Граница «T3.5 → FIFO → eMBPoll» | **Корректно при успешном Post** | `xMBRTUTimerT35Expired()` вызывает Post до остановки таймера и перевода приёмника в idle (`mbrtu.c:325–350`); FIFO защищён PRIMASK-критической секцией. Поведение при `Post == FALSE` отражено как FM-04. |
| T3.5 на 115200 | **Корректно по фактическому таймеру** | Для baud >19200 ядро задаёт 35 тиков по 50 мкс (`mbrtu.c:95–114`), а порт считает 35 update events (`porttimer.c:95–102`): callback T3.5 будет примерно через **1.75 мс** после последнего байта. Первый hardware update произойдёт через 50 мкс, но это только первый software tick, не истечение T3.5. |
| FC23: порядок write → read | **Корректно** | FC23 вызывает `eMBRegHoldingCB(...WRITE)` до `eMBRegHoldingCB(...READ)` (`mbfuncholding.c:263–287`). Для общего `usRegHoldingBuf` из `main.c` это означает чтение уже обновлённого состояния, независимо от того, совпадают ли read/write адреса. Ограничение длины PDU — FM-02. |
| 0-based адресация | **Корректно** | FC04 извлекает `usRegAddress` прямо из двух байтов PDU без `+1` (`mbfuncinput.c:69–73`); FC03/06/16/23 действуют аналогично (`mbfuncholding.c:84–89, 119–134, 178–203, 245–265`). |
| Coils/Discrete по карте проекта | **Корректно by design** | Обработчики ядра вызывают callback и преобразуют `MB_ENOREG` через mapper (`mbfunccoils.c:233–241`, `mbfuncdisc.c:102–109`; `mbutils.c:126–128`) в `MB_EX_ILLEGAL_DATA_ADDRESS` (`0x02`). |
| FC08 diagnostics | **Отключён / не зарегистрирован** | В `mbconfig.h:97–125` нет флага diagnostics, `mb.c:94–125` не содержит handler FC08, а приложенный `mbfuncdiag.c:2–28` не содержит реализации. Наличие кодов diagnostics в `mbproto.h` не регистрирует функцию. |
| CRC16 и wire order | **Корректно** | Табличный алгоритм инициализируется `0xFFFF` и формирует значение в `mbcrc.c:83–97`. Независимая проверка по фактически извлечённым таблицам дала `"123456789" → 0x4B37`; `mbrtu.c:206–208` передаёт low byte, затем high byte. |
| Read PDU с мусорным хвостом | **Корректно** | FC04 требует точную длину PDU (`mbfuncinput.c:67`); FC03 аналогично (`mbfuncholding.c:176`); одиночная запись FC06 также (`mbfuncholding.c:82`). Проблема остаётся только в multiple-write путях — FM-02. |

## Готовые исправления

### 1. FC17: формат ответа и границы буфера

Замените тело `eMBSetSlaveID()` и `eMBFuncReportSlaveID()` в `mbfuncother.c`:

```c
eMBErrorCode
eMBSetSlaveID(UCHAR ucSlaveID, BOOL xIsRunning,
              UCHAR const *pucAdditional, USHORT usAdditionalLen)
{
    if ((usAdditionalLen > (MB_FUNC_OTHER_REP_SLAVEID_BUF - 2U)) ||
        ((usAdditionalLen != 0U) && (pucAdditional == NULL))) {
        return MB_EINVAL;
    }

    ucMBSlaveID[0] = ucSlaveID;
    ucMBSlaveID[1] = (UCHAR)(xIsRunning ? 0xFFU : 0x00U);
    if (usAdditionalLen != 0U) {
        memcpy(&ucMBSlaveID[2], pucAdditional, (size_t)usAdditionalLen);
    }
    usMBSlaveIDLen = (USHORT)(usAdditionalLen + 2U);
    return MB_ENOERR;
}

eMBException
eMBFuncReportSlaveID(UCHAR *pucFrame, USHORT *usLen)
{
    pucFrame[MB_PDU_DATA_OFF] = (UCHAR)usMBSlaveIDLen;
    memcpy(&pucFrame[MB_PDU_DATA_OFF + 1U], ucMBSlaveID,
           (size_t)usMBSlaveIDLen);
    *usLen = (USHORT)(MB_PDU_DATA_OFF + 1U + usMBSlaveIDLen);
    return MB_EX_NONE;
}
```

Инициализируйте FC17 из пользовательского блока `main.c` до `eMBEnable()`:

```c
static const UCHAR slaveIdExtra[] = { 'P', 'U', 'L', 'T' };

if (eMBSetSlaveID(0x0A, TRUE, slaveIdExtra, sizeof(slaveIdExtra)) != MB_ENOERR) {
    Error_Handler();
}
```

### 2. FC15/FC16/FC23: точная длина PDU

В FC16 после чтения `ucRegByteCount` измените внутреннее условие на:

```c
if ((usRegCount >= 1U) &&
    (usRegCount <= MB_PDU_FUNC_WRITE_MUL_REGCNT_MAX) &&
    (ucRegByteCount == (UCHAR)(2U * usRegCount)) &&
    (*usLen == (USHORT)(MB_PDU_FUNC_WRITE_MUL_VALUES_OFF + ucRegByteCount))) {
```

В FC23 добавьте к уже существующему условию:

```c
&& (*usLen == (USHORT)(MB_PDU_FUNC_READWRITE_WRITE_VALUES_OFF +
                       ucRegWriteByteCount))
```

В FC15 замените наружную проверку `*usLen > ...` на:

```c
if (*usLen >= MB_PDU_FUNC_WRITE_MUL_VALUES_OFF)
```

и добавьте к внутреннему условию:

```c
&& (*usLen == (USHORT)(MB_PDU_FUNC_WRITE_MUL_VALUES_OFF + ucByteCount))
```

### 3. UART TX failure в RTU FSM

В `mbrtu.c` замените ветку `usSndBufferCount != 0` в `xMBRTUTransmitFSM()`:

```c
if (usSndBufferCount != 0U) {
    if (xMBPortSerialPutByte((CHAR)*pucSndBufferCur) == TRUE) {
        pucSndBufferCur++;
        usSndBufferCount--;
    } else {
        /* The byte was not accepted by the port. Abort this response rather
           than consuming data without a future TX-complete interrupt. */
        eSndState = STATE_TX_IDLE;
        vMBPortSerialEnable(TRUE, FALSE);
    }
}
```

Для диагностики добавьте отдельный `volatile uint32_t mbRtuTxStartFailures;` и инкрементируйте его в ветке `else`; передавать `EV_FRAME_SENT` в этом случае нельзя, потому что response фактически не ушёл.

### 4. FC04, FC02 и mapper исключений

```c
/* mbfuncinput.c */
&& (usRegCount <= MB_PDU_FUNC_READ_REGCNT_MAX)

/* mbfuncdisc.c */
#if MB_FUNC_READ_DISCRETE_INPUTS_ENABLED > 0

/* mbutils.c, перед case MB_ETIMEDOUT */
case MB_EINVAL:
    eStatus = MB_EX_ILLEGAL_DATA_VALUE;
    break;
```

### 5. Наблюдение переполнения FIFO

В `portevent.c` добавьте счётчик:

```c
volatile uint32_t mbEventPostOverflowCount;
```

и в `xMBPortEventPost()` перед выходом из переполненной ветки:

```c
else {
    mbEventPostOverflowCount++;
}
```

В `mb.c:365` и `mbrtu.c:308/329/335` возврат `FALSE` от `xMBPortEventPost()` следует фиксировать отдельными счётчиками причины. Возвращаемое FSM значение можно не использовать в bare-metal IRQ-обёртке: оно предназначено для scheduler/RTOS, но счётчик переполнения необходим для диагностики реальной потери события.

## Недостающие файлы для полной проверки

| Файл / артефакт | Причина |
|---|---|
| `mbascii.h`, `mbtcp.h` и соответствующие исходники | В `mb.c:48–53` они подключаются только при `MB_ASCII_ENABLED`/`MB_TCP_ENABLED`; сейчас оба флага равны 0, поэтому отсутствие не блокирует проверенный RTU build. Полная проверка всех режимов невозможна. |
| Полное дерево STM32 HAL/CMSIS, startup и build log | Невозможно независимо выполнить целевую ARM-сборку, проверить реальные определения HAL/IRQ и предупреждения компилятора. |
| Актуальные `main.c`, `stm32f1xx_it.c`, `.ioc`, `.map` из одной ветки | Они необходимы для сквозной проверки фактической интеграции патчей, размера RAM/Flash и порядка инициализации, хотя ранее представленные версии использовались как контекст. |
| Спецификация/тестовый журнал Modbus master | Нужны для аппаратного подтверждения FC17, тайминга T3.5 и реакций клиента на exception/timeout. |

## Рекомендуемая последовательность интеграции

1. Сначала исправить **FM-01** и добавить один тест FC17: ответ должен иметь формат `11 <byte_count> <slave_id> <run> ...`.
2. Затем закрыть **FM-02** и отправить CRC-valid кадры FC15/16/23 с лишними байтами и с отсутствующим payload: ожидается exception `0x03`, callback не вызывается.
3. Исправить **FM-03**, после чего искусственно заставить `HAL_UART_Transmit_IT()` вернуть ошибку; RTU FSM должен вернуться в RX idle, а не зависнуть.
4. Добавить диагностику **FM-04** и убедиться, что счётчик переполнения FIFO остаётся нулевым при длительной нагрузке.
5. Выполнить Clean + Build и аппаратный тест на 115200: timer callback T3.5 ожидается примерно через 1.75 мс после последнего байта.
