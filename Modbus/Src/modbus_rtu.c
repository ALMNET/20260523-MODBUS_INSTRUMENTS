/**
 * @file    modbus_rtu.c (UPDATED)
 * @brief   Modbus RTU Slave — with frame counters and handler integration.
 *
 * @details Updated to use modbus_handler for diagnostics and counters.
 *
 * @author  ALMNET
 * @date    2026
 */

#include "modbus_rtu.h"
#include "modbus_crc.h"
#include "modbus_registers.h"
#include "modbus_handler.h"
#include "modbus_uart.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/* ========================================================================== */
/*  Buffer de TX (estático, reutilizado en cada respuesta)                   */
/* ========================================================================== */

static uint8_t tx_buf[MB_TX_BUF_SIZE];

/* ========================================================================== */
/*  Helpers privados                                                          */
/* ========================================================================== */

/**
 * @brief  Arma y transmite una respuesta de excepción Modbus.
 *
 * @details Formato: [ Addr ][ FC | 0x80 ][ ExCode ][ CRC_Lo ][ CRC_Hi ]
 *
 * @param[in] fc        Function code del request que generó la excepción.
 * @param[in] ex_code   Código de excepción (@ref MB_EX_ILLEGAL_FUNCTION, etc).
 */
static void send_exception(uint8_t fc, uint8_t ex_code)
{
    tx_buf[0] = MB_SLAVE_ADDRESS;
    tx_buf[1] = fc | 0x80U;
    tx_buf[2] = ex_code;
    size_t len = modbus_crc_append(tx_buf, 3U);
    modbus_uart_transmit(tx_buf, len);
    modbus_handler_inc_rtu_tx();
    modbus_handler_inc_errors();
}

/**
 * @brief  Procesa FC03 — Read Holding Registers.
 *
 * @details Request:  [ Addr ][ 0x03 ][ RegHi ][ RegLo ][ CntHi ][ CntLo ][ CRC ]
 *          Response: [ Addr ][ 0x03 ][ ByteCnt ][ D0Hi ][ D0Lo ]...[ CRC ]
 *
 * @param[in] frame   Frame completo recibido.
 * @param[in] length  Longitud del frame.
 *
 * @retval MB_RTU_OK         Respuesta normal enviada.
 * @retval MB_RTU_ERR_EXCEPTION  Excepción enviada.
 */
static int handle_fc03(const uint8_t *frame, size_t length)
{
    /* Validar longitud fija del request FC03 */
    if (length != MB_FC03_REQ_LEN)
    {
        send_exception(MB_FC_READ_HOLDING_REGS, MB_EX_ILLEGAL_DATA_VALUE);
        return MB_RTU_ERR_EXCEPTION;
    }

    /* Extraer dirección de inicio y cantidad de registros (big-endian) */
    uint16_t start_addr = ((uint16_t)frame[2] << 8U) | frame[3];
    uint16_t reg_count  = ((uint16_t)frame[4] << 8U) | frame[5];

    /* Validar rango: count debe ser 1..125 y no salirse del mapa */
    if (reg_count == 0U || reg_count > MB_FC03_MAX_REGS)
    {
        send_exception(MB_FC_READ_HOLDING_REGS, MB_EX_ILLEGAL_DATA_VALUE);
        return MB_RTU_ERR_EXCEPTION;
    }

    if ((uint32_t)start_addr + reg_count > MODBUS_HR_COUNT)
    {
        send_exception(MB_FC_READ_HOLDING_REGS, MB_EX_ILLEGAL_DATA_ADDRESS);
        return MB_RTU_ERR_EXCEPTION;
    }

    /* Leer registros en buffer temporal */
    uint16_t reg_values[MODBUS_HR_COUNT];
    int ret = modbus_handler_read_block(start_addr, reg_count, reg_values);
    if (ret != 0)
    {
        send_exception(MB_FC_READ_HOLDING_REGS, MB_EX_SERVER_DEVICE_FAILURE);
        return MB_RTU_ERR_EXCEPTION;
    }

    /* Armar respuesta:
     * Byte count = reg_count × 2 (cada registro ocupa 2 bytes, big-endian)
     */
    uint8_t byte_count = (uint8_t)(reg_count * 2U);

    tx_buf[0] = MB_SLAVE_ADDRESS;
    tx_buf[1] = MB_FC_READ_HOLDING_REGS;
    tx_buf[2] = byte_count;

    /* Volcar registros en big-endian */
    for (uint16_t i = 0; i < reg_count; i++)
    {
        tx_buf[3U + (i * 2U)]      = (uint8_t)(reg_values[i] >> 8U);
        tx_buf[3U + (i * 2U) + 1U] = (uint8_t)(reg_values[i] & 0xFFU);
    }

    size_t pdu_len = 3U + byte_count;
    size_t total   = modbus_crc_append(tx_buf, pdu_len);
    modbus_uart_transmit(tx_buf, total);
    modbus_handler_inc_rtu_tx();

    return MB_RTU_OK;
}

/**
 * @brief  Procesa FC06 — Write Single Register.
 *
 * @details Request:  [ Addr ][ 0x06 ][ RegHi ][ RegLo ][ ValHi ][ ValLo ][ CRC ]
 *          Response: eco exacto del request (los mismos 8 bytes).
 *
 * @param[in] frame   Frame completo recibido.
 * @param[in] length  Longitud del frame.
 *
 * @retval MB_RTU_OK         Registro escrito, eco enviado.
 * @retval MB_RTU_ERR_EXCEPTION  Excepción enviada.
 */
static int handle_fc06(const uint8_t *frame, size_t length)
{
    /* Validar longitud fija del request FC06 */
    if (length != MB_FC06_REQ_LEN)
    {
        send_exception(MB_FC_WRITE_SINGLE_REG, MB_EX_ILLEGAL_DATA_VALUE);
        return MB_RTU_ERR_EXCEPTION;
    }

    uint16_t reg_addr = ((uint16_t)frame[2] << 8U) | frame[3];
    uint16_t value    = ((uint16_t)frame[4] << 8U) | frame[5];

    /* Validar dirección */
    if (reg_addr >= MODBUS_HR_COUNT)
    {
        send_exception(MB_FC_WRITE_SINGLE_REG, MB_EX_ILLEGAL_DATA_ADDRESS);
        return MB_RTU_ERR_EXCEPTION;
    }

    /* Escribir el registro */
    int ret = modbus_handler_write_register(reg_addr, value);
    if (ret != 0)
    {
        send_exception(MB_FC_WRITE_SINGLE_REG, MB_EX_SERVER_DEVICE_FAILURE);
        return MB_RTU_ERR_EXCEPTION;
    }

    /*
     * Respuesta FC06: eco exacto del request completo.
     * El CRC ya está en frame[6] y frame[7] — copiamos los 8 bytes tal cual.
     */
    memcpy(tx_buf, frame, MB_FC06_REQ_LEN);
    modbus_uart_transmit(tx_buf, MB_FC06_REQ_LEN);
    modbus_handler_inc_rtu_tx();

    return MB_RTU_OK;
}

/* ========================================================================== */
/*  Implementación pública                                                    */
/* ========================================================================== */

int modbus_rtu_init(TaskHandle_t modbus_task)
{
    int ret = modbus_registers_init();
    if (ret != 0)
        return ret;

    ret = modbus_uart_init(modbus_task);
    if (ret != 0)
        return ret;

    return MB_RTU_OK;
}

int modbus_rtu_process(const uint8_t *frame, size_t length)
{
    /* 1 — Longitud mínima */
    if (length < MB_RTU_FRAME_MIN)
        return MB_RTU_IGNORED;

    /* 2 — Filtro de dirección:
     *     0x00 = broadcast → los slaves no responden (spec §2.6)
     *     distinto al nuestro → ignorar silenciosamente
     */
    if (frame[0] == 0x00U || frame[0] != MB_SLAVE_ADDRESS)
        return MB_RTU_IGNORED;

    /* 3 — Validar CRC */
    if (!modbus_crc_check(frame, length))
    {
        modbus_handler_inc_errors();
        return MB_RTU_ERR_CRC;
    }

    /* 4 — Despachar por function code */
    uint8_t fc = frame[1];
    modbus_handler_inc_rtu_rx();

    switch (fc)
    {
        case MB_FC_READ_HOLDING_REGS:
            return handle_fc03(frame, length);

        case MB_FC_WRITE_SINGLE_REG:
            return handle_fc06(frame, length);

        default:
            send_exception(fc, MB_EX_ILLEGAL_FUNCTION);
            return MB_RTU_ERR_EXCEPTION;
    }
}

void modbus_rtu_task(void *argument)
{
    (void)argument;

    /* Inicializar todo el stack Modbus desde dentro de la task
     * (el scheduler ya está corriendo, los mutexes y handles son válidos) */
    int ret = modbus_rtu_init(xTaskGetCurrentTaskHandle());
    if (ret != MB_RTU_OK)
    {
        /* Si la init falla, la task no puede funcionar.
         * Error_Handler() con LED parpadeando para debug visual. */
        extern void Error_Handler(void);
        Error_Handler();
    }

    /* Buffer de recepción local a la task — no compartido */
    static uint8_t rx_frame[MB_UART_RX_BUF_SIZE];
    size_t rx_len = 0;

    while (1)
    {
        /*
         * Esperar notificación de la ISR IDLE.
         * ulTaskNotifyTake() bloquea sin consumir CPU hasta que
         * modbus_uart_idle_callback() llame a vTaskNotifyGiveFromISR().
         * pdTRUE limpia el contador de notificaciones al salir.
         * portMAX_DELAY = espera indefinida.
         */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Copiar frame del buffer DMA a nuestro buffer local */
        ret = modbus_uart_get_frame(rx_frame, sizeof(rx_frame), &rx_len);

        if (ret != 0 || rx_len == 0U)
            continue;   /* buffer overflow o frame vacío — descartar */

        /* Procesar el frame: validar, ejecutar FC, responder */
        modbus_rtu_process(rx_frame, rx_len);

        /*
         * No agregamos vTaskDelay acá — la task se bloquea sola en
         * ulTaskNotifyTake() hasta que llegue el próximo frame.
         * El CPU es libre para otras tasks mientras tanto.
         */
    }
}
