# Tamagotchi (ESP8266 mono + ESP32-S3 color)

Este repositorio contiene **dos variantes de firmware** para el mismo Tamagotchi:

| Variante | Placa | Display | Entrypoint |
|---|---|---|---|
| **ESP8266 mono** (original) | ideaspark ESP8266 0.96" OLED V3.2 | SSD1306 128×64 mono | `src/main.cpp` |
| **ESP32-S3 color** (Furbygotchi) | Waveshare ESP32-S3 Zero | JD9853 240×296 IPS TFT | `src/main_esp32.cpp` |

El dominio de negocio (StatsEngine / LifeFSM / Persistence) es **compartido por ambas variantes**. Los 292 tests `native` aplican a las dos.

- **Guía de montaje del Furbygotchi (ESP32-S3):** <https://makerisland.es/projects/furbygotchi/montaje>
- **Licencia:** MIT, ver [`LICENSE`](LICENSE).

---

## Variante ESP8266 mono

Tamagotchi clásico años 90 corriendo en una placa **ideaspark ESP8266 0.96" OLED Module V3.2**. Firmware en C++/Arduino con PlatformIO. Ciclo de vida completo (huevo→bebé→niño→adulto→anciano→muerte→resurrección), 6 stats con decay en tiempo real, persistencia entre apagados y arquitectura modular pensada para crecer.

---

## Hardware

| Componente | Detalle |
|---|---|
| MCU | ESP-12E (Xtensa LX106 80 MHz, 80 KB RAM, 4 MB Flash) |
| Display | SSD1306 128×64 OLED integrado |
| Pines OLED | **SW I2C**: SDA=GPIO12, SCL=GPIO14 (no HW I2C — confirmado V3.2) |
| Orientación render | `U8G2_R1` → 64 wide × 128 tall (portrait, USB-C arriba) |
| Botón | Touch capacitivo TTP223 en GPIO4 (D2) — VCC/GND/I/O |
| Power | JST LiPo + TP4056 + AMS1117 3.3V / USB-C (CH340G) |

---

## Cómo se juega

1. **Pantalla principal**: sprite del Tamagotchi animado según etapa de vida + título "Tamagotchi" + mini-contador de edad ("9m" o "1h2m").
2. **Click corto** (~200-300 ms) → cambia a la pantalla **STATS** y vuelve.
3. **STATS**: 3 barras de progreso — Hambre, Animo, Salud.
4. **Pulsación larga** (>600 ms) → alimentar (Hambre −20) + animación EAT de 2.4s. Guarda inmediatamente.
5. **Cada segundo** el firmware aplica decay (lento — el hambre tarda ~16 min en saturar). En `[env:esp12e_turbo]` el ciclo es ×60 (5 min real ≈ 5 h subjetivas).
6. **Cada 5 minutos** autosave en LittleFS. El Tamagotchi sobrevive a apagones y power cycles.
7. **Overlays asíncronos** sobre el sprite cuando aplique: Z dormido, calavera enfermo, gota hambre, caca (hasta 3 visibles).
8. **Al morir** se muestra `DeadScreen` con causa de muerte y edad alcanzada. Tras 5 min wall-clock renace como huevo (no escala con turbo).

> Touch capacitivo TTP223 en GPIO4: salida active-HIGH sin rebote. Toque breve (<600 ms) = click corto; mantener pulsado >600 ms = pulsación larga. El LED del módulo se enciende mientras detecta contacto.

### Energía, sueño automático y luces (fiel Tamagotchi 97)

La mascota tiene una stat de **energía** (`energy`, 0-100) además de hambre/ánimo/salud. Se **duerme sola** al bajar de 20 (agotada) y **despierta sola** al llegar a 80 (descansada) — el sueño ya no es un botón manual, es 100% automático. Al despertar, las luces se encienden solas ("amanece").

La acción **"Luces"** (fiel al Tamagotchi original del 97) reemplaza al viejo botón de dormir/despertar y **solo aparece en el menú mientras la mascota duerme**: apágalas para que descanse bien y recupere energía a tasa plena; si las dejas encendidas se molesta — penaliza felicidad y salud, y recupera energía más despacio. Con las luces apagadas la pantalla se pone en negro con unas **Zs flotantes** ascendiendo, pero el menú de acciones sigue visible y operable para poder volver a encenderlas.

---

## Estado actual — Fase 3 completada + hitos posteriores

| Capa | Feature | Estado |
|---|---|---|
| **core** | Logger con niveles | ✅ |
| **core** | EventBus pub/sub (sin allocación dinámica) | ✅ |
| **core** | BatteryGauge — filtrado + gate de plausibilidad (host-testable) | ✅ |
| **core** | MessageBox — canal SPSC + sanitizado UTF-8→ASCII | ✅ |
| **domain** | StatsEngine — 7 stats (incluye `energy`) con decay + 6 acciones (incluye `lights`) | ✅ |
| **domain** | LifeFSM — 6 etapas + transiciones automáticas + muerte/resurrección | ✅ |
| **domain** | Persistence LittleFS rotativa + CRC32 + migrate v1→v2→v3 | ✅ |
| **ui** | InputHandler con OneButton calibrado | ✅ |
| **ui** | DisplayRenderer (mono) / DisplayRendererColor (ESP32-S3) | ✅ |
| **ui** | Overlays asíncronos SICK / SLEEP_Z / HUNGRY / POOP | ✅ |
| **ui** | Animaciones IDLE / HAPPY / SAD / EAT one-shot | ✅ |
| **ui** | BootSplash — pantalla de arranque | ✅ |
| **ui** | MarqueeLogic — scroll de texto (lógica pura) + MessageView | ✅ |
| **infra** | WifiManager STA no bloqueante + reconexión (eventos `WiFi.onEvent`) | ✅ |
| **infra** | OTAManager (ArduinoOTA + password + TWDT alimentado durante OTA) | ✅ |
| **infra** | WebUI — AsyncWebServer + 10 endpoints REST + mDNS | ✅ |
| **infra** | `POST /api/v1/message` — mensaje del padre (marquee) | ✅ |
| **infra** | `POST /api/v1/reboot` — reinicio remoto con persist previo | ✅ |
| **infra** | `GET /api/v1/info` — identidad de red (IP/hostname/MAC) + ADC crudo | ✅ |
| **infra** | RateLimiter (token bucket por IP) + AuthChecker (HTTP Basic) | ✅ |
| **infra** | Supervisor de red — re-levanta WebUI/OTA en caliente vía `WiFi.onEvent` | ✅ |
| **infra** | Task Watchdog Timer (loop) + watchdog de conectividad con anti-boot-loop | ✅ |
| **infra** | Telemetría de reinicios en `/api/v1/info` (resetReason/bootCount/minFreeHeap) | ✅ |
| **ui** | Frontend HTML/CSS/JS mobile-first servido desde LittleFS | ✅ |
| **ui** | Dashboard: panel Sistema (diagnóstico) + panel Mensaje + botón Reiniciar | ✅ |
| **domain/ui** | Mecánica de juego: energía + sueño automático + luces on/off (fiel 97) — ver sección abajo | ✅ |
| **build** | TURBO_MODE ×60 (env `esp12e_turbo`) para QA | ✅ |
| **build** | OTA upload via WiFi (env `esp12e_ota`) | ✅ |
| **build** | Tests host-native — **292 tests pasando** | ✅ |
| **build** | Script integración HTTP — **26 tests pasando** | ✅ |
| **diferido** | Sprites EGG/KID/ADULT/OLD/DEAD reales (mono) | ⏸️ usando BABY como placeholder |
| **diferido** | Gestión de energía de HARDWARE (auto-dim de panel, modem sleep WiFi) | ⏸️ Fase 5 — distinto de la mecánica de juego `energy`, ya ✅ |

---

## Setup PlatformIO

Requiere [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) o la extensión de VS Code.

```bash
# Compilar (modo producción)
pio run -e esp12e

# Compilar TURBO (ciclo de vida ×60 — solo para QA, NUNCA producción)
pio run -e esp12e_turbo

# Flashear (USB)
pio run -e esp12e -t upload

# Subir frontend (data/*) al filesystem LittleFS de la placa
pio run -e esp12e -t uploadfs

# Flashear por WiFi (requiere OTA_PASSWORD en env var y placa en la red)
OTA_PASSWORD="tu_password" pio run -e esp12e_ota -t upload

# Monitor serie (115200 baud)
pio device monitor

# Tests host-native (no necesita la placa) — 292/292
pio test -e native
```

> Si el flasheo da `Invalid head of packet` por hub USB, `upload_speed` está fijado a 460800 en `platformio.ini` (921600 falla a través de hub).
>
> Los binarios `esp12e` (`FIRMWARE_VERSION=0x0200`) y `esp12e_turbo` (`FIRMWARE_VERSION=0x02FE`) son **mutuamente excluyentes** vía `static_assert` para evitar mezclas.

### Secrets

Antes del primer build hay que crear `include/secrets.h` (gitignored) a partir de la plantilla:

```bash
cp include/secrets.h.example include/secrets.h
# editar y rellenar WIFI_SSID, WIFI_PASSWORD, OTA_PASSWORD, WEB_AUTH_USER, WEB_AUTH_PASSWORD
```

- **WIFI_SSID vacío** → WiFi se queda en `Disabled`, el Tamagotchi funciona offline.
- **OTA_PASSWORD vacío** → OTA queda deshabilitada (sin error).
- **WEB_AUTH_PASSWORD vacío** → todos los endpoints REST devuelven 503.

---

## Estructura del proyecto

```
Tamagotchi/
├── platformio.ini
├── partitions/
│   └── esp32s3_4mb_ota_fs.csv ← tabla 4 MB del ESP32-S3 (OTA 2×1,125 MB + LittleFS 1,625 MB)
├── include/
│   ├── config.h               ← pines, timings, TURBO_FACTOR
│   └── sprites_generated.h    ← assets como arrays PROGMEM (auto-generado)
├── lib/
│   ├── core/
│   │   ├── Logger/            ← macros LOG_INFO/WARN/ERROR/DEBUG
│   │   ├── EventBus/          ← pub/sub síncrono, function pointers planos
│   │   ├── BatteryGauge/      ← filtrado ADC + gate de plausibilidad (puro)
│   │   └── MessageBox/        ← canal SPSC + sanitizado UTF-8→ASCII del marquee
│   ├── domain/                ← lógica pura, testeable host-native
│   │   ├── StatsEngine/       ← 6 stats + decay + 6 acciones + RNG inyectable
│   │   ├── LifeFSM/           ← 6 etapas + transiciones automáticas
│   │   └── Persistence/       ← LittleFS rotativo + CRC32 + IStorage + migrate
│   ├── infra/
│   │   ├── WifiManager/       ← STA no bloqueante + reconexión con backoff + WifiInfo para la UI
│   │   ├── OTAManager/        ← ArduinoOTA con password
│   │   └── WebUI/             ← AsyncWebServer + JsonCodec + RateLimiter + AuthChecker
│   └── ui/
│       ├── InputHandler/      ← wrapper OneButton → EventBus
│       ├── DisplayRenderer/   ← U8g2 mono + scenes + tabla sprites por etapa + overlays
│       ├── DisplayRendererColor/ ← Arduino_GFX color (ESP32-S3) + MessageView
│       ├── MarqueeLogic/      ← scroll de texto horizontal (lógica pura)
│       ├── MoodLogic/         ← acción → animación del personaje + fallback jump→happy→idle (pura)
│       ├── NetInfoLogic/      ← estado/SSID/IP/RSSI de la página RED de Stats (pura)
│       └── BootSplash/        ← pantalla de arranque
├── data/                      ← LittleFS: index.html + app.css + app.js (frontend)
├── scripts/
│   └── integration_test.sh   ← script de tests HTTP contra placa real (26 tests)
├── src/
│   ├── main.cpp               ← orquestación ESP8266: singletons + suscripciones + loop
│   └── main_esp32.cpp         ← orquestación ESP32-S3 (incluye consumo del MessageBox)
├── test/                      ← 292 tests host-native (17 suites)
│   ├── test_smoke/            ← canario del env native (1)
│   ├── test_stats_engine/     ← 42 tests (incluye energía/auto-sueño/luces)
│   ├── test_persistence/      ← 25 tests (incluye migrate v1→v2→v3)
│   ├── test_life_fsm/         ← 16 tests (transiciones de etapas)
│   ├── test_json_codec/       ← 23 tests (parse + render JSON + telemetría)
│   ├── test_payload_validator/← 7 tests (incluye regresión sleep→lights)
│   ├── test_rate_limiter/     ← 9 tests (token bucket)
│   ├── test_auth_checker/     ← 6 tests (HTTP Basic Auth)
│   ├── test_battery_gauge/    ← 25 tests (filtrado + gate)
│   ├── test_boot_splash/      ← 5 tests
│   ├── test_marquee_logic/    ← 13 tests (scroll)
│   ├── test_mood_logic/       ← 23 tests (acción→mood, fallback, duración del salto)
│   ├── test_net_info_logic/   ← 35 tests (estado WiFi, SSID truncado, IP, RSSI→barras)
│   ├── test_message_box/      ← 13 tests (sanitizado UTF-8→ASCII)
│   ├── test_bootloop_guard/   ← 9 tests (anti-boot-loop, decisión pura)
│   ├── test_screen_power_logic/ ← 6 tests (apagado de pantalla por inactividad)
│   └── test_connectivity_watchdog_logic/ ← 9 tests (tiempo real sin WiFi)
├── assets/sprites/            ← PNGs fuente (1-bit, 32×32 y 16×16)
├── tools/
│   ├── slice_sheet.py         ← trocea sprite sheets (Nano Banana)
│   └── png_to_xbm.py          ← convierte PNGs → arrays C PROGMEM (--invert)
└── docs/
    ├── fase-1-plan.md         ← plan vinculante de Fase 1 (referencia histórica)
    ├── fase-2-plan.md         ← plan vinculante de Fase 2 (referencia histórica)
    └── sprites-plan.md        ← inventario y pipeline de sprites
```

---

## Web UI — Fase 3

Una vez la placa esté en la WiFi y `data/*` esté subido al filesystem:

| URL | Descripción |
|---|---|
| `http://tamagotchi.local/` | Frontend mobile-first (HTML/CSS/JS desde LittleFS) |
| `GET /api/v1/state` | Snapshot del Tamagotchi (`lifeStage`, stats, flags, `energy`, `lightsOn`, `growthCap`) |
| `GET /api/v1/info` | Telemetría + identidad de red + diagnóstico de batería (ver abajo) |
| `POST /api/v1/{feed,play,discipline}` | Body `{"amount": 1..100}` |
| `POST /api/v1/{clean,medicate}` | Sin body |
| `POST /api/v1/lights` | Body `{"value": true}` o `{"value": false}` — luces on/off (ex-`/sleep`, ver abajo) |
| `POST /api/v1/message` | Body `{"text": "..."}` — mensaje del padre (marquee). Solo variante color |
| `POST /api/v1/reboot` | Sin body. Persiste el estado y reinicia el dispositivo (~750ms después de responder) |

> **Nota:** la acción/endpoint **Heal fue eliminada** (redundante con `medicate`) — su sprite, botón y tests salieron del repo. Quedan 6 acciones: feed, play, clean, medicate, discipline, lights.
>
> **Sueño automático + luces (energía/97):** `isSleeping` pasó a ser **solo lectura** en `GET /api/v1/state` — el sueño ya no es controlable por el cliente, lo gobierna `energy` (0-100) en el firmware (duerme sola ≤20, despierta sola ≥80). El antiguo `POST /api/v1/sleep` **fue retirado** (404); `POST /api/v1/lights` alterna `lightsOn` y solo tiene efecto visible mientras la mascota duerme. `GET /api/v1/state` expone `energy` y `lightsOn` junto al resto de stats.

#### `GET /api/v1/info` — campos

Además de `firmwareVersion`, `uptimeMs`, `freeHeap`, `wifiRssi`:

- **Identidad de red** (siempre presente): `ipAddress`, `hostname`, `macAddress`.
- **Batería** (gateada por validez): `batteryPercent`, `batteryCharging`, `batteryValid`, `batteryMillivolts`.
- **Diagnóstico de soldadura** (nunca gateado — sirve para depurar el divisor de VBAT): `batteryRawMillivolts` (última lectura ADC cruda) y `batteryGateProgress` (0-8 muestras consecutivas plausibles hacia la validez).
- **Telemetría de reinicios**: `resetReason` (`POWERON` / `SW` / `TASK_WDT` / `BROWNOUT` / `PANIC`…), `bootCount` (reinicios soft consecutivos — contador anti-boot-loop) y `minFreeHeap` (mínimo histórico de heap; **incluye PSRAM**, por eso puede salir mayor que `freeHeap`, que es solo DRAM interna). Sirven para diagnosticar por qué se reinició el dispositivo sin cable serie.

#### `POST /api/v1/message` — mensaje del padre

- **Body:** `{"text": "..."}`. Máximo **192 bytes UTF-8 crudos**; se sanitizan a **ASCII imprimible** (≤ 96 chars) con transliteración de acentos (á→a, ñ→n, …) y descarte de emoji/controles (ver `MessageBox` / `TextSanitizer`).
- **200:** `{"data": {"messageAccepted": true, "displayedChars": N}}`.
- **422 `MISSING_FIELD`:** falta `text` o no es string.
- **422 `INVALID_TEXT`:** texto crudo > 192 bytes, vacío tras sanitizar (solo espacios/emoji) o > 96 chars sanitizados.
- El firmware (ESP32-S3) lo consume del `MessageBox` en `loop()` y lo muestra como marquee (`MessageView`); se descarta con el botón físico (click corto o pulsación larga), restaurando la escena previa.

**Autenticación:** HTTP Basic Auth con `WEB_AUTH_USER:WEB_AUTH_PASSWORD` de `secrets.h`. Todos los endpoints `/api/v1/*` la requieren.

**Rate limiting:** token bucket por IP, 10 tokens iniciales + 1 token/600 ms. 429 si se agota.

**Errores:** formato estándar (`code` / `message` / `details`).

### Tests de integración HTTP

```bash
# El frontend tiene que estar subido (uploadfs) y la placa en WiFi.
HOST=tamagotchi.local AUTH_USER=admin AUTH_PASS=tu_pass ./scripts/integration_test.sh

# Si mDNS no resuelve, ataca por IP:
HOST=192.168.x.x AUTH_USER=admin AUTH_PASS=tu_pass ./scripts/integration_test.sh
```

Cubre 26 escenarios: auth (401), happy path por endpoint (200 + `actionApplied`), validaciones (422 amount fuera de rango / tipo equivocado, 400 JSON malformado), 404 en rutas inexistentes y los assets estáticos del frontend. (Los endpoints `message` y `reboot` aún no están cubiertos por el script — ver backlog.)

---

## Verificación

### Tests automáticos
```bash
pio test -e native   # 292/292 deben pasar
```

### Pruebas funcionales en hardware
- [x] Boot carga estado migrado de Fase 1 (log `Persist load: Ok ... stage=N age=M`)
- [x] Pantalla principal muestra sprite + "Tamagotchi" + contador de edad
- [x] Click corto alterna IdleMain ↔ StatsView
- [x] Pulsación larga: hambre baja 20, animación EAT 2.4s, persist on-action
- [x] FPS estable a 5.00
- [x] WiFi conecta a SSID configurado en `secrets.h` y aparece en `IdleMain` con icono
- [x] OTA upload funciona (`OTA_PASSWORD="..." pio run -e esp12e_ota -t upload`)
- [x] `tamagotchi.local` accesible desde móvil y sirve el frontend
- [x] `scripts/integration_test.sh` 26/26 PASS
- [ ] Ciclo completo de vida verificado con TURBO (~25 min wall-clock para llegar a DeadScreen)
- [ ] Resurrección Dead→Egg tras 5 min wall-clock

### Endurance opcional (24h)
Igual que en Fase 1:
```bash
pio device monitor 2>&1 | tee tamagotchi-24h.log
```
Comprueba:
- Sin `[ERROR]` en el log
- `Boot Fase` aparece una sola vez (sin reinicios espontáneos)
- Transiciones de etapa logueadas: `Life: stage -> N (subtype=M)`
- Power cycle test (≥10×): cada arranque debe loguear `Persist load: Ok` con stage/age plausibles

---

## Diseño y pipeline de sprites

1. **Generar el sprite sheet horizontal** con Nano Banana (u otra IA) — fondo blanco, sujeto negro.
2. **Trocear en frames**:
   ```bash
   python3 tools/slice_sheet.py assets/sprites/raw/baby_idle.png assets/sprites/ \
       --name baby_idle --size 32
   ```
3. **Convertir a arrays C PROGMEM** (los PNGs vienen con polaridad invertida — usar `--invert`):
   ```bash
   python3 tools/png_to_xbm.py --invert assets/sprites/ include/sprites_generated.h
   ```
4. **Compilar y flashear** — los sprites nuevos se ven en la próxima sesión.

**Convención de nombres**: `baby_idle_0.png` → `kSpriteBabyIdle0`. Los sprites se mapean a `lifeStage × mood` en la tabla `kSpritesByStage` de `DisplayRenderer.cpp` — al añadir sprites EGG/KID/ADULT/OLD/DEAD basta con reemplazar las entradas placeholder.

---

## Variante ESP32-S3 color (Furbygotchi)

Tamagotchi a color con sprites pixel-art RGB565 que evolucionan por etapa de vida: **huevo → bebé → niño → adulto → viejo → fantasma**. El dominio de negocio (StatsEngine / LifeFSM / Persistence) es exactamente el mismo que la versión ESP8266; los 292 tests `native` validan ambas variantes.

### Hardware ESP32-S3

| Componente | Detalle |
|---|---|
| MCU | Waveshare ESP32-S3 Zero (ESP32-S3FH4R2: 4 MB flash, 2 MB PSRAM) |
| Display | TFT SPI JD9853 240×296 IPS color |
| Librería display | Arduino_GFX v1.3.7 (fijada por compatibilidad con arduino-esp32 core 2.0.17 / IDF4) |
| Botón | Módulo táctil TTP223 en GPIO4 — VCC/GND/I/O, salida active-HIGH, sin pull-up interno (GPIO4 no es pin de strapping) |

**Medidor de batería:** firmware (`lib/core/BatteryGauge/`, 25 tests host), icono TFT y API listos (`batteryValid`/`batteryPercent`/`batteryCharging` en `/api/v1/state`); la soldadura del divisor (GPIO1) y del sensado de carga (GPIO2) está **pendiente** — hasta entonces el icono permanece oculto. Para depurar la soldadura, el panel **Sistema → Batería (diagnóstico)** del dashboard muestra `batteryRawMillivolts` (ADC crudo, nunca gateado) y `batteryGateProgress` (racha 0-8 hacia la validez). Guía completa de soldadura en [`docs/hardware-battery.md`](docs/hardware-battery.md).

#### Cableado TFT JD9853

| Pin TFT | GPIO ESP32-S3 |
|---|---|
| SCK | GPIO12 |
| MOSI | GPIO11 |
| RST | GPIO10 |
| DC | GPIO9 |
| CS | GPIO8 |
| BL | GPIO7 |
| VCC | 3V3 |
| GND | GND |

### Display y renderizado

El JD9853 **no arranca con la secuencia de init ST7789 estándar** — requiere una subclase propia (`Arduino_JD9853`, declarada en `include/jd9853_panel.h`) con init nativa (unlock `0xDF {0x98, 0x53}`).

El framebuffer usa `Arduino_Canvas` 240×296 en PSRAM (doble buffer) → sin parpadeo. Velocidad: **15 FPS**, SPI a **27 MHz**.

### Sprites y sistema de assets

Los sprites se generan con el proyecto **Pixel Sprites** (repo separado), se exportan a RGB565 (`.bin`, little-endian, 2 bytes/píxel) y se sirven desde LittleFS, cargándose a PSRAM por `lib/ui/SpriteStore/`.

| Aspecto | Detalle |
|---|---|
| Descriptor | `data/sprites/manifest.json` — `version`, `colorKey`, `sprites[]` |
| Color key | Magenta `0xF81F` (63519) — píxeles con alpha < 128 → transparente en firmware |
| Longitud carpetas | Máximo 31 caracteres (límite LittleFS) → usar nombres cortos |
| Etapas | `egg` / `baby` / `kid` / `adult` / `old` / `ghost` |
| Animaciones | `idle` / `happy` / `sad` / `eat` / `sleep` (egg y ghost solo `idle`) + `jump` (solo baby) |

#### Animación `jump` — Play en el baby

Al ejecutar **Play** desde el menú del botón, el furby-baby da un **salto** (`baby/jump`, 8 frames 48×48 a 8 fps). Con `SPRITE_ANIM_SLOWDOWN=2` cada frame dura 250 ms, así que el salto completo dura **2 s**; la pelota (overlay de Play) se mantiene y dura lo mismo que el salto.

- La lógica de qué animación se ve vive en `lib/ui/MoodLogic/MoodLogic.h` (pura, 23 tests host): `Play → Jump`, con cadena de fallback **Jump → Happy → Idle** si la etapa no tiene `jump` (kid/adult/old se ven `happy`; egg/ghost `idle`).
- Duración del one-shot = `max(1200 ms, frames × 1000/fps × slowdown)`: happy/sad siguen en 1,2 s, eat 1,25 s, jump 2 s. El frame se calcula por tiempo desde que se pulsa, así que el salto **siempre empieza en el suelo** y el aterrizaje no se corta.
- `jump` es **opcional** al cargar: si el LittleFS es antiguo (sin `uploadfs`), se loguea un `WARN` y Play se ve como `happy` sin dejar al resto de etapas sin sprite.
- Solo el botón físico dispara animaciones: `POST /api/v1/play` desde el dashboard aplica la acción pero no anima.
- Para reexportar los sprites del baby desde Pixel Sprites hay que pasar `--folder baby` y **las 6 animaciones** (el upsert del manifest reemplaza la entrada entera):
  ```bash
  node tools/export-esp32.mjs --id 797ec06d-c81f-4f02-882a-df63976879b6 --folder baby \
    --animations idle,happy,sad,eat,sleep,jump --merge-into <ruta-a-este-repo>/data/sprites/manifest.json
  ```

### Mensajes del padre (marquee)

Un padre puede enviar texto al Tamagotchi desde el dashboard (panel **Mensaje**) o vía `POST /api/v1/message`. El texto se muestra como marquee a color y se descarta con el botón físico.

- **`MessageBox`** (`lib/core/`): canal **SPSC** sin mutex (productor = handler HTTP en task `async_tcp`; consumidor = `loopTask`), con par release/acquire. Sanitiza UTF-8→ASCII imprimible (transliteración de acentos, descarte de emoji/controles). Límites: **192 bytes** crudos → **≤ 96 chars** sanitizados. Política **last-wins**.
- **`MarqueeLogic`** (`lib/ui/`): lógica pura de scroll horizontal (posición, vueltas, timeout), testeable en host — 13 tests.
- **`MessageView`** (`DisplayRendererColor`): escena que renderiza el marquee. `loop()` la activa cuando llega un mensaje y la mascota está viva; al terminar (timeout, vueltas agotadas o descarte por botón) restaura la escena previa (`sceneBeforeMessage_`).

### Pausa de crecimiento (tope en Baby)

**Ahora mismo el firmware ESP32-S3 tiene el crecimiento pausado en Baby.** De momento solo el bebé tiene las animaciones nuevas (Kling), así que la mascota no evoluciona hasta que el resto de etapas tengan las suyas.

- **Qué hace:** el huevo eclosiona con normalidad, pero el bebé **no pasa a Kid**: ni por atención (60 min y `attentionScore >= 300`) ni por la transición forzada de los 90 min. La edad se congela en **59 min** (justo antes de `kBabyToKidMin`), así que tampoco hay muerte por vejez. La muerte por salud y el renacer como huevo funcionan igual.
- **Pets que ya habían crecido:** al arrancar, un pet guardado como Kid, Adult u Old **vuelve a Baby** con edad `kEggHatchMin` (5 min) y **conserva sus stats** (hambre, ánimo, salud, peso, atención…). El estado degradado se guarda en flash nada más arrancar. Un pet muerto (Dead) no se toca. Por serie se ve:
  ```
  [INFO ] [life] growth cap activo: Baby
  [WARN ] [life] growth cap: etapa guardada Adult (age=300) supera el tope Baby -> degradada a Baby (age=5), stats conservados
  ```
- **API:** `GET /api/v1/state` (y las respuestas de las acciones) incluyen `"growthCap": "Baby"`, o `null` si no hay tope.
- **Dónde se configura:** `-D GROWTH_CAP_STAGE=1` en `build_flags` de `[env:esp32s3]` en `platformio.ini` (lo heredan `esp32s3_turbo` y `esp32s3_ota`). El valor es el de `LifeStage` (`0`=Egg `1`=Baby `2`=Kid `3`=Adult `4`=Old). La lógica está en `LifeFSM::setGrowthCap()` / `restoreLifeState()`. `esp12e` no lo lleva.

**Cómo quitarla:** borra la línea `-D GROWTH_CAP_STAGE=1` de `platformio.ini` y vuelve a flashear (`pio run -e esp32s3 -t upload`, o por OTA). La mascota sigue desde Baby con 59 min de edad y evoluciona con las reglas de siempre: pasa a Kid en cuanto cumple 60 min si `attentionScore >= 300`, o a los 90 min como mucho. No se salta ninguna etapa. Las etapas que se perdieron con la degradación no se recuperan.

### Compilar y flashear — ESP32-S3

```bash
# Firmware (producción)
pio run -e esp32s3 -t upload

# Subir sprites + dashboard a LittleFS
pio run -e esp32s3 -t uploadfs

# Ciclo de vida ×60 (QA — NUNCA producción)
pio run -e esp32s3_turbo -t upload

# Flashear por WiFi (OTA)
pio run -e esp32s3_ota -t upload

# Tests host-native (aplican a ambas variantes)
pio test -e native
```

> **Puerto serie:** `platformio.ini` no fija ningún puerto USB, así que PlatformIO detecta la placa solo. Si hay varias placas conectadas o elige la que no es, indica el puerto al llamar (`pio device list` muestra los disponibles):
> ```bash
> pio run -e esp32s3 -t upload --upload-port /dev/cu.usbmodemXXXX   # macOS; en Linux /dev/ttyACM0, en Windows COM5
> pio device monitor --port /dev/cu.usbmodemXXXX
> ```
>
> **Plataforma fijada:** `esp32s3` usa `espressif32@6.10.0` (arduino-esp32 core 2.0.17). Arduino_GFX 1.3.7 no compila con el core 3.x, así que no subas la plataforma sin subir también la librería.

#### Tabla de particiones propia (4 MB)

El ESP32-S3 usa `partitions/esp32s3_4mb_ota_fs.csv` (la heredan `esp32s3_turbo` y `esp32s3_ota`) en lugar de `default.csv`, porque los sprites ya no cabían: con el `jump` del baby, `buildfs` fallaba con `lfs_write error(-28): File system is full`.

| Partición | `default.csv` | Propia |
|---|---|---|
| `app0` / `app1` (OTA) | 2 × 1,25 MB | 2 × 1,125 MB (firmware actual ≈ 0,95 MB, 80 %) |
| `spiffs` (LittleFS) | 1,375 MB @ `0x290000` | **1,625 MB** @ `0x250000` |
| `nvs` / `otadata` / `coredump` | sin cambios | sin cambios |

> **El primer flasheo con esta tabla tiene que ir por USB**, firmware y filesystem:
> ```bash
> pio run -e esp32s3 -t upload     # escribe también la tabla de particiones
> pio run -e esp32s3 -t uploadfs   # LittleFS con sprites + dashboard
> ```
> La tabla de particiones **no viaja por OTA** (`esp32s3_ota` solo reescribe el slot de app) y el LittleFS cambia de dirección, así que un OTA sobre una placa con la tabla antigua no la migra.
>
> **`uploadfs` borra el estado del pet**: la partida se guarda en el mismo LittleFS, y la imagen nueva lo reemplaza entero → la mascota vuelve a **Egg** (eclosiona a los 5 min de edad, ver `LifeFSM::kEggHatchMin`; con `esp32s3_turbo` ×60, a los ~5 s).

### WiFi / OTA / WebUI — ESP32-S3

Igual que en la variante ESP8266: crear `include/secrets.h` desde la plantilla antes del primer build.

```bash
cp include/secrets.h.example include/secrets.h
# rellenar WIFI_SSID, WIFI_PASSWORD, OTA_PASSWORD, WEB_AUTH_USER, WEB_AUTH_PASSWORD
```

- **WIFI_SSID vacío** → arranca offline sin error.
- Dashboard disponible en `http://tamagotchi.local` (HTTP Basic Auth). Incluye panel **Sistema** colapsable (`<details>`, polling propio cada 10 s) con IP, hostname, MAC, firmware, uptime, RAM libre, RSSI y el diagnóstico de batería.
- OTA: `pio run -e esp32s3_ota -t upload` (requiere OTA_PASSWORD y placa en red).

#### Pantalla de Stats: página RED (IP para flashear por OTA)

Stats tiene **2 páginas** en el TFT. La 1/2 ya ocupa todo el alto (barras, flags, edad, RIPs y batería), así que la red va en una **segunda página** en vez de apretarla y meterla en las esquinas curvas del cristal.

- **Navegación (1 botón):** pulsación larga sobre `STATS` en el menú → **STATS 1/2** → click corto → **RED 2/2** → click corto → pantalla principal. El indicador `1/2` / `2/2` va junto al título.
- **Página RED:**

  ```
  BABY                    ((·))
  1h5m
  RED                       2/2
  ───────────────────────────
  ● Conectado
  SSID
  MiRedWiFi
  IP
  192.168.1.42
  HOST (mDNS)
  tamagotchi.local
  RSSI
  -62 dBm  ▂▄▆█
  ```

  | Campo | Detalle |
  |---|---|
  | Estado | `Conectado` (verde) / `Conectando...` (ámbar) / `Sin conexion` (rojo) / `WiFi no configurada` (gris, `WIFI_SSID` vacío) |
  | SSID | Truncado con `...` a 17 caracteres; los bytes no ASCII se pintan como `?` |
  | IP | Grande y en cian; `-` si no hay IP (0.0.0.0) |
  | HOST (mDNS) | `<OTA_HOSTNAME>.local`; en gris si no hay conexión |
  | RSSI | dBm + 4 barras (≥ -55 → 4, ≥ -67 → 3, ≥ -78 → 2, ≥ -89 → 1) |

- **Refresco:** conexión/caída al instante (`WifiStateChanged`); al entrar en la página se relee todo, y mientras está visible cada 2 s (`NET_INFO_REFRESH_MS`) para que se muevan las barras.
- **Capas:** `WifiManager::info()` (infra, el único que lee `WiFi.*`) → `WifiInfo` (`lib/core/WifiState.h`) → `DisplayRendererColor::setWifiInfo()` → formateo puro en `lib/ui/NetInfoLogic/` (35 tests host). Nunca se muestran ni se loguean contraseñas.
- **Serie:** en cada conexión (boot o reconexión) sale una línea con lo necesario para flashear:
  ```
  [INFO ] [wifi] Conectado a 'MiRedWiFi' IP=192.168.1.42 host=tamagotchi.local
  ```

> **Si `tamagotchi.local` no resuelve**, flashea por IP (la de la página RED o la del log) sin tocar `platformio.ini`:
> ```bash
> OTA_PASSWORD="..." pio run -e esp32s3_ota -t upload --upload-port 192.168.1.42
> ```
> `.local` depende solo de mDNS (multicast UDP 5353): no cruza subredes, VLAN ni redes de invitados, y hay routers que filtran el multicast entre clientes WiFi. La placa ESP8266 usa el mismo `OTA_HOSTNAME` por defecto: si las dos están encendidas en la misma red, compiten por el mismo nombre.

> **Fixes de red (13 jul):**
> - `WiFi.setSleep(false)` — el modem sleep descartaba los paquetes UDP de invitación de OTA, dejando la placa "invisible" para `espota`. Con el sleep desactivado el OTA vuelve a ser fiable.
> - Eliminado `setPort(8266)` en el ESP32-S3 — ese es el puerto del ESP8266; el ESP32 usa su puerto OTA por defecto (3232). Con el `setPort` erróneo el OTA escuchaba en el puerto equivocado.

---

## Roadmap

- **Fase 2.5** ✅ — sprites reales para EGG, KID, ADULT (variante base), OLD, DEAD + animación EAT por etapa + animaciones de transición (eclosión, evolve).
- **Fase 3** ✅ — WiFi STA + OTA + AsyncWebServer + REST `/api/v1/*` + frontend HTML/CSS/JS desde LittleFS.
- **Fase 4 (ESP32-S3)** — Furbygotchi color: JD9853 + sprites RGB565 + LittleFS assets + evolución visual por etapa.
- **Fase 5** — gestión de energía (auto-dim/off del OLED/TFT, modem sleep WiFi) para llegar a 6+ h con LiPo 500 mAh.

### Changelog reciente (13 jul - 4 oct 2026)

**4 oct — Pausa de crecimiento en Baby:**
- `LifeFSM` con tope de crecimiento configurable (`setGrowthCap`): el bebé no pasa a Kid y la edad se congela en 59 min. Al cargar, un pet guardado por encima del tope vuelve a Baby (edad 5 min) conservando sus stats, y se persiste en el acto.
- Activado en `[env:esp32s3]` con `-D GROWTH_CAP_STAGE=1` hasta que el resto de etapas tengan las animaciones de Kling. Para quitarlo, ver [Pausa de crecimiento](#pausa-de-crecimiento-tope-en-baby).
- `GET /api/v1/state` añade `growthCap` (`"Baby"` o `null`).
- Tests host-native: **267 → 292**.

**4 oct (noche) — Red en pantalla para flashear sin mDNS:**
- Stats pasa a 2 páginas: **RED 2/2** con estado WiFi, SSID, IP, `<host>.local` y RSSI (dBm + barras). Lógica pura en `NetInfoLogic` — 35 tests host nuevos.
- Línea `[wifi] Conectado a '<ssid>' IP=<ip> host=<host>.local` por serie en cada conexión (antes la IP no se imprimía en ningún sitio).
- El icono WiFi parpadea en ámbar desde el arranque mientras conecta (antes seguía oculto hasta la primera transición).
- Tests host-native: **232 → 267**.

**4 oct — Salto del baby al jugar + tabla de particiones propia:**
- **Play → `jump`** en el furby-baby (8 frames, 2 s, empieza en el suelo); fallback Jump → Happy → Idle en las etapas sin salto. Lógica pura en `MoodLogic` — 23 tests host nuevos.
- **Tabla de particiones 4 MB** con LittleFS de 1,625 MB (antes 1,375 MB, sin sitio para el salto). Primer flasheo por USB; `uploadfs` reinicia la mascota a Egg.
- Tests host-native: **209 → 232**.

**15 jul — Energía, sueño automático y luces on/off (fiel Tamagotchi 97):**
- Nueva stat **`energy`** (0-100) en `StatsEngine`: drena despierta, se duerme sola al llegar a ≤20 y despierta sola al llegar a ≥80 (histéresis 20↔80) — `isSleeping` deja de ser un toggle manual, pasa a ser 100% automático.
- Acción **`lights`** (reemplaza al antiguo `sleep`): único control manual restante, alterna `lightsOn` sin tocar nunca `isSleeping`. Fiel al Tamagotchi 97 — solo aparece en el menú **mientras la mascota duerme** (despierta no tendría efecto visible). Dejarlas encendidas mientras duerme la molesta: penaliza felicidad/salud y recupera energía más despacio; apagadas descansa correctamente.
- **Pantalla oscura + Zs flotantes**: con las luces apagadas y dormida, la pantalla se pone en negro con un rastro de "z" ascendiendo y desvaneciéndose (color y mono) — el HUD de acciones se mantiene visible/operable para poder reencenderlas sin quedar atrapado.
- **Persistencia v3**: nuevo campo `energy` + bit `lightsOn` en `flags`, migración `v1→v2→v3` encadenada (incluye fix de un bug latente que habría saltado el paso `v2→v3`). `sizeof(PersistedState)` se mantiene en 40 bytes.
- API: `POST /api/v1/sleep` retirado (404) → `POST /api/v1/lights`; `GET /api/v1/state` expone `energy`/`lightsOn`, `isSleeping` pasa a solo lectura.
- ~13 tests nuevos de dominio/persistencia/API + 1 test de regresión (`"sleep"` ya no es acción válida). Tests host-native: **190 → 203**.

**14 jul (noche) — Robustez de red y auto-recuperación:**
- **Supervisor de red** (`WiFi.onEvent` + re-`begin()` idempotente en el `loop`): re-levanta WebUI (puerto 80) y OTA (3232) **en caliente** si caen tras una reconexión de WiFi, sin reiniciar. Corrige el fallo donde `tamagotchi.local` quedaba muerto (ping + mDNS vivos, puerto 80 `connection refused`) hasta reset físico.
- **Task Watchdog Timer** (TWDT, 10 s) sobre la `loopTask` + reemplazo de los 2 `while(true)` fatales de `setup()` (fallo TFT/PSRAM) por reinicio: si el `loop` se cuelga, el chip se reinicia solo.
- **Watchdog de conectividad** con anti-boot-loop (`BootLoopGuard` + `RTC_NOINIT_ATTR`): si el WiFi lleva caído > 5 min habiendo conectado antes, reinicia; sin entrar en bucle si el router está apagado desde el arranque (juego offline indefinido).
- **Telemetría de reinicios** en `GET /api/v1/info`: `uptimeMs`, `resetReason`, `bootCount`, `minFreeHeap` — permite diagnosticar por qué se reinició sin cable serie.
- **2 bugs cazados en pruebas de hardware** (ni QA ni los tests los vieron): el watchdog de conectividad medía mal el tiempo sin WiFi (reiniciaba al instante de perder señal si el uptime > 5 min) y el TWDT abortaba el OTA (mataba el flasheo a los 10 s). Ambos con cobertura de la lógica pura (`BootLoopGuardLogic`, `ConnectivityWatchdogLogic`).
- Tests host-native: **171 → 190**.

**14 jul — Mensajes del padre (marquee):**
- `MessageBox` (core, SPSC + sanitizado UTF-8→ASCII) — 13 tests host.
- `MarqueeLogic` (ui, scroll puro) — 13 tests host.
- Escena `MessageView` en `DisplayRendererColor` + integración en `main_esp32` (consumo del `MessageBox`, descarte por botón).
- Endpoint `POST /api/v1/message` + panel **Mensaje** en el dashboard.

**13 jul — Batería, telemetría, dashboard y fixes:**
- `BatteryGauge` (core) con filtrado + gate de plausibilidad (exige muestras consecutivas; charging gateado por validez) — 25 tests host.
- `GET /api/v1/info` ampliado: identidad de red (IP/hostname/MAC) + ADC crudo (`batteryRawMillivolts`/`batteryGateProgress`).
- Dashboard: panel **Sistema** (diagnóstico colapsable) + etiquetas de batería autoexplicativas + botón **Reiniciar**.
- `POST /api/v1/reboot` (persist previo + reinicio).
- Fix OTA: `WiFi.setSleep(false)` (modem sleep perdía UDP de invitación) y eliminado `setPort(8266)` erróneo en ESP32-S3.
- Acción/botón/sprite **Heal eliminados** (redundante con `medicate`): 7 → 6 acciones.
- `BootSplash` (pantalla de arranque) — 5 tests host.
- Actualización del modelo de carcasa + proyecto de slicer 3MF; `Videos/` fuera del repo (`.gitignore`).

### Backlog específico (no perder)

- **ActionMenu en pantalla** — menú con iconos navegable con 1 botón. Permite ejecutar todas las acciones sin WiFi.
- **Cobertura HTTP de `message`/`reboot`** — `integration_test.sh` aún no los cubre (26 escenarios actuales).
- **Sprites faltantes** — Adult sad, Old happy/sad. Baja prioridad (placeholders idle son coherentes con los estados emocionales contenidos de adultos serios y ancianos lentos).
- **AdultType variants** — sprites distintos según `attentionScore` (athletic / normal / obese / grumpy).
- **`/api/v1/reset`** — endpoint para forzar resurrección (huevo nuevo) desde la web. Planeado pero no implementado en Fase 3.
- **`actionApplied` de `lights`** (antes `sleep`) — sigue reportando `amount: 1`/`0` en lugar de `value: true/false` (cosmético).

Planes completos en `docs/fase-1-plan.md`, `docs/fase-2-plan.md` y `docs/fase-3-plan.md`.
