/**
 * @file    modbus_registers.c
 * @brief   Implementación de la tabla de Holding Registers Modbus.
 *
 * @details El arreglo @c hr_table[] vive en RAM y es el único punto de
 *          almacenamiento de los registros. Todas las operaciones de lectura
 *          y escritura pasan por un mutex FreeRTOS con timeout de
 *          @ref MB_MUTEX_TIMEOUT_MS milisegundos para evitar deadlocks.
 *
 *          Para compilar y testear en PC sin FreeRTOS:
 *          @code
 *            gcc -DMB_REG_TEST modbus_registers.c modbus_crc.c -o test_regs
 *          @endcode
 *          El stub de FreeRTOS al final del archivo reemplaza el mutex por
 *          un no-op cuando se compila con -DMB_REG_TEST.
 *
 * @author  Armando
 * @date    2025
 */

#include "modbus_registers.h"

/* ========================================================================== */
/*  Stub FreeRTOS para test en PC                                              */
/* ========================================================================== */

#ifdef MB_REG_TEST
/*
 * Cuando se compila fuera del target STM32 reemplazamos las llamadas FreeRTOS
 * por no-ops para poder testear la lógica pura de registros en PC.
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>

typedef int  SemaphoreHandle_t;
typedef int  BaseType_t;
typedef unsigned int TickType_t;

#define pdTRUE              1
#define pdFALSE             0
#define portMAX_DELAY       0xFFFFFFFFU
#define pdMS_TO_TICKS(ms)   (ms)

static SemaphoreHandle_t xSemaphoreCreateMutex(void)     { return 1; }
static BaseType_t xSemaphoreTake(SemaphoreHandle_t h,
                                  TickType_t t)           { (void)h;(void)t; return pdTRUE; }
static void       xSemaphoreGive(SemaphoreHandle_t h)    { (void)h; }

#else
/*
 * Build real: incluir FreeRTOS normalmente.
 */
#include "FreeRTOS.h"
#include "semphr.h"
#endif /* MB_REG_TEST */

/* ========================================================================== */
/*  Constantes internas                                                         */
/* ========================================================================== */

/** @brief Timeout máximo esperando el mutex (ms). Ajustar según necesidad. */
#define MB_MUTEX_TIMEOUT_MS   10U

/** @brief Versión de firmware codificada en BCD: 0x0100 = v1.00 */
#define MB_FIRMWARE_VERSION   0x0100U

/* ========================================================================== */
/*  Estado interno (privado)                                                    */
/* ========================================================================== */

/** Tabla de registros en RAM. */
static uint16_t         hr_table[MODBUS_HR_COUNT];

/** Mutex FreeRTOS que protege hr_table[]. */
static SemaphoreHandle_t hr_mutex = NULL;

/* ========================================================================== */
/*  Helpers privados                                                            */
/* ========================================================================== */

/**
 * @brief  Intenta tomar el mutex con timeout.
 * @retval MB_REG_OK       Mutex adquirido.
 * @retval MB_REG_ERR_MUTEX  Timeout o mutex no inicializado.
 */
static inline int take_mutex(void)
{
    if (hr_mutex == NULL)
        return MB_REG_ERR_MUTEX;

    if (xSemaphoreTake(hr_mutex, pdMS_TO_TICKS(MB_MUTEX_TIMEOUT_MS)) != pdTRUE)
        return MB_REG_ERR_MUTEX;

    return MB_REG_OK;
}

/**
 * @brief  Libera el mutex.
 */
static inline void give_mutex(void)
{
    xSemaphoreGive(hr_mutex);
}

/* ========================================================================== */
/*  Implementación pública                                                      */
/* ========================================================================== */

int modbus_registers_init(void)
{
    /* Limpiar tabla */
    for (uint16_t i = 0; i < MODBUS_HR_COUNT; i++)
        hr_table[i] = 0U;

    /* Precargar valores conocidos */
    hr_table[MB_HR_FIRMWARE_VER]  = MB_FIRMWARE_VERSION;
    hr_table[MB_HR_DEVICE_STATUS] = 0x0001U;  /* 1 = online */

    /* Crear mutex */
    hr_mutex = xSemaphoreCreateMutex();
    if (hr_mutex == NULL)
        return MB_REG_ERR_MUTEX;

    return MB_REG_OK;
}

int modbus_hr_read(uint16_t address, uint16_t *value)
{
    if (value == NULL)
        return MB_REG_ERR_NULL;

    if (address >= MODBUS_HR_COUNT)
        return MB_REG_ERR_RANGE;

    int ret = take_mutex();
    if (ret != MB_REG_OK)
        return ret;

    *value = hr_table[address];

    give_mutex();
    return MB_REG_OK;
}

int modbus_hr_read_block(uint16_t start_addr, uint16_t count, uint16_t *out)
{
    if (out == NULL)
        return MB_REG_ERR_NULL;

    if (count == 0U)
        return MB_REG_ERR_COUNT;

    if ((uint32_t)start_addr + (uint32_t)count > (uint32_t)MODBUS_HR_COUNT)
        return MB_REG_ERR_RANGE;

    int ret = take_mutex();
    if (ret != MB_REG_OK)
        return ret;

    for (uint16_t i = 0; i < count; i++)
        out[i] = hr_table[start_addr + i];

    give_mutex();
    return MB_REG_OK;
}

int modbus_hr_write(uint16_t address, uint16_t value)
{
    if (address >= MODBUS_HR_COUNT)
        return MB_REG_ERR_RANGE;

    int ret = take_mutex();
    if (ret != MB_REG_OK)
        return ret;

    hr_table[address] = value;

    give_mutex();
    return MB_REG_OK;
}

int modbus_hr_write_block(uint16_t start_addr, uint16_t count, const uint16_t *values)
{
    if (values == NULL)
        return MB_REG_ERR_NULL;

    if (count == 0U)
        return MB_REG_ERR_COUNT;

    if ((uint32_t)start_addr + (uint32_t)count > (uint32_t)MODBUS_HR_COUNT)
        return MB_REG_ERR_RANGE;

    int ret = take_mutex();
    if (ret != MB_REG_OK)
        return ret;

    for (uint16_t i = 0; i < count; i++)
        hr_table[start_addr + i] = values[i];

    give_mutex();
    return MB_REG_OK;
}

/* ========================================================================== */
/*  Test standalone  (gcc -DMB_REG_TEST modbus_registers.c -o test_regs)      */
/* ========================================================================== */

#ifdef MB_REG_TEST

#define PASS(msg) do { printf("[PASS] %s\n", msg); pass++; } while(0)
#define FAIL(msg) do { printf("[FAIL] %s\n", msg); fail++; } while(0)
#define CHECK(cond, msg) do { if(cond) PASS(msg); else FAIL(msg); } while(0)

int main(void)
{
    int pass = 0, fail = 0;
    uint16_t val   = 0;
    uint16_t block[8] = {0};

    printf("=== modbus_registers — Test suite ===\n\n");

    /* ------------------------------------------------------------------ */
    /* Init                                                                 */
    /* ------------------------------------------------------------------ */
    CHECK(modbus_registers_init() == MB_REG_OK,
          "modbus_registers_init() retorna OK");

    /* Verificar valores precargados */
    modbus_hr_read(MB_HR_FIRMWARE_VER, &val);
    CHECK(val == 0x0100U,
          "MB_HR_FIRMWARE_VER precargado = 0x0100");

    modbus_hr_read(MB_HR_DEVICE_STATUS, &val);
    CHECK(val == 0x0001U,
          "MB_HR_DEVICE_STATUS precargado = 0x0001 (online)");

    /* ------------------------------------------------------------------ */
    /* Lectura/escritura simple                                             */
    /* ------------------------------------------------------------------ */
    CHECK(modbus_hr_write(MB_HR_SETPOINT_0, 250U) == MB_REG_OK,
          "modbus_hr_write SETPOINT_0 = 250 OK");

    modbus_hr_read(MB_HR_SETPOINT_0, &val);
    CHECK(val == 250U,
          "modbus_hr_read SETPOINT_0 devuelve 250");

    /* Sobrescribir y verificar */
    modbus_hr_write(MB_HR_SETPOINT_0, 999U);
    modbus_hr_read(MB_HR_SETPOINT_0, &val);
    CHECK(val == 999U,
          "modbus_hr_write sobrescribe correctamente");

    /* ------------------------------------------------------------------ */
    /* Lectura/escritura en bloque                                          */
    /* ------------------------------------------------------------------ */
    uint16_t write_vals[4] = { 0x1111U, 0x2222U, 0x3333U, 0x4444U };
    CHECK(modbus_hr_write_block(MB_HR_USER_0, 4U, write_vals) == MB_REG_OK,
          "modbus_hr_write_block 4 registros OK");

    CHECK(modbus_hr_read_block(MB_HR_USER_0, 4U, block) == MB_REG_OK,
          "modbus_hr_read_block 4 registros OK");

    CHECK(block[0] == 0x1111U && block[1] == 0x2222U &&
          block[2] == 0x3333U && block[3] == 0x4444U,
          "Valores de bloque leídos correctamente");

    /* ------------------------------------------------------------------ */
    /* Casos borde — dirección fuera de rango                              */
    /* ------------------------------------------------------------------ */
    CHECK(modbus_hr_read(MODBUS_HR_COUNT, &val) == MB_REG_ERR_RANGE,
          "read con addr == COUNT retorna ERR_RANGE");

    CHECK(modbus_hr_write(MODBUS_HR_COUNT, 0) == MB_REG_ERR_RANGE,
          "write con addr == COUNT retorna ERR_RANGE");

    /* ------------------------------------------------------------------ */
    /* Casos borde — bloque que se sale del mapa                           */
    /* ------------------------------------------------------------------ */
    CHECK(modbus_hr_read_block(14U, 3U, block) == MB_REG_ERR_RANGE,
          "read_block que excede el mapa retorna ERR_RANGE");

    CHECK(modbus_hr_write_block(14U, 3U, write_vals) == MB_REG_ERR_RANGE,
          "write_block que excede el mapa retorna ERR_RANGE");

    /* ------------------------------------------------------------------ */
    /* Casos borde — punteros nulos                                        */
    /* ------------------------------------------------------------------ */
    CHECK(modbus_hr_read(0U, NULL) == MB_REG_ERR_NULL,
          "read con NULL retorna ERR_NULL");

    CHECK(modbus_hr_read_block(0U, 4U, NULL) == MB_REG_ERR_NULL,
          "read_block con NULL retorna ERR_NULL");

    CHECK(modbus_hr_write_block(0U, 4U, NULL) == MB_REG_ERR_NULL,
          "write_block con NULL retorna ERR_NULL");

    /* ------------------------------------------------------------------ */
    /* Casos borde — count = 0                                             */
    /* ------------------------------------------------------------------ */
    CHECK(modbus_hr_read_block(0U, 0U, block) == MB_REG_ERR_COUNT,
          "read_block con count=0 retorna ERR_COUNT");

    CHECK(modbus_hr_write_block(0U, 0U, write_vals) == MB_REG_ERR_COUNT,
          "write_block con count=0 retorna ERR_COUNT");

    /* ------------------------------------------------------------------ */
    /* Último registro válido                                               */
    /* ------------------------------------------------------------------ */
    CHECK(modbus_hr_write(MODBUS_HR_COUNT - 1U, 0xBEEFU) == MB_REG_OK,
          "write en último registro válido OK");

    modbus_hr_read(MODBUS_HR_COUNT - 1U, &val);
    CHECK(val == 0xBEEFU,
          "read del último registro devuelve 0xBEEF");

    /* ------------------------------------------------------------------ */
    /* Resultado                                                            */
    /* ------------------------------------------------------------------ */
    printf("\n=== Resultado: %d/%d pasaron ===\n", pass, pass + fail);
    return (fail == 0) ? 0 : 1;
}

#endif /* MB_REG_TEST */
