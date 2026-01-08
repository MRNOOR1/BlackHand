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
#include "adc.h"

extern ADC_HandleTypeDef hadc1;

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
     // Keep for ADC
#define MEM_TEST_SIZE 1024  
uint8_t src_buffer[MEM_TEST_SIZE];
uint8_t dst_buffer[MEM_TEST_SIZE];



SemaphoreHandle_t ButtonSemaphore;


osThreadId EventTaskHandle;
/* USER CODE END Variables */
osThreadId defaultTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void EventTask(void const * argument);
void MtMTask(void const * argument);
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


  osThreadDef(mtmTask, MtMTask, osPriorityNormal, 0, 256);
  osThreadCreate(osThread(mtmTask), NULL);
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
void MtMTask(void const *argument)
{
  
  DMA_HandleTypeDef hdma_memtomem;
  for(int i = 0; i < MEM_TEST_SIZE; i++ ){
    src_buffer[i] = i;
  }
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  printf("[MemTest] DWT cycle counter enabled\n");

  // Enable DMA2 clock
  __HAL_RCC_DMA2_CLK_ENABLE();

  // Configure DMA
  hdma_memtomem.Instance = DMA2_Stream1;
  hdma_memtomem.Init.Channel = DMA_CHANNEL_0;
  hdma_memtomem.Init.Direction = DMA_MEMORY_TO_MEMORY;
  hdma_memtomem.Init.PeriphInc = DMA_PINC_ENABLE;
  hdma_memtomem.Init.MemInc = DMA_MINC_ENABLE;
  hdma_memtomem.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_memtomem.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_memtomem.Init.Mode = DMA_NORMAL;
  hdma_memtomem.Init.Priority = DMA_PRIORITY_HIGH;
  hdma_memtomem.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

  // Initialize DMA
  if (HAL_DMA_Init(&hdma_memtomem) != HAL_OK) {
      printf("[MemTest] ERROR: DMA init failed!\n");
      Error_Handler();
  }

  printf("[MemTest] DMA configured successfully\n");
  
  // ========== STEP 4: CPU Copy Test ==========
  printf("\n[MemTest] Starting CPU copy test...\n");

  // Clear destination buffer
  memset(dst_buffer, 0, MEM_TEST_SIZE);

  // Measure CPU copy
  uint32_t start_cpu = DWT->CYCCNT;
  for (int i = 0; i < MEM_TEST_SIZE; i++) {
      dst_buffer[i] = src_buffer[i];
  }
  uint32_t end_cpu = DWT->CYCCNT;

  uint32_t cycles_cpu = end_cpu - start_cpu;
  uint32_t time_us_cpu = cycles_cpu / 72;

  printf("[MemTest] CPU Copy: %lu cycles = %lu microseconds\n", cycles_cpu, time_us_cpu);

  // ========== STEP 5: DMA Copy Test ==========
  printf("\n[MemTest] Starting DMA copy test...\n");

  // Clear destination buffer
  memset(dst_buffer, 0, MEM_TEST_SIZE);

  // Measure DMA copy
  uint32_t start_dma = DWT->CYCCNT;

  // Start DMA transfer
  HAL_DMA_Start(&hdma_memtomem,
                (uint32_t)src_buffer,
                (uint32_t)dst_buffer,
                MEM_TEST_SIZE);

  // Wait for DMA to complete
  HAL_DMA_PollForTransfer(&hdma_memtomem, HAL_DMA_FULL_TRANSFER, 100);

  uint32_t end_dma = DWT->CYCCNT;

  uint32_t cycles_dma = end_dma - start_dma;
  uint32_t time_us_dma = cycles_dma / 72;

  printf("[MemTest] DMA Copy: %lu cycles = %lu microseconds\n",
         cycles_dma, time_us_dma);

         int errors = 0;
  for (int i = 0; i < MEM_TEST_SIZE; i++) {
      if (src_buffer[i] != dst_buffer[i]) {
          errors++;
      }
  }

  if (errors == 0) {
      printf("\n✓ Copy verified - all %d bytes match!\n", MEM_TEST_SIZE);
  } else {
      printf("\n✗ Copy FAILED - %d byte mismatches!\n", errors);
  }

  // Calculate speedup
  float speedup = (float)time_us_cpu / (float)time_us_dma;

  // Print comparison
  printf("\n========== PERFORMANCE COMPARISON ==========\n");
  printf("Data size: %d bytes\n", MEM_TEST_SIZE);
  printf("CPU copy:  %lu cycles = %lu μs\n", cycles_cpu, time_us_cpu);
  printf("DMA copy:  %lu cycles = %lu μs\n", cycles_dma, time_us_dma);
  printf("Speedup:   DMA is %.2fx faster!\n", speedup);
  printf("============================================\n\n");

  // Task done, delete itself
  vTaskDelete(NULL);


}


/* USER CODE END Application */
