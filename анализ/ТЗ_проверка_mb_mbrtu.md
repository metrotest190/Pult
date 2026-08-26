# ТЗ на аудит ядра диспетчера и RTU-машины состояний (mb.c, mbrtu.c)

## 1. Цель

Полный статический аудит последнего непроверенного слоя FreeModbus: **mb.c** (диспетчер `eMBPoll`, `eMBInit`/`eMBEnable`/`eMBDisable`, таблица обработчиков) и **rtu/mbrtu.c** (FSM приёма/передачи, T3.5, CRC, буфер кадра). Порт (mt_port/portevent/porttimer/portserial) и функции (FC01-17, FC23) уже проверены и исправлены — проверять их НЕ нужно, только согласованность с ядром.

## 2. Контекст

- Платформа: STM32F103C8 (Cortex-M3), STM32CubeIDE, HAL, gnu11, -O0, FreeModbus RTU.
- Связь: slave 0x0A, 115200 8N1, USART1, 4-проводная RS-485 (DE не требуется). Таймер T3.5 — TIM3, период 50 мкс.
- Карта регистров (ФИКСИРОВАНА, не предлагать изменений): Input FC04 0x4000-0x4002; Holding FC03/06/16 0x1000-0x1007 (4× float, старшее слово первым), 0x2000 (дисплей), 0x4002 (быстрая команда). Coils/Discrete не используются: колбэки возвращают MB_ENOREG → exception 0x02 **by design**.
- Адресация 0-based: `MB_PDU_FUNC_OFF=0`, `MB_PDU_DATA_OFF=1` (mbframe.h:65-66); обработчики пишут ответ в буфер кадра, адрес подставляет `eMBRTUSend` (голова буфера `ucRTUBuf[0]`).
- Уже исправлено (не переоткрывать):
  - portevent.c — FIFO(8) событий, `xMBPortEventPost` возвращает FALSE при переполнении без перезаписи.
  - mt_port.c — критические секции с сохранением/восстановлением PRIMASK, underflow → Error_Handler.
  - porttimer.c — перезапуск T3.5 с CNT=0, очистка UIF/NVIC-pending, без внешних inline.
  - functions/: FC15/16/23 — точная проверка длины payload; FC17 — byte-count, полный буфер, NULL-проверка, явная запись FC.
  - FC01-04: исправлен guard FC02 (`MB_FUNC_READ_DISCRETE_INPUTS_ENABLED`), границы количества — `<=` (FC01/02: 2000, FC03/04: 125).
  - Стек увеличен до 0x1000 (4 КБ).

## 3. Файлы для проверки

Ядро (главный предмет):
- `Modules/modbus/mb.c`
- `Modules/modbus/rtu/mbrtu.c`, `Modules/modbus/rtu/mbrtu.h`
- `Modules/modbus/rtu/mbcrc.c`, `Modules/modbus/rtu/mbcrc.h`

Контекст (только для согласованности):
- `Modules/modbus/include/mb.h`, `mbport.h`, `mbproto.h`, `mbframe.h`, `mbconfig.h`
- `Modules/modbus/port/mt_port.c`, `portevent.c`, `porttimer.c`, `portserial.c`, `port.h`

## 4. Конкретные вопросы

1. **eMBPoll и FIFO(8).** Цепочка EV_READY → EV_FRAME_RECEIVED → EV_EXECUTE → EV_FRAME_SENT при глубине очереди 8: не теряется ли кадр, если события накапливаются (например, EV_FRAME_RECEIVED + EV_EXECUTE + повторный EV_FRAME_RECEIVED до следующего `eMBPoll()`)? Обработка broadcast: mb.c ставит EV_EXECUTE и для broadcast (адрес 0), ответ не шлётся, но обработчик ВЫПОЛНЯЕТСЯ — корректно ли это для записей (FC06/16) и безопасно ли для чтений (ответ не уходит)? Что делает ветка EV_READY — не теряется ли она после eMBEnable?

2. **eMBRTUReceive / буфер.** Границы `ucRTUBuf` (usRcvBufferPos против MB_SER_PDU_SIZE_MAX), проверка CRC (`usMBCRC16(ucRTUBuf, usRcvBufferPos) == 0` — замечание: CRC считается по всему буферу с CRC включительно), `*pusLength = usRcvBufferPos - 1` (без байта адреса) — корректность для кадра минимальной длины (3 байта: addr+FC+CRC16) и максимальной. Что при переполнении буфера (STATE_RX_ERROR)?

3. **xMBRTUReceiveFSM.** Состояния INIT/IDLE/RCV/ERROR: перезапуск T3.5 на каждый байт, обработка первого байта (адрес), отсев чужих адресов, приём во время активной передачи (коллизия TX/RX на 4-проводной шине), переход в ERROR и событие EV_ERROR (обрабатывается ли оно в eMBPoll — в mb.c нет case EV_ERROR!).

4. **xMBRTUTransmitFSM.** Байт-за-байтом через `xMBPortSerialPutByte` (в portserial.c — HAL_UART_Transmit_IT на 1 байт), EV_FRAME_SENT, повторное включение RX (`vMBPortSerialEnable(TRUE, FALSE)`) — нет ли окна, когда RX выключен дольше необходимого; возврат `xNeedPoll` (в порту ISR-обёртки гасят `(void)`) — не теряется ли из-за этого событие.

5. **xMBRTUTimerT35Expired.** INIT → EV_READY, RCV → EV_FRAME_RECEIVED, ERROR — события и сброс `eRcvState = STATE_RX_IDLE`; взаимодействие с новым porttimer (перезапуск с CNT=0).

6. **eMBRTUInit / T3.5.** Две ветки: `ulBaudRate > 19200` → фиксированные 35 тиков × 50 мкс = **1750 мкс** (стандарт Modbus для >19200 — это НЕ баг, не переоткрывать); `≤ 19200` → `(7*220000)/(2*baud)` тиков. Проверить арифметику для 9600 и 115200, корректность `xMBPortTimersInit`, инициализацию CRC.

7. **eMBInit.** Проверка адреса (1..247, не 0/broadcast), dispatch по режиму (RTU/ASCII), проброс ошибок eMBRTUInit; параметр ucPort (в порту игнорируется — OK?).

8. **eMBEnable/eMBDisable.** Машина eMBState (STATE_DISABLED/STATE_ENABLED/STATE_ENABLING/STATE_DISABLING?), eMBRTUStart/Stop внутри критической секции (mbrtu.c:125-135 — вложенность с porttimer), повторный eMBEnable после eMBDisable — не теряется ли EV_READY.

9. **Таблица обработчиков.** `MB_FUNC_HANDLERS_MAX` против фактического числа включённых FC (mbconfig: 10 включено); `eMBRegisterCB` — перекрытие кодов, размер массива, порядок поиска в eMBPoll.

10. **mbcrc.c.** Полином/инициализация/порядок байт (младший первым в кадре), вызов в eMBRTUReceive, совместимость с CRC отладчика (Modbus RTU, init 0xFFFF, poly 0xA001).

## 5. Правила для ревьюера

- Цитировать ТОЛЬКО фактический код из приложенных файлов с `file:line`. Цитирование несуществующего кода = брак.
- Если нужен файл, которого нет в пакете — пометить «требует проверки по полному дереву», не делать вывод.
- **НЕ-дефекты** (известно, проверено): T3.5 = 1750 мкс при 115200 (фикс. значение для >19200); карта регистров; coils/discrete → 0x02 by design; отсутствие DE-переключения (4-проводная шина); 0-based адресация; `xNeedPoll` игнорируется ISR-обёртками (осознанно).
- Формат ответа — таблица: | ID | Место (file:line) | Дефект | Сценарий | Исправление | Приоритет (блокер/критич./высокий/средний/низкий) |.
- Особое внимание пункту 3 про EV_ERROR: в eMBPoll (mb.c) нет обработчика EV_ERROR — если mbrtu.c его постит, событие молча теряется; проверить, постится ли он вообще.

## 6. Ожидаемый результат

- Таблица находок с приоритетами.
- Блок «подтверждено корректным» по пунктам 1-10, где дефектов нет.
- Готовый код исправлений для каждого подтверждённого дефекта в стиле проекта (gnu11, HAL, FreeModbus-конвенции).
