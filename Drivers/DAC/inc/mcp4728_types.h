/**
 * @file    mcp4728_types.h
 * @brief   Definiciones de registros, direcciones y enums del MCP4728.
 *
 * @details El MCP4728 es un DAC I2C de 4 canales, 12 bits, con EEPROM
 *          interna para valores de power-on y referencia interna
 *          seleccionable de 2.048V.
 *
 *          Mapa de memoria relevante:
 *            - 4 × DAC registers (volátiles, efecto inmediato)
 *            - 4 × EEPROM registers (no volátiles, power-on default)
 *
 * @author  Armando
 * @date    2025
 */

#ifndef MCP4728_TYPES_H
#define MCP4728_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ========================================================================== */
/*  Dirección I2C                                                              */
/* ========================================================================== */

/**
 * @brief Dirección base del MCP4728 (7-bit).
 * @details Fija de fábrica salvo que se reprograme via LDAC + I2C General
 *          Call Write Address Bits Command. La mayoría de los breakouts
 *          la dejan en 0x60.
 */
#define MCP4728_I2C_ADDR_BASE       0x60U

/* ========================================================================== */
/*  Canales                                                                    */
/* ========================================================================== */

/** @brief Identificador de canal DAC (A, B, C, D). */
typedef enum {
    MCP4728_CHANNEL_A = 0U,
    MCP4728_CHANNEL_B = 1U,
    MCP4728_CHANNEL_C = 2U,
    MCP4728_CHANNEL_D = 3U,
    MCP4728_CHANNEL_COUNT = 4U
} mcp4728_channel_t;

/* ========================================================================== */
/*  Referencia de voltaje (Vref)                                               */
/* ========================================================================== */

/**
 * @brief Selección de referencia de voltaje por canal.
 * @details VDD: usa la tensión de alimentación como referencia (ratiométrico).
 *          INTERNAL: usa la referencia interna de 2.048V (más estable, pero
 *          requiere habilitar el Gain x2 si se necesita rango completo).
 */
typedef enum {
    MCP4728_VREF_VDD      = 0U,   /**< Referencia = VDD                    */
    MCP4728_VREF_INTERNAL = 1U    /**< Referencia interna 2.048V           */
} mcp4728_vref_t;

/* ========================================================================== */
/*  Ganancia (solo aplica con VREF_INTERNAL)                                  */
/* ========================================================================== */

/**
 * @brief Ganancia del DAC. Solo tiene efecto si VREF = INTERNAL.
 * @details Con VDD como referencia, la ganancia se ignora (siempre x1 efectivo).
 */
typedef enum {
    MCP4728_GAIN_X1 = 0U,   /**< Vout = Vref × (D/4096), D=0..4095        */
    MCP4728_GAIN_X2 = 1U    /**< Vout = 2 × Vref × (D/4096)               */
} mcp4728_gain_t;

/* ========================================================================== */
/*  Power-Down mode                                                            */
/* ========================================================================== */

/**
 * @brief Modo de power-down por canal.
 * @details En NORMAL el canal opera con impedancia de salida normal.
 *          Los modos PD1-PD3 desconectan la salida y la referencian a
 *          tierra a través de distintas resistencias, reduciendo consumo.
 */
typedef enum {
    MCP4728_PD_NORMAL  = 0U,   /**< Operación normal                      */
    MCP4728_PD_1K      = 1U,   /**< Power-down, salida a GND vía 1kΩ      */
    MCP4728_PD_100K    = 2U,   /**< Power-down, salida a GND vía 100kΩ    */
    MCP4728_PD_500K    = 3U    /**< Power-down, salida a GND vía 500kΩ    */
} mcp4728_powerdown_t;

/* ========================================================================== */
/*  Configuración de canal — agrupa vref/gain/pd/value                        */
/* ========================================================================== */

/**
 * @brief Configuración completa de un canal DAC.
 */
typedef struct {
    mcp4728_vref_t      vref;    /**< Referencia de voltaje              */
    mcp4728_powerdown_t pd_mode; /**< Modo de power-down                 */
    mcp4728_gain_t      gain;    /**< Ganancia (solo con VREF_INTERNAL)  */
    uint16_t             value;   /**< Valor DAC, 0–4095 (12 bits)         */
} mcp4728_channel_config_t;

/* ========================================================================== */
/*  Comandos del protocolo MCP4728                                            */
/* ========================================================================== */

/**
 * @defgroup mcp4728_cmd Comandos del Command Byte
 * @details El MCP4728 usa los 3 bits superiores del primer byte para
 *          determinar el tipo de operación (Fast Write, Multi-Write,
 *          Single Write, Sequential Write, etc).
 * @{
 */
#define MCP4728_CMD_FAST_WRITE          0x00U  /**< 2 bytes/canal, sin config */
#define MCP4728_CMD_MULTI_WRITE         0x40U  /**< 3 bytes/canal, con config  */
#define MCP4728_CMD_SEQ_WRITE           0x50U  /**< Escritura secuencial       */
#define MCP4728_CMD_SINGLE_WRITE        0x58U  /**< 1 canal, DAC + EEPROM      */
/** @} */

/* ========================================================================== */
/*  Códigos de retorno                                                         */
/* ========================================================================== */

#define MCP4728_OK                0
#define MCP4728_ERR_I2C          -1
#define MCP4728_ERR_PARAM        -2
#define MCP4728_ERR_RANGE        -3

#ifdef __cplusplus
}
#endif

#endif /* MCP4728_TYPES_H */
