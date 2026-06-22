/**
 * @file    i2c_host_interface.h
 * @brief   Interfaz abstracta de transporte I2C — inyección de dependencia
 *          para drivers de periféricos I2C independientes del HAL.
 *
 * @details Este header define el contrato que cualquier driver de chip I2C
 *          (MCP4728, INA219, MPU-6050, etc.) necesita para comunicarse con
 *          el bus, sin saber si por debajo hay HAL de STM32, MCC Melody,
 *          un mock para test en PC, o cualquier otra implementación.
 *
 *          El driver del chip recibe un puntero a esta interfaz en su
 *          función de init y la usa para todas sus transacciones I2C.
 *
 * @par Patrón de uso:
 * @code
 *   // Implementación concreta (ej: i2c1_bus.c, con mutex FreeRTOS)
 *   i2c_host_interface_t i2c1_if = {
 *       .write = i2c1_bus_write,
 *       .read  = i2c1_bus_read,
 *       .ctx   = NULL
 *   };
 *
 *   // El driver del chip no sabe nada de HAL ni FreeRTOS
 *   mcp4728_init(&dac, &i2c1_if, MCP4728_I2C_ADDR_BASE);
 * @endcode
 *
 * @author  Armando
 * @date    2025
 */

#ifndef I2C_HOST_INTERFACE_H
#define I2C_HOST_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ========================================================================== */
/*  Códigos de retorno del transporte                                          */
/* ========================================================================== */

#define I2C_HOST_OK              0   /**< Transacción exitosa                */
#define I2C_HOST_ERR_TIMEOUT    -1   /**< Timeout de hardware o mutex         */
#define I2C_HOST_ERR_NACK       -2   /**< Dispositivo no respondió (NACK)     */
#define I2C_HOST_ERR_PARAM      -3   /**< Parámetro inválido                  */

/* ========================================================================== */
/*  Firmas de función del transporte                                          */
/* ========================================================================== */

/**
 * @brief  Escribe un bloque de bytes a un dispositivo I2C.
 *
 * @param[in] ctx      Contexto opaco de la implementación concreta
 *                     (ej: puntero al handle I2C, o NULL si no se usa).
 * @param[in] dev_addr Dirección I2C de 7 bits del dispositivo destino.
 * @param[in] data     Buffer con los bytes a escribir.
 * @param[in] len      Cantidad de bytes a escribir.
 *
 * @retval  I2C_HOST_OK            Escritura exitosa.
 * @retval  I2C_HOST_ERR_TIMEOUT   Timeout de bus o mutex.
 * @retval  I2C_HOST_ERR_NACK      Dispositivo no reconoció su dirección.
 */
typedef int (*i2c_host_write_fn)(void *ctx, uint8_t dev_addr,
                                 const uint8_t *data, size_t len);

/**
 * @brief  Lee un bloque de bytes desde un dispositivo I2C.
 *
 * @param[in]  ctx      Contexto opaco de la implementación concreta.
 * @param[in]  dev_addr Dirección I2C de 7 bits del dispositivo origen.
 * @param[out] data     Buffer donde se escriben los bytes leídos.
 * @param[in]  len      Cantidad de bytes a leer.
 *
 * @retval  I2C_HOST_OK            Lectura exitosa.
 * @retval  I2C_HOST_ERR_TIMEOUT   Timeout de bus o mutex.
 * @retval  I2C_HOST_ERR_NACK      Dispositivo no reconoció su dirección.
 */
typedef int (*i2c_host_read_fn)(void *ctx, uint8_t dev_addr,
                                uint8_t *data, size_t len);

/* ========================================================================== */
/*  Interfaz — estructura inyectada en cada driver de chip                     */
/* ========================================================================== */

/**
 * @brief  Interfaz de transporte I2C inyectada en drivers de periféricos.
 *
 * @details Cada implementación concreta del bus (con o sin mutex, con o sin
 *          RTOS, con HAL o con MCC Melody) llena esta estructura una vez y
 *          la pasa a cualquier driver de chip que la necesite.
 */
typedef struct {
    i2c_host_write_fn write;   /**< Función de escritura del bus           */
    i2c_host_read_fn  read;    /**< Función de lectura del bus             */
    void              *ctx;     /**< Contexto opaco (handle, mutex, etc.)   */
} i2c_host_interface_t;

#ifdef __cplusplus
}
#endif

#endif /* I2C_HOST_INTERFACE_H */
