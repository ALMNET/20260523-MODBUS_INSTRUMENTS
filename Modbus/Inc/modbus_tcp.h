/**
 * @file    modbus_tcp.h
 * @brief   Servidor Modbus TCP sobre W5500 — socket state machine + MBAP parser.
 *
 * @details Implementa el protocolo Modbus TCP (puerto 502) sobre el stack
 *          TCP/IP hardware del W5500 via ioLibrary_Driver.
 *
 *          **Diferencias Modbus TCP vs RTU:**
 *          - Sin CRC (TCP garantiza integridad)
 *          - Sin dirección de slave en el frame (reemplazada por Unit ID en MBAP)
 *          - Agrega MBAP header de 6 bytes antes de la PDU
 *
 *          **Estructura MBAP + PDU:**
 * @verbatim
 *   [ TI_Hi ][ TI_Lo ]  Transaction Identifier — eco al master
 *   [ PI_Hi ][ PI_Lo ]  Protocol Identifier    — siempre 0x0000
 *   [ Len_Hi][ Len_Lo]  Length                 — bytes que siguen (Unit ID + PDU)
 *   [ Unit  ]           Unit Identifier        — equivalente slave address RTU
 *   [ FC    ]           Function Code
 *   [ Data ...       ]  Datos del FC
 * @endverbatim
 *
 *          **Function codes soportados:**
 *          - FC03: Read Holding Registers
 *          - FC06: Write Single Register
 *
 *          **Máquina de estados del socket:**
 * @verbatim
 *   SOCK_CLOSED ──► socket()+listen() ──► SOCK_LISTEN
 *   SOCK_LISTEN ──► (esperar master)
 *   SOCK_ESTABLISHED ──► recv()+process()+send()
 *   SOCK_CLOSE_WAIT ──► disconnect()+close() ──► SOCK_CLOSED
 *   cualquier otro ──► close() ──► SOCK_CLOSED
 * @endverbatim
 *
 * @author  Armando
 * @date    2025
 */

#ifndef MODBUS_TCP_H
#define MODBUS_TCP_H

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

/** @brief Socket W5500 a usar para Modbus TCP (0–7). */
#define MB_TCP_SOCKET           0U

/** @brief Puerto Modbus estándar. */
#define MB_TCP_PORT             502U

/**
 * @brief Tamaño del buffer de RX/TX.
 * MBAP(7) + PDU máxima FC03 con 125 regs = 7 + 3 + 250 = 260 bytes.
 * 300 bytes da margen.
 */
#define MB_TCP_BUF_SIZE         300U

/** @brief Timeout de recv en ms — evita bloqueo eterno si el master cuelga. */
#define MB_TCP_RECV_TIMEOUT_MS  100U

/** @brief Longitud del MBAP header. */
#define MB_TCP_MBAP_LEN         7U

/** @brief Longitud mínima de un frame Modbus TCP válido (MBAP + FC). */
#define MB_TCP_FRAME_MIN        8U

/* ========================================================================== */
/*  Códigos de retorno                                                          */
/* ========================================================================== */

#define MB_TCP_OK               0
#define MB_TCP_ERR_SOCKET      -1
#define MB_TCP_ERR_MBAP        -2
#define MB_TCP_ERR_PROTO       -3
#define MB_TCP_ERR_EXCEPTION   -4
#define MB_TCP_IGNORED         -5

/* ========================================================================== */
/*  API pública                                                                 */
/* ========================================================================== */

/**
 * @brief  Procesa un frame Modbus TCP completo (MBAP + PDU).
 *
 * @details Valida el MBAP header, extrae la PDU, despacha al FC
 *          correspondiente usando modbus_registers, y construye
 *          la respuesta completa (MBAP + PDU respuesta) en out_buf.
 *
 * @param[in]  in_buf    Buffer con el frame recibido completo.
 * @param[in]  in_len    Longitud del frame recibido.
 * @param[out] out_buf   Buffer donde se escribe la respuesta.
 * @param[out] out_len   Longitud de la respuesta generada.
 *
 * @retval  MB_TCP_OK          Respuesta generada en out_buf.
 * @retval  MB_TCP_ERR_MBAP    Header inválido (protocol ID != 0).
 * @retval  MB_TCP_ERR_EXCEPTION  FC válido pero generó excepción Modbus.
 * @retval  MB_TCP_IGNORED     Frame demasiado corto.
 */
int modbus_tcp_process(const uint8_t *in_buf,  size_t  in_len,
                             uint8_t *out_buf, size_t *out_len);

/**
 * @brief  Task FreeRTOS — servidor Modbus TCP completo.
 *
 * @details Maneja el ciclo de vida del socket W5500 y llama a
 *          modbus_tcp_process() por cada frame recibido.
 *          Crear con stack mínimo de 512 words.
 *
 *          @code
 *            xTaskCreate(modbus_tcp_task, "ModbusTCP", 512, NULL, 3, NULL);
 *          @endcode
 *
 * @param[in]  pvParameters  No usado.
 */
void modbus_tcp_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_TCP_H */
