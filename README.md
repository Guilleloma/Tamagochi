# Tamagochi ESP8266

Tamagotchi clásico años 90 corriendo en una placa **ideaspark ESP8266 0.96" OLED Module V3.2**. Firmware en C++/Arduino con PlatformIO. Stats, evoluciones por etapas, muerte permanente, persistencia entre apagados e interfaz web servida por el propio ESP8266.

---

## Hardware

| Componente | Detalle |
|---|---|
| MCU | ESP-12E (Xtensa LX106 80 MHz, 80 KB RAM, 4 MB Flash) |
| Display | SSD1306 128x64 I2C integrado (0.96") |
| Pines I2C | SDA=GPIO4, SCL=GPIO5 (verificar en Fase 0 — ver `include/config.h`) |
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

**Qué verificar en esta fase:**
1. OLED enciende y muestra el texto + sprite correctamente con `OLED_SDA=4, OLED_SCL=5`
2. El botón GPIO0 responde sin bloquear el boot
3. El FPS report muestra >= 5.0 fps por monitor serie

**Si el OLED no responde:** cambiar `OLED_SDA` y `OLED_SCL` en `include/config.h` a `12` y `14` respectivamente y reflashear.
