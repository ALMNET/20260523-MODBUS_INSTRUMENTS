/**
 * @file    spfd5408.c
 * @brief   Implementación del driver SPFD5408 — bus paralelo 8080, 8 bits.
 *
 * @details Todos los accesos al bus de datos usan escritura directa al ODR
 *          de GPIOB para máxima velocidad. Las macros de control de señales
 *          usan BSRR (bit set/reset register) que es atómico y más rápido
 *          que leer-modificar-escribir el ODR.
 *
 *          Timing del ciclo de escritura 8080:
 * @verbatim
 *   CS  ____/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\____
 *   RS  ____/ cmd o dato           \____
 *   WR  ‾‾‾‾‾\______/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
 *   D0-7    ====[ válido ]============
 *                ↑ setup  ↑ WR sube → dato capturado
 * @endverbatim
 *
 *          A 72 MHz del F103 cada instrucción GPIO toma ~14 ns. El SPFD5408
 *          requiere t_WR mínimo de 15 ns — una instrucción de por medio entre
 *          WR bajo y WR alto es suficiente.
 *
 * @author  Armando
 * @date    2025
 */

#include "spfd5408.h"
#include "main.h"
#include <string.h>

/* ========================================================================== */
/*  Macros de control de señales — usan BSRR para atomicidad y velocidad      */
/* ========================================================================== */

/** @cond INTERNAL */
#define CS_LOW()   (SPFD5408_CS_PORT->BSRR  = (uint32_t)SPFD5408_CS_PIN  << 16U)
#define CS_HIGH()  (SPFD5408_CS_PORT->BSRR  = SPFD5408_CS_PIN)
#define RS_LOW()   (SPFD5408_RS_PORT->BSRR  = (uint32_t)SPFD5408_RS_PIN  << 16U)
#define RS_HIGH()  (SPFD5408_RS_PORT->BSRR  = SPFD5408_RS_PIN)
#define WR_LOW()   (SPFD5408_WR_PORT->BSRR  = (uint32_t)SPFD5408_WR_PIN  << 16U)
#define WR_HIGH()  (SPFD5408_WR_PORT->BSRR  = SPFD5408_WR_PIN)
#define RST_LOW()  (SPFD5408_RST_PORT->BSRR = (uint32_t)SPFD5408_RST_PIN << 16U)
#define RST_HIGH() (SPFD5408_RST_PORT->BSRR = SPFD5408_RST_PIN)
/** @endcond */

/* ========================================================================== */
/*  Escritura al bus de datos — operación central del driver                  */
/* ========================================================================== */

/**
 * @brief  Escribe un byte al bus de datos D0–D7 y genera el pulso WR.
 *
 * @details Secuencia:
 *          1. Escribe el byte en GPIOB bits 3..10 preservando los demás bits
 *          2. Baja WR (dato capturado en el flanco ascendente de WR)
 *          3. Sube WR
 *
 *          La escritura al ODR y el WR_LOW están separados por al menos
 *          una instrucción, garantizando el t_DS (data setup time) del SPFD5408.
 *
 * @param[in] byte  Byte a escribir en el bus.
 */
static inline void write_bus(uint8_t byte)
{
    /* Preservar bits ajenos al bus de datos y escribir el nuevo valor */
    SPFD5408_DATA_PORT->ODR =
        (SPFD5408_DATA_PORT->ODR & ~SPFD5408_DATA_MASK) |
        ((uint32_t)(byte) << SPFD5408_DATA_SHIFT);

    WR_LOW();
    WR_HIGH();
}

/* ========================================================================== */
/*  Funciones de escritura de comandos y datos                                */
/* ========================================================================== */

/**
 * @brief  Envía un byte de comando al controlador (RS=0).
 * @param[in] cmd  Código de comando.
 */
static void write_cmd(uint8_t cmd)
{
    RS_LOW();
    write_bus(cmd);
}

/**
 * @brief  Envía un byte de dato al controlador (RS=1).
 * @param[in] data  Byte de dato.
 */
static void write_data8(uint8_t data)
{
    RS_HIGH();
    write_bus(data);
}

/* ========================================================================== */
/*  API pública — funciones de bajo nivel                                     */
/* ========================================================================== */

void SPFD5408_WriteData16(uint16_t data)
{
    RS_HIGH();
    write_bus((uint8_t)(data >> 8U));
    write_bus((uint8_t)(data & 0xFFU));
}

void SPFD5408_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    /* Column Address Set (0x2A) */
    write_cmd(0x2A);
    write_data8((uint8_t)(x0 >> 8U));
    write_data8((uint8_t)(x0 & 0xFFU));
    write_data8((uint8_t)(x1 >> 8U));
    write_data8((uint8_t)(x1 & 0xFFU));

    /* Page Address Set (0x2B) */
    write_cmd(0x2B);
    write_data8((uint8_t)(y0 >> 8U));
    write_data8((uint8_t)(y0 & 0xFFU));
    write_data8((uint8_t)(y1 >> 8U));
    write_data8((uint8_t)(y1 & 0xFFU));

    /* Memory Write (0x2C) — activa escritura al GRAM */
    write_cmd(0x2C);
}

/* ========================================================================== */
/*  Inicialización GPIO                                                        */
/* ========================================================================== */

/**
 * @brief  Configura todos los pines del display como GPIO Output PP.
 */
static void gpio_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    /* Bus de datos D0–D7 + señales de control en un solo init */
    gpio.Pin = SPFD5408_D0_PIN | SPFD5408_D1_PIN | SPFD5408_D2_PIN |
               SPFD5408_D3_PIN | SPFD5408_D4_PIN | SPFD5408_D5_PIN |
               SPFD5408_D6_PIN | SPFD5408_D7_PIN |
               SPFD5408_RS_PIN | SPFD5408_WR_PIN |
               SPFD5408_RST_PIN | SPFD5408_CS_PIN;

    HAL_GPIO_Init(GPIOB, &gpio);

    /* Estado inicial: todo inactivo */
    CS_HIGH();
    WR_HIGH();
    RST_HIGH();
    RS_HIGH();
}

/* ========================================================================== */
/*  Secuencia de inicialización del SPFD5408                                  */
/* ========================================================================== */

/**
 * @brief  Envía la secuencia completa de registros de init del SPFD5408.
 *
 * @details Basada en el Application Note oficial del SPFD5408 con ajustes
 *          para operación a 3.3V, modo landscape 320×240, RGB565, MADCTL
 *          configurado para origen en esquina superior izquierda.
 */
static void send_init_sequence(void)
{
    CS_LOW();

    /* Software reset */
    write_cmd(0x01);
    HAL_Delay(5);

    /* Display OFF durante la configuración */
    write_cmd(0x28);

    /* Power Control A */
    write_cmd(0xCB);
    write_data8(0x39); write_data8(0x2C); write_data8(0x00);
    write_data8(0x34); write_data8(0x02);

    /* Power Control B */
    write_cmd(0xCF);
    write_data8(0x00); write_data8(0xC1); write_data8(0x30);

    /* Driver Timing Control A */
    write_cmd(0xE8);
    write_data8(0x85); write_data8(0x00); write_data8(0x78);

    /* Driver Timing Control B */
    write_cmd(0xEA);
    write_data8(0x00); write_data8(0x00);

    /* Power On Sequence Control */
    write_cmd(0xED);
    write_data8(0x64); write_data8(0x03);
    write_data8(0x12); write_data8(0x81);

    /* Pump Ratio Control */
    write_cmd(0xF7);
    write_data8(0x20);

    /* Power Control 1 — VRH: 4.60V */
    write_cmd(0xC0);
    write_data8(0x23);

    /* Power Control 2 — SAP, BT */
    write_cmd(0xC1);
    write_data8(0x10);

    /* VCOM Control 1 */
    write_cmd(0xC5);
    write_data8(0x3E); write_data8(0x28);

    /* VCOM Control 2 */
    write_cmd(0xC7);
    write_data8(0x86);

    /*
     * Memory Access Control (MADCTL — 0x36)
     * Landscape 320×240, origen superior izquierdo:
     *   MY=0, MX=1, MV=1, ML=0, BGR=1, MH=0 → 0x68
     *   MX: columnas espejadas
     *   MV: intercambio fila/columna (portrait→landscape)
     *   BGR: orden de color BGR (típico en paneles chinos)
     */
    write_cmd(0x36);
    write_data8(0x68);

    /* Pixel Format — 16 bits/pixel RGB565 */
    write_cmd(0x3A);
    write_data8(0x55);

    /* Frame Rate Control — 79 Hz */
    write_cmd(0xB1);
    write_data8(0x00); write_data8(0x18);

    /* Display Function Control */
    write_cmd(0xB6);
    write_data8(0x08); write_data8(0x82); write_data8(0x27);

    /* Disable 3-gamma */
    write_cmd(0xF2);
    write_data8(0x00);

    /* Gamma Set — curva 1 */
    write_cmd(0x26);
    write_data8(0x01);

    /* Positive Gamma Correction */
    write_cmd(0xE0);
    write_data8(0x0F); write_data8(0x31); write_data8(0x2B);
    write_data8(0x0C); write_data8(0x0E); write_data8(0x08);
    write_data8(0x4E); write_data8(0xF1); write_data8(0x37);
    write_data8(0x07); write_data8(0x10); write_data8(0x03);
    write_data8(0x0E); write_data8(0x09); write_data8(0x00);

    /* Negative Gamma Correction */
    write_cmd(0xE1);
    write_data8(0x00); write_data8(0x0E); write_data8(0x14);
    write_data8(0x03); write_data8(0x11); write_data8(0x07);
    write_data8(0x31); write_data8(0xC1); write_data8(0x48);
    write_data8(0x08); write_data8(0x0F); write_data8(0x0C);
    write_data8(0x31); write_data8(0x36); write_data8(0x0F);

    /* Sleep Out */
    write_cmd(0x11);
    HAL_Delay(120);

    /* Display ON */
    write_cmd(0x29);
    HAL_Delay(5);

    CS_HIGH();
}

/* ========================================================================== */
/*  API pública — inicialización                                               */
/* ========================================================================== */

void SPFD5408_Init(void)
{
    gpio_init();

    /* Reset hardware: mínimo 10 µs en LOW según datasheet */
    RST_LOW();
    HAL_Delay(10);
    RST_HIGH();
    HAL_Delay(120);     /* esperar power-on del panel */

    send_init_sequence();
}

/* ========================================================================== */
/*  API pública — gráficos                                                    */
/* ========================================================================== */

void SPFD5408_FillScreen(uint16_t color)
{
    CS_LOW();
    SPFD5408_SetWindow(0, 0, SPFD5408_WIDTH - 1U, SPFD5408_HEIGHT - 1U);

    /* Pre-calcular los bytes alto y bajo del color para evitar el shift
     * en cada iteración dentro del loop — pequeña pero real optimización */
    uint8_t hi = (uint8_t)(color >> 8U);
    uint8_t lo = (uint8_t)(color & 0xFFU);

    RS_HIGH();
    uint32_t total = (uint32_t)SPFD5408_WIDTH * SPFD5408_HEIGHT;
    while (total--)
    {
        write_bus(hi);
        write_bus(lo);
    }
    CS_HIGH();
}

void SPFD5408_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= SPFD5408_WIDTH || y >= SPFD5408_HEIGHT)
        return;

    CS_LOW();
    SPFD5408_SetWindow(x, y, x, y);
    SPFD5408_WriteData16(color);
    CS_HIGH();
}

void SPFD5408_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t color)
{
    if (x >= SPFD5408_WIDTH || y >= SPFD5408_HEIGHT)
        return;

    /* Recortar al borde de pantalla */
    if ((x + w) > SPFD5408_WIDTH)  w = SPFD5408_WIDTH  - x;
    if ((y + h) > SPFD5408_HEIGHT) h = SPFD5408_HEIGHT - y;

    CS_LOW();
    SPFD5408_SetWindow(x, y, x + w - 1U, y + h - 1U);

    uint8_t hi = (uint8_t)(color >> 8U);
    uint8_t lo = (uint8_t)(color & 0xFFU);

    RS_HIGH();
    uint32_t total = (uint32_t)w * h;
    while (total--)
    {
        write_bus(hi);
        write_bus(lo);
    }
    CS_HIGH();
}

void SPFD5408_DrawHLine(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    SPFD5408_FillRect(x, y, len, 1U, color);
}

void SPFD5408_DrawVLine(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    SPFD5408_FillRect(x, y, 1U, len, color);
}

void SPFD5408_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t color)
{
    SPFD5408_DrawHLine(x,         y,         w, color);   /* top    */
    SPFD5408_DrawHLine(x,         y + h - 1U, w, color);  /* bottom */
    SPFD5408_DrawVLine(x,         y,         h, color);   /* left   */
    SPFD5408_DrawVLine(x + w - 1U, y,         h, color);  /* right  */
}

/* ========================================================================== */
/*  Fuente 6×8 interna (ASCII 0x20–0x7E)                                     */
/* ========================================================================== */

/**
 * @brief Tabla de fuente monoespaciada 6×8 pixels.
 *
 * @details Cada carácter ocupa 6 bytes. Cada byte representa una columna
 *          de 8 pixels (bit 0 = pixel superior). Solo se cubren los
 *          caracteres imprimibles ASCII 0x20 (espacio) a 0x7E (~).
 */
static const uint8_t font6x8[][6] = {
    {0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x20 space  */
    {0x00,0x00,0x5F,0x00,0x00,0x00}, /* 0x21 !      */
    {0x00,0x07,0x00,0x07,0x00,0x00}, /* 0x22 "      */
    {0x14,0x7F,0x14,0x7F,0x14,0x00}, /* 0x23 #      */
    {0x24,0x2A,0x7F,0x2A,0x12,0x00}, /* 0x24 $      */
    {0x23,0x13,0x08,0x64,0x62,0x00}, /* 0x25 %      */
    {0x36,0x49,0x55,0x22,0x50,0x00}, /* 0x26 &      */
    {0x00,0x05,0x03,0x00,0x00,0x00}, /* 0x27 '      */
    {0x00,0x1C,0x22,0x41,0x00,0x00}, /* 0x28 (      */
    {0x00,0x41,0x22,0x1C,0x00,0x00}, /* 0x29 )      */
    {0x08,0x2A,0x1C,0x2A,0x08,0x00}, /* 0x2A *      */
    {0x08,0x08,0x3E,0x08,0x08,0x00}, /* 0x2B +      */
    {0x00,0x50,0x30,0x00,0x00,0x00}, /* 0x2C ,      */
    {0x08,0x08,0x08,0x08,0x08,0x00}, /* 0x2D -      */
    {0x00,0x60,0x60,0x00,0x00,0x00}, /* 0x2E .      */
    {0x20,0x10,0x08,0x04,0x02,0x00}, /* 0x2F /      */
    {0x3E,0x51,0x49,0x45,0x3E,0x00}, /* 0x30 0      */
    {0x00,0x42,0x7F,0x40,0x00,0x00}, /* 0x31 1      */
    {0x42,0x61,0x51,0x49,0x46,0x00}, /* 0x32 2      */
    {0x21,0x41,0x45,0x4B,0x31,0x00}, /* 0x33 3      */
    {0x18,0x14,0x12,0x7F,0x10,0x00}, /* 0x34 4      */
    {0x27,0x45,0x45,0x45,0x39,0x00}, /* 0x35 5      */
    {0x3C,0x4A,0x49,0x49,0x30,0x00}, /* 0x36 6      */
    {0x01,0x71,0x09,0x05,0x03,0x00}, /* 0x37 7      */
    {0x36,0x49,0x49,0x49,0x36,0x00}, /* 0x38 8      */
    {0x06,0x49,0x49,0x29,0x1E,0x00}, /* 0x39 9      */
    {0x00,0x36,0x36,0x00,0x00,0x00}, /* 0x3A :      */
    {0x00,0x56,0x36,0x00,0x00,0x00}, /* 0x3B ;      */
    {0x00,0x08,0x14,0x22,0x41,0x00}, /* 0x3C <      */
    {0x14,0x14,0x14,0x14,0x14,0x00}, /* 0x3D =      */
    {0x41,0x22,0x14,0x08,0x00,0x00}, /* 0x3E >      */
    {0x02,0x01,0x51,0x09,0x06,0x00}, /* 0x3F ?      */
    {0x32,0x49,0x79,0x41,0x3E,0x00}, /* 0x40 @      */
    {0x7E,0x11,0x11,0x11,0x7E,0x00}, /* 0x41 A      */
    {0x7F,0x49,0x49,0x49,0x36,0x00}, /* 0x42 B      */
    {0x3E,0x41,0x41,0x41,0x22,0x00}, /* 0x43 C      */
    {0x7F,0x41,0x41,0x22,0x1C,0x00}, /* 0x44 D      */
    {0x7F,0x49,0x49,0x49,0x41,0x00}, /* 0x45 E      */
    {0x7F,0x09,0x09,0x01,0x01,0x00}, /* 0x46 F      */
    {0x3E,0x41,0x41,0x51,0x32,0x00}, /* 0x47 G      */
    {0x7F,0x08,0x08,0x08,0x7F,0x00}, /* 0x48 H      */
    {0x00,0x41,0x7F,0x41,0x00,0x00}, /* 0x49 I      */
    {0x20,0x40,0x41,0x3F,0x01,0x00}, /* 0x4A J      */
    {0x7F,0x08,0x14,0x22,0x41,0x00}, /* 0x4B K      */
    {0x7F,0x40,0x40,0x40,0x40,0x00}, /* 0x4C L      */
    {0x7F,0x02,0x04,0x02,0x7F,0x00}, /* 0x4D M      */
    {0x7F,0x04,0x08,0x10,0x7F,0x00}, /* 0x4E N      */
    {0x3E,0x41,0x41,0x41,0x3E,0x00}, /* 0x4F O      */
    {0x7F,0x09,0x09,0x09,0x06,0x00}, /* 0x50 P      */
    {0x3E,0x41,0x51,0x21,0x5E,0x00}, /* 0x51 Q      */
    {0x7F,0x09,0x19,0x29,0x46,0x00}, /* 0x52 R      */
    {0x46,0x49,0x49,0x49,0x31,0x00}, /* 0x53 S      */
    {0x01,0x01,0x7F,0x01,0x01,0x00}, /* 0x54 T      */
    {0x3F,0x40,0x40,0x40,0x3F,0x00}, /* 0x55 U      */
    {0x1F,0x20,0x40,0x20,0x1F,0x00}, /* 0x56 V      */
    {0x7F,0x20,0x18,0x20,0x7F,0x00}, /* 0x57 W      */
    {0x63,0x14,0x08,0x14,0x63,0x00}, /* 0x58 X      */
    {0x03,0x04,0x78,0x04,0x03,0x00}, /* 0x59 Y      */
    {0x61,0x51,0x49,0x45,0x43,0x00}, /* 0x5A Z      */
    {0x00,0x00,0x7F,0x41,0x41,0x00}, /* 0x5B [      */
    {0x02,0x04,0x08,0x10,0x20,0x00}, /* 0x5C \      */
    {0x41,0x41,0x7F,0x00,0x00,0x00}, /* 0x5D ]      */
    {0x04,0x02,0x01,0x02,0x04,0x00}, /* 0x5E ^      */
    {0x40,0x40,0x40,0x40,0x40,0x00}, /* 0x5F _      */
    {0x00,0x01,0x02,0x04,0x00,0x00}, /* 0x60 `      */
    {0x20,0x54,0x54,0x54,0x78,0x00}, /* 0x61 a      */
    {0x7F,0x48,0x44,0x44,0x38,0x00}, /* 0x62 b      */
    {0x38,0x44,0x44,0x44,0x20,0x00}, /* 0x63 c      */
    {0x38,0x44,0x44,0x48,0x7F,0x00}, /* 0x64 d      */
    {0x38,0x54,0x54,0x54,0x18,0x00}, /* 0x65 e      */
    {0x08,0x7E,0x09,0x01,0x02,0x00}, /* 0x66 f      */
    {0x08,0x14,0x54,0x54,0x3C,0x00}, /* 0x67 g      */
    {0x7F,0x08,0x04,0x04,0x78,0x00}, /* 0x68 h      */
    {0x00,0x44,0x7D,0x40,0x00,0x00}, /* 0x69 i      */
    {0x20,0x40,0x44,0x3D,0x00,0x00}, /* 0x6A j      */
    {0x00,0x7F,0x10,0x28,0x44,0x00}, /* 0x6B k      */
    {0x00,0x41,0x7F,0x40,0x00,0x00}, /* 0x6C l      */
    {0x7C,0x04,0x18,0x04,0x78,0x00}, /* 0x6D m      */
    {0x7C,0x08,0x04,0x04,0x78,0x00}, /* 0x6E n      */
    {0x38,0x44,0x44,0x44,0x38,0x00}, /* 0x6F o      */
    {0x7C,0x14,0x14,0x14,0x08,0x00}, /* 0x70 p      */
    {0x08,0x14,0x14,0x18,0x7C,0x00}, /* 0x71 q      */
    {0x7C,0x08,0x04,0x04,0x08,0x00}, /* 0x72 r      */
    {0x48,0x54,0x54,0x54,0x20,0x00}, /* 0x73 s      */
    {0x04,0x3F,0x44,0x40,0x20,0x00}, /* 0x74 t      */
    {0x3C,0x40,0x40,0x20,0x7C,0x00}, /* 0x75 u      */
    {0x1C,0x20,0x40,0x20,0x1C,0x00}, /* 0x76 v      */
    {0x3C,0x40,0x30,0x40,0x3C,0x00}, /* 0x77 w      */
    {0x44,0x28,0x10,0x28,0x44,0x00}, /* 0x78 x      */
    {0x0C,0x50,0x50,0x50,0x3C,0x00}, /* 0x79 y      */
    {0x44,0x64,0x54,0x4C,0x44,0x00}, /* 0x7A z      */
    {0x00,0x08,0x36,0x41,0x00,0x00}, /* 0x7B {      */
    {0x00,0x00,0x7F,0x00,0x00,0x00}, /* 0x7C |      */
    {0x00,0x41,0x36,0x08,0x00,0x00}, /* 0x7D }      */
    {0x08,0x04,0x08,0x10,0x08,0x00}, /* 0x7E ~      */
};

#define FONT_CHAR_WIDTH     6U
#define FONT_CHAR_HEIGHT    8U
#define FONT_FIRST_CHAR     0x20U
#define FONT_LAST_CHAR      0x7EU

void SPFD5408_DrawChar(uint16_t x, uint16_t y, char ch,
                       uint16_t fg, uint16_t bg, uint8_t scale)
{
    if (scale == 0U) scale = 1U;

    uint8_t c = (uint8_t)ch;
    if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR)
        c = '?';

    const uint8_t *glyph = font6x8[c - FONT_FIRST_CHAR];

    for (uint8_t col = 0; col < FONT_CHAR_WIDTH; col++)
    {
        uint8_t col_data = glyph[col];
        for (uint8_t row = 0; row < FONT_CHAR_HEIGHT; row++)
        {
            uint16_t color = (col_data & (1U << row)) ? fg : bg;
            if (scale == 1U)
            {
                SPFD5408_DrawPixel(x + col, y + row, color);
            }
            else
            {
                SPFD5408_FillRect(
                    x + (uint16_t)col * scale,
                    y + (uint16_t)row * scale,
                    scale, scale, color
                );
            }
        }
    }
}

void SPFD5408_DrawString(uint16_t x, uint16_t y, const char *str,
                         uint16_t fg, uint16_t bg, uint8_t scale)
{
    if (str == NULL) return;
    if (scale == 0U) scale = 1U;

    uint16_t cursor_x = x;
    while (*str)
    {
        SPFD5408_DrawChar(cursor_x, y, *str, fg, bg, scale);
        cursor_x += (uint16_t)(FONT_CHAR_WIDTH * scale);
        str++;
    }
}
