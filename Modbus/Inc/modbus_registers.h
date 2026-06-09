/**
 * @file    modbus_registers.h
 * @brief   Tabla de Holding Registers Modbus con acceso thread-safe (FreeRTOS).
 *
 * @details Define un mapa plano de @ref MODBUS_HR_COUNT registros de 16 bits
 *          (uint16_t), accesible desde cualquier task a través de las
 *          funciones de lectura/escritura que adquieren un mutex interno.
 *
 *          Mapa inicial (extender según necesidad del proyecto):
 *
 *          | Dirección | Alias                    | Descripción              |
 *          |-----------|--------------------------|--------------------------|
 *          | 0x0000    | MB_HR_DEVICE_STATUS      | Estado del dispositivo   |
 *          | 0x0001    | MB_HR_FIRMWARE_VER       | Versión de firmware      |
 *          | 0x0002    | MB_HR_OUTPUT_0           | Salida digital 0         |
 *          | 0x0003    | MB_HR_OUTPUT_1           | Salida digital 1         |
 *          | 0x0004    | MB_HR_SETPOINT_0         | Setpoint canal 0 (×10)   |
 *          | 0x0005    | MB_HR_SETPOINT_1         | Setpoint canal 1 (×10)   |
 *          | 0x0006    | MB_HR_MEAS_0             | Medición canal 0 (×10)   |
 *          | 0x0007    | MB_HR_MEAS_1             | Medición canal 1 (×10)   |
 *          | 0x0008    | MB_HR_ERROR_FLAGS        | Flags de error           |
 *          | 0x0009–F  | MB_HR_USER_0–6           | Registros de usuario     |
 *
 * @note    Uso típico desde otra task:
 * @code
 *   // Escribir setpoint desde task de control
 *   modbus_hr_write(MB_HR_SETPOINT_0, 250);   // 25.0 °C × 10
 *
 *   // Leer medición desde task Modbus para armar respuesta
 *   uint16_t val;
 *   modbus_hr_read(MB_HR_MEAS_0, &val);
 * @endcode
 *
 * @author  Armando
 * @date    2025
 */

#ifndef MODBUS_REGISTERS_H
#define MODBUS_REGISTERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ========================================================================== */
/*  Configuración                                                               */
/* ========================================================================== */

/** @brief Cantidad total de Holding Registers. Modificar para escalar. */
#define MODBUS_HR_COUNT     16U

/** @brief Dirección Modbus del primer holding register (base 0). */
#define MODBUS_HR_ADDR_BASE 0x0000U

/* ========================================================================== */
/*  Mapa de direcciones (aliases)                                               */
/* ========================================================================== */

/** @defgroup modbus_hr_map Mapa de Holding Registers
 *  Aliases para acceder a los registros por nombre en lugar de índice.
 *  @{
 */
#define MB_HR_DEVICE_STATUS   0U   /**< Estado del dispositivo (R/W)       */
#define MB_HR_FIRMWARE_VER    1U   /**< Versión de firmware (R/W)          */
#define MB_HR_OUTPUT_0        2U   /**< Salida digital 0: 0=OFF, 1=ON      */
#define MB_HR_OUTPUT_1        3U   /**< Salida digital 1: 0=OFF, 1=ON      */
#define MB_HR_SETPOINT_0      4U   /**< Setpoint canal 0 (unidades × 10)   */
#define MB_HR_SETPOINT_1      5U   /**< Setpoint canal 1 (unidades × 10)   */
#define MB_HR_MEAS_0          6U   /**< Medición canal 0 (unidades × 10)   */
#define MB_HR_MEAS_1          7U   /**< Medición canal 1 (unidades × 10)   */
#define MB_HR_ERROR_FLAGS     8U   /**< Flags de error (bitmap)            */
#define MB_HR_USER_0          9U   /**< Registro de usuario 0              */
#define MB_HR_USER_1         10U   /**< Registro de usuario 1              */
#define MB_HR_USER_2         11U   /**< Registro de usuario 2              */
#define MB_HR_USER_3         12U   /**< Registro de usuario 3              */
#define MB_HR_USER_4         13U   /**< Registro de usuario 4              */
#define MB_HR_USER_5         14U   /**< Registro de usuario 5              */
#define MB_HR_USER_6         15U   /**< Registro de usuario 6              */
/** @} */

/* ========================================================================== */
/*  Códigos de retorno                                                          */
/* ========================================================================== */

/** @defgroup modbus_reg_ret Códigos de retorno
 *  @{
 */
#define MB_REG_OK            0    /**< Operación exitosa                   */
#define MB_REG_ERR_RANGE    -1    /**< Dirección fuera de rango            */
#define MB_REG_ERR_MUTEX    -2    /**< Timeout esperando el mutex          */
#define MB_REG_ERR_NULL     -3    /**< Puntero nulo                        */
#define MB_REG_ERR_COUNT    -4    /**< Count = 0 o excede el mapa          */
/** @} */

/* ========================================================================== */
/*  API pública                                                                 */
/* ========================================================================== */

/**
 * @brief  Inicializa la tabla de registros y crea el mutex FreeRTOS.
 *
 * @details Debe llamarse una sola vez desde la task de inicialización o desde
 *          @c tasks_create(), antes de arrancar el scheduler o al inicio de
 *          cualquier task que acceda a los registros.
 *          Inicializa todos los registros en 0 excepto MB_HR_FIRMWARE_VER.
 *
 * @retval  MB_REG_OK       Inicialización correcta.
 * @retval  MB_REG_ERR_MUTEX  No se pudo crear el mutex (heap insuficiente).
 */
int modbus_registers_init(void);

/**
 * @brief  Lee un único Holding Register.
 *
 * @param[in]  address  Índice del registro (0 … MODBUS_HR_COUNT-1).
 * @param[out] value    Puntero donde se escribe el valor leído.
 *
 * @retval  MB_REG_OK          Lectura exitosa.
 * @retval  MB_REG_ERR_RANGE   Dirección fuera de rango.
 * @retval  MB_REG_ERR_NULL    @p value es NULL.
 * @retval  MB_REG_ERR_MUTEX   Timeout adquiriendo el mutex.
 */
int modbus_hr_read(uint16_t address, uint16_t *value);

/**
 * @brief  Lee múltiples Holding Registers consecutivos.
 *
 * @details Equivalente a FC03. Adquiere el mutex una sola vez para la
 *          lectura completa del bloque.
 *
 * @param[in]  start_addr  Dirección del primer registro.
 * @param[in]  count       Cantidad de registros a leer (≥1).
 * @param[out] out         Buffer de salida, debe tener al menos @p count
 *                         elementos de tipo uint16_t.
 *
 * @retval  MB_REG_OK          Lectura exitosa.
 * @retval  MB_REG_ERR_RANGE   start_addr + count excede el mapa.
 * @retval  MB_REG_ERR_NULL    @p out es NULL.
 * @retval  MB_REG_ERR_COUNT   @p count es 0.
 * @retval  MB_REG_ERR_MUTEX   Timeout adquiriendo el mutex.
 */
int modbus_hr_read_block(uint16_t start_addr, uint16_t count, uint16_t *out);

/**
 * @brief  Escribe un único Holding Register.
 *
 * @details Equivalente a FC06.
 *
 * @param[in]  address  Índice del registro (0 … MODBUS_HR_COUNT-1).
 * @param[in]  value    Valor a escribir.
 *
 * @retval  MB_REG_OK          Escritura exitosa.
 * @retval  MB_REG_ERR_RANGE   Dirección fuera de rango.
 * @retval  MB_REG_ERR_MUTEX   Timeout adquiriendo el mutex.
 */
int modbus_hr_write(uint16_t address, uint16_t value);

/**
 * @brief  Escribe múltiples Holding Registers consecutivos.
 *
 * @details Equivalente a FC16 (Write Multiple Registers). Útil para
 *          actualizar un bloque de sensores en una sola operación atómica.
 *
 * @param[in]  start_addr  Dirección del primer registro.
 * @param[in]  count       Cantidad de registros a escribir (≥1).
 * @param[in]  values      Buffer con los valores a escribir.
 *
 * @retval  MB_REG_OK          Escritura exitosa.
 * @retval  MB_REG_ERR_RANGE   start_addr + count excede el mapa.
 * @retval  MB_REG_ERR_NULL    @p values es NULL.
 * @retval  MB_REG_ERR_COUNT   @p count es 0.
 * @retval  MB_REG_ERR_MUTEX   Timeout adquiriendo el mutex.
 */
int modbus_hr_write_block(uint16_t start_addr, uint16_t count, const uint16_t *values);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_REGISTERS_H */
