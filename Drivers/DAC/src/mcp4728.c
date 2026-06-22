/**
 * @file    mcp4728.c
 * @brief   Implementación del driver bare-metal MCP4728.
 *
 * @details Formato de los frames I2C:
 *
 *          **Fast Write** (1 frame, 4 canales, 8 bytes de datos):
 * @verbatim
 *   Byte 1: [0|0|PD1|PD0|D11|D10|D9|D8]    ← Channel A
 *   Byte 2: [D7|D6|D5|D4|D3|D2|D1|D0]
 *   Byte 3: [0|0|PD1|PD0|D11|D10|D9|D8]    ← Channel B
 *   Byte 4: [D7|D6|D5|D4|D3|D2|D1|D0]
 *   ... (C, D)
 * @endverbatim
 *
 *          **Multi-Write** (1 canal por frame, con vref/gain/pd, 3 bytes):
 * @verbatim
 *   Byte 1: [0|1|0|0|0|DAC1|DAC0|UDAC]     ← Command + channel select
 *   Byte 2: [VREF|PD1|PD0|GAIN|D11|D10|D9|D8]
 *   Byte 3: [D7|D6|D5|D4|D3|D2|D1|D0]
 * @endverbatim
 *
 * @author  Armando
 * @date    2025
 */

#include "mcp4728.h"
#include <string.h>

/* ========================================================================== */
/*  Helpers privados                                                            */
/* ========================================================================== */

/**
 * @brief  Satura un valor de 16 bits al rango válido del DAC (0–4095).
 */
static inline uint16_t saturate_value(uint16_t value)
{
    return (value > MCP4728_MAX_VALUE) ? MCP4728_MAX_VALUE : value;
}

/**
 * @brief  Valida que el canal esté en rango.
 */
static inline bool channel_is_valid(mcp4728_channel_t channel)
{
    return (channel < MCP4728_CHANNEL_COUNT);
}

/**
 * @brief  Construye los 2 bytes de un canal para el frame Fast Write.
 *
 * @param[in]  cfg    Configuración del canal (solo se usa pd_mode y value).
 * @param[out] out    Buffer de 2 bytes donde se escribe el frame.
 */
static void encode_fast_write_channel(const mcp4728_channel_config_t *cfg,
                                       uint8_t out[2])
{
    uint16_t value = saturate_value(cfg->value);

    /* Byte alto: 00 | PD1 PD0 | D11..D8 */
    out[0] = (uint8_t)(((cfg->pd_mode & 0x03U) << 4U) |
                       ((value >> 8U) & 0x0FU));

    /* Byte bajo: D7..D0 */
    out[1] = (uint8_t)(value & 0xFFU);
}

/* ========================================================================== */
/*  API pública                                                                 */
/* ========================================================================== */

int mcp4728_init(mcp4728_t *dev, const i2c_host_interface_t *i2c,
                 uint8_t i2c_addr)
{
    if (dev == NULL || i2c == NULL)
        return MCP4728_ERR_PARAM;

    if (i2c->write == NULL)
        return MCP4728_ERR_PARAM;

    dev->i2c      = i2c;
    dev->i2c_addr = i2c_addr;

    /* Inicializar cache con defaults de power-on del MCP4728 */
    for (uint8_t i = 0; i < MCP4728_CHANNEL_COUNT; i++)
    {
        dev->channels[i].vref    = MCP4728_VREF_VDD;
        dev->channels[i].gain    = MCP4728_GAIN_X1;
        dev->channels[i].pd_mode = MCP4728_PD_NORMAL;
        dev->channels[i].value   = 0U;
    }

    dev->initialized = true;

    return MCP4728_OK;
}

int mcp4728_set_channel_raw(mcp4728_t *dev, mcp4728_channel_t channel,
                            uint16_t value)
{
    if (dev == NULL || !channel_is_valid(channel))
        return MCP4728_ERR_PARAM;

    /* Actualizar cache local */
    dev->channels[channel].value = saturate_value(value);

    /*
     * Fast Write escribe los 4 canales en una sola transacción.
     * Se envía el cache completo (los 3 canales no tocados mantienen
     * su último valor conocido) para no alterar nada fuera de este canal.
     */
    uint8_t frame[8];

    for (uint8_t ch = 0; ch < MCP4728_CHANNEL_COUNT; ch++)
        encode_fast_write_channel(&dev->channels[ch], &frame[ch * 2U]);

    int ret = dev->i2c->write(dev->i2c->ctx, dev->i2c_addr, frame, sizeof(frame));

    return (ret == I2C_HOST_OK) ? MCP4728_OK : MCP4728_ERR_I2C;
}

int mcp4728_set_channel_config(mcp4728_t *dev, mcp4728_channel_t channel,
                               const mcp4728_channel_config_t *config)
{
    if (dev == NULL || config == NULL || !channel_is_valid(channel))
        return MCP4728_ERR_PARAM;

    if (config->value > MCP4728_MAX_VALUE)
        return MCP4728_ERR_PARAM;

    /*
     * Multi-Write frame (3 bytes), un canal por transacción:
     *   Byte 1: 0 1 0 0 0 DAC1 DAC0 UDAC(=0, update inmediato)
     *   Byte 2: VREF PD1 PD0 GAIN D11 D10 D9 D8
     *   Byte 3: D7..D0
     */
    uint8_t frame[3];

    frame[0] = (uint8_t)(MCP4728_CMD_MULTI_WRITE |
                         ((uint8_t)channel << 1U));   /* UDAC=0 siempre */

    frame[1] = (uint8_t)(((config->vref    & 0x01U) << 7U) |
                         ((config->pd_mode & 0x03U) << 5U) |
                         ((config->gain    & 0x01U) << 4U) |
                         ((config->value >> 8U)     & 0x0FU));

    frame[2] = (uint8_t)(config->value & 0xFFU);

    int ret = dev->i2c->write(dev->i2c->ctx, dev->i2c_addr, frame, sizeof(frame));

    if (ret != I2C_HOST_OK)
        return MCP4728_ERR_I2C;

    /* Actualizar cache solo si la transacción fue exitosa */
    dev->channels[channel] = *config;

    return MCP4728_OK;
}

int mcp4728_set_channel_ma(mcp4728_t *dev, mcp4728_channel_t channel, float ma)
{
    if (dev == NULL || !channel_is_valid(channel))
        return MCP4728_ERR_PARAM;

    /* Saturar al rango físico del AD694 */
    if (ma < MCP4728_AD694_I_MIN_MA) ma = MCP4728_AD694_I_MIN_MA;
    if (ma > MCP4728_AD694_I_MAX_MA) ma = MCP4728_AD694_I_MAX_MA;

    /*
     * Mapeo lineal: 4mA → 0, 20mA → 4095
     * value = (ma - 4.0) / (20.0 - 4.0) × 4095
     */
    float ratio = (ma - MCP4728_AD694_I_MIN_MA) /
                  (MCP4728_AD694_I_MAX_MA - MCP4728_AD694_I_MIN_MA);

    uint16_t value = (uint16_t)(ratio * (float)MCP4728_MAX_VALUE + 0.5f);

    return mcp4728_set_channel_raw(dev, channel, value);
}

int mcp4728_get_channel_cached(const mcp4728_t *dev, mcp4728_channel_t channel,
                               uint16_t *value)
{
    if (dev == NULL || value == NULL || !channel_is_valid(channel))
        return MCP4728_ERR_PARAM;

    *value = dev->channels[channel].value;

    return MCP4728_OK;
}
