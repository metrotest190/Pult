/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define F2_Pin GPIO_PIN_1
#define F2_GPIO_Port GPIOA
#define F1_Pin GPIO_PIN_2
#define F1_GPIO_Port GPIOA
#define Upper_grip_close_Pin GPIO_PIN_3
#define Upper_grip_close_GPIO_Port GPIOA
#define Jog_up_Pin GPIO_PIN_6
#define Jog_up_GPIO_Port GPIOA
#define Jog_down_Pin GPIO_PIN_7
#define Jog_down_GPIO_Port GPIOA
#define Encoder_But_Pin GPIO_PIN_12
#define Encoder_But_GPIO_Port GPIOB
#define Encoder_down_Pin GPIO_PIN_13
#define Encoder_down_GPIO_Port GPIOB
#define Encoder_up_Pin GPIO_PIN_14
#define Encoder_up_GPIO_Port GPIOB
#define Upper_grip_open_Pin GPIO_PIN_15
#define Upper_grip_open_GPIO_Port GPIOB
#define Lower_grip_open_Pin GPIO_PIN_8
#define Lower_grip_open_GPIO_Port GPIOA
#define Return_Pin GPIO_PIN_11
#define Return_GPIO_Port GPIOA
#define Record_zero_Pin GPIO_PIN_12
#define Record_zero_GPIO_Port GPIOA
#define Stop_Pin GPIO_PIN_15
#define Stop_GPIO_Port GPIOA
#define Lower_grip_close_Pin GPIO_PIN_3
#define Lower_grip_close_GPIO_Port GPIOB
#define Protect_Pin GPIO_PIN_8
#define Protect_GPIO_Port GPIOB
#define Start_Pin GPIO_PIN_9
#define Start_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
