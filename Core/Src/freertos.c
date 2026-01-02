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

SemaphoreHandle_t ButtonSemaphore;

osThreadId PeriodicTaskHandle;
osThreadId EventTaskHandle;
osThreadId OverflowTaskHandle;

void PeriodicTask(void const * argument);
void EventTask(void const * argument);
void OverflowTask(void const * argument);
// LED definitions for STM32F429I-Discovery
#define LED_PORT        GPIOG
#define LED_GREEN_PIN   GPIO_PIN_13  // Green LED
#define LED_RED_PIN     GPIO_PIN_14  // Red LED

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

/* USER CODE END Variables */
osThreadId defaultTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

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
  ButtonSemaphore = xSemaphoreCreateBinary();
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */
    // LED task: normal priority, 128 words stack
  osThreadDef(Periodic, PeriodicTask, osPriorityNormal, 0, 128);
  PeriodicTaskHandle = osThreadCreate(osThread(Periodic), NULL);


  // Button task: normal priority, 128 words stack
  osThreadDef(Event, EventTask, osPriorityNormal, 0, 128);
  EventTaskHandle = osThreadCreate(osThread(Event), NULL);

  osThreadDef(stackoverflow, OverflowTask, osPriorityNormal , 0, 128);
  OverflowTaskHandle = osThreadCreate(osThread(stackoverflow),NULL);
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

    //osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

void PeriodicTask(void const * argument){
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t frequency = pdMS_TO_TICKS(1000);
  uint32_t count = 0;

  for(;;){
    count++;
    printf("[%lu] Periodic tick #%lu\r\n", HAL_GetTick(), count);
    vTaskDelayUntil(&lastWakeTime, frequency);
  }
}

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
          HAL_GPIO_TogglePin(LED_PORT, LED_RED_PIN);
      }
  }
  void OverflowTask(void const * argument)
  {
     vTaskDelay(pdMS_TO_TICKS(5000)); 
      // Huge array that doesn't fit in 256-byte stack
      uint32_t hugeArray[100];  // 100 × 4 = 400 bytes (exceeds 256!)

      // Use it to prevent optimization
      for(uint32_t i = 0; i < 100; i++) {
          hugeArray[i] = i;
      }

      printf("OverflowTask: Array sum = %lu\n", hugeArray[0] + hugeArray[99]);

      for(;;) {
          vTaskDelay(pdMS_TO_TICKS(1000));
      }
  }




/* USER CODE END Application */
