/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "mb.h"
#include "mbport.h"
#include "mt_port.h"
#include "tjc_usart_hmi.h"
#include <stdbool.h>
#define FRAME_LENGTH 7
#define SIZE 1
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
typedef enum {
    STATE_POWER_ON,
    STATE_SEND_TO_SCREEN,
    STATE_SCREEN_POLLING,
    STATE_ADDT_SEND_CMD,
    STATE_ADDT_WAIT_FE,
    STATE_ADDT_SEND_DATA,
    STATE_ADDT_WAIT_DATA_DONE
} SystemState_t;

#define HISTORY_LEN 320
#define GRAPH_ID 1

#define TJC_CMD_HEADER_0  0x55
#define TJC_CMD_HEADER_1  0xAA

volatile uint16_t realtime_state_A = 0;
volatile uint16_t realtime_state_B = 0;

// --- ВОЗВРАЩАЕМ ИСХОДНУЮ АДРЕСАЦИЮ ---
#define REG_INPUT_START 0x4000
#define REG_INPUT_NREGS 3

#define REG_HOLDING_START   0x1000
#define REG_HOLDING_NREGS   8

#define REG_DISPLAY_START   0x2000
#define REG_DISPLAY_NREGS   1
// -------------------------------------

#define PORT_A_MASK  0x99CE
#define PORT_A_INPUT_MASK  (PORT_A_MASK & ~((1 << 6) | (1 << 7)))
#define PORT_B_MASK  0xF308

#define ENCODER_A_BIT   (1 << 14)
#define ENCODER_B_BIT   (1 << 13)
#define ENCODER_BTN_BIT (1 << 12)
#define ENCODER_MASK    (ENCODER_A_BIT | ENCODER_B_BIT)
#define PORT_B_BUTTON_MASK  (PORT_B_MASK & ~ENCODER_MASK)

#define GRAPH_WIDTH 320

// === КРИТИЧЕСКИЕ СЕКЦИИ ДЛЯ ЗАЩИТЫ ОТ ГОНОК ===
#define CRITICAL_SECTION_ENTER() __disable_irq()
#define CRITICAL_SECTION_EXIT()  __enable_irq()

// === ВОЛАТИЛЬНЫЕ ПЕРЕМЕННЫЕ (общие между прерываниями и основным циклом) ===
static volatile int16_t fast_cmd_value = 0;
static volatile uint8_t fast_btn_pressed = 0;
static volatile int16_t last_fast_cmd_value = 0;
static volatile uint8_t encoder_btn_pressed = 0;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
typedef union {
    float f;
    uint32_t u32;
    struct {
        uint16_t lo;
        uint16_t hi;
    } words;
} FloatUnion_t;
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;
DMA_HandleTypeDef hdma_tim4_up;
DMA_HandleTypeDef hdma_tim4_ch1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
static volatile uint8_t tjc_visible_flags = 0x07; // Биты 0,1,2 = c0,c1,c2 (все видны по умолчанию)
static uint8_t graph_prev_visible[3] = {0, 0, 0};

// История для 3-х графиков
static float graph_history[3][HISTORY_LEN];
static uint16_t graph_head[3] = {0, 0, 0};
static uint16_t graph_count[3] = {0, 0, 0};

// Машинная логика addt
static uint8_t redraw_queue = 0;
static int8_t current_redraw_ch = -1;
static uint8_t addt_busy = 0;

// Глобальные zoom и offset (перенесены из функции)
static float zoom[3] = {1.0f, 1.0f, 1.0f};
static float offset[3] = {0.0f, 0.0f, 0.0f};

volatile int pin;
volatile uint16_t latch_buffer_A = 0;
volatile uint16_t latch_buffer_B = 0;

volatile uint16_t input_buffer_A[SIZE];
volatile uint16_t input_buffer_B[SIZE];

uint16_t last_portA = 0;
uint16_t last_portB = 0;

static uint8_t encoder_prev_state = 0;
static uint8_t encoder_initialized = 0;
static volatile int8_t encoder_value = 0;

static SystemState_t currentState = STATE_POWER_ON;

static volatile uint16_t usRegHoldingBuf[REG_HOLDING_NREGS];

// === Float переменные для Modbus (IEEE 754, 2 регистра на float) ===
static volatile FloatUnion_t holdingFloat0;
static volatile FloatUnion_t holdingFloat1;
static volatile FloatUnion_t holdingFloat2;
static volatile FloatUnion_t holdingFloat3;

static volatile uint16_t display_value = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */
extern void intToStr(int num, char* str);
extern void uart_send_char(char ch);
extern void uart_send_string(char* str);
extern void ProcessButtons(void);
extern void ProcessEncoder(uint16_t current_B_raw);
extern void tjc_flush_tx(void);
extern uint8_t tjc_tx_is_busy(void);

// === ДОБАВЬТЕ ЭТУ ФУНКЦИЮ СЮДА (ПЕРЕД SwitchTechnology_Logic) ===
void ParseTjcCommands(void) {
    while (getRingBufferLength() >= 4) {
        if (read1ByteFromRingBuffer(0) == TJC_CMD_HEADER_0 &&
            read1ByteFromRingBuffer(1) == TJC_CMD_HEADER_1) {

            uint8_t ch   = read1ByteFromRingBuffer(2);
            uint8_t state = read1ByteFromRingBuffer(3);

            if (ch <= 2) {
                uint8_t old_flags = tjc_visible_flags;

                if (state) {
                    tjc_visible_flags |= (1 << ch);
                    if (!(old_flags & (1 << ch))) {
                        redraw_queue |= (1 << ch);
                    }
                } else {
                    tjc_visible_flags &= ~(1 << ch);
                }
            }
            deleteRingBuffer(4);
        } else {
            deleteRingBuffer(1);
        }
    }
}
void SwitchTechnology_Logic(void) {
    char txt_buf[12];
    static uint32_t lastSendTime = 0;
    float vals[3] = {holdingFloat0.f, holdingFloat1.f, holdingFloat2.f};
    static uint32_t addt_timeout = 0;

    ProcessButtons();
    eMBPoll();

    switch (currentState) {
        case STATE_POWER_ON:
            lastSendTime = HAL_GetTick();
            currentState = STATE_SEND_TO_SCREEN;
            break;

        case STATE_SEND_TO_SCREEN:
        {
            if (tjc_tx_is_busy()) break;
            uint8_t needs_clear[3] = {0, 0, 0};

            for (int i = 0; i < 3; i++) {
                uint8_t is_visible = (tjc_visible_flags & (1 << i)) ? 1 : 0;

                if (!is_visible && graph_prev_visible[i]) {
                    uart_send_string("cle 1,");
                    intToStr(i, txt_buf);
                    uart_send_string(txt_buf);
                    uart_send_char(0xff); uart_send_char(0xff); uart_send_char(0xff);
                    graph_prev_visible[i] = 0;
                } else if (is_visible && !graph_prev_visible[i]) {
                    graph_prev_visible[i] = 1;
                }

                graph_history[i][graph_head[i]] = vals[i];
                graph_head[i] = (graph_head[i] + 1) % HISTORY_LEN;
                if (graph_count[i] < HISTORY_LEN) graph_count[i]++;

                if (is_visible && graph_count[i] > 2) {
                    float max_val = -1e30f;
                    float min_val = 1e30f;
                    for (int j = 0; j < graph_count[i]; j++) {
                        uint16_t idx = (graph_head[i] - graph_count[i] + j + HISTORY_LEN) % HISTORY_LEN;
                        float h = graph_history[i][idx];
                        if (h > max_val) max_val = h;
                        if (h < min_val) min_val = h;
                    }

                    float amplitude = max_val - min_val;
                    if (amplitude < 0.001f) amplitude = 0.001f;

                    float target_offset = (max_val + min_val) / 2.0f;
                    float target_zoom = 240.0f / amplitude;
                    if (target_zoom < 0.001f) target_zoom = 0.001f;

                    // ИСПРАВЛЕНИЕ: Гистерезис 25% (0.8 и 1.25). Запрещает "съезжать" масштабу по незначительным изменениям.
                    if (target_zoom < zoom[i] * 0.8f || target_zoom > zoom[i] * 1.25f ||
                        target_offset < offset[i] - amplitude * 0.2f ||
                        target_offset > offset[i] + amplitude * 0.2f) {

                        zoom[i] = target_zoom;
                        offset[i] = target_offset;
                        needs_clear[i] = 1;
                    }
                }
            }

            tjc_send_val("x0", "val", (int)((holdingFloat0.f) * 10));
            tjc_send_val("x1", "val", (int)((holdingFloat1.f) * 10));
            tjc_send_val("x2", "val", (int)((holdingFloat2.f) * 10));
            tjc_send_val("x3", "val", (int)(holdingFloat3.f * 10));

            {
                char num_str[6];
                int display_val;
                if (encoder_btn_pressed) display_val = encoder_value;
                else display_val = (int)display_value;
                intToStr(display_val, num_str);
                tjc_send_txt("t9", "txt", num_str);
            }

            for (int i = 0; i < 3; i++) {
                if (!(tjc_visible_flags & (1 << i))) continue;

                if (needs_clear[i]) {
                    if (!addt_busy) {
                        redraw_queue |= (1 << i);
                    }
                    // ИСПРАВЛЕНИЕ: Пока ждёт перерисовки, НЕ ОТПРАВЛЯЕМ add! Иначе будут смешаны масштабы.
                } else {
                    float scaled_val = (vals[i] - offset[i]) * zoom[i];
                    int val = 128 + (int)scaled_val + i;
                    if (val < 0) val = 0;
                    if (val > 255) val = 255;

                    uart_send_string("add 1,");
                    intToStr(i, txt_buf);
                    uart_send_string(txt_buf);
                    uart_send_char(',');
                    intToStr(val, txt_buf);
                    uart_send_string(txt_buf);
                    uart_send_char(0xff); uart_send_char(0xff); uart_send_char(0xff);
                }
            }

            tjc_flush_tx();
            lastSendTime = HAL_GetTick();
            currentState = STATE_SCREEN_POLLING;
            break;
        }

        // --- Неблокирующий процесс addt ---
        case STATE_ADDT_SEND_CMD:
            if (tjc_tx_is_busy()) break;
            if (!(tjc_visible_flags & (1 << current_redraw_ch)) || graph_count[current_redraw_ch] == 0) {
                addt_busy = 0;
                currentState = STATE_SCREEN_POLLING;
                break;
            }

            uart_send_string("cle 1,");
            intToStr(current_redraw_ch, txt_buf);
            uart_send_string(txt_buf);
            uart_send_char(0xff); uart_send_char(0xff); uart_send_char(0xff);

            uart_send_string("addt 1,");
            intToStr(current_redraw_ch, txt_buf);
            uart_send_string(txt_buf);
            uart_send_char(',');
            intToStr(graph_count[current_redraw_ch], txt_buf);
            uart_send_string(txt_buf);
            uart_send_char(0xff); uart_send_char(0xff); uart_send_char(0xff);

            tjc_flush_tx();
            addt_timeout = HAL_GetTick();
            currentState = STATE_ADDT_WAIT_FE;
            break;

        case STATE_ADDT_WAIT_FE:
        {
            uint16_t len = getRingBufferLength();
            if (len > 0) {
                if (read1ByteFromRingBuffer(0) == 0xFE) {
                    deleteRingBuffer(len);
                    currentState = STATE_ADDT_SEND_DATA;
                } else {
                    deleteRingBuffer(1);
                }
            } else if (HAL_GetTick() - addt_timeout > 200) {
                deleteRingBuffer(len);
                addt_busy = 0;
                currentState = STATE_SCREEN_POLLING;
            }
            break;
        }

        case STATE_ADDT_SEND_DATA:
            if (tjc_tx_is_busy()) break;
            for (int i = 0; i < graph_count[current_redraw_ch]; i++) {
                uint16_t idx = (graph_head[current_redraw_ch] - graph_count[current_redraw_ch] + i + HISTORY_LEN) % HISTORY_LEN;
                float v = (graph_history[current_redraw_ch][idx] - offset[current_redraw_ch]) * zoom[current_redraw_ch];
                int val = 128 + (int)v + current_redraw_ch;
                if (val < 0) val = 0;
                if (val > 255) val = 255;
                uart_send_char((char)val);
            }
            tjc_flush_tx();
            addt_timeout = HAL_GetTick();
            currentState = STATE_ADDT_WAIT_DATA_DONE;
            break;

        case STATE_ADDT_WAIT_DATA_DONE:
            if (!tjc_tx_is_busy()) {
                addt_busy = 1; // Данные ушли, ждем 0xFD в фоне
                addt_timeout = HAL_GetTick();
                currentState = STATE_SCREEN_POLLING;
            } else if (HAL_GetTick() - addt_timeout > 500) {
                addt_busy = 0;
                currentState = STATE_SCREEN_POLLING;
            }
            break;

        // --- Фоновые задачи ---
        case STATE_SCREEN_POLLING:
        {
            if (addt_busy) {
                uint16_t len = getRingBufferLength();
                if (len > 0) {
                    if (read1ByteFromRingBuffer(0) == 0xFD) {
                        deleteRingBuffer(len);
                        addt_busy = 0; // Экран свободен
                        lastSendTime = HAL_GetTick(); // Синхронизируем таймер
                    } else {
                        deleteRingBuffer(1);
                    }
                } else if (HAL_GetTick() - addt_timeout > 1000) {
                    addt_busy = 0; // Таймаут
                }
                // ИСПРАВЛЕНИЕ: Строго запрещаем STATE_SEND_TO_SCREEN пока addt_busy!
                break;
            }

            // ИСПРАВЛЕНИЕ: Парсер вызывается только тут! Безопасно для 0xFE/0xFD.
            ParseTjcCommands();

            if (redraw_queue > 0) {
                current_redraw_ch = __builtin_ctz(redraw_queue);
                redraw_queue &= ~(1 << current_redraw_ch);
                currentState = STATE_ADDT_SEND_CMD;
                break;
            }

            if ((HAL_GetTick() - lastSendTime) >= 100) {
                currentState = STATE_SEND_TO_SCREEN;
            }
            break;
        }

        default:
            currentState = STATE_POWER_ON;
            break;
    }
}
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART3_UART_Init();
  MX_USART1_UART_Init();
  MX_TIM4_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
    MT_PORT_SetTimerModule(&htim3);
    MT_PORT_SetUartModule(&huart1);

    eMBErrorCode eStatus;
    eStatus = eMBInit(MB_RTU, 0x0A, 0, 115200, MB_PAR_NONE);
    if (eStatus != MB_ENOERR) {
        Error_Handler();
    }
    eStatus = eMBEnable();
    if (eStatus != MB_ENOERR) {
        Error_Handler();
    }

    initRingBuffer();
    if (HAL_UART_Receive_IT(&TJC_UART, RxBuffer, 1) != HAL_OK) {
        Error_Handler();
    }

    usRegHoldingBuf[0] = 255;

    // === ИСПРАВЛЕНИЕ: циклический режим DMA для непрерывного чтения портов ===
    // Запускаем DMA для порта A (TIM4_UPDATE)
    HAL_DMA_Start(&hdma_tim4_up, (uint32_t)&GPIOA->IDR, (uint32_t)input_buffer_A, SIZE);

    // Запускаем DMA для порта B (TIM4_CH1)
    HAL_DMA_Start(&hdma_tim4_ch1, (uint32_t)&GPIOB->IDR, (uint32_t)input_buffer_B, SIZE);

    // Включаем прерывания по завершении DMA для перезапуска
    __HAL_DMA_ENABLE_IT(&hdma_tim4_up, DMA_IT_TC);
    __HAL_DMA_ENABLE_IT(&hdma_tim4_ch1, DMA_IT_TC);

    // Включаем DMA запросы от таймера
    __HAL_TIM_ENABLE_DMA(&htim4, TIM_DMA_UPDATE);
    __HAL_TIM_ENABLE_DMA(&htim4, TIM_DMA_CC1);

    // Запускаем таймер
    HAL_TIM_Base_Start(&htim4);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1) {
        SwitchTechnology_Logic();
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 71;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 49;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */
  /* USER CODE END TIM4_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */
  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 7199;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 499;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 250;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */
  /* USER CODE END TIM4_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */
  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */
  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */
  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */
  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */
  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */
  /* USER CODE END USART3_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
  /* DMA1_Channel7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pins : F2_Pin F1_Pin Return_Pin Record_zero_Pin
                           Stop_Pin */
  GPIO_InitStruct.Pin = F2_Pin|F1_Pin|Return_Pin|Record_zero_Pin
                          |Stop_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : Upper_grip_close_Pin Jog_up_Pin Jog_down_Pin Lower_grip_open_Pin */
  GPIO_InitStruct.Pin = Upper_grip_close_Pin|Jog_up_Pin|Jog_down_Pin|Lower_grip_open_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : Encoder_But_Pin Upper_grip_open_Pin Lower_grip_close_Pin */
  GPIO_InitStruct.Pin = Encoder_But_Pin|Upper_grip_open_Pin|Lower_grip_close_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : Encoder_down_Pin Encoder_up_Pin Protect_Pin Start_Pin */
  GPIO_InitStruct.Pin = Encoder_down_Pin|Encoder_up_Pin|Protect_Pin|Start_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
eMBErrorCode eMBRegInputCB(UCHAR *pucRegBuffer, USHORT usAddress, USHORT usNRegs) {
    eMBErrorCode eStatus = MB_ENOERR;
    int iRegIndex = (int)(usAddress - REG_INPUT_START);

    if (iRegIndex >= 0 && (iRegIndex + usNRegs) <= REG_INPUT_NREGS) {
        uint16_t local_A = input_buffer_A[0];
        uint16_t local_B = input_buffer_B[0];

        while (usNRegs > 0) {
            uint16_t usValue = 0;

            if (iRegIndex == 0) {
                usValue = (~local_A) & PORT_A_INPUT_MASK;
            }
            else if (iRegIndex == 1) {
                usValue = (~local_B) & PORT_B_BUTTON_MASK;
            }
            else if (iRegIndex == 2) {
                if (fast_btn_pressed) {
                    usValue = (uint16_t)fast_cmd_value;
                } else if (encoder_btn_pressed) {
                    usValue = (uint16_t)encoder_value;
                } else {
                    usValue = 0;
                }
            }

            *pucRegBuffer++ = (UCHAR)(usValue >> 8);
            *pucRegBuffer++ = (UCHAR)(usValue & 0xFF);

            iRegIndex++;
            usNRegs--;
        }
    } else {
        eStatus = MB_ENOREG;
    }
    return eStatus;
}

eMBErrorCode eMBRegHoldingCB(UCHAR *pucRegBuffer, USHORT usAddress,
        USHORT usNRegs, eMBRegisterMode eMode) {
    eMBErrorCode eStatus = MB_ENOERR;
    int iRegIndex;

    // 1. Обработка специальных регистров 0x4000-0x4002
    if (usAddress >= 0x4000 && usAddress + usNRegs <= 0x4003) {
        USHORT regAddr = usAddress;
        while (usNRegs > 0) {
            if (regAddr == 0x4002) {
                if (eMode == MB_REG_READ) {
                    int16_t val = fast_cmd_value;
                    *pucRegBuffer++ = (UCHAR)(val >> 8);
                    *pucRegBuffer++ = (UCHAR)(val & 0xFF);
                } else {
                    uint8_t high_byte = *pucRegBuffer++;
                    uint8_t low_byte = *pucRegBuffer++;
                    int16_t val = (int16_t)((high_byte << 8) | low_byte);

                    fast_cmd_value = val;
                    fast_btn_pressed = (val != 0) ? 1 : 0;
                }
            } else {
                if (eMode == MB_REG_READ) {
                    *pucRegBuffer++ = 0;
                    *pucRegBuffer++ = 0;
                } else {
                    pucRegBuffer += 2;
                }
            }
            regAddr++;
            usNRegs--;
        }
        return MB_ENOERR;
    }

    // 2. Обработка регистра дисплея 0x2000
    if (usAddress == REG_DISPLAY_START && usNRegs == 1) {
        if (eMode == MB_REG_READ) {
            *pucRegBuffer++ = (UCHAR)(display_value >> 8);
            *pucRegBuffer++ = (UCHAR)(display_value & 0xFF);
        } else {
            uint8_t high_byte = *pucRegBuffer++;
            uint8_t low_byte = *pucRegBuffer++;
            uint16_t val = (uint16_t)((high_byte << 8) | low_byte);

            if (val <= 30) {
                display_value = val;
            }
        }
        return MB_ENOERR;
    }

    // 3. Обработка стандартных Holding Registers (начинаются с 0x1000)
    if ((usAddress >= REG_HOLDING_START)
            && (usAddress + usNRegs <= REG_HOLDING_START + REG_HOLDING_NREGS)) {
        iRegIndex = (int) (usAddress - REG_HOLDING_START);

        switch (eMode) {
        case MB_REG_READ:
            while (usNRegs > 0) {
                *pucRegBuffer++ = (UCHAR) (usRegHoldingBuf[iRegIndex] >> 8);
                *pucRegBuffer++ = (UCHAR) (usRegHoldingBuf[iRegIndex] & 0xFF);
                iRegIndex++;
                usNRegs--;
            }
            break;

        case MB_REG_WRITE:
        {
            int start_idx = iRegIndex;
            int end_idx = iRegIndex + usNRegs - 1;

            while (usNRegs > 0) {
                uint8_t high_byte = *pucRegBuffer++;
                uint8_t low_byte = *pucRegBuffer++;

                usRegHoldingBuf[iRegIndex] = (uint16_t)((high_byte << 8) | low_byte);
                iRegIndex++;
                usNRegs--;
            }

            // === Конвертация IEEE 754 float (2 регистра на float) ===
            if (start_idx <= 0 && end_idx >= 1) {
                holdingFloat0.u32 = ((uint32_t)usRegHoldingBuf[0] << 16) | usRegHoldingBuf[1];
            }
            if (start_idx <= 2 && end_idx >= 3) {
                holdingFloat1.u32 = ((uint32_t)usRegHoldingBuf[2] << 16) | usRegHoldingBuf[3];
            }
            if (start_idx <= 4 && end_idx >= 5) {
                holdingFloat2.u32 = ((uint32_t)usRegHoldingBuf[4] << 16) | usRegHoldingBuf[5];
            }
            if (start_idx <= 6 && end_idx >= 7) {
                holdingFloat3.u32 = ((uint32_t)usRegHoldingBuf[6] << 16) | usRegHoldingBuf[7];
            }
            break;
        }
        }
    } else {
        eStatus = MB_ENOREG;
    }
    return eStatus;
}

/*----------------------------------------------------------------------------*/
eMBErrorCode eMBRegCoilsCB(UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNCoils,
                           eMBRegisterMode eMode)
{
    // В этом проекте катушки (Coils) не используются
    return MB_ENOREG;
}

/*----------------------------------------------------------------------------*/
eMBErrorCode eMBRegDiscreteCB(UCHAR *pucRegBuffer, USHORT usAddress,
        USHORT usNDiscrete)
{
    // В этом проекте дискретные входы (Discrete Inputs) не используются
    return MB_ENOREG;
}
void ProcessButtons(void) {
    uint16_t current_A = (~input_buffer_A[0]) & PORT_A_MASK;
    uint16_t current_B_raw = (~input_buffer_B[0]) & PORT_B_MASK;

    ProcessEncoder(current_B_raw);

    // Используем корректную маску (включает кнопку PB12, исключает A/B энкодера)
    uint16_t current_B = current_B_raw & PORT_B_BUTTON_MASK;

    uint16_t pressed_A = current_A & (~last_portA);
    uint16_t pressed_B = current_B & (~last_portB);
    uint16_t released_A = last_portA & (~current_A);
    uint16_t released_B = last_portB & (~current_B);

    // === ИСПРАВЛЕНИЕ: проверка pressed_A != 0 перед __builtin_ctz ===
    if (pressed_A) {
        pin = __builtin_ctz(pressed_A);  // Безопасно: pressed_A != 0
        switch (pin) {
            case 1:  break;
            case 2:  break;
            case 3:  tjc_send_val("p6", "pic", 17); break;
            case 6:
                tjc_send_val("p6", "pic", 15);
                fast_cmd_value = 5; fast_btn_pressed = 1; last_fast_cmd_value = 5; encoder_value = 0;
                break;
            case 7:
                tjc_send_val("p6", "pic", 18);
                fast_cmd_value = -5; fast_btn_pressed = 1; last_fast_cmd_value = -5; encoder_value = 0;
                break;
            case 8:  tjc_send_val("p6", "pic", 20); break;
            case 11: tjc_send_val("p6", "pic", 19); break;
            case 12: tjc_send_val("p6", "pic", 22); break;
            case 15: tjc_send_val("p6", "pic", 24); break;
        }
    }

    if (released_A & (1 << 6)) {
        fast_cmd_value = 0; fast_btn_pressed = 0;
    }
    if (released_A & (1 << 7)) {
        fast_cmd_value = 0; fast_btn_pressed = 0;
    }

    if (released_B & (1 << 12)) {
        encoder_btn_pressed = 0; encoder_value = 0;
        tjc_send_val("p6", "pic", 27);
    }

    // === ИСПРАВЛЕНИЕ: проверка pressed_B != 0 перед __builtin_ctz ===
    if (pressed_B) {
        pin = __builtin_ctz(pressed_B);  // Безопасно: pressed_B != 0
        switch (pin) {
            case 3:  tjc_send_val("p6", "pic", 21); break;
            case 8:  tjc_send_val("p6", "pic", 26); break;
            case 9:  tjc_send_val("p6", "pic", 23); break;
            case 12: encoder_btn_pressed = 1; encoder_value = 0; break;
            case 15: tjc_send_val("p6", "pic", 16); break;
        }
    }

    last_portA = current_A;
    last_portB = current_B;
}

void ProcessEncoder(uint16_t current_B_raw) {
    uint8_t a = (current_B_raw >> 14) & 1;
    uint8_t b = (current_B_raw >> 13) & 1;
    uint8_t curr_state = (a << 1) | b;

    if (!encoder_initialized) {
        encoder_prev_state = curr_state;
        encoder_initialized = 1;
        return;
    }

    if (curr_state != encoder_prev_state) {
        static uint32_t last_transition_tick = 0;
        uint32_t now = HAL_GetTick();

        // Антидребезг уменьшен до 2 мс (100 мс убивали вращение)
        if (now - last_transition_tick < 2) {
            encoder_prev_state = curr_state;
            return;
        }
        last_transition_tick = now;

        uint8_t transition = (encoder_prev_state << 2) | curr_state;

        switch (transition) {
            case 0b0001: case 0b0111: case 0b1110: case 0b1000:
                if (encoder_value > -5) {
                    encoder_value--;
                    last_fast_cmd_value = 0;
                    tjc_send_val("p6", "pic", 18);
                }
                break;
            case 0b0010: case 0b1011: case 0b1101: case 0b0100:
                if (encoder_value < 5) {
                    encoder_value++;
                    last_fast_cmd_value = 0;
                    tjc_send_val("p6", "pic", 15);
                }
                break;
        }
        // tjc_flush_tx() УБРАНО ОТСЮДА!
        encoder_prev_state = curr_state;
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1) {
    }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
