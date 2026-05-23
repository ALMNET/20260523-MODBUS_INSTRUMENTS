# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Compiler options
target_compile_options(${BUILD_UNIT_0_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:C>: ${CUBE_CMAKE_C_FLAGS}>
    $<$<COMPILE_LANGUAGE:CXX>: ${CUBE_CMAKE_CXX_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>: ${CUBE_CMAKE_ASM_FLAGS}>
)

# Linker options
target_link_options(${BUILD_UNIT_0_NAME} PRIVATE ${CUBE_CMAKE_EXE_LINKER_FLAGS})

# Add sources to executable/library
target_sources(${BUILD_UNIT_0_NAME} PRIVATE
    "Core/Src/fonts.c"
    "Core/Src/freertos.c"
    "Core/Src/main.c"
    "Core/Src/ssd1306.c"
    "Core/Src/stm32f1xx_hal_msp.c"
    "Core/Src/stm32f1xx_hal_timebase_tim.c"
    "Core/Src/stm32f1xx_it.c"
    "Core/Src/syscalls.c"
    "Core/Src/sysmem.c"
    "Core/Src/system_stm32f1xx.c"
    "Core/Src/test.c"
    "Core/Startup/startup_stm32f103c8tx.s"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_exti.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_i2c.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim_ex.c"
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_uart.c"
    "Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS/cmsis_os.c"
    "Middlewares/Third_Party/FreeRTOS/Source/croutine.c"
    "Middlewares/Third_Party/FreeRTOS/Source/event_groups.c"
    "Middlewares/Third_Party/FreeRTOS/Source/list.c"
    "Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3/port.c"
    "Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c"
    "Middlewares/Third_Party/FreeRTOS/Source/queue.c"
    "Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c"
    "Middlewares/Third_Party/FreeRTOS/Source/tasks.c"
    "Middlewares/Third_Party/FreeRTOS/Source/timers.c"
)

target_include_directories(${BUILD_UNIT_0_NAME} PRIVATE
    "Core/Inc"
    "Drivers/STM32F1xx_HAL_Driver/Inc/Legacy"
    "Drivers/STM32F1xx_HAL_Driver/Inc"
    "Drivers/CMSIS/Device/ST/STM32F1xx/Include"
    "Drivers/CMSIS/Include"
    "Middlewares/Third_Party/FreeRTOS/Source/include"
    "Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS"
    "Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3"
)

configure_file("${CMAKE_SOURCE_DIR}/STM32F103C8TX_FLASH.ld" "${CMAKE_BINARY_DIR}" COPYONLY)

set_target_properties(${BUILD_UNIT_0_NAME} PROPERTIES LINK_DEPENDS "STM32F103C8TX_FLASH.ld")

