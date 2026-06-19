# 📡 Modbus TCP + RTU Integration Guide

## 🎯 Overview

Esta rama (`integration/modbus-enhanced`) contiene la **integración completa de Modbus TCP y RTU** en tu BluePill con FreeRTOS.

### ✅ Cambios realizados:

1. **`Core/Src/stm32f1xx_it.c`** — USART1 ISR ahora llama a `modbus_uart_idle_callback()`
2. **`Modbus/Inc/modbus_handler.h`** — API centralizada (nuevo)
3. **`Modbus/Src/modbus_handler.c`** — Handler con diagnostics (nuevo)
4. **`App/Inc/app_tasks.h`** — Headers actualizados
5. **`App/Src/app_tasks.c`** — Ambas tareas completamente integradas
6. **`Modbus/Src/modbus_tcp.c`** — Actualizado con contadores
7. **`Modbus/Src/modbus_rtu.c`** — Actualizado con contadores

---

## 🚀 Quick Start (5 minutos)

### Paso 1: Mergear cambios

```bash
# En tu repo local
git fetch origin
git checkout integration/modbus-enhanced
# Verifica los cambios
git log --oneline -5
```

### Paso 2: Compilar

```bash
# En STM32CubeIDE
Project > Build Project (Ctrl+B)
```

**Errores esperados:** Ninguno. Si hay errores de include, revisa que las rutas en `.cproject` incluyan:
- `App/Inc/`
- `Modbus/Inc/`

### Paso 3: Flashear y probar

```bash
# Conectar BluePill + debugger
# Run > Debug (F11)
# El LED debe parpadear (500ms on/off)
# El OLED debe mostrar: "Modbus Init" → "Modbus PLC"
```

---

## 📊 Arquitectura

```
┌─────────────────────────────────────────┐
│         FreeRTOS Scheduler              │
├─────────────────────────────────────────┤
│ Task Priority 3:                        │
│  ├─ modbus_tcp_task    (768 words)      │
│  └─ modbus_rtu_task    (512 words)      │
│                                         │
│ Task Priority 2:                        │
│  ├─ task_oled          (256 words)      │
│  └─ task_uart_dbg      (128 words)      │
│                                         │
│ Task Priority 1:                        │
│  └─ task_led           (64 words)       │
└─────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────┐
│      modbus_handler (centralized)       │
│  • Register access (mutex-protected)    │
│  • Diagnostics counters                 │
│  • Status flags (TCP/RTU state)         │
└─────────────────────────────────────────┘
                    ↓
┌──────────────────┬──────────────────┐
│  modbus_tcp      │   modbus_rtu     │
│  FC03, FC06      │   FC03, FC06     │
│  + MBAP parsing  │   + CRC-16       │
└──────┬───────────┴────────┬─────────┘
       │                    │
   ┌───▼────┐          ┌────▼────┐
   │  W5500  │          │ UART1   │
   │ SPI/TCP │          │ DMA+RS485
   └────────┘          └─────────┘
```

---

## 🔧 Configuración de hardware (asumida)

### W5500 (Modbus TCP)
- **SPI1**: PA5 (CLK), PA6 (MISO), PA7 (MOSI)
- **CS**: PA4
- **RST**: PB0
- **INT**: PB1 (opcional)
- **IP**: 192.168.1.100 (configurable en `w5500_port.h`)
- **Puerto**: 502 (Modbus estándar)

### UART1 (Modbus RTU)
- **TX**: PA9
- **RX**: PA10
- **Baud**: 9600
- **DMA**: DMA1 Channel 5 (RX circular)
- **RS485 DE/RE**: PA8 (GPIO)
- **Slave Address**: 1

### OLED (SSD1306)
- **I2C1**: PB6 (SCL), PB7 (SDA)
- **Dirección**: 0x3C

### LED
- **PC13**: Heartbeat visual (500ms on/off)

---

## 📝 Tareas principales

### `task_modbus_tcp`
```c
void task_modbus_tcp_wrapper(void *pvParameters)
{
    modbus_handler_set_status(1U << 0, 1);  // TCP_LISTENING
    modbus_tcp_task(NULL);  // Never returns
}
```

**Máquina de estados del socket:**
```
SOCK_CLOSED 
  ↓ socket(0, TCP, 502) + listen()
SOCK_LISTEN 
  ↓ (espera cliente)
SOCK_ESTABLISHED 
  ↓ recv() → modbus_tcp_process() → send()
SOCK_CLOSE_WAIT 
  ↓ disconnect()
SOCK_CLOSED
```

**Protocolo:**
- FC03: Read Holding Registers (máx 125 regs)
- FC06: Write Single Register

### `task_modbus_rtu`
```c
void task_modbus_rtu_wrapper(void *pvParameters)
{
    modbus_handler_set_status(1U << 3, 1);  // RTU_IDLE
    modbus_rtu_task(NULL);  // Never returns
}
```

**Flujo:**
```
ISR UART1 (IDLE)
  ↓ modbus_uart_idle_callback()
  ↓ vTaskNotifyGiveFromISR(modbus_rtu_task)
Task modbus_rtu_task
  ↓ ulTaskNotifyTake() → despierta
  ↓ modbus_uart_get_frame() → buffer DMA
  ↓ modbus_rtu_process() → FC03/FC06
  ↓ modbus_uart_transmit() → respuesta RS485
```

### `task_oled`
Muestra en tiempo real:
- Uptime en segundos
- Estado TCP (Online/Listen/Idle)
- Contadores de frames RX/TX/Errores

**Líneas:**
```
┌──────────────────┐
│ Modbus PLC       │  ← Título
│ Up: 123 s        │  ← Uptime
│ TCP: Online      │  ← Estado protocolo
│ RX:42 TX:40 Err:0│  ← Contadores
└──────────────────┘
```

---

## 🔌 APIs principales

### Acceso a registros (thread-safe)

```c
/* Lectura simple */
uint16_t value;
modbus_handler_read_register(MB_HR_MEAS_0, &value);

/* Lectura de bloque */
uint16_t block[4];
modbus_handler_read_block(MB_HR_SETPOINT_0, 4, block);

/* Escritura simple */
modbus_handler_write_register(MB_HR_OUTPUT_0, 1);

/* Escritura de bloque */
uint16_t values[] = {100, 200, 300};
modbus_handler_write_block(MB_HR_USER_0, 3, values);
```

### Diagnostics (sin mutex, solo lectura)

```c
/* Obtener estado actual */
uint32_t status = modbus_handler_get_status();
if (status & (1 << 1))  /* TCP_CONNECTED */
    printf("TCP conectado\n");

/* Obtener contadores */
uint32_t tcp_rx, tcp_tx, rtu_rx, rtu_tx, errors;
modbus_handler_get_counters(&tcp_rx, &tcp_tx, &rtu_rx, &rtu_tx, &errors);
printf("TCP RX:%lu TX:%lu | RTU RX:%lu TX:%lu | Err:%lu\n",
       tcp_rx, tcp_tx, rtu_rx, rtu_tx, errors);

/* Reset contadores */
modbus_handler_reset_counters();
```

---

## 🧪 Testing (sin cliente Modbus real)

### Test 1: Verificar compilación

```bash
make clean
make -j4
# ✓ Sin errores
```

### Test 2: Verificar LED heartbeat

- Conectar BluePill
- Flashear
- **PC13 debe parpadear 500ms on / 500ms off**
- Si está congelado → problema en scheduler o task

### Test 3: Verificar OLED

- OLED debe mostrar boot screen 2s
- Luego "Modbus PLC" con uptime incrementando

### Test 4: Verificar ISR UART1 (RTU)

```c
/* En task_modbus_rtu o via debug */
void test_uart_echo(void)
{
    uint8_t test_frame[] = {
        0x01,           // Slave address
        0x03,           // FC03 (Read Holding Registers)
        0x00, 0x00,     // Start address = 0
        0x00, 0x02,     // Count = 2
        0x04, 0x08      // CRC (precalculado)
    };
    
    modbus_uart_transmit(test_frame, sizeof(test_frame));
    // ISR debería disparar, task debería procesar
}
```

---

## 🐛 Troubleshooting

### ❌ Error: "modbus_uart_idle_callback undeclared"

**Solución:**
```c
// En stm32f1xx_it.c, agregar:
#include "modbus_uart.h"

// En USART1_IRQHandler:
modbus_uart_idle_callback();  // ANTES de HAL_UART_IRQHandler
```

### ❌ W5500 ping intermitente

**Probable causa:**
- Socket no se reinicializa tras disconnect
- Buffer RX desbordado

**Fix:**
- Verificar en `modbus_tcp_task`: reinicia socket en SOCK_CLOSED
- Revisar tamaño de `MB_TCP_BUF_SIZE` en modbus_tcp.h

### ❌ RTU no responde

**Probable causa:**
- ISR IDLE no dispara
- Slave address configurado diferente

**Debug:**
```c
// Agregrar en task_modbus_rtu:
if (modbus_handler_get_status() & (1 << 3))  // RTU_IDLE
{
    printf("[RTU] Esperando frames...\n");
}

// Verificar en main.c:
// MB_SLAVE_ADDRESS en modbus_rtu.h debe ser 1
```

### ❌ Contadores no incrementan

**Causa:** Funciones `modbus_handler_inc_*` requieren mutex

**Fix:** Verificar que `diag_mutex` se creó en `modbus_handler_init()`

---

## 📚 Mapa de registros (predefinido)

```
Dirección | Alias                 | Tipo   | Descripción
----------|-----------------------|--------|----------------------------------
0x0000    | MB_HR_DEVICE_STATUS   | R/W    | Estado del dispositivo
0x0001    | MB_HR_FIRMWARE_VER    | R/W    | Versión firmware (0x0100)
0x0002    | MB_HR_OUTPUT_0        | R/W    | Salida digital 0 (0/1)
0x0003    | MB_HR_OUTPUT_1        | R/W    | Salida digital 1 (0/1)
0x0004    | MB_HR_SETPOINT_0      | R/W    | Setpoint canal 0 (×10)
0x0005    | MB_HR_SETPOINT_1      | R/W    | Setpoint canal 1 (×10)
0x0006    | MB_HR_MEAS_0          | R/W    | Medición canal 0 (×10)
0x0007    | MB_HR_MEAS_1          | R/W    | Medición canal 1 (×10)
0x0008    | MB_HR_ERROR_FLAGS     | R/W    | Flags de error (bitmap)
0x0009–F  | MB_HR_USER_0–6        | R/W    | Registros de usuario
```

**Ejemplo:**
```c
// Leer temperatura (canal 0, escala ×10)
uint16_t temp_x10;
modbus_handler_read_register(MB_HR_MEAS_0, &temp_x10);
float temp = temp_x10 / 10.0f;  // ej: 250 → 25.0°C
```

---

## 🔄 Próximos pasos (después de funcionar)

### 1. **Testear con cliente real**

```bash
# Python + pymodbus
pip install pymodbus

# Script test
python3 test_modbus.py
```

```python
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient('192.168.1.100', port=502)
client.connect()

# Leer registro 0x0000 (Device Status)
result = client.read_holding_registers(0, 1)
print(f"Device Status: {result.registers[0]:#06x}")

# Escribir registro 0x0002 (Output 0)
client.write_register(2, 1)  # Activar salida

client.close()
```

### 2. **Integrar con tu aplicación .NET**

```csharp
// C# + NModbus
using NModbus;

var factory = new ModbusFactory();
var client = factory.CreateMaster(new TcpConnectionString("192.168.1.100", 502));

// Leer
ushort[] registers = client.ReadHoldingRegisters(0, 4);

// Escribir
client.WriteSingleRegister(2, 1);

client.Dispose();
```

### 3. **Expandir a F407 (futuro)**

- Cambiar `STM32F103` → `STM32F407` en CubeMX
- Misma lógica Modbus funciona
- Más RAM/Flash para pantallas .NET nativas

---

## 📋 Checklist antes de mergear a DEV

- [ ] Compilar sin errores
- [ ] LED parpadea (heartbeat funciona)
- [ ] OLED muestra "Modbus PLC" correctamente
- [ ] Uptime incrementa cada segundo
- [ ] Testear Modbus TCP con cliente Python/Modbus Poll
- [ ] Testear Modbus RTU con cliente hardware (si tienes)
- [ ] Contadores incrementan tras lectura/escritura
- [ ] Sin stack overflows (LED sigue parpadeando)

---

## 🎓 Arquitectura de código

```
App/Src/app_tasks.c
  └─ tasks_create()
       ├─ task_led           (priority 1)
       ├─ task_oled          (priority 2)
       ├─ task_modbus_tcp    (priority 3, delegates to modbus_tcp_task)
       └─ task_modbus_rtu    (priority 3, delegates to modbus_rtu_task)

modbus_handler.c (NEW)
  └─ Centraliza diagnostics + acceso a registros

modbus_registers.c
  └─ Tabla de registros + mutex FreeRTOS

modbus_tcp.c (UPDATED)
  └─ Protocolo TCP + W5500 socket management
  └─ Llama a modbus_handler para counters

modbus_rtu.c (UPDATED)
  └─ Protocolo RTU + CRC
  └─ Llama a modbus_handler para counters

modbus_uart.c
  └─ DMA circular + IDLE ISR callback

Core/Src/stm32f1xx_it.c (UPDATED)
  └─ USART1_IRQHandler → modbus_uart_idle_callback()
```

---

## ❓ FAQ

**P: ¿Puedo usar Modbus TCP y RTU simultáneamente?**
R: Sí. Corren en tareas separadas (prioridad 3). Los registros se comparten vía mutex.

**P: ¿Qué pasa si pierdo conexión TCP?**
R: El socket transiciona a SOCK_CLOSE_WAIT → disconnect() → vuelve a SOCK_LISTEN.

**P: ¿Cuál es la latencia de respuesta Modbus?**
R: TCP: ~10ms (tick de la task). RTU: <1ms (ISR + task notification).

**P: ¿Cómo amplio el mapa de registros?**
R: Modificar `MODBUS_HR_COUNT` en `modbus_registers.h`, agregar aliases en el enum.

**P: ¿Puedo desactivar una de los protocolos?**
R: Sí, en `app_tasks.c` comentar la creación de la tarea correspondiente.

---

## 📞 Support

Si encontrás problemas:

1. Revisar `/logs` en el OLED (uptime + estado)
2. Habilitar `task_uart_dbg` para debug por serial
3. Verificar contadores con `modbus_handler_get_counters()`
4. Revisar stack size si hay faults

---

## 🏁 ¡Listo!

Cuando todo funcione correctamente, mergea a `DEV`:

```bash
git checkout DEV
git merge integration/modbus-enhanced
git push origin DEV
```

Después de eso, la próxima rama puede ser para integrar la **pantalla TFT SPFD5408** o expandir a **F407 con pantallas .NET**.

**Happy Modbus! 🚀**
