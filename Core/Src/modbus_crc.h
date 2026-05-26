/**
 * @file    modbus_crc.h
 * @brief   CRC-16/ARC (Modbus) — lookup table implementation.
 *
 * @details Implementación del CRC-16 estándar Modbus (también conocido como
 *          CRC-16/ARC o CRC-16/IBM):
 *            - Polinomio : 0x8005 (reflejado: 0xA001)
 *            - Seed      : 0xFFFF
 *            - Input ref : true  (LSB-first)
 *            - Output ref: true
 *            - XOR out   : 0x0000
 *
 *          La tabla de 512 bytes se computa en tiempo de compilación (arreglo
 *          ROM) y permite calcular el CRC en O(n) con un byte de overhead por
 *          byte procesado.
 *
 * @note    Uso típico en frame Modbus RTU:
 * @code
 *   uint8_t  pdu[]  = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A };
 *   uint16_t crc    = modbus_crc16(pdu, sizeof(pdu));
 *   // Transmitir: pdu[0..5], (crc & 0xFF), (crc >> 8)
 *   //             low byte primero — little-endian en el wire
 * @endcode
 *
 * @author  Armando
 * @date    2025
 */

#ifndef MODBUS_CRC_H
#define MODBUS_CRC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ========================================================================== */
/*  Public API                                                                  */
/* ========================================================================== */

/**
 * @brief  Calcula el CRC-16/ARC (Modbus) de un bloque de datos.
 *
 * @param[in]  data   Puntero al buffer de datos.
 * @param[in]  length Longitud en bytes del buffer.
 *
 * @return     Valor CRC-16 de 16 bits.
 *             El byte LOW se transmite primero en el wire Modbus RTU.
 *
 * @note       No modifica el buffer de entrada.
 */
uint16_t modbus_crc16(const uint8_t *data, size_t length);

/**
 * @brief  Verifica el CRC de un frame Modbus RTU completo (datos + CRC).
 *
 * @details El frame debe incluir los 2 bytes de CRC al final, tal como
 *          llegan por el bus (low byte, high byte).
 *          La función recalcula el CRC sobre los primeros (length - 2) bytes
 *          y lo compara con el CRC embebido.
 *
 * @param[in]  frame  Puntero al frame completo (PDU + 2 bytes CRC).
 * @param[in]  length Longitud total del frame incluyendo los 2 bytes de CRC.
 *
 * @retval  1  CRC válido — frame íntegro.
 * @retval  0  CRC inválido — frame corrupto o incompleto.
 */
uint8_t modbus_crc_check(const uint8_t *frame, size_t length);

/**
 * @brief  Agrega el CRC al final de un buffer PDU en construcción.
 *
 * @details Escribe los 2 bytes de CRC en buffer[pdu_length] y
 *          buffer[pdu_length + 1] (low byte primero).
 *          El buffer debe tener al menos (pdu_length + 2) bytes de capacidad.
 *
 * @param[in,out] buffer      Buffer con la PDU ya armada.
 * @param[in]     pdu_length  Longitud de la PDU sin el CRC.
 *
 * @return  Longitud total del frame con CRC (pdu_length + 2).
 */
size_t modbus_crc_append(uint8_t *buffer, size_t pdu_length);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_CRC_H */
