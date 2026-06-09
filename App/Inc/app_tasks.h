#ifndef APP_TASKS_H
#define APP_TASKS_H

/**
  ******************************************************************************
  * @file    app_tasks.h
  * @brief   Declaraciones públicas de las tasks de aplicación.
  *
  *          CubeIDE no toca este archivo. Agregar acá todo lo que main.c
  *          necesite conocer de las tasks (handles, eventos, colas).
  ******************************************************************************
  */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ---------------------------------------------------------------------------
 * Handles de tasks (extern — definidos en app_tasks.c)
 * Útiles para suspender/resumir/notificar desde otros módulos.
 * --------------------------------------------------------------------------- */
extern TaskHandle_t h_task_led;
extern TaskHandle_t h_task_oled;
extern TaskHandle_t h_task_uart_dbg;
extern TaskHandle_t h_task_modbus;

/* ---------------------------------------------------------------------------
 * Recursos compartidos
 * --------------------------------------------------------------------------- */

/** Mutex que serializa el acceso a printf / UART2. Definido en main.c. */
extern SemaphoreHandle_t g_uart_mutex;

/* ---------------------------------------------------------------------------
 * Macro de debug thread-safe.
 * Uso:  LOG("rx=%d\r\n", len);
 * No llamar desde ISR.
 * --------------------------------------------------------------------------- */
#define LOG(fmt, ...)                                        \
    do {                                                     \
        xSemaphoreTake(g_uart_mutex, portMAX_DELAY);         \
        printf(fmt, ##__VA_ARGS__);                          \
        xSemaphoreGive(g_uart_mutex);                        \
    } while (0)

/* ---------------------------------------------------------------------------
 * API pública
 * --------------------------------------------------------------------------- */

/**
  * @brief Crea todas las tasks de aplicación.
  *        Llamar desde main() antes de vTaskStartScheduler(),
  *        después de crear g_uart_mutex.
  */
void tasks_create(void);

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);


#endif /* APP_TASKS_H */
