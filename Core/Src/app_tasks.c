/**
  ******************************************************************************
  * @file    app_tasks.c
  * @brief   Implementación de las tasks FreeRTOS de aplicación.
  *
  *          CubeIDE no toca este archivo. Toda la lógica de aplicación
  *          vive acá o en módulos incluidos desde acá.
  *
  *          Tasks presentes:
  *            - task_led       Heartbeat visual (PC13)
  *            - task_oled      Estado en display SSD1306
  *            - task_uart_dbg  Debug por UART2 (usa LOG())
  *            - task_modbus    Servidor Modbus TCP via W5500 [TODO]
  ******************************************************************************
  */

#include <stdio.h>
#include <string.h>
#include "main.h"
#include "app_tasks.h"
#include "fonts.h"
#include "ssd1306.h"
#include "w5500_port.h"
#include "modbus_rtu.h"

/* ---------------------------------------------------------------------------
 * Handles (definidos acá, declarados extern en app_tasks.h)
 * --------------------------------------------------------------------------- */
TaskHandle_t h_task_led      = NULL;
TaskHandle_t h_task_oled     = NULL;
TaskHandle_t h_task_uart_dbg = NULL;
TaskHandle_t h_task_modbus   = NULL;

/* ---------------------------------------------------------------------------
 * Prototipos privados
 * --------------------------------------------------------------------------- */
static void task_led      (void *pvParameters);
static void task_oled     (void *pvParameters);
static void task_uart_dbg (void *pvParameters);
static void task_modbus_tcp   (void *pvParameters);

/* ---------------------------------------------------------------------------
 * tasks_create()
 * --------------------------------------------------------------------------- */

/**
  * @brief Crea todas las tasks. Llamar desde main() antes del scheduler.
  *
  * Stack sizing:
  *   task_led       64 words — solo GPIO, sin strings
  *   task_oled     256 words — SSD1306 + snprintf
  *   task_uart_dbg 128 words — printf liviano
  *   task_modbus   512 words — buffers frame Modbus + ioLibrary W5500
  *
  * Prioridades:
  *   3 — task_modbus   (respuesta TCP, no puede esperar)
  *   2 — task_oled     (display, puede perder un ciclo)
  *   2 — task_uart_dbg (debug, baja urgencia pero igual que OLED)
  *   1 — task_led      (heartbeat, lo más prescindible)
  */
void tasks_create(void)
{
    BaseType_t status;


    status = xTaskCreate(task_led, "LED", 64, NULL, 1, &h_task_led);

	configASSERT(status == pdPASS);

	status = xTaskCreate(task_oled, "OLED", 256, NULL, 2, &h_task_oled);

	configASSERT(status == pdPASS);

	/* --- Task Modbus RTU Slave ------------------------------------------- */
	    /*
	     * Stack: 256 words (1024 bytes en Cortex-M3).
	     *   - modbus_rtu_task usa ~200 bytes de stack local (rx_frame + variables)
	     *   - 256 words da margen cómodo para el call stack de HAL_UART_Transmit
	     *
	     * Prioridad: 3 (por encima de osPriorityNormal=2, por debajo de tareas críticas)
	     *   - Debe responder en <500 ms al master (típicamente <100 ms esperado)
	     *   - Prioridad más alta que tasks de display o logging
	     *
	     * El handle se guarda solo si necesitás suspenderla/resumirla desde afuera.
	     * Por ahora NULL es suficiente.
	     */
	status = xTaskCreate(
	        modbus_rtu_task,    /* función de la task                    */
	        "ModbusRTU",        /* nombre para el debugger               */
	        256,                /* stack en words (256 × 4 = 1024 bytes) */
	        NULL,               /* argumento — no usado                  */
	        3,                  /* prioridad                             */
	        NULL                /* handle de salida — no necesario       */
	    );

//	status = xTaskCreate(task_uart_dbg, "UART-DBG", 512, NULL, 2, &h_task_uart_dbg);
//
//	configASSERT(status == pdPASS);
	//
	//	status = xTaskCreate(task_modbus, "MODBUS", 1024, NULL, 2, &h_task_modbus);
	//
	//	configASSERT(status == pdPASS);
}

/* ---------------------------------------------------------------------------
 * Implementaciones
 * --------------------------------------------------------------------------- */

/**
  * @brief Heartbeat visual.
  *        Parpadeo 500ms/500ms = sistema vivo y scheduler corriendo.
  *        Si el LED se congela, algo bloqueó el scheduler o esta task.
  */
static void task_led(void *pvParameters)
{
    (void)pvParameters;
    while (1)
    {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
  * @brief Actualiza el display OLED cada 1 segundo.
  *
  *        Línea 0: título fijo
  *        Línea 1: uptime en segundos
  *        Línea 2: [reservada para estado Modbus / IP]
  *        Línea 3: [reservada para último registro leído]
  *
  * TODO: recibir estado de task_modbus via cola o variable compartida
  *       para mostrar IP asignada y estado de conexión.
  */
static void task_oled(void *pvParameters)
{
    (void)pvParameters;
    char buf[22];
    uint32_t uptime_s = 0;

    SSD1306_Init();

    SSD1306_GotoXY (0,0);
	SSD1306_Puts ("MENU", &Font_11x18, SSD1306_COLOR_WHITE);
	SSD1306_GotoXY (0, 20);
	SSD1306_Puts ("(1) CONGRESO", &Font_11x18, SSD1306_COLOR_WHITE);
	SSD1306_GotoXY (0, 40);
	SSD1306_Puts ("(2) HARTONG", &Font_11x18, SSD1306_COLOR_WHITE);

	SSD1306_UpdateScreen();
	vTaskDelay(pdMS_TO_TICKS(1000));

    while (1)
    {
        SSD1306_Clear();

        SSD1306_GotoXY(0, 0);
        SSD1306_Puts("Modbus TCP", &Font_11x18, SSD1306_COLOR_WHITE);

        SSD1306_GotoXY(0, 20);
        snprintf(buf, sizeof(buf), "up: %lu s", uptime_s);
        SSD1306_Puts(buf, &Font_11x18, SSD1306_COLOR_WHITE);

        /* Líneas 24 y 36 reservadas para estado W5500 / Modbus */
        SSD1306_GotoXY(0, 40);
        SSD1306_Puts("W5500: --", &Font_11x18, SSD1306_COLOR_WHITE);

        SSD1306_UpdateScreen();

        uptime_s++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
  * @brief Debug por UART2. Imprime un heartbeat cada 2 segundos.
  *        Usá LOG() desde cualquier otra task para debug adicional.
  *        Esta task no es estrictamente necesaria en producción.
  */
static void task_uart_dbg(void *pvParameters)
{
    (void)pvParameters;
    uint32_t count = 0;

    LOG("[DBG] sistema iniciado\r\n");

    while (1)
    {
        LOG("[DBG] tick %lu\r\n", count++);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/**
  * @brief Servidor Modbus TCP.
  *
  *        Flujo previsto (a implementar):
  *          1. w5500_init()          — SPI + reset + IP estática
  *          2. socket(0, TCP, 502)   — abrir socket en puerto Modbus
  *          3. listen(0)             — esperar conexión de master
  *          4. recv(0, buf, size)    — recibir frame
  *          5. modbus_process(buf)   — parsear MBAP + PDU, ejecutar FC
  *          6. send(0, resp, len)    — enviar respuesta
  *          7. volver a 4
  *
  *        Registros Modbus:
  *          holding_registers[0..N] — tabla de uint16_t, compartida
  *          con otras tasks via mutex o acceso atómico.
  *
  * TODO: integrar ioLibrary_Driver (WIZnet) para W5500
  * TODO: implementar FC03 (Read Holding Registers)
  * TODO: implementar FC06 (Write Single Register)
  */
static void task_modbus_tcp(void *pvParameters)
{
    (void)pvParameters;

//    LOG("[MODBUS] task arrancada\r\n");

    /* Init W5500: SPI + reset + red estática */
//    w5500_port_init();
//    LOG("[MODBUS] W5500 init OK — IP 192.168.1.100\r\n");

    /*
     * TODO próximo paso:
     *   socket(0, Sn_MR_TCP, 502, 0);
     *   listen(0);
     *   loop: recv → modbus_process → send
     */

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}



void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    // Si llegás acá, pcTaskName te dice cuál task se quedó sin stack
    (void)xTask;
    (void)pcTaskName;
    __disable_irq();
    while(1){}
}
