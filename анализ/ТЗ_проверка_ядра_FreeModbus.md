# ТЗ на проверку ядра FreeModbus (пульт разрывной машины, STM32F103C8)

## 1. Цель

Статическая проверка **ядра FreeModbus** (mb.c, rtu/, functions/, include/) в связке с уже исправленным портом (mt_port.c, portevent.c, porttimer.c, portserial.c). Предыдущие аудиты покрыли приложение/TJC (main.c, portserial.c), порт FreeModbus и IRQ-обработчики; ядро библиотеки до сих пор не ревьюилось.

## 2. Контекст

- Платформа: STM32F103C8 (Cortex-M3), STM32CubeIDE, HAL, gnu11, -O0 (Debug).
- FreeModbus RTU slave: адрес 0x0A, 115200 8N1, USART1 (PA9/PA10), 4-проводная RS-485 (переключение DE не требуется).
- Таймер T3.5: TIM3, период 50 мкс (PSC=71, ARR=49, APB1 36 МГц → 50 мкс).
- Карта регистров (фиксирована, НЕ менять):
  - Input Registers (FC04): 0x4000 (порт A, инвертир.), 0x4001 (порт B, инвертир.), 0x4002 (энкодер/быстрая команда).
  - Holding (FC03/06/16): 0x1000-0x1007 = 4× float IEEE754 (старшее слово первым); 0x2000 = дисплей (uint16, запись ≤30); 0x4002 = быстрая команда int16.
  - Coils и Discrete Inputs не используются: колбэки возвращают MB_ENOREG — это by design (мастер получит exception 0x02).
- Колбэки регистров: `eMBRegInputCB()` / `eMBRegHoldingCB()` в main.c. Адресация 0-based: библиотека передаёт адрес из кадра БЕЗ +1 (проверено в mbfuncinput.c:94, mbfuncholding.c:88/133/202 — совпадает с ожиданиями колбэков).
- Уже исправленный порт (последняя версия, интеграция выполнена, сборка 0 ошибок/0 предупреждений):
  - `portevent.c` — FIFO на 8 событий под критической секцией; `xMBPortEventPost()` возвращает FALSE при переполнении **без перезаписи**.
  - `mt_port.c` — критическая секция с сохранением/восстановлением PRIMASK, счётчик вложенности, underflow → Error_Handler().
  - `porttimer.c` — перезапуск T3.5 с CNT=0, очистка UIF и NVIC-pending, без внешних inline.
  - `portserial.c` — неблокирующая передача TJC (не относится к ядру, но файл включён для контекста колбэков HAL_UART_*).

## 3. Файлы для проверки

Ядро (главный предмет):
- `Modules/modbus/mb.c`
- `Modules/modbus/rtu/mbrtu.c`, `Modules/modbus/rtu/mbrtu.h`
- `Modules/modbus/rtu/mbcrc.c`, `Modules/modbus/rtu/mbcrc.h`
- `Modules/modbus/functions/mbfunccoils.c`, `mbfuncdiag.c`, `mbfuncdisc.c`, `mbfuncholding.c`, `mbfuncinput.c`, `mbfuncother.c`, `mbutils.c`
- `Modules/modbus/include/mb.h`, `mbconfig.h`, `mbport.h`, `mbproto.h`, `mbframe.h`, `mbfunc.h`, `mbutils.h`

Порт (контекст, уже исправлен — проверять на согласованность с ядром, а не «переоткрывать»):
- `Modules/modbus/port/mt_port.c`, `portevent.c`, `porttimer.c`, `portserial.c`, `port.h`, `mt_port.h`

## 4. Конкретные вопросы

1. **Событийный цикл с FIFO(8).** В mb.c:365 `xMBPortEventPost(EV_EXECUTE)` игнорирует результат; в mbrtu.c:308/329/335 результат Post кладётся в `xNeedPoll`, но ISR-обёртки (portserial.c `prvvUARTRxISR`/`prvvUARTTxReadyISR`, porttimer.c `prvvTIMERExpiredISR`) гасят его `(void)`. Проверить:
   - не теряется ли кадр на границе «T3.5 истёк → событие в FIFO → eMBPoll()`;
   - корректность порядка EV_READY / EV_FRAME_RECEIVED / EV_EXECUTE / EV_FRAME_SENT при глубине 8;
   - что происходит при переполнении FIFO (Post==FALSE) в каждом вызывающем месте — где добавить счётчик диагностики.
2. **T3.5 тайминг с новым porttimer.** Перезапуск таймера на каждый принятый байт (xMBRTUReceiveFSM → vMBRTUStartTimer → vMBPortTimersEnable): первое прерывание ровно через 50 мкс после перезапуска? Межсимвольный интервал 1.5/3.5 символа при 115200 (1 символ ≈ 86,8 мкс; T1.5 ≈ 130 мкс, T3.5 ≈ 304 мкс — проверьте расчёт в eMBInit/eMBRTUInit: `usTim1Timerout50us`).
3. **FC23 (read/write multiple) включён** (MB_FUNC_READWRITE_HOLDING_ENABLED=1). eMBRegHoldingCB обрабатывает и READ и WRITE через общий `usRegHoldingBuf` + пересчёт float после любой записи. Проверить семантику FC23: запись применяется, затем читается тот же буфер — корректно ли это для нашего колбэка (адреса чтения и записи могут различаться, буфер один)?
4. **FC17 (slave ID, mbfuncother.c)** — содержимое буфера (32 байта), поля Device ID/Run Status/Additional Data; согласовано ли с mbconfig MB_FUNC_OTHER_REP_SLAVEID_BUF?
5. **FC08 diagnostics (mbfuncdiag.c)** — в mbconfig.h нет MB_FUNC_DIAG_ENABLED. Убедиться, что mbfuncdiag.c корректно отключён #if-ом и не попадает в сборку; если включён где-то ещё — указать.
6. **Исключения:** prveMBError2Exception — маппинг MB_ENOREG→0x02, MB_ENORSP→0x01, MB_EINVAL→0x03; корректность для всех вызовов колбэков (coils → MB_ENOREG → 0x02).
7. **Границы и длина кадра:** проверка usRegCount (макс 0x007D для чтения, 0x0078 для записи), длины PDU в eMBFunc*, обработка «мусорного» кадра с валидным CRC.
8. **mbcrc.c** — совпадает ли табличный CRC с полиномом Modbus (0xA001, init 0xFFFF); проверить порядок байт CRC в кадре (младший первым).

## 5. Правила для ревьюера

- Цитировать ТОЛЬКО фактический код из приложенных файлов, с указанием `file:line`. Запрещено цитировать код, которого нет в пакете («рецензии, цитирующие несуществующий код, считаются браком»).
- Если утверждение требует файла, которого нет в пакете (например, полный HAL/CMSIS) — явно пометить как «требует проверки по полному дереву», а не делать вывод.
- Известные факты, которые НЕ являются дефектами: карта регистров 0x1000/0x2000/0x4000/0x4002 (фиксирована), coils/discrete возвращают MB_ENOREG by design, отсутствие DE-переключения (4-проводная шина), адресация без +1 (проверена).
- Формат ответа — таблица: | ID | Место (file:line) | Дефект | Сценарий | Исправление | Приоритет (блокер/критич./высокий/средний/низкий) |.
- В конце — список файлов, которых не хватало для полной проверки.

## 6. Ожидаемый результат

- Таблица находок с приоритетами.
- Отдельный блок «подтверждено корректным» для пунктов 1-8, где дефектов нет.
- Конкретный текст исправлений (готовый код) для каждого подтверждённого дефекта, готовый к интеграции в существующий стиль проекта (gnu11, -O0, HAL).
