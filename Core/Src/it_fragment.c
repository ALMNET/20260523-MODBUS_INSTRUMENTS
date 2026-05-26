/*
 * =============================================================================
 * INSTRUCCIONES: agregar en stm32f1xx_it.c
 * =============================================================================
 *
 * 1) Al inicio del archivo, dentro del bloque USER CODE BEGIN Includes:
 *
 *    #include "modbus_uart.h"
 *
 * 2) Localizar la función USART1_IRQHandler() y agregar la llamada al callback
 *    ANTES de HAL_UART_IRQHandler(), dentro del bloque USER CODE BEGIN:
 *
 * =============================================================================
 */

/* Ejemplo de cómo debe quedar USART1_IRQHandler en stm32f1xx_it.c: */

void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */

  /*
   * Llamar primero al callback IDLE antes que HAL_UART_IRQHandler(),
   * porque HAL limpia algunos flags en su propio handler y nos quedaríamos
   * sin poder detectar el IDLE correctamente si lo llamamos después.
   */
  modbus_uart_idle_callback();

  /* USER CODE END USART1_IRQn 0 */

  HAL_UART_IRQHandler(&huart1);

  /* USER CODE BEGIN USART1_IRQn 1 */
  /* USER CODE END USART1_IRQn 1 */
}

/*
 * =============================================================================
 * NOTA IMPORTANTE sobre el orden de las llamadas:
 * =============================================================================
 *
 * modbus_uart_idle_callback() debe ir ANTES de HAL_UART_IRQHandler() porque:
 *
 *   - HAL_UART_IRQHandler() limpia el flag IDLE internamente en algunos casos
 *   - Nuestro callback chequea ese flag con __HAL_UART_GET_FLAG(UART_FLAG_IDLE)
 *   - Si HAL lo limpia primero, el callback no detecta el evento
 *
 * =============================================================================
 */
