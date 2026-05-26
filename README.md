# Tamagochi ESP8266

Tamagotchi clásico años 90 corriendo en una placa **ideaspark ESP8266 0.96" OLED Module V3.2**. Firmware en C++/Arduino con PlatformIO. Stats, evoluciones por etapas, muerte permanente, persistencia entre apagados e interfaz web servida por el propio ESP8266.

---

## Hardware

| Componente | Detalle |
|---|---|
| MCU | ESP-12E (Xtensa LX106 80 MHz, 80 KB RAM, 4 MB Flash) |
| Display | SSD1306 128x64 I2C integrado (0.96") |
| Pines OLED | SW I2C: SDA=GPIO12, SCL=GPIO14 (no HW I2C — confirmado V3.2) |
| Botón | 1 botón onboard GPIO0 (FLASH) |
| Power | JST LiPo + TP4056 + AMS1117 3.3V / Micro USB (CH340G) |

---

## Setup PlatformIO

Requiere [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) o la extension de VS Code.

```bash
# Compilar
pio run

# Flashear
# Si la placa no entra en modo programación automáticamente:
# mantener pulsado el botón GPIO0 durante el power-on y luego soltar.
pio run -t upload

# Monitor serie (115200 baud)
pio device monitor
```

Para bajar la velocidad de carga si hay errores con CH340G:

```ini
; en platformio.ini
upload_speed = 460800
```

---

## Estado actual — Fase 0 (Bring-up)

Objetivo: validar empíricamente el hardware antes de invertir en Fase 1.

**Qué hace este firmware:**
- Muestra "Hola Tamagochi / Fase 0" + sprite dummy 32x32 animado en el OLED
- Loguea por monitor serie cada pulsación del botón GPIO0 (short / long / double click)
- Mide y reporta los FPS reales de render cada 5 segundos (objetivo: >= 5 FPS)

**Verificado en hardware (2026-05-26):**
1. OLED inicializa y renderiza correctamente con SW I2C en GPIO12/14
2. Botón GPIO0 responde a click corto y pulsación larga sin bloquear el boot
3. FPS report estable en 5.00 fps (budget cumplido)
4. Upload estable a 460800 baud vía hub USB (921600 falla con CH340G en hub)
