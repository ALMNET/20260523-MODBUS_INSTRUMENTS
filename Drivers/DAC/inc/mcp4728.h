/**
 * @file    mcp4728.h
 * @brief   Driver bare-metal del DAC MCP4728 — 4 canales, 12 bits, I2C.
 *
 * @details Driver completamente independiente de HAL/RTOS. Recibe una
 *          @ref i2c_host_interface_t inyectada en la inicialización y la
 *          usa para todas las transacciones I2C. El mismo código corre
 *          sobre STM32 HAL, MCC Melody, un mock de test en PC, o cualquier
 *          otra plataforma que implemente la interfaz.
 *
 *          **Protocolo Fast Write (usado por defecto):**
 *          Escribe los 4 canales en una sola transacción I2C de 8 bytes
 *          (2 bytes por canal). No persiste en EEPROM — al perder
 *          alimentación los canales vuelven al último valor guardado en
 *          EEPROM (o 0 si nunca se guardó).
 *
 *          **Uso típico con AD694 (salida 4-20mA):**
 *          El AD694 espera 0–5V (o 0–2V según configuración de pines RANGE/SPAN)
 *          en su entrada VIN para generar 4–20mA en su salida. El DAC genera
 *          esa tensión de control:
 * @code
 *   mcp4728_t dac;
 *   mcp4728_init(&dac, &i2c1_if, MCP4728_I2C_ADDR_BASE);
 *
 *   // 50% de escala → 12 mA en el AD694 (config estándar 0-5V → 4-20mA)
 *   mcp4728_set_channel_raw(&dac, MCP4728_CHANNEL_A, 2048);
 *
 *   // O usando el helper de mA directamente:
 *   mcp4728_set_channel_ma(&dac, MCP4728_CHANNEL_A, 12.0f);
 * @endcode
 *
 * @author  Armando
 * @date    2025
 */

#ifndef MCP4728_H
#define MCP4728_H

#ifdef __cplusplus
extern "C" {
#endif

#include "mcp4728_types.h"
#include "i2c_host_interface.h"
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================== */
/*  Constantes de escala                                                       */
/* ========================================================================== */

/** @brief Resolución del DAC: 12 bits → 0..4095. */
#define MCP4728_MAX_VALUE       4095U

/**
 * @brief Rango estándar de corriente del AD694 en mA.
 * @details Configuración clásica: VIN 0-5V (Vref interno MCP4728 con gain x2,
 *          o VDD=5V con gain x1) mapea linealmente a 4-20mA en el AD694.
 */
#define MCP4728_AD694_I_MIN_MA   4.0f
#define MCP4728_AD694_I_MAX_MA   20.0f

/* ========================================================================== */
/*  Handle del driver                                                           */
/* ========================================================================== */

/**
 * @brief  Handle de instancia del MCP4728.
 *
 * @details Guarda la interfaz I2C inyectada, la dirección del dispositivo,
 *          y un cache local de la configuración de cada canal para evitar
 *          relecturas innecesarias y permitir escrituras parciales
 *          (ej: cambiar solo el valor sin tocar vref/gain/pd).
 */
typedef struct {
    const i2c_host_interface_t *i2c;        /**< Interfaz I2C inyectada      */
    uint8_t                     i2c_addr;   /**< Dirección I2C 7-bit          */
    mcp4728_channel_config_t    channels[MCP4728_CHANNEL_COUNT]; /**< Cache  */
    bool                        initialized;
} mcp4728_t;

/* ========================================================================== */
/*  API pública                                                                */
/* ========================================================================== */

/**
 * @brief  Inicializa el handle del MCP4728.
 *
 * @details No realiza ninguna transacción I2C — solo guarda la interfaz y
 *          la dirección, e inicializa el cache de canales con valores por
 *          defecto (VDD, gain x1, normal, value=0).
 *          Llamar a @ref mcp4728_set_channel_config o
 *          @ref mcp4728_set_channel_raw para aplicar configuración real
 *          al hardware.
 *
 * @param[out] dev       Handle a inicializar.
 * @param[in]  i2c       Interfaz I2C ya configurada (no debe ser NULL).
 * @param[in]  i2c_addr  Dirección I2C 7-bit del MCP4728.
 *
 * @retval  MCP4728_OK          Inicialización correcta.
 * @retval  MCP4728_ERR_PARAM   dev o i2c son NULL.
 */
int mcp4728_init(mcp4728_t *dev, const i2c_host_interface_t *i2c,
                 uint8_t i2c_addr);

/**
 * @brief  Escribe un valor crudo (0–4095) en un canal, usando Fast Write.
 *
 * @details Fast Write es la operación más liviana: 2 bytes por canal,
 *          sin tocar vref/gain/pd (usa lo que ya esté configurado en el
 *          chip — por defecto VDD, gain x1, normal tras power-on).
 *          No persiste en EEPROM.
 *
 *          Esta función escribe los 4 canales en una sola transacción I2C,
 *          enviando el cache actualizado de los otros 3 canales sin cambios
 *          para no alterarlos.
 *
 * @param[in,out] dev      Handle del MCP4728.
 * @param[in]     channel  Canal a escribir.
 * @param[in]     value    Valor DAC, 0–4095 (se satura si excede el rango).
 *
 * @retval  MCP4728_OK          Escritura exitosa.
 * @retval  MCP4728_ERR_PARAM   dev es NULL o channel inválido.
 * @retval  MCP4728_ERR_I2C     Falla de transporte I2C.
 */
int mcp4728_set_channel_raw(mcp4728_t *dev, mcp4728_channel_t channel,
                            uint16_t value);

/**
 * @brief  Configura un canal completo (vref, gain, power-down, value)
 *         usando Multi-Write, sin persistir en EEPROM.
 *
 * @param[in,out] dev      Handle del MCP4728.
 * @param[in]     channel  Canal a configurar.
 * @param[in]     config   Configuración completa a aplicar.
 *
 * @retval  MCP4728_OK          Configuración aplicada correctamente.
 * @retval  MCP4728_ERR_PARAM   dev es NULL, channel inválido, o value > 4095.
 * @retval  MCP4728_ERR_I2C     Falla de transporte I2C.
 */
int mcp4728_set_channel_config(mcp4728_t *dev, mcp4728_channel_t channel,
                               const mcp4728_channel_config_t *config);

/**
 * @brief  Helper: convierte una corriente deseada en mA a valor DAC y la
 *         aplica al canal, asumiendo configuración estándar AD694 (4-20mA
 *         lineal sobre rango completo del DAC, VDD como referencia).
 *
 * @details Fórmula: value = (mA - 4.0) / (20.0 - 4.0) × 4095
 *          Si tu AD694 usa una R_SET distinta a la estándar del datasheet,
 *          esta función NO es válida — calibrar manualmente con
 *          @ref mcp4728_set_channel_raw.
 *
 * @param[in,out] dev      Handle del MCP4728.
 * @param[in]     channel  Canal a escribir.
 * @param[in]     ma       Corriente deseada en mA (4.0–20.0).
 *                         Valores fuera de rango se saturan.
 *
 * @retval  MCP4728_OK          Escritura exitosa.
 * @retval  MCP4728_ERR_PARAM   dev es NULL o channel inválido.
 * @retval  MCP4728_ERR_I2C     Falla de transporte I2C.
 */
int mcp4728_set_channel_ma(mcp4728_t *dev, mcp4728_channel_t channel,
                           float ma);

/**
 * @brief  Lee del cache local el último valor escrito a un canal.
 *
 * @details No realiza transacción I2C — el MCP4728 no tiene un registro
 *          de lectura directa de "valor actual" sencillo de usar sin
 *          ambigüedad de EEPROM vs DAC, por lo que el driver mantiene
 *          su propio cache de lo último escrito vía esta API.
 *
 * @param[in]  dev      Handle del MCP4728.
 * @param[in]  channel  Canal a consultar.
 * @param[out] value    Puntero donde se escribe el valor cacheado.
 *
 * @retval  MCP4728_OK          Lectura exitosa.
 * @retval  MCP4728_ERR_PARAM   dev, value son NULL o channel inválido.
 */
int mcp4728_get_channel_cached(const mcp4728_t *dev, mcp4728_channel_t channel,
                               uint16_t *value);

#ifdef __cplusplus
}
#endif

#endif /* MCP4728_H */
