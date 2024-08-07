#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "semphr.h"
#include"FreeRTOSConfig.h"



// Configuration
#define QUEUE_LENGTH 3 // change me to 10
#define QUEUE_ITEM_SIZE sizeof(char[20])
#define NUMBER_OF_SENDERS 3
#define CCM_RAM

// Global variables
QueueHandle_t xQueue;
SemaphoreHandle_t xSenderSemaphore[NUMBER_OF_SENDERS], xReceiverSemaphore;
int sentMessages[NUMBER_OF_SENDERS] = {0};
int blockedMessages[NUMBER_OF_SENDERS] = {0};
int receivedMessages = 0;
volatile TickType_t totalWaitTime[NUMBER_OF_SENDERS] = {0};
TickType_t waitTime = 0; // Initialize waitTime



// Timer periods (in milliseconds)
const int lowerBoundValues[] = {50, 80, 110, 140, 170, 200};
const int upperBoundValues[] = {150, 200, 250, 300, 350, 400};
int currentPeriodIndex = -1;
const int Treceiver = 100;

// Timer handles
TimerHandle_t xSenderTimer[NUMBER_OF_SENDERS], xReceiverTimer;
TimerHandle_t xOneShotTimer, xAutoReloadTimer;

// Function prototypes
void vSenderTask(void *pvParameters);
void vReceiverTask(void *pvParameters);
void vSenderTimerCallback(TimerHandle_t xTimer);
void vReceiverTimerCallback(TimerHandle_t xTimer);
void vResetFunction1(void);
void vResetFunction2(void);
void prvOneShotTimerCallback(TimerHandle_t xTimer);
void prvAutoReloadTimerCallback(TimerHandle_t xTimer);
void vApplicationMallocFailedHook(void);
void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName);
void vApplicationIdleHook(void);
void vApplicationTickHook(void);
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize);
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize);

// Utility function to get random time period within bounds
int getRandomTimePeriod(int lowerBound, int upperBound) {
    return (rand() % (upperBound - lowerBound + 1)) + lowerBound;
}

int main() {
    srand(time(NULL));

    // Initialize the queue
    xQueue = xQueueCreate(QUEUE_LENGTH, QUEUE_ITEM_SIZE);

    // Create semaphores
    for (int i = 0; i < NUMBER_OF_SENDERS; i++) {
        xSenderSemaphore[i] = xSemaphoreCreateBinary();
    }
    xReceiverSemaphore = xSemaphoreCreateBinary();

    // Create tasks
    for (int i = 0; i < 2; i++) {
        xTaskCreate(vSenderTask, "Sender", configMINIMAL_STACK_SIZE, (void *)i, 2, NULL);
    }
    xTaskCreate(vSenderTask, "Sender", configMINIMAL_STACK_SIZE, (void *)2, 3, NULL);

    xTaskCreate(vReceiverTask, "Receiver", configMINIMAL_STACK_SIZE, NULL, 4, NULL);

    // Create timers
    for (int i = 0; i < NUMBER_OF_SENDERS; i++) {
        waitTime=getRandomTimePeriod(lowerBoundValues[currentPeriodIndex], upperBoundValues[currentPeriodIndex]);
        xSenderTimer[i] = xTimerCreate("SenderTimer", pdMS_TO_TICKS(waitTime), pdTRUE, (void *)i, vSenderTimerCallback);
        xTimerStart(xSenderTimer[i], 0);
    }
    xReceiverTimer = xTimerCreate("ReceiverTimer", pdMS_TO_TICKS(Treceiver), pdTRUE, NULL, vReceiverTimerCallback);
    xTimerStart(xReceiverTimer, 0);

    // One-shot and auto-reload timers
    xOneShotTimer = xTimerCreate("OneShotTimer", pdMS_TO_TICKS(5000), pdFALSE, 0, prvOneShotTimerCallback);
    xAutoReloadTimer = xTimerCreate("AutoReloadTimer", pdMS_TO_TICKS(1000), pdTRUE, 0, prvAutoReloadTimerCallback);
    xTimerStart(xOneShotTimer, 0);
    xTimerStart(xAutoReloadTimer, 0);

    // Call the reset function initially
    vResetFunction1();

    // Start the scheduler
    vTaskStartScheduler();

    // Should never reach here
    return 0;
}

void vSenderTask(void *pvParameters) {
    int taskIndex = (int)pvParameters;
    char message[20];
    TickType_t xNextWakeTime;
    //TickType_t waitTime = 0; // Initialize waitTime


    // Initialize xNextWakeTime to the current tick count
    xNextWakeTime = xTaskGetTickCount();

    for (;;) {
        // Wait for the semaphore to be released by the timer callback
        xSemaphoreTake(xSenderSemaphore[taskIndex], portMAX_DELAY);

        // Create the message
        snprintf(message, 20, "Time is %lu", xTaskGetTickCount());

        // Try to send the message to the queue
        if (xQueueSend(xQueue, &message, 0) == pdPASS) {
            sentMessages[taskIndex]++;

        } else {
            blockedMessages[taskIndex]++;
        }

        // Sleep for a random period
        waitTime = getRandomTimePeriod(lowerBoundValues[currentPeriodIndex], upperBoundValues[currentPeriodIndex]);
        totalWaitTime[taskIndex] += waitTime; // Accumulate waitTime here

        xTimerChangePeriod(xSenderTimer[taskIndex], pdMS_TO_TICKS(waitTime), 0);
    }
}

void vReceiverTask(void *pvParameters) {
    char message[20];
    TickType_t xNextWakeTime;

    // Initialize xNextWakeTime to the current tick count
    xNextWakeTime = xTaskGetTickCount();

    for (;;) {
        // Wait for the semaphore to be released by the timer callback
        xSemaphoreTake(xReceiverSemaphore, portMAX_DELAY);

        // Check for messages in the queue
        if (xQueueReceive(xQueue, &message, 0) == pdPASS) {
            receivedMessages++;
            printf("Received: %s\n", message);


        }

        // Sleep for the fixed period , receiver timer is already periodic


    }
}

void vSenderTimerCallback(TimerHandle_t xTimer) {
    int taskIndex = (int)pvTimerGetTimerID(xTimer);
    xSemaphoreGive(xSenderSemaphore[taskIndex]);
}

void vReceiverTimerCallback(TimerHandle_t xTimer) {
        xSemaphoreGive(xReceiverSemaphore);
// important note: we had to give the semaphore at first and then check for number of received messages, if we check first then give the semaphore later,it might cause a significant increment in total blocked messages
      // If 1000 messages are received, reset the system
              if (receivedMessages >= 1000) {
                       vResetFunction2();
                       vResetFunction1();

                   }

}

void prvOneShotTimerCallback(TimerHandle_t xTimer) {
    trace_puts("One-shot timer callback executing");
    // Assuming turn_on function is available
    // turn_on(&blinkLeds[1]);
}

void prvAutoReloadTimerCallback(TimerHandle_t xTimer) {
    trace_puts("Auto-Reload timer callback executing");
    // Assuming isOn and turn_off/turn_on functions are available
    // if (isOn(blinkLeds[0])) {
    //     turn_off(&blinkLeds[0]);
    // } else {
    //     turn_on(&blinkLeds[0]);
    // }
}
// reset function upon design are separated to 2 sections vResetFunction1 called in main only while both VresetFunction1 and vResetFunction2 called in receivercallback function
void vResetFunction1(void) {


    // Reset counters
    for (int i = 0; i < NUMBER_OF_SENDERS; i++) {
        sentMessages[i] = 0;
        blockedMessages[i] = 0;
    }
    receivedMessages = 0;

    // Clear the queue
    xQueueReset(xQueue);

    // Update the timer periods
    currentPeriodIndex++;
    if (currentPeriodIndex >= sizeof(lowerBoundValues) / sizeof(lowerBoundValues[0])) {
        // If all values are used, destroy the timers and stop execution
        for (int i = 0; i < NUMBER_OF_SENDERS; i++) {
            xTimerDelete(xSenderTimer[i], 0);
        }
        xTimerDelete(xReceiverTimer, 0);
        printf("Game Over\n");
        exit(0);   // terminate
        vTaskEndScheduler();
    } else {
        // Update the timer periods
        for (int i = 0; i < NUMBER_OF_SENDERS; i++) {
            xTimerChangePeriod(xSenderTimer[i], pdMS_TO_TICKS(getRandomTimePeriod(lowerBoundValues[currentPeriodIndex], upperBoundValues[currentPeriodIndex])), 0);
        }
    }
}
void vResetFunction2(void) {
    // Print the statistics
    printf("Total Sent Messages: %d\n", sentMessages[0] + sentMessages[1] + sentMessages[2]);
    printf("Total Blocked Messages: %d\n", blockedMessages[0] + blockedMessages[1] + blockedMessages[2]);
    // calculation af average T sender for each iteration
    printf("Average Time Sender: %lu\n", (totalWaitTime[0] + totalWaitTime[1] + totalWaitTime[2]) / (sentMessages[0] + sentMessages[1] + sentMessages[2]+blockedMessages[0] + blockedMessages[1] + blockedMessages[2]));

    for (int i = 0; i < NUMBER_OF_SENDERS; i++) {
        printf("Task %d - Sent: %d, Blocked: %d\n", i, sentMessages[i], blockedMessages[i]);
    }
    // make totalWaitTime array ready for the next iteration
    totalWaitTime[0] = 0;
    totalWaitTime[1] = 0;
    totalWaitTime[2] = 0;

}

// Hook functions
void vApplicationMallocFailedHook(void) {
    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) {
    (void)pcTaskName;
    (void)pxTask;
    for (;;) {
    }
}

void vApplicationIdleHook(void) {
    volatile size_t xFreeStackSpace = xPortGetFreeHeapSize();
    if (xFreeStackSpace > 100) {
        // If there is a lot of heap remaining unallocated then
        // the value of configTOTAL_HEAP_SIZE in FreeRTOSConfig.h can be reduced.
    }
}

void vApplicationTickHook(void) {
}

StaticTask_t xIdleTaskTCB CCM_RAM;
StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE] CCM_RAM;

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize) {
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

StaticTask_t xTimerTaskTCB CCM_RAM;
StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH] CCM_RAM;

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize) {
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
