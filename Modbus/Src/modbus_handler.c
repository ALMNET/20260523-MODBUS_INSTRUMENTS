/**
 * @file    modbus_handler.c
 * @brief   Implementation of centralized Modbus handler.
 *
 * @author  ALMNET
 * @date    2026
 */

#include "modbus_handler.h"
#include "modbus_registers.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include <string.h>

/* ========================================================================== */
/*  Diagnostic state (protected by mutex)                                    */
/* ========================================================================== */

typedef struct {
    uint32_t status;        /**< Current status flags (@ref mb_handler_status_t) */
    uint32_t tcp_rx_count;  /**< TCP frames received                          */
    uint32_t tcp_tx_count;  /**< TCP frames transmitted                       */
    uint32_t rtu_rx_count;  /**< RTU frames received                          */
    uint32_t rtu_tx_count;  /**< RTU frames transmitted                       */
    uint32_t error_count;   /**< Protocol errors                              */
} mb_handler_diag_t;

static mb_handler_diag_t diag = {0};
static SemaphoreHandle_t diag_mutex = NULL;

/* ========================================================================== */
/*  Initialization                                                            */
/* ========================================================================== */

int modbus_handler_init(void)
{
    /* Initialize registers (includes mutex creation) */
    int ret = modbus_registers_init();
    if (ret != 0)
        return ret;

    /* Create diagnostics mutex */
    diag_mutex = xSemaphoreCreateMutex();
    if (diag_mutex == NULL)
        return -1;

    /* Initialize diagnostics */
    memset(&diag, 0, sizeof(diag));

    return 0;
}

/* ========================================================================== */
/*  Status management                                                         */
/* ========================================================================== */

uint32_t modbus_handler_get_status(void)
{
    uint32_t status = 0;

    if (xSemaphoreTake(diag_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        status = diag.status;
        xSemaphoreGive(diag_mutex);
    }

    return status;
}

void modbus_handler_set_status(uint32_t flags, int clear)
{
    if (xSemaphoreTake(diag_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        if (clear)
            diag.status = flags;
        else
            diag.status |= flags;

        xSemaphoreGive(diag_mutex);
    }
}

/* ========================================================================== */
/*  Diagnostics counters                                                      */
/* ========================================================================== */

void modbus_handler_get_counters(
    uint32_t *tcp_rx,
    uint32_t *tcp_tx,
    uint32_t *rtu_rx,
    uint32_t *rtu_tx,
    uint32_t *errors)
{
    if (xSemaphoreTake(diag_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        if (tcp_rx)   *tcp_rx   = diag.tcp_rx_count;
        if (tcp_tx)   *tcp_tx   = diag.tcp_tx_count;
        if (rtu_rx)   *rtu_rx   = diag.rtu_rx_count;
        if (rtu_tx)   *rtu_tx   = diag.rtu_tx_count;
        if (errors)   *errors   = diag.error_count;

        xSemaphoreGive(diag_mutex);
    }
}

void modbus_handler_reset_counters(void)
{
    if (xSemaphoreTake(diag_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        diag.tcp_rx_count = 0;
        diag.tcp_tx_count = 0;
        diag.rtu_rx_count = 0;
        diag.rtu_tx_count = 0;
        diag.error_count  = 0;

        xSemaphoreGive(diag_mutex);
    }
}

/* ========================================================================== */
/*  Register access (delegates to modbus_registers)                          */
/* ========================================================================== */

int modbus_handler_read_register(uint16_t address, uint16_t *value)
{
    return modbus_hr_read(address, value);
}

int modbus_handler_write_register(uint16_t address, uint16_t value)
{
    return modbus_hr_write(address, value);
}

int modbus_handler_read_block(uint16_t start_addr, uint16_t count, uint16_t *out)
{
    return modbus_hr_read_block(start_addr, count, out);
}

int modbus_handler_write_block(uint16_t start_addr, uint16_t count, const uint16_t *values)
{
    return modbus_hr_write_block(start_addr, count, values);
}

/* ========================================================================== */
/*  Counter increment (thread-safe)                                          */
/* ========================================================================== */

void modbus_handler_inc_tcp_rx(void)
{
    if (xSemaphoreTake(diag_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        diag.tcp_rx_count++;
        xSemaphoreGive(diag_mutex);
    }
}

void modbus_handler_inc_tcp_tx(void)
{
    if (xSemaphoreTake(diag_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        diag.tcp_tx_count++;
        xSemaphoreGive(diag_mutex);
    }
}

void modbus_handler_inc_rtu_rx(void)
{
    if (xSemaphoreTake(diag_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        diag.rtu_rx_count++;
        xSemaphoreGive(diag_mutex);
    }
}

void modbus_handler_inc_rtu_tx(void)
{
    if (xSemaphoreTake(diag_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        diag.rtu_tx_count++;
        xSemaphoreGive(diag_mutex);
    }
}

void modbus_handler_inc_errors(void)
{
    if (xSemaphoreTake(diag_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        diag.error_count++;
        xSemaphoreGive(diag_mutex);
    }
}
