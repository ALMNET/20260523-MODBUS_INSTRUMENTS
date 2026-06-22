/**
 * @file    i2c1_bus.c
 * @brief   Implementación de la capa de arbitraje del bus I2C1.
 *
 * @details Wrappea HAL_I2C_Master_Transmit/Receive con un mutex FreeRTOS,
 *          exponiendo el resultado como @ref i2c_host_interface_t para
 *          que cualquier driver de chip bare-metal lo use sin saber que
 *          el bus está compartido ni que existe un RTOS por debajo.
 *
 * @author  Armando
 * @date    2025
 */

#include "i2c1_bus.h"
#include "main.h"          /* hi2c1 */

#include "FreeRTOS.h"
#include "semphr.h"

/* ========================================================================== */
/*  Referencia al handle generado por CubeMX                                   */
/* ========================================================================== */

extern I2C_HandleTypeDef hi2c1;

/* ========================================================================== */
/*  Estado interno (privado)                                                    */
/* ========================================================================== */

static SemaphoreHandle_t i2c1_mutex = NULL;

/* ========================================================================== */
/*  Implementación de las funciones de transporte                              */
/* ========================================================================== */

/**
 * @brief  Implementación concreta de i2c_host_write_fn para I2C1.
 *
 * @details Toma el mutex, hace la transacción HAL bloqueante, libera el
 *          mutex. El parámetro ctx no se usa (NULL) — el handle hi2c1 es
 *          fijo para esta implementación.
 */
static int i2c1_bus_write(void *ctx, uint8_t dev_addr,
                          const uint8_t *data, size_t len)
{
    (void)ctx;

    if (i2c1_mutex == NULL)
        return I2C_HOST_ERR_TIMEOUT;

    if (xSemaphoreTake(i2c1_mutex, pdMS_TO_TICKS(I2C1_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE)
        return I2C_HOST_ERR_TIMEOUT;

    /* HAL espera la dirección de 7 bits ya shifteada a la izquierda */
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
        &hi2c1,
        (uint16_t)(dev_addr << 1U),
        (uint8_t *)data,
        (uint16_t)len,
        I2C1_BUS_HAL_TIMEOUT_MS
    );

    xSemaphoreGive(i2c1_mutex);

    if (status == HAL_OK)
        return I2C_HOST_OK;

    return (status == HAL_TIMEOUT) ? I2C_HOST_ERR_TIMEOUT : I2C_HOST_ERR_NACK;
}

/**
 * @brief  Implementación concreta de i2c_host_read_fn para I2C1.
 */
static int i2c1_bus_read(void *ctx, uint8_t dev_addr,
                         uint8_t *data, size_t len)
{
    (void)ctx;

    if (i2c1_mutex == NULL)
        return I2C_HOST_ERR_TIMEOUT;

    if (xSemaphoreTake(i2c1_mutex, pdMS_TO_TICKS(I2C1_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE)
        return I2C_HOST_ERR_TIMEOUT;

    HAL_StatusTypeDef status = HAL_I2C_Master_Receive(
        &hi2c1,
        (uint16_t)(dev_addr << 1U),
        data,
        (uint16_t)len,
        I2C1_BUS_HAL_TIMEOUT_MS
    );

    xSemaphoreGive(i2c1_mutex);

    if (status == HAL_OK)
        return I2C_HOST_OK;

    return (status == HAL_TIMEOUT) ? I2C_HOST_ERR_TIMEOUT : I2C_HOST_ERR_NACK;
}

/* ========================================================================== */
/*  Instancia estática de la interfaz                                          */
/* ========================================================================== */

static const i2c_host_interface_t i2c1_interface = {
    .write = i2c1_bus_write,
    .read  = i2c1_bus_read,
    .ctx   = NULL
};

/* ========================================================================== */
/*  API pública                                                                 */
/* ========================================================================== */

int i2c1_bus_init(void)
{
    i2c1_mutex = xSemaphoreCreateMutex();

    if (i2c1_mutex == NULL)
        return I2C_HOST_ERR_TIMEOUT;

    return I2C_HOST_OK;
}

const i2c_host_interface_t *i2c1_bus_get_interface(void)
{
    return &i2c1_interface;
}
