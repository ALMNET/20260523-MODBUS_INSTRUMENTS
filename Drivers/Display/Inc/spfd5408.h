/**
 * @file    spfd5408.h
 * @brief   Driver para display SPFD5408 — interfaz paralela 8080, 8 bits.
 *
 * @details Implementa la interfaz de escritura hacia el controlador SPFD5408
 *          (compatible ILI9341 en pinout, distinto en inicialización) usando
 *          el bus paralelo 8080 de 8 bits con las siguientes señales:
 *
 *          | Señal | Función                          | Activo |
 *          |-------|----------------------------------|--------|
 *          | D0–D7 | Bus de datos 8 bits              | —      |
 *          | RS    | Register Select: 0=cmd, 1=data   | —      |
 *          | WR    | Write strobe                     | Bajo   |
 *          | RST   | Reset hardware                   | Bajo   |
 *          | CS    | Chip Select                      | Bajo   |
 *          | RD    | Read (no usado — tie HIGH)       | Bajo   |
 *
 *          **Optimización del bus de datos:**
 *          D0–D7 están mapeados a PB3–PB10 (bits contiguos de GPIOB).
 *          Escribir un byte al bus es una operación de máscara sobre GPIOB->ODR:
 *          @code
 *            GPIOB->ODR = (GPIOB->ODR & ~DATA_MASK) | ((uint32_t)(byte) << 3);
 *          @endcode
 *          Esto evita 8 llamadas individuales a HAL_GPIO_WritePin().
 *
 *          **Resolución y orientación:**
 *          Landscape 320×240, origen (0,0) en esquina superior izquierda.
 *
 *          **Formato de color:**
 *          RGB565 — 16 bits por pixel (5 rojo, 6 verde, 5 azul).
 *          Helper @ref SPFD5408_COLOR para convertir componentes R,G,B.
 *
 * @par Pinout asumido (configurable en este header):
 * @verbatim
 *   D0  → PB3     RS  → PB11
 *   D1  → PB4     WR  → PB12
 *   D2  → PB5     RST → PB13
 *   D3  → PB6     CS  → PB14
 *   D4  → PB7     RD  → tie HIGH (no conectar al MCU)
 *   D5  → PB8
 *   D6  → PB9
 *   D7  → PB10
 * @endverbatim
 *
 * @author  Armando
 * @date    2025
 */

#ifndef SPFD5408_H
#define SPFD5408_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stddef.h>

/* ========================================================================== */
/*  Pinout — modificar acá si se reasignan pines                              */
/* ========================================================================== */

/** @defgroup spfd5408_pinout Asignación de pines
 *  Todos los pines de datos están en GPIOB para escritura eficiente al bus.
 *  @{
 */

/* Puerto del bus de datos (D0–D7) y señales de control */
#define SPFD5408_DATA_PORT      GPIOB

/* Bus de datos: D0=PB3 … D7=PB10 */
#define SPFD5408_D0_PIN         GPIO_PIN_3
#define SPFD5408_D1_PIN         GPIO_PIN_4
#define SPFD5408_D2_PIN         GPIO_PIN_5
#define SPFD5408_D3_PIN         GPIO_PIN_6
#define SPFD5408_D4_PIN         GPIO_PIN_7
#define SPFD5408_D5_PIN         GPIO_PIN_8
#define SPFD5408_D6_PIN         GPIO_PIN_9
#define SPFD5408_D7_PIN         GPIO_PIN_10

/* Señales de control (mismo puerto) */
#define SPFD5408_RS_PORT        GPIOB
#define SPFD5408_RS_PIN         GPIO_PIN_11   /**< 0=comando, 1=dato */

#define SPFD5408_WR_PORT        GPIOB
#define SPFD5408_WR_PIN         GPIO_PIN_12   /**< Write strobe, activo bajo */

#define SPFD5408_RST_PORT       GPIOB
#define SPFD5408_RST_PIN        GPIO_PIN_13   /**< Reset hardware, activo bajo */

#define SPFD5408_CS_PORT        GPIOB
#define SPFD5408_CS_PIN         GPIO_PIN_14   /**< Chip select, activo bajo */

/** @} */

/* ========================================================================== */
/*  Máscara del bus de datos para escritura directa al ODR                    */
/* ========================================================================== */

/**
 * @brief Máscara de los 8 bits de datos en GPIOB (PB3–PB10).
 * @details Usada internamente en SPFD5408_WriteData8() para limpiar solo
 *          los bits de datos antes de escribir el nuevo valor.
 */
#define SPFD5408_DATA_MASK      (0x07F8UL)   /* bits 3..10 = 0b0000011111111000 */

/**
 * @brief Desplazamiento del LSB del bus de datos dentro del ODR.
 *        D0 está en PB3, por lo tanto el byte se desplaza 3 posiciones.
 */
#define SPFD5408_DATA_SHIFT     (3U)

/* ========================================================================== */
/*  Dimensiones del display                                                    */
/* ========================================================================== */

#define SPFD5408_WIDTH          320U    /**< Ancho en pixels (landscape)  */
#define SPFD5408_HEIGHT         240U    /**< Alto en pixels (landscape)   */

/* ========================================================================== */
/*  Colores predefinidos RGB565                                                */
/* ========================================================================== */

/** @brief Convierte componentes R(0-255), G(0-255), B(0-255) a RGB565. */
#define SPFD5408_COLOR(r, g, b) \
    ((uint16_t)(((r) & 0xF8U) << 8U) | \
     (uint16_t)(((g) & 0xFCU) << 3U) | \
     (uint16_t)(((b) & 0xF8U) >> 3U))

/** @defgroup spfd5408_colors Colores predefinidos
 *  @{
 */
#define SPFD5408_BLACK          0x0000U
#define SPFD5408_WHITE          0xFFFFU
#define SPFD5408_RED            0xF800U
#define SPFD5408_GREEN          0x07E0U
#define SPFD5408_BLUE           0x001FU
#define SPFD5408_YELLOW         0xFFE0U
#define SPFD5408_CYAN           0x07FFU
#define SPFD5408_MAGENTA        0xF81FU
#define SPFD5408_ORANGE         SPFD5408_COLOR(255, 165, 0)
#define SPFD5408_GRAY           SPFD5408_COLOR(128, 128, 128)
/** @} */

/* ========================================================================== */
/*  API pública                                                                */
/* ========================================================================== */

/**
 * @brief  Inicializa el hardware GPIO y envía la secuencia de init al SPFD5408.
 *
 * @details Configura todos los pines como GPIO Output PP, aplica el reset
 *          hardware, y envía la secuencia de registros de inicialización
 *          específica del SPFD5408 para modo landscape 320×240, RGB565.
 *          Debe llamarse una vez antes de cualquier otra función del driver.
 */
void SPFD5408_Init(void);

/**
 * @brief  Rellena toda la pantalla con un color uniforme.
 *
 * @details Operación optimizada: una sola configuración de ventana y
 *          320×240 = 76800 escrituras de 16 bits al GRAM.
 *
 * @param[in] color  Color en formato RGB565.
 */
void SPFD5408_FillScreen(uint16_t color);

/**
 * @brief  Dibuja un pixel individual.
 *
 * @param[in] x      Coordenada X (0 … SPFD5408_WIDTH-1).
 * @param[in] y      Coordenada Y (0 … SPFD5408_HEIGHT-1).
 * @param[in] color  Color en formato RGB565.
 */
void SPFD5408_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief  Rellena un rectángulo con un color uniforme.
 *
 * @param[in] x      Coordenada X de la esquina superior izquierda.
 * @param[in] y      Coordenada Y de la esquina superior izquierda.
 * @param[in] w      Ancho en pixels.
 * @param[in] h      Alto en pixels.
 * @param[in] color  Color en formato RGB565.
 */
void SPFD5408_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t color);

/**
 * @brief  Dibuja una línea horizontal optimizada.
 *
 * @details Más eficiente que dibujar pixel a pixel porque configura la
 *          ventana una sola vez y escribe todos los pixels en secuencia.
 *
 * @param[in] x      Coordenada X inicial.
 * @param[in] y      Coordenada Y.
 * @param[in] len    Longitud en pixels.
 * @param[in] color  Color en formato RGB565.
 */
void SPFD5408_DrawHLine(uint16_t x, uint16_t y, uint16_t len, uint16_t color);

/**
 * @brief  Dibuja una línea vertical optimizada.
 *
 * @param[in] x      Coordenada X.
 * @param[in] y      Coordenada Y inicial.
 * @param[in] len    Longitud en pixels.
 * @param[in] color  Color en formato RGB565.
 */
void SPFD5408_DrawVLine(uint16_t x, uint16_t y, uint16_t len, uint16_t color);

/**
 * @brief  Dibuja el borde de un rectángulo (sin relleno).
 *
 * @param[in] x      Coordenada X de la esquina superior izquierda.
 * @param[in] y      Coordenada Y de la esquina superior izquierda.
 * @param[in] w      Ancho en pixels.
 * @param[in] h      Alto en pixels.
 * @param[in] color  Color en formato RGB565.
 */
void SPFD5408_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t color);

/**
 * @brief  Escribe un carácter con la fuente 6×8 interna.
 *
 * @param[in] x       Coordenada X de la esquina superior izquierda del carácter.
 * @param[in] y       Coordenada Y.
 * @param[in] ch      Carácter ASCII a dibujar.
 * @param[in] fg      Color del carácter (foreground).
 * @param[in] bg      Color del fondo (background).
 * @param[in] scale   Factor de escala (1=6×8, 2=12×16, etc).
 */
void SPFD5408_DrawChar(uint16_t x, uint16_t y, char ch,
                       uint16_t fg, uint16_t bg, uint8_t scale);

/**
 * @brief  Escribe una cadena de texto.
 *
 * @details Avanza automáticamente la posición X por el ancho del carácter
 *          escalado. No hace word-wrap.
 *
 * @param[in] x       Coordenada X inicial.
 * @param[in] y       Coordenada Y.
 * @param[in] str     Cadena terminada en '\0'.
 * @param[in] fg      Color del texto.
 * @param[in] bg      Color del fondo.
 * @param[in] scale   Factor de escala.
 */
void SPFD5408_DrawString(uint16_t x, uint16_t y, const char *str,
                         uint16_t fg, uint16_t bg, uint8_t scale);

/**
 * @brief  Define la ventana de escritura activa (Address Window).
 *
 * @details Establece las coordenadas del rectángulo sobre el que operarán
 *          las escrituras al GRAM. Después de llamar esta función, cada
 *          par de bytes enviado con SPFD5408_WriteData16() avanza el cursor
 *          dentro de la ventana de izquierda a derecha, de arriba a abajo.
 *
 * @note    Función pública para uso avanzado. En el uso normal se llama
 *          internamente desde FillRect, DrawPixel, etc.
 *
 * @param[in] x0  Columna izquierda.
 * @param[in] y0  Fila superior.
 * @param[in] x1  Columna derecha (inclusive).
 * @param[in] y1  Fila inferior (inclusive).
 */
void SPFD5408_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief  Escribe un valor de 16 bits al GRAM (un pixel RGB565).
 *
 * @details RS debe estar en HIGH (modo dato) antes de llamar esta función.
 *          Se hacen dos escrituras de 8 bits: byte alto primero.
 *
 * @param[in] data  Valor RGB565 a escribir.
 */
void SPFD5408_WriteData16(uint16_t data);

#ifdef __cplusplus
}
#endif

#endif /* SPFD5408_H */
