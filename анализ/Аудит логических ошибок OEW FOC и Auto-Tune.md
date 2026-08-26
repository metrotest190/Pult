# Аудит логических ошибок OEW FOC и Auto-Tune

## Итоговое заключение

Проверены предоставленные исходники прошивки, GUI, руководство Auto-Tune, линкерный скрипт и Makefile. Проект **компилируется**, hosted-регрессионные тесты проходят, а CubeMX-самопроверка сообщает `PASS`. Однако это не означает работоспособность силового сценария: в текущем составе исходников присутствует несколько взаимоисключающих fail-closed-гейтов, из-за которых штатный путь «измерить → построить карту → запустить FOC/V/f» фактически замкнут.

Главный вывод: **измерения Auto-Tune, нормальный FOC и V/f сейчас не могут быть полноценно запущены из штатного production-сценария**. Это обусловлено не одной ошибкой, а цепочкой из нескольких блокировок: `both_enable()` намеренно не включает PWM; карта токовой реконструкции не может быть загружена через имеющийся CLI; commissioning-профили всегда отклоняются; `VFC_Start()` всегда возвращает `VFC_START_CONTEXT_UNVERIFIED`; а после команды `ch` флаг активности Auto-Tune остаётся установленным.

> Поэтому руководство `AUTOTUNE_GUI_GUIDE.md` описывает сценарий, который не соответствует фактическому состоянию предоставленной прошивки. Документ следует считать инструкцией к будущей commissioning-сборке, а не рабочей инструкцией для текущего бинарника.

## Сводка по приоритетам

| ID | Приоритет | Область | Вывод | Практический эффект |
|---|---|---|---|---|
| L1 | Критический | Auto-Tune/PWM | `both_enable()` только инвалидирует контекст и ставит `g_autotune_abort=1` | `ch`, `idle`, `oew`, `rr`, `noload`, `scope` не могут подать энергию |
| L2 | Критический | Map capture | `MapCaptureProfile_BuildRequest()` всегда возвращает `false` | Из production CLI невозможно начать измерение карты |
| L3 | Критический | FOC | В предоставленных файлах нет вызова `CurrentMap_LoadMeasured()` | `FOC_Start()` всегда отказывает с `FOC_START_MAP_UNVERIFIED` |
| L4 | Критический | V/f | `VFC_Start()` безусловно возвращает `VFC_START_CONTEXT_UNVERIFIED` | V/f не запускается ни при каких входных параметрах |
| L5 | Высокий | Auto-Tune state | `Autotune_DetectChannel()` ставит `g_autotune_busy=1` и не сбрасывает его | После `ch` commissioning capture блокируется как будто Auto-Tune всё ещё активен |
| L6 | Высокий | PWM release | В Makefile не задан `OEW_HS1_COMMISSIONING_RELEASE=1`, а default-deny возвращает `false` | `PWM_Enable()` и `PWM_ServiceCaptureStart()` всегда получают interlock error |
| L7 | Высокий | V/f trigger | При неудаче `VFC_Start()` после `TRIG_High()` не вызывается `TRIG_Low()` | PB6 может остаться постоянно высоким после неудачного старта |
| L8 | Высокий | V/f parameters | `VFC_Start(int32_t target_rpm)` игнорирует `target_rpm` | Даже после снятия гейта старт может идти с нулевой/старой целью |
| L9 | Средний | V/f pole pairs | `Autotune_Init()` обнуляет `g_motor_params.pole_pairs`; V/f использует fallback `1`, FOC — собственное значение `4` | Электрическая частота V/f может быть рассчитана для неверного числа пар полюсов |
| L10 | Средний | `mp=`/Lσ | В `main.c` `FOC_SetMotorParams()` вызывается до записи новых `Rr/Lm/Tr` в `g_motor_params` | Первый `mp=` рассчитывает `Lσ` по старым роторным параметрам |
| L11 | Средний | GUI logging | Формат `@VFLOG` содержит `drp`, но `CSV_FIELDS` его не записывает | Счётчик потерянных UART-пакетов не попадает в CSV |
| L12 | Средний | GUI capture | Sigrok-захват запускается в отдельном потоке одновременно с отправкой `vf=...` | Начальный PB6-фронт может произойти до фактического старта захвата |

## Критические логические блокировки

### L1. Auto-Tune вызывает функцию, которая сама останавливает тест

В `autotune.c` функция `both_enable()` больше не разрешает мосты. Она вызывает `PWM_InvalidateSampleContext()`, устанавливает `g_autotune_abort = 1` и печатает `@AT:ERROR:POWER_BLOCKED:no measured OEW sector/window map` [1]. После этого все основные тесты продолжают выполнение, но их циклы видят установленный `g_autotune_abort` и завершаются как aborted или fail.

При этом `both_enable()` вызывается из `Autotune_DetectChannel()`, `Autotune_MeasureRs_IV()`, `Autotune_MeasureAllPairs()`, `Autotune_Idle()`, `Autotune_MeasureLs_OEW()`, `Autotune_MeasureRr()`, `Autotune_MeasureNoLoad()` и `Autotune_Scope()` [1]. Следовательно, проблема системная, а не относящаяся к одной кнопке GUI.

**Исправление:** разделить режимы. Для normal FOC нужен только путь `PWM_Enable()` после доказанного sample context. Для service commissioning нужен отдельный, явно разрешённый `PWM_ServiceCaptureStart()` с immutable-профилем. Нельзя просто вернуть старую прямую запись `CCER/BDTR/CEN`: это разрушит установленный safety-дизайн.

### L2. Commissioning-профили всегда отклоняются

`MapCaptureProfile_IsApproved()` безусловно возвращает `false`, а `MapCaptureProfile_BuildRequest()` также безусловно возвращает `false` [2]. В `main.c` команда `mcarm=<profile_id>` вызывает `MapCaptureProfile_BuildRequest()` и в текущей сборке всегда отвечает `@MC:ARM:BLOCKED:PROFILE` [3].

Это выглядит намеренным fail-closed-поведением для generic source package, но оно противоречит наличию рабочего пользовательского цикла Auto-Tune и FOC в руководстве.

**Исправление:** либо явно маркировать текущую прошивку как `generic safety-locked`, либо добавить board-specific таблицу разрешённых профилей, согласованную с реальными PWM/ADC timing и проверяемую на идентичность платы. Профиль не должен приниматься произвольной UART-командой.

### L3. FOC не имеет пути загрузки карты

`FOC_Start()` требует `CurrentRecon_IsReady()`, затем `CurrentMap_SelectInitialStartupContext()` и валидный `PwmSampleContext` [4]. Но среди предоставленных `.c`-файлов вызов `CurrentMap_LoadMeasured()` отсутствует: функция существует, валидирует карту, CRC, identity и 6×2 regions, но нигде не вызывается production-командой [5].

Даже успешный `mapcap drain` только выгружает записи в UART; он не строит объект `OewCurrentMap`, не вычисляет регионы и не устанавливает карту через `CurrentMap_LoadMeasured()` [3]. Поэтому `FOC_Start()` должен оставаться заблокированным всегда.

**Исправление:** добавить отдельный offline/commissioning pipeline: сборка записей → вычисление карты → проверка CRC/identity → атомарная загрузка карты при выключенных PWM/ADC → подтверждение `MAP_READY`. Если карта должна загружаться из Flash, нужен также проверенный формат хранения и версия данных.

### L4. V/f отключён безусловно

`VFC_Start()` проверяет базовые условия, затем независимо от них инвалидирует context и возвращает `VFC_START_CONTEXT_UNVERIFIED` [6]. Поля `target_rpm` не используются. Поэтому команда `vf=N` из `main.c` всегда попадает в ветвь `vfc_rc != VFC_START_OK` [7].

Это согласуется с fail-closed-комментариями в коде, но не согласуется с `vf_panel.py`, где кнопка `Start V/f` описана как рабочая closed-loop функция [8], и с GUI-инструкцией.

**Исправление:** сначала реализовать полноценный selector для V/f, который на каждом PWM/TRGO обновлении публикует допустимый context. Только после этого разрешать VFC. До реализации следует изменить GUI и документацию так, чтобы V/f отображался как недоступный, а не как рабочий.

## Состояния и защита

### L5. `g_autotune_busy` остаётся равным единице после `ch`

`Autotune_DetectChannel()` устанавливает `g_autotune_busy = 1` в начале [9]. В отличие от `AT_TestBegin()`/`AT_TestEnd()`, функция не сбрасывает его в ноль ни на успешном пути, ни на большинстве error-путей. `Autotune_ProbePhase()` сбрасывает флаг только на одном успешном завершении [9].

`map_capture_port.c` запрещает capture, если `Autotune_IsActive()` возвращает true [10]. Поэтому даже если commissioning-профиль будет разрешён, последовательность `ch → mapcap` заблокируется после `ch`.

**Исправление:** использовать единый cleanup-объект с `goto cleanup` во всех Auto-Tune entry points и гарантированно выполнять `g_autotune_busy = 0`. Желательно также добавить в `main.c` явную проверку busy-состояния перед запуском другой сервисной операции.

### L6. Production PWM interlock всегда закрыт

В `pwm.c` при `OEW_HS1_COMMISSIONING_RELEASE == 0` функция `PWM_HardwareInterlockHealthy()` безусловно возвращает `false` [11]. В предоставленном Makefile этот macro для firmware build не задан; он используется только в отдельных hosted-тестах [12]. Поскольку `pwm_common_arm_preconditions()` требует этот interlock перед `PWM_Enable()` и service capture [11], штатный бинарник не может включить мост.

Это может быть правильным защитным режимом для generic сборки, но тогда руководство не должно обещать рабочие измерения и запуск FOC.

**Исправление:** не включать macro без аппаратной валидации. Нужно создать отдельную board-qualified release-конфигурацию, где interlock разрешается только после проверки SD/BKIN, timer break configuration, EN-линий и ADC trigger identity.

### L7. Неудачный V/f start оставляет PB6 в высоком состоянии

`main.c` вызывает `TRIG_High()` перед `VFC_Start()` [7]. Если `VFC_Start()` возвращает ошибку, код сбрасывает период логирования и делает `break`, но не вызывает `TRIG_Low()` [7]. Так как текущий `VFC_Start()` всегда возвращает ошибку, это воспроизводимый путь.

**Исправление:** добавить `TRIG_Low()` в error-ветвь непосредственно перед `break` и дополнительно делать его в `VFC_Stop()`/общем fault cleanup.

## Ошибки передачи параметров

### L8. `VFC_Start()` игнорирует цель скорости

В `main.c` передаётся `VFC_Start(a1)` [7], но реализация `VFC_Start(int32_t target_rpm)` объявляет аргумент неиспользуемым и не вызывает `VFC_SetTarget()` [6]. Даже если context gate будет снят, стартовая цель не гарантируется.

**Исправление:** либо вызвать `VFC_SetTarget(target_rpm)` внутри `VFC_Start()`, либо сделать API двухшаговым и запретить start без заранее установленной цели. Первый вариант безопаснее для текущего CLI.

### L9. Несогласованное число пар полюсов

`Autotune_Init()` обнуляет весь `g_motor_params` [13]. После этого `g_motor_params.pole_pairs == 0`. В `VFC_Update()` ноль заменяется на единицу [6], тогда как FOC хранит собственное значение `pole_pairs`, которое в `FOC_Init()` получает default `4` [14]. Команда `pp=N` синхронизирует оба значения, но GUI V/f не отправляет `pp` автоматически.

В результате V/f может рассчитывать `f_e = 1·n/60 + slip`, а FOC и документация могут предполагать четыре пары полюсов.

**Исправление:** иметь один источник истины. Например, после `FOC_Init()` явно инициализировать `g_motor_params.pole_pairs = FOC_GetPolePairs()`, а VFC использовать `FOC_GetPolePairs()` вместо отдельного поля.

### L10. Первый `mp=` рассчитывает Lσ по старым параметрам

В `main.c` при `mp=...` сначала вызывается `FOC_SetMotorParams(a1, a2, Vbus)`, а затем записываются `Rr`, `Lm`, `Tr`, `Ke`, `p` и `J` в `g_motor_params` [7]. Однако `FOC_SetMotorParams()` рассчитывает `Lσ` по текущим `g_motor_params.Lm_uH`, `Tr_rotor_us` и `Rr_mOhm` [14]. На первом применении они ещё содержат старые значения, обычно нули.

Следствие: первый `mp=` устанавливает `Lσ ≈ Ls`, даже если в той же команде переданы корректные `Rr/Lm/Tr`. Повторный `mpapply` может дать другой результат, поскольку globals уже обновлены.

**Исправление:** сначала валидировать и записать весь набор параметров во временную структуру, затем передать его в единую функцию применения, которая рассчитывает Lσ из этого же набора атомарно.

## GUI и телеметрия

### L11. Поле `drp` теряется при сохранении V/f CSV

Формат `@VFLOG` в `main.c` содержит поля до `fault`, а затем `drp` — счётчик отброшенных UART-пакетов [7]. `vf_panel.py` определяет `CSV_FIELDS` только до `fault` и при записи проходит именно по этому списку [8]. В результате `drp` никогда не попадает в `telemetry.csv`.

**Исправление:** добавить `drp` в конец `CSV_FIELDS` и проверить, что количество колонок совпадает с числом аргументов `UART_TrySendTelemetry()`.

### L12. Возможна гонка между sigrok и PB6-триггером

`VfPanel._vf_start()` запускает `_capture_sigrok()` в отдельном потоке, а затем сразу отправляет `vfk=...` и `vf=...` [8]. `VFC_Start()` должен поднять PB6 почти сразу после обработки UART-команды. Поток захвата ещё может находиться в `probe`/`sigrok-cli`, поэтому начальный фронт PB6 способен произойти до готовности анализатора.

**Исправление:** сначала подготовить и подтвердить capture, затем отправлять стартовую UART-команду; либо использовать аппаратный внешний trigger с pre-trigger buffer. Нельзя считать параллельный запуск потоков синхронизацией.

## Что проверено и что не является ошибкой

Сборка `make clean && make all` на рабочем репозитории завершилась успешно. Полученный размер firmware составил примерно 66 KiB `.text`, 516 байт `.data` и 11 KiB `.bss`; linker warnings относятся к ожидаемым `nosys.specs` syscall stubs, а не к ошибкам линковки.

Hosted-регрессионный набор завершился с PASS для FOC math, V/f control, CORDIC, voltage manager, ADC frame, current reconstruction, handoff gates, PWM interlock, protection, current-map selector и map-capture unit tests. CubeMX-самопроверка также сообщила `PASS` по пинам, TIM1/TIM8, ADC2 и PLL. Эти результаты подтверждают качество отдельных изолированных компонентов, но не опровергают системные runtime-гейты: тесты намеренно компилируют некоторые варианты с `OEW_HS1_COMMISSIONING_RELEASE=1`, тогда как обычная firmware-сборка его не задаёт.

## Рекомендуемый порядок исправления

Сначала следует принять архитектурное решение: текущая ветка является либо **generic safety-locked source package**, либо **commissioning/release firmware**. Для generic package нужно исправить GUI и руководство, чтобы они не обещали недоступные функции. Для commissioning/release firmware нужно по порядку реализовать board-qualified profile, путь построения и загрузки 6×2 current map, затем V/f selector, и только после этого разрешать PWM.

Технически первым исправлением должен быть единый lifecycle cleanup для Auto-Tune и устранение зависания `g_autotune_busy`. Вторым — отдельный pipeline map capture → map builder → `CurrentMap_LoadMeasured()`. Третьим — устранение безусловного `VFC_START_CONTEXT_UNVERIFIED` и добавление `VFC_SetTarget()`. После этого следует исправить PB6 cleanup, единый источник `pole_pairs`, атомарное применение `mp=` и поле `drp` в CSV.

## Недостающие материалы

Для проверки аппаратной корректности до уровня «можно безопасно включать мост» всё ещё нужны фактические результаты измерений SD/BKIN, oscilloscope/sigrok capture PWM и ADC trigger, а также board-qualified таблица commissioning profile. Исходники сами по себе подтверждают логические блокировки, но не могут подтвердить корректность уровней EN/SD, полярности силовых драйверов и реального соответствия каналов шунтов.

## References

[1]: file:///home/ubuntu/upload/autotune.c "Auto-Tune implementation"
[2]: file:///home/ubuntu/upload/map_capture_profiles.c "Commissioning profile gate"
[3]: file:///home/ubuntu/upload/main.c "Firmware CLI and command dispatch"
[4]: file:///home/ubuntu/upload/foc.c "FOC startup and control path"
[5]: file:///home/ubuntu/upload/current_map_selector.c "Measured current-map loader and selector"
[6]: file:///home/ubuntu/upload/vf_control.c "V/f control implementation"
[7]: file:///home/ubuntu/upload/main.c "V/f, motor-parameter and telemetry commands"
[8]: file:///home/ubuntu/upload/vf_panel.py "V/f GUI panel and CSV logger"
[9]: file:///home/ubuntu/upload/autotune.c "Auto-Tune busy-state paths"
[10]: file:///home/ubuntu/upload/map_capture_port.c "Map-capture control-path gate"
[11]: file:///home/ubuntu/upload/pwm.c "PWM interlock and arming logic"
[12]: file:///home/ubuntu/upload/Makefile "Firmware and hosted-test build flags"
[13]: file:///home/ubuntu/upload/autotune.c "Auto-Tune initialization"
[14]: file:///home/ubuntu/upload/foc.c "FOC parameter storage and PI/Lσ calculation"
