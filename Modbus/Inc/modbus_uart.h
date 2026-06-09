/**
 * @file    modbus_uart.h
 * @brief   Capa de transporte UART para Modbus RTU — USART1 + DMA + IDLE line.
 *
 * @details Este módulo abstrae completamente el hardware UART del core Modbus:
 *
 *          **Recepción (RX):**
 *          DMA1 Channel5 en modo Circular llena @ref MB_UART_RX_BUF_SIZE bytes
 *          de forma continua sin intervención del CPU. La detección del fin de
 *          frame se hace mediante la interrupción IDLE del USART1: cuando el
 *          bus queda en silencio ≥1 tiempo de carácter, la ISR calcula cuántos
 *          bytes llegaron y notifica a la task Modbus vía TaskNotify.
 *
 *          **Transmisión (TX):**
 *          Bloqueante por HAL_UART_Transmit(). Para frames de respuesta RTU
 *          (máx ~25 bytes a 9600 baud ≈ 26 ms) es aceptable dentro de la task.
 *          El pin DE/RE (PA8) se eleva antes de transmitir y se baja al final.
 *
 *          **Timing inter-frame:**
 *          Modbus RTU especifica silencio ≥ 3.5 × t_char entre frames.
 *          A 9600 baud: t_char = 1.042 ms → t_silence ≥ 3.65 ms.
 *          El IDLE interrupt dispara después de ~1 t_char de silencio, que es
 *          suficiente para detectar el fin de un frame real en la práctica.
 *
 * @par Configuración de hardware asumida (generada por CubeMX):
 *   - USART1: PA9 TX / PA10 RX, 9600 baud, 8N1
 *   - DMA1 Channel5: USART1_RX, Circular, byte
 *   - PA8: RS485_DE_Pin, GPIO Output PP, activo alto
 *
 * @author  Armando
 * @date    2025
 */

#ifndef MODBUS_UART_H
#define MODBUS_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"

/* ========================================================================== */
/*  Configuración                                                               */
/* ========================================================================== */

/**
 * @brief Tamaño del buffer circular de recepción DMA.
 *
 * Debe ser mayor que el frame Modbus RTU más largo posible.
 * FC03 con 125 registros = 255 bytes es el máximo del estándar.
 * Para 16 registros: 2 (addr+fc) + 1 (byte count) + 32 (datos) + 2 (CRC) = 37 bytes.
 * 64 bytes da margen cómodo sin desperdiciar RAM.
 */
#define MB_UART_RX_BUF_SIZE     64U

/**
 * @brief Timeout de transmisión TX en ms.
 * Calculado para el peor caso: 256 bytes × ~1.04 ms/byte @ 9600 baud ≈ 270 ms.
 */
#define MB_UART_TX_TIMEOUT_MS   300U

/**
 * @brief Valor de notificación FreeRTOS que la ISR envía a la task Modbus.
 * Usar un bit específico permite combinar con otras notificaciones en el futuro.
 */
#define MB_UART_NOTIFY_RX_DONE  (1UL << 0)

/* ========================================================================== */
/*  Códigos de retorno                                                          */
/* ========================================================================== */

#define MB_UART_OK              0   /**< Operación exitosa                   */
#define MB_UART_ERR_PARAM      -1   /**< Parámetro inválido                  */
#define MB_UART_ERR_HAL        -2   /**< Error de HAL (DMA/UART)             */
#define MB_UART_ERR_TIMEOUT    -3   /**< Timeout de TX                       */
#define MB_UART_ERR_OVERFLOW   -4   /**< Frame más grande que el buffer      */

/* ========================================================================== */
/*  API pública                                                                 */
/* ========================================================================== */

/**
 * @brief  Inicializa la capa UART Modbus.
 *
 * @details Registra el handle de la task Modbus para las notificaciones,
 *          arranca el DMA circular en modo recepción continua, y habilita
 *          la interrupción IDLE del USART1. Debe llamarse desde dentro de
 *          la task Modbus, después de que el scheduler arrancó.
 *
 * @param[in] modbus_task  Handle de la task Modbus que recibirá las
 *                         notificaciones de frame recibido.
 *
 * @retval  MB_UART_OK       Inicialización correcta.
 * @retval  MB_UART_ERR_HAL  Falla al arrancar el DMA.
 */
int modbus_uart_init(TaskHandle_t modbus_task);

/**
 * @brief  Copia el último frame recibido al buffer del llamador.
 *
 * @details Debe llamarse inmediatamente después de recibir la notificación
 *          @ref MB_UART_NOTIFY_RX_DONE. Calcula la cantidad de bytes
 *          recibidos comparando la posición actual del DMA con la última
 *          posición guardada, y copia los datos al buffer destino.
 *
 * @param[out] dst      Buffer destino donde se copia el frame.
 * @param[in]  dst_size Tamaño del buffer destino en bytes.
 * @param[out] rx_len   Cantidad de bytes efectivamente copiados.
 *
 * @retval  MB_UART_OK          Frame copiado correctamente.
 * @retval  MB_UART_ERR_PARAM   dst o rx_len son NULL.
 * @retval  MB_UART_ERR_OVERFLOW Frame más grande que dst_size; frame descartado.
 */
int modbus_uart_get_frame(uint8_t *dst, size_t dst_size, size_t *rx_len);

/**
 * @brief  Transmite un frame de respuesta por RS-485.
 *
 * @details Eleva DE/RE (PA8) antes de transmitir, envía los datos de forma
 *          bloqueante, espera a que el último bit salga del shift register
 *          del USART, y baja DE/RE. La espera posterior al TX evita cortar
 *          el último byte antes de que salga al bus físico.
 *
 * @param[in]  data    Puntero al frame a transmitir (PDU + CRC ya incluidos).
 * @param[in]  length  Longitud del frame en bytes.
 *
 * @retval  MB_UART_OK          Transmisión exitosa.
 * @retval  MB_UART_ERR_PARAM   data es NULL o length es 0.
 * @retval  MB_UART_ERR_TIMEOUT Timeout de HAL_UART_Transmit.
 */
int modbus_uart_transmit(const uint8_t *data, size_t length);

/**
 * @brief  Callback llamado desde la ISR de USART1 cuando se detecta IDLE.
 *
 * @details Esta función debe ser llamada desde HAL_UART_IRQHandler() en
 *          stm32f1xx_it.c dentro del bloque USER CODE de USART1_IRQHandler.
 *          Guarda la posición actual del DMA, calcula el tamaño del frame,
 *          y notifica a la task Modbus con @ref MB_UART_NOTIFY_RX_DONE.
 *
 * @note    Se ejecuta en contexto de interrupción — no bloquear, no usar
 *          funciones HAL que no sean ISR-safe.
 */
void modbus_uart_idle_callback(void);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_UART_H */
