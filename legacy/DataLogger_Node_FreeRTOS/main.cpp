/*******************************************************
 * @file module.c
 * @brief Brief description of module
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/

/*******************************************************
 * Macros
 *******************************************************/

/*******************************************************
 * Type Definitions
 *******************************************************/

/*******************************************************
 * Static Variables
 *******************************************************/

/*******************************************************
 * Static Function Prototypes
 *******************************************************/

/*******************************************************
 * Static Function Definitions
 *******************************************************/

/*******************************************************
 * Public Function Definitions
 *******************************************************/

#include "FreeRTOS.h"
#include "task.h"
#include <Arduino.h>

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;

    taskDISABLE_INTERRUPTS();

    pinMode(13, OUTPUT);

    while (1) {
        digitalWriteFast(13, HIGH);
        for (volatile int i = 0; i < 1000000; i++) {
        }
        digitalWriteFast(13, LOW);
        for (volatile int i = 0; i < 1000000; i++) {
        }
    }
}

extern "C" void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();

    pinMode(13, OUTPUT);

    while (1) {
        digitalWriteFast(13, HIGH);
        for (volatile int i = 0; i < 5000000; i++) {
        }
        digitalWriteFast(13, LOW);
        for (volatile int i = 0; i < 5000000; i++) {
        }
    }
}

extern "C" void vApplicationTickHook(void)
{ /* Leave empty for now. */
}

void foo(void *param)
{
    (void)param;
    pinMode(13, OUTPUT);
    while (1) {
        digitalWriteFast(13, HIGH);
        for (volatile int i = 0; i < 10000000; i++) {
        }

        digitalWriteFast(13, LOW);
        for (volatile int i = 0; i < 10000000; i++) {
        }
    }
}

extern "C" void (*volatile _VectorsRam[NVIC_NUM_INTERRUPTS + 16])(void);
extern "C" void vPortSVCHandler(void);
extern "C" void xPortPendSVHandler(void);
extern "C" void xPortSysTickHandler(void);

int main(void)
{
    pinMode(13, OUTPUT);

    // BaseType_t ok = xTaskCreate(foo, "foo", 256, NULL, 1, NULL);

    // _VectorsRam[11] = vPortSVCHandler;
    // _VectorsRam[14] = xPortPendSVHandler;
    // _VectorsRam[15] = xPortSysTickHandler;

    // vTaskStartScheduler();
    // while (1) {
    // }

    Serial.begin(115200);

    while (!Serial) {
        digitalWriteFast(13, !digitalReadFast(13));
        delay(500);
    }

    Serial.println("USB serial is working");

    int ok = 1;

    if (ok == pdPASS) {
        while (1) {
            digitalWriteFast(13, HIGH);
            delay(500);
            Serial.println("USB serial is working1");
            digitalWriteFast(13, LOW);
            delay(500);
        }
    }

    while (1) {
        digitalWriteFast(13, HIGH);
        delay(50);
        Serial.println("USB serial is working2");
        digitalWriteFast(13, LOW);
        delay(50);
    }
}
