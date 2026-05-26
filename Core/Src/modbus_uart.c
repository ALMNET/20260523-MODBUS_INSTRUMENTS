/**
 * @file    modbus_uart.c
 * @brief   Implementación de la capa UART para Modbus RTU.
 *
 * @details Flujo de recepción completo:
 *
 * @verbatim
 *  Bus RS-485        DMA (circular)          ISR IDLE              Task Modbus
 *  ──────────        ──────────────          ────────              ───────────
 *  bytes llegan ──► rx_dma_buf[]  ──IDLE──► guarda dma_pos   ──► ulTaskNotifyTake
 *                                           calcula rx_len        modbus_uart_get_frame()
 *                                           notifica task         procesa PDU
 * @endverbatim
 *
 *          El buffer DMA es circular: el DMA escribe continuamente sin
 *          reiniciarse. La posición de lectura se actualiza en cada callback
 *          IDLE comparando el contador actual del DMA con el anterior.
 *
 * @author  Armando
 * @date    2025
 */

#include "modbus_uart.h"
#include "main.h"           /* huart1, RS485_DE_Pin, RS485_DE_GPIO_Port */

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/* ========================================================================== */
/*  Referencias a handles generados por CubeMX                                 */
/* ========================================================================== */

extern UART_HandleTypeDef  huart1;
extern DMA_HandleTypeDef   hdma_usart1_rx;

/* ========================================================================== */
/*  Estado interno (privado)                                                    */
/* ========================================================================== */

/** Buffer circular llenado por DMA. Solo la ISR y get_frame lo leen. */
static uint8_t      rx_dma_buf[MB_UART_RX_BUF_SIZE];

/**
 * @brief Posición de escritura del DMA en la última captura IDLE.
 *
 * El DMA cuenta hacia atrás (NDTR = bytes restantes en el buffer).
 * Posición de escritura = MB_UART_RX_BUF_SIZE - NDTR.
 */
static volatile uint16_t dma_write_pos = 0U;

/** Cantidad de bytes del último frame detectado por IDLE. */
static volatile uint16_t rx_frame_len  = 0U;

/** Handle de la task Modbus para notificaciones desde la ISR. */
static TaskHandle_t modbus_task_handle = NULL;

/* ========================================================================== */
/*  Helpers privados                                                            */
/* ========================================================================== */

/**
 * @brief  Devuelve la posición actual de escritura del DMA en el buffer.
 *
 * @details El registro NDTR (Number of Data To Transfer) del DMA cuenta
 *          hacia atrás desde el tamaño del buffer hasta 0, donde reinicia.
 *          La posición actual de escritura es (BUF_SIZE - NDTR).
 *
 * @return Índice en rx_dma_buf[] donde escribirá el DMA a continuación.
 */
static inline uint16_t dma_get_write_pos(void)
{
    return (uint16_t)(MB_UART_RX_BUF_SIZE -
                      __HAL_DMA_GET_COUNTER(&hdma_usart1_rx));
}

/**
 * @brief  Espera a que el shift register del USART vacíe el último byte.
 *
 * @details HAL_UART_Transmit() retorna cuando el TDR (Transmit Data Register)
 *          queda vacío, pero el shift register puede aún estar enviando el
 *          último byte. Si bajamos DE/RE inmediatamente, cortamos ese byte.
 *
 *          TC (Transmission Complete) flag indica que el shift register
 *          también quedó vacío. Lo esperamos manualmente.
 *
 * @param[in] timeout_ms Tiempo máximo de espera en ms.
 */
static void wait_tx_complete(uint32_t timeout_ms)
{
    uint32_t t_start = HAL_GetTick();
    while (!(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC)))
    {
        if ((HAL_GetTick() - t_start) >= timeout_ms)
            break;
    }
}

/* ========================================================================== */
/*  Implementación pública                                                      */
/* ========================================================================== */

int modbus_uart_init(TaskHandle_t modbus_task)
{
    if (modbus_task == NULL)
        return MB_UART_ERR_PARAM;

    modbus_task_handle = modbus_task;

    /* DE/RE en LOW = modo recepción (receiver habilitado, driver deshabilitado) */
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);

    /* Arrancar DMA circular sobre el buffer de recepción */
    if (HAL_UART_Receive_DMA(&huart1, rx_dma_buf, MB_UART_RX_BUF_SIZE) != HAL_OK)
        return MB_UART_ERR_HAL;

    /*
     * Habilitar la interrupción IDLE del USART1.
     * CubeMX habilita la IRQ del USART1 en NVIC pero NO activa el IDLE interrupt
     * dentro del periférico — hay que hacerlo a mano acá.
     */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);

    /* Inicializar posición de lectura en 0 */
    dma_write_pos = 0U;
    rx_frame_len  = 0U;

    return MB_UART_OK;
}

int modbus_uart_get_frame(uint8_t *dst, size_t dst_size, size_t *rx_len)
{
    if (dst == NULL || rx_len == NULL)
        return MB_UART_ERR_PARAM;

    uint16_t len = rx_frame_len;    /* captura atómica — uint16 en Cortex-M3 */

    if (len == 0U)
    {
        *rx_len = 0;
        return MB_UART_OK;
    }

    if (len > dst_size)
    {
        rx_frame_len = 0U;
        *rx_len = 0;
        return MB_UART_ERR_OVERFLOW;
    }

    /*
     * Calcular dónde empieza el frame en el buffer circular.
     * El DMA escribió hacia adelante desde last_read_pos hasta dma_write_pos.
     * Si dma_write_pos >= len: el frame es contiguo y empieza en (dma_write_pos - len).
     * Si dma_write_pos < len: el frame cruzó el wrap-around del buffer circular.
     */
    uint16_t write_pos = dma_write_pos;

    if (write_pos >= len)
    {
        /* Caso simple: frame contiguo en el buffer */
        memcpy(dst, &rx_dma_buf[write_pos - len], len);
    }
    else
    {
        /* Caso wrap: frame dividido entre el final y el inicio del buffer */
        uint16_t tail = len - write_pos;            /* bytes al final del buffer */
        memcpy(dst,        &rx_dma_buf[MB_UART_RX_BUF_SIZE - tail], tail);
        memcpy(dst + tail, &rx_dma_buf[0],          write_pos);
    }

    *rx_len      = len;
    rx_frame_len = 0U;          /* marcar como consumido */

    return MB_UART_OK;
}

int modbus_uart_transmit(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0U)
        return MB_UART_ERR_PARAM;

    /* Habilitar driver RS-485 (DE/RE = HIGH → transmisión) */
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);

    HAL_StatusTypeDef status = HAL_UART_Transmit(
        &huart1,
        (uint8_t *)data,
        (uint16_t)length,
        MB_UART_TX_TIMEOUT_MS
    );

    /* Esperar a que el shift register vacíe el último bit antes de bajar DE */
    wait_tx_complete(5U);

    /* Volver a modo recepción (DE/RE = LOW) */
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);

    if (status != HAL_OK)
        return MB_UART_ERR_TIMEOUT;

    return MB_UART_OK;
}

void modbus_uart_idle_callback(void)
{
    /*
     * Verificar que fue efectivamente un IDLE y limpiarlo.
     * Si no verificamos, cualquier interrupción del USART1 entraría acá.
     */
    if (!__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE))
        return;

    __HAL_UART_CLEAR_IDLEFLAG(&huart1);

    /* Posición actual del DMA = bytes escritos desde el inicio del buffer */
    uint16_t current_pos = dma_get_write_pos();

    /*
     * Calcular cuántos bytes llegaron desde la última captura.
     * Contempla el caso de wrap-around del buffer circular.
     */
    uint16_t last_pos = dma_write_pos;
    uint16_t len;

    if (current_pos >= last_pos)
        len = current_pos - last_pos;
    else
        len = (uint16_t)(MB_UART_RX_BUF_SIZE - last_pos + current_pos);

    if (len == 0U)
        return;     /* IDLE espurio — sin datos nuevos */

    /* Guardar estado para que get_frame() pueda reconstruir el frame */
    dma_write_pos = current_pos;
    rx_frame_len  = len;

    /* Notificar a la task Modbus desde ISR */
    BaseType_t higher_priority_woken = pdFALSE;
    vTaskNotifyGiveFromISR(modbus_task_handle, &higher_priority_woken);
    portYIELD_FROM_ISR(higher_priority_woken);
}
