/**
 * @file    modbus_rtu.h
 * @brief   Core Modbus RTU Slave — FC03 y FC06.
 *
 * @details Este módulo implementa la lógica de protocolo Modbus RTU para un
 *          dispositivo slave. Recibe frames crudos desde @ref modbus_uart,
 *          los valida, los despacha a @ref modbus_registers, y construye
 *          las respuestas (normales o de excepción) para enviar por
 *          @ref modbus_uart_transmit.
 *
 *          **Estructura de un frame Modbus RTU:**
 * @verbatim
 *   [ Addr ][ FC ][ Data ... ][ CRC_Lo ][ CRC_Hi ]
 *     1 byte  1 byte  N bytes    1 byte    1 byte
 * @endverbatim
 *
 *          **FC03 — Read Holding Registers:**
 * @verbatim
 *   Request : [ Addr ][ 0x03 ][ RegHi ][ RegLo ][ CntHi ][ CntLo ][ CRC ]
 *   Response: [ Addr ][ 0x03 ][ ByteCnt ][ D0Hi ][ D0Lo ]...[ CRC ]
 * @endverbatim
 *
 *          **FC06 — Write Single Register:**
 * @verbatim
 *   Request : [ Addr ][ 0x06 ][ RegHi ][ RegLo ][ ValHi ][ ValLo ][ CRC ]
 *   Response: eco exacto del request (mismo frame de 8 bytes)
 * @endverbatim
 *
 *          **Respuesta de excepción (cualquier FC):**
 * @verbatim
 *   Response: [ Addr ][ FC | 0x80 ][ ExceptionCode ][ CRC ]
 * @endverbatim
 *
 * @author  Armando
 * @date    2025
 */

#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"

/* ========================================================================== */
/*  Configuración del slave                                                     */
/* ========================================================================== */

/**
 * @brief Dirección Modbus de este dispositivo (1–247).
 * @note  0 es broadcast (sin respuesta). 248–255 reservados.
 */
#define MB_SLAVE_ADDRESS        1U

/* ========================================================================== */
/*  Constantes del protocolo                                                    */
/* ========================================================================== */

/** @brief Longitud mínima de cualquier frame RTU válido (Addr+FC+CRC = 4 bytes). */
#define MB_RTU_FRAME_MIN        4U

/** @brief Longitud fija de un request FC03 (8 bytes). */
#define MB_FC03_REQ_LEN         8U

/** @brief Longitud fija de un request FC06 (8 bytes). */
#define MB_FC06_REQ_LEN         8U

/** @brief Máximo de registros que FC03 puede leer en una sola request. */
#define MB_FC03_MAX_REGS        125U

/**
 * @brief Tamaño máximo del buffer de respuesta TX.
 *
 * FC03 con 16 registros: 1(addr) + 1(fc) + 1(bytecount) + 32(datos) + 2(crc) = 37 bytes.
 * Redondeamos a 64 por margen.
 */
#define MB_TX_BUF_SIZE          64U

/* ========================================================================== */
/*  Function codes soportados                                                   */
/* ========================================================================== */

#define MB_FC_READ_HOLDING_REGS     0x03U   /**< Leer Holding Registers        */
#define MB_FC_WRITE_SINGLE_REG      0x06U   /**< Escribir un Holding Register   */

/* ========================================================================== */
/*  Códigos de excepción Modbus                                                 */
/* ========================================================================== */

/** @defgroup modbus_exceptions Códigos de excepción Modbus estándar
 *  Definidos en Modbus Application Protocol V1.1b3, Sección 7.
 *  @{
 */
#define MB_EX_ILLEGAL_FUNCTION      0x01U   /**< FC no soportado                */
#define MB_EX_ILLEGAL_DATA_ADDRESS  0x02U   /**< Dirección de registro inválida */
#define MB_EX_ILLEGAL_DATA_VALUE    0x03U   /**< Valor fuera de rango           */
#define MB_EX_SERVER_DEVICE_FAILURE 0x04U   /**< Falla interna del dispositivo  */
/** @} */

/* ========================================================================== */
/*  Códigos de retorno internos                                                 */
/* ========================================================================== */

#define MB_RTU_OK               0   /**< Frame procesado y respondido OK     */
#define MB_RTU_IGNORED         -1   /**< Frame ignorado (broadcast, addr!=, frame muy corto) */
#define MB_RTU_ERR_CRC         -2   /**< CRC inválido                        */
#define MB_RTU_ERR_EXCEPTION   -3   /**< Se respondió con excepción Modbus   */

/* ========================================================================== */
/*  API pública                                                                 */
/* ========================================================================== */

/**
 * @brief  Inicializa el core Modbus RTU.
 *
 * @details Llama internamente a @ref modbus_registers_init() y
 *          @ref modbus_uart_init(). Debe llamarse al inicio de la task Modbus,
 *          después de que el scheduler arrancó.
 *
 * @param[in] modbus_task  Handle de la task Modbus (para pasar a uart_init).
 *
 * @retval  MB_RTU_OK       Inicialización correcta.
 * @retval  otro            Error de capa inferior (ver modbus_registers.h / modbus_uart.h).
 */
int modbus_rtu_init(TaskHandle_t modbus_task);

/**
 * @brief  Procesa un frame RTU recibido y emite la respuesta correspondiente.
 *
 * @details Secuencia interna:
 *          1. Verifica longitud mínima.
 *          2. Verifica que el byte de dirección coincida con @ref MB_SLAVE_ADDRESS.
 *             Si es broadcast (0x00) o dirección distinta, retorna @ref MB_RTU_IGNORED.
 *          3. Valida CRC-16.
 *          4. Despacha al handler del FC correspondiente (FC03 o FC06).
 *          5. Arma respuesta normal o de excepción.
 *          6. Transmite por @ref modbus_uart_transmit.
 *
 * @param[in]  frame   Buffer con el frame completo recibido (incluye CRC).
 * @param[in]  length  Longitud del frame en bytes.
 *
 * @retval  MB_RTU_OK         Frame válido, respuesta enviada.
 * @retval  MB_RTU_IGNORED    Frame ignorado legítimamente.
 * @retval  MB_RTU_ERR_CRC    CRC incorrecto, sin respuesta.
 * @retval  MB_RTU_ERR_EXCEPTION  Frame válido pero generó excepción Modbus.
 */
int modbus_rtu_process(const uint8_t *frame, size_t length);

/**
 * @brief  Task FreeRTOS que implementa el loop completo del slave Modbus RTU.
 *
 * @details Loop:
 *          1. Espera notificación de @ref modbus_uart_idle_callback (frame listo).
 *          2. Copia el frame con @ref modbus_uart_get_frame.
 *          3. Llama a @ref modbus_rtu_process.
 *          4. Vuelve a esperar.
 *
 *          Crear con:
 *          @code
 *            xTaskCreate(modbus_rtu_task, "ModbusRTU", 256, NULL, 3, NULL);
 *          @endcode
 *
 * @param[in]  argument  No usado (requerido por FreeRTOS).
 */
void modbus_rtu_task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_RTU_H */
