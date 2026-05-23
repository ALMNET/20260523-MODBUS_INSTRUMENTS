#ifndef W5500_PORT_H
#define W5500_PORT_H

/**
  ******************************************************************************
  * @file    w5500_port.h
  * @brief   Port layer del W5500 para STM32F103 + HAL.
  *
  *          Pinout SPI1 (Blue Pill):
  *            SCK   → PA5
  *            MISO  → PA6
  *            MOSI  → PA7
  *            CS    → PA4  (GPIO output, control manual)
  *            RST   → PB0  (GPIO output, active low)
  *            INT   → PB1  (GPIO input — no usado por ahora)
  *
  *          ioLibrary_Driver espera que el usuario provea:
  *            - cs_select() / cs_deselect()
  *            - w5500_reset()
  *            - spi_read_byte() / spi_write_byte()
  *          Esas funciones viven en w5500_port.c.
  ******************************************************************************
  */

#include <stdint.h>
#include "main.h"

/* ---------------------------------------------------------------------------
 * Pines — ajustar si cambiás el pinout en CubeMX
 * --------------------------------------------------------------------------- */
#define W5500_CS_PORT     GPIOA
#define W5500_CS_PIN      GPIO_PIN_4

#define W5500_RST_PORT    GPIOB
#define W5500_RST_PIN     GPIO_PIN_0

/* ---------------------------------------------------------------------------
 * Handle SPI — definido en main.c por CubeMX
 * --------------------------------------------------------------------------- */
extern SPI_HandleTypeDef hspi1;

/* ---------------------------------------------------------------------------
 * API del port layer
 * --------------------------------------------------------------------------- */

/** @brief Inicializa SPI1 y hace reset del W5500. Llamar antes de wizchip_init(). */
void w5500_port_init(void);

/** @brief Pulsa RST low 10ms y espera 150ms para que el W5500 levante. */
void w5500_reset(void);

/** Funciones de chip select — registradas en wizchip_setnetinfo() */
void w5500_cs_select(void);
void w5500_cs_deselect(void);

/** Funciones de SPI byte — registradas en reg_wizchip_spi_cbfunc() */
uint8_t w5500_spi_read(void);
void    w5500_spi_write(uint8_t byte);

#endif /* W5500_PORT_H */
