/**
  ******************************************************************************
  * @file    w5500_port.c
  * @brief   Port layer del W5500: SPI + CS + reset para STM32F103 HAL.
  *
  *          Este archivo conecta el ioLibrary_Driver de WIZnet con el HAL
  *          de ST. No contiene lógica Modbus — solo transporta bytes.
  *
  * Dependencias externas:
  *   - ioLibrary_Driver/Ethernet/wizchip_conf.h  (WIZnet)
  *   - hspi1 inicializado por CubeMX antes de llamar w5500_port_init()
  ******************************************************************************
  */

#include "w5500_port.h"
#include "wizchip_conf.h"   /* ioLibrary: reg_wizchip_spi_cbfunc, etc. */

/* ---------------------------------------------------------------------------
 * CS y RST inline
 * --------------------------------------------------------------------------- */

void w5500_cs_select(void)
{
    HAL_GPIO_WritePin(W5500_CS_PORT, W5500_CS_PIN, GPIO_PIN_RESET);
}

void w5500_cs_deselect(void)
{
    HAL_GPIO_WritePin(W5500_CS_PORT, W5500_CS_PIN, GPIO_PIN_SET);
}

void w5500_reset(void)
{
    HAL_GPIO_WritePin(W5500_RST_PORT, W5500_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(10);   /* mínimo 500µs según datasheet W5500, 10ms por seguridad */
    HAL_GPIO_WritePin(W5500_RST_PORT, W5500_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(150);  /* tiempo de levantada del W5500 antes de aceptar SPI */
}

/* ---------------------------------------------------------------------------
 * SPI byte-level
 * --------------------------------------------------------------------------- */

uint8_t w5500_spi_read(void)
{
    uint8_t rx = 0;
    HAL_SPI_Receive(&hspi1, &rx, 1, HAL_MAX_DELAY);
    return rx;
}

void w5500_spi_write(uint8_t byte)
{
    HAL_SPI_Transmit(&hspi1, &byte, 1, HAL_MAX_DELAY);
}

/* ---------------------------------------------------------------------------
 * w5500_port_init()
 * --------------------------------------------------------------------------- */

/**
  * @brief  Registra los callbacks SPI/CS en ioLibrary y hace reset del chip.
  *
  *         Secuencia:
  *           1. CS desactivado (precaución antes de cualquier cosa)
  *           2. Reset hardware del W5500
  *           3. Registro de callbacks en ioLibrary
  *           4. wizchip_init() con tamaño de buffers por socket
  *           5. Configuración de red (IP, mask, GW, MAC)
  *
  *         Los pasos 4 y 5 están acá como referencia — se pueden mover
  *         a task_modbus si preferís tener todo junto.
  */
void w5500_port_init(void)
{
    uint8_t memsize[2][8] = {
        { 2, 2, 2, 2, 2, 2, 2, 2 },   /* TX: 2KB por socket (8 sockets = 16KB total) */
        { 2, 2, 2, 2, 2, 2, 2, 2 }    /* RX: ídem */
    };

    /* 1. CS inactivo antes de tocar nada */
    w5500_cs_deselect();

    /* 2. Reset hardware */
    w5500_reset();

    /* 3. Registrar callbacks SPI en ioLibrary */
    reg_wizchip_cs_cbfunc(w5500_cs_select, w5500_cs_deselect);
    reg_wizchip_spi_cbfunc(w5500_spi_read, w5500_spi_write);

    /* 4. Inicializar chip con distribución de buffers */
    if (ctlwizchip(CW_INIT_WIZCHIP, (void *)memsize) != 0)
    {
        /* Si falla acá: revisar SPI (frecuencia, modo CPOL/CPHA), cables, alimentación */
        Error_Handler();
    }

    /* 5. Configuración de red estática
     *    Ajustar según tu LAN. MAC debe ser único en la red. */
    wiz_NetInfo netinfo = {
        .mac  = { 0x00, 0x08, 0xDC, 0xAB, 0xCD, 0xEF },  /* WIZnet OUI + bytes arbitrarios */
        .ip   = { 192, 168, 1, 100 },
        .sn   = { 255, 255, 255, 0 },
        .gw   = { 192, 168, 1, 1 },
        .dns  = { 8, 8, 8, 8 },
        .dhcp = NETINFO_STATIC
    };
    ctlnetwork(CN_SET_NETINFO, (void *)&netinfo);
}
