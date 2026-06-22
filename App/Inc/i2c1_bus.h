/**
 * @file    i2c1_bus.h
 * @brief   Capa de arbitraje del bus I2C1 — HAL_I2C + mutex FreeRTOS.
 *
 * @details Esta es la implementación CONCRETA de @ref i2c_host_interface_t
 *          para el bus I2C1 de este proyecto (compartido por SSD1306 y
 *          MCP4728). Cualquier driver de chip bare-metal que reciba esta
 *          interfaz queda automáticamente protegido contra accesos
 *          concurrentes desde distintas tasks, sin saber que el mutex existe.
 *
 *          El driver del chip (mcp4728.c, futuro ssd1306 migrado) no incluye
 *          este header ni sabe de su existencia — solo recibe el puntero
 *          a @ref i2c_host_interface_t ya armado.
 *
 * @par Uso típico en app_tasks.c:
 * @code
 *   i2c1_bus_init();   // una sola vez, antes de crear las tasks
 *
 *   mcp4728_t dac;
 *   mcp4728_init(&dac, i2c1_bus_get_interface(), MCP4728_I2C_ADDR_BASE);
 * @endcode
 *
 * @author  Armando
 * @date    2025
 */

#ifndef I2C1_BUS_H
#define I2C1_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "i2c_host_interface.h"
#include <stdint.h>

/* ========================================================================== */
/*  Configuración                                                              */
/* ========================================================================== */

/** @brief Timeout del mutex al tomar el bus, en ms. */
#define I2C1_BUS_MUTEX_TIMEOUT_MS    50U

/** @brief Timeout de las transacciones HAL_I2C, en ms. */
#define I2C1_BUS_HAL_TIMEOUT_MS      100U

/* ========================================================================== */
/*  API pública                                                                 */
/* ========================================================================== */

/**
 * @brief  Inicializa la capa de arbitraje del bus I2C1.
 *
 * @details Crea el mutex FreeRTOS interno. Debe llamarse una sola vez,
 *          después de que MX_I2C1_Init() haya configurado el periférico
 *          HAL, y antes de crear cualquier task que use
 *          @ref i2c1_bus_get_interface.
 *
 * @retval  I2C_HOST_OK            Inicialización correcta.
 * @retval  I2C_HOST_ERR_TIMEOUT   No se pudo crear el mutex (heap insuficiente).
 */
int i2c1_bus_init(void);

/**
 * @brief  Devuelve el puntero a la interfaz I2C lista para inyectar en
 *         cualquier driver de chip bare-metal (MCP4728, SSD1306, etc.).
 *
 * @details El puntero devuelto es estático — válido durante toda la vida
 *          del programa. Llamar a @ref i2c1_bus_init antes de usar esta
 *          interfaz.
 *
 * @return  Puntero constante a la interfaz I2C del bus I2C1.
 */
const i2c_host_interface_t *i2c1_bus_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C1_BUS_H */
