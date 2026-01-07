/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "usart.h"
#include <stdio.h>



/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */


// LED definitions for STM32F429I-Discovery
#define LED_PORT        GPIOG
#define LED_GREEN_PIN   GPIO_PIN_13  // Green LED
#define LED_RED_PIN     GPIO_PIN_14  // Red LED

#define ADC_BUFFER_SIZE 256
uint16_t adc_dma_buffer[ADC_BUFFER_SIZE];

SemaphoreHandle_t ButtonSemaphore;
// Semaphores for synchronization
SemaphoreHandle_t adcHalfCpltSem;   // Signaled when buffer half full
SemaphoreHandle_t adcFullCpltSem;   // Signaled when buffer full
// Statistics
uint32_t half_transfer_count = 0;
uint32_t full_transfer_count = 0;

osThreadId EventTaskHandle;
/* USER CODE END Variables */
osThreadId defaultTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void EventTask(void const * argument);
void StartAdcProcessTask(void const * argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);

extern void MX_USB_HOST_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* Hook prototypes */
void vApplicationIdleHook(void);
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 2 */
__weak void vApplicationIdleHook( void )
{
   /* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
   to 1 in FreeRTOSConfig.h. It will be called on each iteration of the idle
   task. It is essential that code added to this hook function never attempts
   to block in any way (for example, call xQueueReceive() with a block time
   specified, or call vTaskDelay()). If the application makes use of the
   vTaskDelete() API function (as this demo application does) then it is also
   important that vApplicationIdleHook() is permitted to return to its calling
   function, because it is the responsibility of the idle task to clean up
   memory allocated by the kernel to any task that has since been deleted. */
}
/* USER CODE END 2 */

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */

   // CRITICAL: Cannot use printf() in hooks! ISR context - not safe!
   // Just blink red LED rapidly to indicate stack overflow
   (void)xTask;        // Unused parameter
   (void)pcTaskName;   // Unused parameter

   while(1) {
      HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_14);  // Red LED
      for(volatile uint32_t i = 0; i < 500000; i++);  // Delay
   }
}

/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
__weak void vApplicationMallocFailedHook(void)
{
   /* vApplicationMallocFailedHook() will only be called if
   configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
   function that will get called if a call to pvPortMalloc() fails.
   pvPortMalloc() is called internally by the kernel whenever a task, queue,
   timer or semaphore is created. It is also called by various parts of the
   demo application. If heap_1.c or heap_2.c are used, then the size of the
   heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
   FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
   to query the size of free heap space that remains (although it does not
   provide information on how the remaining heap might be fragmented). */
}
/* USER CODE END 5 */

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
/* Create semaphore for half-transfer interrupt */
  adcHalfCpltSem = xSemaphoreCreateBinary();
  if (adcHalfCpltSem == NULL) {
    printf("ERROR: Failed to create adcHalfCpltSem!\n");
  } else {
    printf("✓ Half-transfer semaphore created\n");
  }
  
  /* Create semaphore for full-transfer interrupt */
  adcFullCpltSem = xSemaphoreCreateBinary();
  if (adcFullCpltSem == NULL) {
    printf("ERROR: Failed to create adcFullCpltSem!\n");
  } else {
    printf("✓ Full-transfer semaphore created\n");
  }
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
    ButtonSemaphore = xSemaphoreCreateBinary();
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */

  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 4096);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);


  osThreadDef(adcTask, StartAdcProcessTask, osPriorityNormal, 0, 256);
  osThreadCreate(osThread(adcTask), NULL);
  printf("✓ ADC processing task created\n");

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */



  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* init code for USB_HOST */
  MX_USB_HOST_Init();
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);  // Yield to other tasks
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */


  void EventTask(void const * argument)
  {
      uint32_t count = 0;

      for(;;)
      {
          // Block until semaphore given (button pressed)
          xSemaphoreTake(ButtonSemaphore, portMAX_DELAY);

          // Instant response!
          count++;
          printf("[%lu] Button event! Count: %lu\r\n", HAL_GetTick(), count);

          // Simulate slow processing work (100ms as per Task 3.4)
          vTaskDelay(pdMS_TO_TICKS(100));

          HAL_GPIO_TogglePin(LED_PORT, LED_RED_PIN);
          
      }
  }

/**
  * @brief  ADC processing task
  * @param  argument: Not used
  * @retval None
  */
void StartAdcProcessTask(void const *argument)
{
  /* USER CODE BEGIN StartAdcProcessTask */
  
  printf("[AdcTask] Started - waiting for data\n\n");
  
  for(;;)
  {
    /* ============================================
       Wait for EITHER half-complete OR full-complete
       ============================================ */
    
    // Check half-transfer first (non-blocking)
    if (xSemaphoreTake(adcHalfCpltSem, 0) == pdPASS)
    {
      /* ────────────────────────────────────────
         HALF-TRANSFER: Process first half [0-127]
         DMA is currently filling second half [128-255]
         ──────────────────────────────────────── */
      
      half_transfer_count++;
      
      printf("\n[HALF-TRANSFER #%lu]\n", half_transfer_count);
      printf("Processing samples [0-127] while DMA fills [128-255]\n");
      
      // Calculate average of first half
      uint32_t sum = 0;
      for (int i = 0; i < ADC_BUFFER_SIZE / 2; i++) {
        sum += adc_dma_buffer[i];
      }
      float average = sum / (ADC_BUFFER_SIZE / 2.0);
      float voltage = (average / 4095.0) * 3.3;
      
      printf("  Average ADC: %.1f\n", average);
      printf("  Average Voltage: %.3fV\n", voltage);
      
      // YOUR PROCESSING HERE
      // Example: Check if voltage in range, trigger actions, etc.
    }
    
    // Check full-transfer (non-blocking)
    if (xSemaphoreTake(adcFullCpltSem, 0) == pdPASS)
    {
      /* ────────────────────────────────────────
         FULL-TRANSFER: Process second half [128-255]
         DMA wrapped around, filling first half [0-127]
         ──────────────────────────────────────── */
      
      full_transfer_count++;
      
      printf("\n[FULL-TRANSFER #%lu]\n", full_transfer_count);
      printf("Processing samples [128-255] while DMA fills [0-127]\n");
      
      // Calculate average of second half
      uint32_t sum = 0;
      for (int i = ADC_BUFFER_SIZE / 2; i < ADC_BUFFER_SIZE; i++) {
        sum += adc_dma_buffer[i];
      }
      float average = sum / (ADC_BUFFER_SIZE / 2.0);
      float voltage = (average / 4095.0) * 3.3;
      
      printf("  Average ADC: %.1f\n", average);
      printf("  Average Voltage: %.3fV\n", voltage);
      
      // YOUR PROCESSING HERE
    }
    
    /* If neither semaphore available, wait a bit */
    if (xSemaphoreTake(adcHalfCpltSem, pdMS_TO_TICKS(100)) != pdPASS &&
        xSemaphoreTake(adcFullCpltSem, 0) != pdPASS)
    {
      // Timeout - no data in 100ms (shouldn't happen in continuous mode)
      printf("[AdcTask] Waiting for data...\n");
    }
  }
  
  /* USER CODE END StartAdcProcessTask */
}


/* USER CODE END Application */
