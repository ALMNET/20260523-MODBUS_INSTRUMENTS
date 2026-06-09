/**
 * @file    modbus_tcp.c
 * @brief   Implementación del servidor Modbus TCP sobre W5500.
 *
 * @details El flujo completo por frame:
 *
 * @verbatim
 *  master conecta
 *       │
 *  SOCK_ESTABLISHED
 *       │
 *  getSn_RX_RSR() > 0
 *       │
 *  recv() ──► in_buf[]
 *       │
 *  modbus_tcp_process()
 *       ├── validar MBAP (Protocol ID == 0x0000)
 *       ├── extraer PDU (in_buf[7..])
 *       ├── despachar FC03 / FC06
 *       │       └── modbus_hr_read_block() / modbus_hr_write()
 *       └── armar respuesta MBAP + PDU en out_buf[]
 *       │
 *  send() ──► out_buf[]
 *       │
 *  volver a recv()
 * @endverbatim
 *
 * @author  Armando
 * @date    2025
 */

#include "modbus_tcp.h"
#include "modbus_registers.h"

/* ioLibrary */
#include "socket.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/* ========================================================================== */
/*  Códigos de excepción Modbus (mismos que RTU)                              */
/* ========================================================================== */

#define MB_EX_ILLEGAL_FUNCTION      0x01U
#define MB_EX_ILLEGAL_DATA_ADDRESS  0x02U
#define MB_EX_ILLEGAL_DATA_VALUE    0x03U
#define MB_EX_SERVER_DEVICE_FAILURE 0x04U

/* ========================================================================== */
/*  Helpers MBAP                                                               */
/* ========================================================================== */

/**
 * @brief  Escribe el MBAP header de respuesta en out_buf.
 *
 * @details Copia Transaction ID del request, Protocol ID = 0x0000,
 *          Length = pdu_len + 1 (Unit ID incluido), Unit ID del request.
 *
 * @param[out] out         Buffer destino (debe tener ≥ 7 bytes).
 * @param[in]  req         Frame request completo (para copiar TI y Unit ID).
 * @param[in]  pdu_resp_len Longitud de la PDU de respuesta (sin MBAP, sin Unit ID).
 */
static void write_mbap(uint8_t *out, const uint8_t *req, uint16_t pdu_resp_len)
{
    /* Transaction Identifier — eco exacto del request */
    out[0] = req[0];
    out[1] = req[1];

    /* Protocol Identifier — siempre 0x0000 */
    out[2] = 0x00U;
    out[3] = 0x00U;

    /*
     * Length = bytes que siguen al campo Length
     *        = 1 (Unit ID) + pdu_resp_len
     */
    uint16_t length = pdu_resp_len + 1U;
    out[4] = (uint8_t)(length >> 8U);
    out[5] = (uint8_t)(length & 0xFFU);

    /* Unit Identifier — eco del request */
    out[6] = req[6];
}

/**
 * @brief  Arma una respuesta de excepción Modbus TCP en out_buf.
 *
 * @param[out] out      Buffer destino (debe tener ≥ 9 bytes).
 * @param[in]  req      Frame request.
 * @param[in]  fc       Function code que generó la excepción.
 * @param[in]  ex_code  Código de excepción Modbus.
 * @param[out] out_len  Longitud total de la respuesta.
 */
static void build_exception(uint8_t *out, const uint8_t *req,
                             uint8_t fc, uint8_t ex_code, size_t *out_len)
{
    /* PDU de excepción: FC|0x80 + ExCode = 2 bytes */
    write_mbap(out, req, 2U);
    out[7] = fc | 0x80U;
    out[8] = ex_code;
    *out_len = 9U;
}

/* ========================================================================== */
/*  Handlers de Function Codes                                                 */
/* ========================================================================== */

/**
 * @brief  Procesa FC03 — Read Holding Registers.
 *
 * @param[in]  req      Frame request completo.
 * @param[in]  req_len  Longitud del request.
 * @param[out] out      Buffer de respuesta.
 * @param[out] out_len  Longitud de la respuesta.
 *
 * @retval MB_TCP_OK         Respuesta normal generada.
 * @retval MB_TCP_ERR_EXCEPTION  Excepción generada.
 */
static int handle_fc03(const uint8_t *req, size_t req_len,
                             uint8_t *out,  size_t *out_len)
{
    /* PDU del request: FC(1) + StartAddr(2) + Count(2) = 5 bytes
     * Frame mínimo FC03: MBAP(7) + 5 = 12 bytes */
    if (req_len < 12U)
    {
        build_exception(out, req, 0x03U, MB_EX_ILLEGAL_DATA_VALUE, out_len);
        return MB_TCP_ERR_EXCEPTION;
    }

    /* PDU empieza en req[7] (después del MBAP):
     * req[7] = FC = 0x03
     * req[8..9] = starting address (big-endian)
     * req[10..11] = quantity of registers (big-endian)
     */
    uint16_t start_addr = ((uint16_t)req[8]  << 8U) | req[9];
    uint16_t reg_count  = ((uint16_t)req[10] << 8U) | req[11];

    /* Validar count: 1..125 */
    if (reg_count == 0U || reg_count > 125U)
    {
        build_exception(out, req, 0x03U, MB_EX_ILLEGAL_DATA_VALUE, out_len);
        return MB_TCP_ERR_EXCEPTION;
    }

    /* Validar rango en el mapa de registros */
    if ((uint32_t)start_addr + reg_count > MODBUS_HR_COUNT)
    {
        build_exception(out, req, 0x03U, MB_EX_ILLEGAL_DATA_ADDRESS, out_len);
        return MB_TCP_ERR_EXCEPTION;
    }

    /* Leer registros */
    uint16_t values[MODBUS_HR_COUNT];
    int ret = modbus_hr_read_block(start_addr, reg_count, values);
    if (ret != MB_REG_OK)
    {
        build_exception(out, req, 0x03U, MB_EX_SERVER_DEVICE_FAILURE, out_len);
        return MB_TCP_ERR_EXCEPTION;
    }

    /*
     * PDU respuesta FC03:
     *   FC(1) + ByteCount(1) + Data(reg_count × 2)
     *   = 2 + reg_count*2 bytes
     */
    uint8_t byte_count  = (uint8_t)(reg_count * 2U);
    uint16_t pdu_resp_len = 2U + byte_count;   /* FC + ByteCount + Data */

    write_mbap(out, req, pdu_resp_len);

    out[7] = 0x03U;         /* FC */
    out[8] = byte_count;    /* Byte Count */

    for (uint16_t i = 0; i < reg_count; i++)
    {
        out[9U  + (i * 2U)] = (uint8_t)(values[i] >> 8U);
        out[10U + (i * 2U)] = (uint8_t)(values[i] & 0xFFU);
    }

    *out_len = (size_t)(MB_TCP_MBAP_LEN + pdu_resp_len);
    return MB_TCP_OK;
}

/**
 * @brief  Procesa FC06 — Write Single Register.
 *
 * @param[in]  req      Frame request completo.
 * @param[in]  req_len  Longitud del request.
 * @param[out] out      Buffer de respuesta.
 * @param[out] out_len  Longitud de la respuesta.
 *
 * @retval MB_TCP_OK         Registro escrito, eco generado.
 * @retval MB_TCP_ERR_EXCEPTION  Excepción generada.
 */
static int handle_fc06(const uint8_t *req, size_t req_len,
                             uint8_t *out,  size_t *out_len)
{
    /* Frame mínimo FC06: MBAP(7) + FC(1) + Addr(2) + Val(2) = 12 bytes */
    if (req_len < 12U)
    {
        build_exception(out, req, 0x06U, MB_EX_ILLEGAL_DATA_VALUE, out_len);
        return MB_TCP_ERR_EXCEPTION;
    }

    uint16_t reg_addr = ((uint16_t)req[8]  << 8U) | req[9];
    uint16_t value    = ((uint16_t)req[10] << 8U) | req[11];

    if (reg_addr >= MODBUS_HR_COUNT)
    {
        build_exception(out, req, 0x06U, MB_EX_ILLEGAL_DATA_ADDRESS, out_len);
        return MB_TCP_ERR_EXCEPTION;
    }

    int ret = modbus_hr_write(reg_addr, value);
    if (ret != MB_REG_OK)
    {
        build_exception(out, req, 0x06U, MB_EX_SERVER_DEVICE_FAILURE, out_len);
        return MB_TCP_ERR_EXCEPTION;
    }

    /*
     * Respuesta FC06: eco exacto del request
     * PDU respuesta: FC(1) + Addr(2) + Val(2) = 5 bytes
     */
    write_mbap(out, req, 5U);
    out[7]  = 0x06U;
    out[8]  = req[8];    /* addr hi */
    out[9]  = req[9];    /* addr lo */
    out[10] = req[10];   /* val hi  */
    out[11] = req[11];   /* val lo  */

    *out_len = 12U;
    return MB_TCP_OK;
}

/* ========================================================================== */
/*  API pública — procesamiento de frame                                       */
/* ========================================================================== */

int modbus_tcp_process(const uint8_t *in_buf,  size_t  in_len,
                             uint8_t *out_buf, size_t *out_len)
{
    *out_len = 0U;

    /* Longitud mínima: MBAP(7) + FC(1) = 8 bytes */
    if (in_len < MB_TCP_FRAME_MIN)
        return MB_TCP_IGNORED;

    /* Validar Protocol Identifier — debe ser 0x0000 */
    if (in_buf[2] != 0x00U || in_buf[3] != 0x00U)
        return MB_TCP_ERR_MBAP;

    /* Extraer FC — está en in_buf[7] */
    uint8_t fc = in_buf[7];

    switch (fc)
    {
        case 0x03U:
            return handle_fc03(in_buf, in_len, out_buf, out_len);

        case 0x06U:
            return handle_fc06(in_buf, in_len, out_buf, out_len);

        default:
            build_exception(out_buf, in_buf, fc,
                            MB_EX_ILLEGAL_FUNCTION, out_len);
            return MB_TCP_ERR_EXCEPTION;
    }
}

/* ========================================================================== */
/*  Task FreeRTOS — state machine del socket                                  */
/* ========================================================================== */

void modbus_tcp_task(void *pvParameters)
{
    (void)pvParameters;

    /* Inicializar tabla de registros (mutex incluido) */
    modbus_registers_init();

    static uint8_t rx_buf[MB_TCP_BUF_SIZE];
    static uint8_t tx_buf[MB_TCP_BUF_SIZE];

    for (;;)
    {
        uint8_t sock_status = getSn_SR(MB_TCP_SOCKET);

        switch (sock_status)
        {
            /* ------------------------------------------------------------ */
            case SOCK_CLOSED:
            /*
             * Socket cerrado — (re)inicializar y ponerse a escuchar.
             * Se llega acá al arranque y después de cada desconexión.
             */
                socket(MB_TCP_SOCKET, Sn_MR_TCP, MB_TCP_PORT, 0U);
                listen(MB_TCP_SOCKET);
                break;

            /* ------------------------------------------------------------ */
            case SOCK_LISTEN:
            /*
             * Esperando conexión del master.
             * No hay nada que hacer — el W5500 maneja el TCP handshake
             * por hardware. Solo ceder el CPU.
             */
                break;

            /* ------------------------------------------------------------ */
            case SOCK_ESTABLISHED:
            {
            /*
             * Conexión activa — procesar frames entrantes.
             */
                int32_t rx_size = getSn_RX_RSR(MB_TCP_SOCKET);

                if (rx_size <= 0)
                    break;  /* nada en el buffer — ceder CPU */

                if (rx_size > (int32_t)MB_TCP_BUF_SIZE)
                    rx_size = (int32_t)MB_TCP_BUF_SIZE;

                int32_t received = recv(MB_TCP_SOCKET, rx_buf, (uint16_t)rx_size);

                if (received <= 0)
                    break;

                size_t out_len = 0U;
                int ret = modbus_tcp_process(rx_buf, (size_t)received,
                                             tx_buf, &out_len);

                /*
                 * Enviar respuesta si se generó una (normal o excepción).
                 * MB_TCP_IGNORED = frame muy corto, sin respuesta.
                 * MB_TCP_ERR_MBAP = Protocol ID inválido, sin respuesta.
                 */
                if (ret != MB_TCP_IGNORED && ret != MB_TCP_ERR_MBAP
                    && out_len > 0U)
                {
                    send(MB_TCP_SOCKET, tx_buf, (uint16_t)out_len);
                }
                break;
            }

            /* ------------------------------------------------------------ */
            case SOCK_CLOSE_WAIT:
            /*
             * El master inició el cierre de conexión (FIN recibido).
             * Completar el cierre y volver a LISTEN.
             */
                disconnect(MB_TCP_SOCKET);
                break;

            /* ------------------------------------------------------------ */
            default:
            /*
             * Estado transitorio (SYN_RECV, FIN_WAIT, TIME_WAIT, etc.)
             * o estado inesperado — forzar cierre limpio.
             */
                close(MB_TCP_SOCKET);
                break;
        }

        /* Ceder CPU entre iteraciones — 10ms es suficiente para Modbus TCP */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
