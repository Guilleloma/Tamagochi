# Tamagochi ESP8266

Tamagotchi clásico años 90 corriendo en una placa **ideaspark ESP8266 0.96" OLED Module V3.2**. Firmware en C++/Arduino con PlatformIO. Stats con decay en tiempo real, persistencia entre apagados y arquitectura modular pensada para crecer hacia evoluciones por etapas, muerte permanente e interfaz web.

---

## Hardware

| Componente | Detalle |
|---|---|
| MCU | ESP-12E (Xtensa LX106 80 MHz, 80 KB RAM, 4 MB Flash) |
| Display | SSD1306 128×64 OLED integrado |
| Pines OLED | **SW I2C**: SDA=GPIO12, SCL=GPIO14 (no HW I2C — confirmado V3.2) |
| Orientación render | `U8G2_R1` → 64 wide × 128 tall (portrait, USB-C arriba) |
| Botón | 1 botón onboard GPIO0 (FLASH) |
| Power | JST LiPo + TP4056 + AMS1117 3.3V / USB-C (CH340G) |

---

## Cómo se juega

1. **Pantalla principal**: sprite del bebé BABY animado (2 frames cada 400 ms) + título "Tamagochi".
2. **Click corto** (~200-300 ms) → cambia a la pantalla **STATS** y vuelve.
3. **STATS**: 3 barras de progreso — Hambre, Animo, Salud.
4. **Pulsación larga** (>600 ms) → alimentar (Hambre −20). Guarda el estado en flash inmediatamente.
5. **Cada segundo** el firmware aplica decay (lento — el hambre tarda ~16 min en saturar).
6. **Cada 5 minutos** autosave en LittleFS. El Tamagochi sobrevive a apagones y power cycles.

> El botón GPIO0 tiene rebote mecánico. Para que se interprete como click corto pulsa entre 100-500 ms (como un botón físico estándar). Mantener >600 ms = pulsación larga.

---

## Estado actual — Fase 1 completada

| Feature | Estado |
|---|---|
| Logger con niveles | ✅ |
| EventBus pub/sub (sin allocación dinámica) | ✅ |
| StatsEngine con decay + acciones | ✅ |
| Persistence LittleFS rotativa con CRC32 + migrate() | ✅ |
| InputHandler con OneButton calibrado | ✅ |
| DisplayRenderer con IdleMain + StatsView | ✅ |
| Sprite BABY real animado (escalado x2) | ✅ |
| Persistencia entre reinicios y power cycles | ✅ |
| Tests host-native (Unity) — **32 tests pasando** | ✅ |
| OTA + WiFi STA | ⏸️ diferido — Fase 2 o posterior |

---

## Setup PlatformIO

Requiere [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) o la extensión de VS Code.

```bash
# Compilar el firmware
pio run -e esp12e

# Flashear (USB)
pio run -e esp12e -t upload

# Monitor serie (115200 baud)
pio device monitor

# Tests host-native (no necesita la placa)
pio test -e native
```

> Si el flasheo da `Invalid head of packet` por hub USB, `upload_speed` está fijado a 460800 en `platformio.ini` (921600 falla a través de hub).

---

## Estructura del proyecto

```
Tamagochi/
├── platformio.ini
├── include/
│   ├── config.h               ← pines, timings
│   └── sprites_generated.h    ← assets BABY como arrays PROGMEM (auto-generado)
├── lib/
│   ├── core/
│   │   ├── Logger/            ← macros LOG_INFO/WARN/ERROR/DEBUG
│   │   └── EventBus/          ← pub/sub síncrono con function pointers planos
│   ├── domain/                ← lógica pura, testeable host-native
│   │   ├── StatsEngine/       ← decay, applyFeed/Play/Heal
│   │   └── Persistence/       ← LittleFS rotativo + CRC32 + IStorage abstraction
│   └── ui/
│       ├── InputHandler/      ← wrapper OneButton → EventBus
│       └── DisplayRenderer/   ← U8g2 + scenes (IdleMain, StatsView, OtaProgress)
├── src/main.cpp               ← orquestación: singletons + suscripciones + loop
├── test/
│   ├── test_smoke/            ← canario del env native
│   ├── test_stats_engine/     ← 14 tests
│   └── test_persistence/      ← 17 tests con mock InMemoryStorage
├── assets/sprites/            ← PNGs fuente (1-bit, 32×32 y 16×16)
├── tools/
│   ├── slice_sheet.py         ← trocea sprite sheets (Nano Banana)
│   └── png_to_xbm.py          ← convierte PNGs → arrays C PROGMEM (--invert disponible)
└── docs/
    ├── fase-1-plan.md         ← plan vinculante de Fase 1 (referencia histórica)
    └── sprites-plan.md        ← inventario y pipeline de sprites
```

---

## Verificación de Fase 1

### Tests automáticos
```bash
pio test -e native   # 32/32 deben pasar
```

### Pruebas funcionales en hardware
- [x] Boot muestra el bebé animado en pantalla principal
- [x] Click corto alterna pantallas
- [x] Pulsación larga baja hambre 20 puntos (visible en STATS)
- [x] Tras RESET (botón S2) el firmware recupera el último estado guardado (log `Persist load: Ok (hunger=X happ=Y health=Z)`)
- [x] FPS estable a 5.00 (log `[FPS] 5.00 fps`)

### Endurance opcional (24h)
Para validar que el firmware aguanta una sesión larga sin reinicios ni leaks:
1. Conecta la placa al USB y `pio device monitor` con `tee` a un archivo de log:
   ```bash
   pio device monitor 2>&1 | tee tamagochi-24h.log
   ```
2. Déjalo corriendo 24h sin tocar nada.
3. Revisa el log:
   - No debe haber `[ERROR]`
   - No debe aparecer `Boot Fase 1` más de una vez (sin reinicios espontáneos)
   - El decay debe haber llevado hambre/animo a valores extremos sin crash
4. Power cycle test: desenchufa y vuelve a enchufar ≥10 veces. Cada arranque debe loguear `Persist load: Ok` con valores plausibles.

---

## Roadmap

- **Fase 2** — etapas de vida (EGG → BABY → KID → ADULT → OLD → DEAD), animaciones de feliz/triste/comer (sprites ya generados), overlays sleep/sick/poop/hungry, evoluciones por `attention_score`, muerte y resurrección.
- **Fase 3** — interfaz web servida por el ESP8266 + REST `/api/v1/*` para acciones secundarias (limpiar caca, curar, etc).
- **Fase 4** — gestión de energía (auto-dim/off del OLED, modem sleep WiFi) para llegar a 6+ h con LiPo 500 mAh.
- **OTA diferido** — cuando haga falta actualizar la placa sin USB (carcasa montada, ubicación fija).

Plan completo en `docs/fase-1-plan.md` (Fase 1) y `docs/plan.md` (maestro).
