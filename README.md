# Tamagotchi ESP8266

Tamagotchi clásico años 90 corriendo en una placa **ideaspark ESP8266 0.96" OLED Module V3.2**. Firmware en C++/Arduino con PlatformIO. Ciclo de vida completo (huevo→bebé→niño→adulto→anciano→muerte→resurrección), 6 stats con decay en tiempo real, persistencia entre apagados y arquitectura modular pensada para crecer.

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

1. **Pantalla principal**: sprite del Tamagotchi animado según etapa de vida + título "Tamagotchi" + mini-contador de edad ("9m" o "1h2m").
2. **Click corto** (~200-300 ms) → cambia a la pantalla **STATS** y vuelve.
3. **STATS**: 3 barras de progreso — Hambre, Animo, Salud.
4. **Pulsación larga** (>600 ms) → alimentar (Hambre −20) + animación EAT de 2.4s. Guarda inmediatamente.
5. **Cada segundo** el firmware aplica decay (lento — el hambre tarda ~16 min en saturar). En `[env:esp12e_turbo]` el ciclo es ×60 (5 min real ≈ 5 h subjetivas).
6. **Cada 5 minutos** autosave en LittleFS. El Tamagotchi sobrevive a apagones y power cycles.
7. **Overlays asíncronos** sobre el sprite cuando aplique: Z dormido, calavera enfermo, gota hambre, caca (hasta 3 visibles).
8. **Al morir** se muestra `DeadScreen` con causa de muerte y edad alcanzada. Tras 5 min wall-clock renace como huevo (no escala con turbo).

> El botón GPIO0 tiene rebote mecánico. Para que se interprete como click corto pulsa entre 100-500 ms (como un botón físico estándar). Mantener >600 ms = pulsación larga.

---

## Estado actual — Fase 3 completada

| Capa | Feature | Estado |
|---|---|---|
| **core** | Logger con niveles | ✅ |
| **core** | EventBus pub/sub (sin allocación dinámica) | ✅ |
| **domain** | StatsEngine — 6 stats con decay + 7 acciones | ✅ |
| **domain** | LifeFSM — 6 etapas + transiciones automáticas + muerte/resurrección | ✅ |
| **domain** | Persistence LittleFS rotativa + CRC32 + migrate v1→v2 | ✅ |
| **ui** | InputHandler con OneButton calibrado | ✅ |
| **ui** | DisplayRenderer multi-sprite por etapa + DeadScreen | ✅ |
| **ui** | Overlays asíncronos SICK / SLEEP_Z / HUNGRY / POOP | ✅ |
| **ui** | Animaciones IDLE / HAPPY / SAD / EAT one-shot | ✅ |
| **infra** | WifiManager STA no bloqueante + reconexión | ✅ |
| **infra** | OTAManager (ArduinoOTA + password) | ✅ |
| **infra** | WebUI — AsyncWebServer + 9 endpoints REST + mDNS | ✅ |
| **infra** | RateLimiter (token bucket por IP) + AuthChecker (HTTP Basic) | ✅ |
| **ui** | Frontend HTML/CSS/JS mobile-first servido desde LittleFS | ✅ |
| **build** | TURBO_MODE ×60 (env `esp12e_turbo`) para QA | ✅ |
| **build** | OTA upload via WiFi (env `esp12e_ota`) | ✅ |
| **build** | Tests host-native — **108 tests pasando** | ✅ |
| **build** | Script integración HTTP — **27 tests pasando** | ✅ |
| **diferido** | Sprites EGG/KID/ADULT/OLD/DEAD reales | ⏸️ usando BABY como placeholder |
| **diferido** | Gestión de energía (auto-dim, sleep) | ⏸️ Fase 4 |

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

# Tests host-native (no necesita la placa)
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
├── include/
│   ├── config.h               ← pines, timings, TURBO_FACTOR
│   └── sprites_generated.h    ← assets como arrays PROGMEM (auto-generado)
├── lib/
│   ├── core/
│   │   ├── Logger/            ← macros LOG_INFO/WARN/ERROR/DEBUG
│   │   └── EventBus/          ← pub/sub síncrono, function pointers planos
│   ├── domain/                ← lógica pura, testeable host-native
│   │   ├── StatsEngine/       ← 6 stats + decay + 7 acciones + RNG inyectable
│   │   ├── LifeFSM/           ← 6 etapas + transiciones automáticas
│   │   └── Persistence/       ← LittleFS rotativo + CRC32 + IStorage + migrate
│   ├── infra/
│   │   ├── WifiManager/       ← STA no bloqueante + reconexión con backoff
│   │   ├── OTAManager/        ← ArduinoOTA con password (port 8266)
│   │   └── WebUI/             ← AsyncWebServer + JsonCodec + RateLimiter + AuthChecker
│   └── ui/
│       ├── InputHandler/      ← wrapper OneButton → EventBus
│       └── DisplayRenderer/   ← U8g2 + scenes + tabla sprites por etapa + overlays
├── data/                      ← LittleFS: index.html + app.css + app.js (frontend)
├── scripts/
│   └── integration_test.sh   ← script de tests HTTP contra placa real (27 tests)
├── src/main.cpp               ← orquestación: singletons + suscripciones + loop
├── test/
│   ├── test_smoke/            ← canario del env native
│   ├── test_stats_engine/     ← 34 tests
│   ├── test_persistence/      ← 22 tests (incluye migrate v1→v2)
│   ├── test_life_fsm/         ← 16 tests (transiciones de etapas)
│   ├── test_json_codec/       ← 14 tests (parse + render JSON)
│   ├── test_payload_validator/← 6 tests
│   ├── test_rate_limiter/     ← 9 tests (token bucket)
│   └── test_auth_checker/     ← 6 tests (HTTP Basic Auth)
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
| `http://tamagotchi.local/api/v1/state` | Snapshot del Tamagotchi (`lifeStage`, stats, flags) |
| `http://tamagotchi.local/api/v1/info` | Telemetría: `freeHeap`, `uptimeMs`, `rssi`, etc. |
| `POST /api/v1/{feed,play,heal,discipline}` | Body `{"amount": 1..100}` |
| `POST /api/v1/{clean,medicate}` | Sin body |
| `POST /api/v1/sleep` | Body `{"value": true}` o `{"value": false}` |

**Autenticación:** HTTP Basic Auth con `WEB_AUTH_USER:WEB_AUTH_PASSWORD` de `secrets.h`. Todos los endpoints `/api/v1/*` la requieren.

**Rate limiting:** token bucket por IP, 10 tokens iniciales + 1 token/600 ms. 429 si se agota.

**Errores:** formato estándar (`code` / `message` / `details`) según `docs/api-conventions.md`.

### Tests de integración HTTP

```bash
# El frontend tiene que estar subido (uploadfs) y la placa en WiFi.
HOST=tamagotchi.local AUTH_USER=admin AUTH_PASS=tu_pass ./scripts/integration_test.sh

# Si mDNS no resuelve, ataca por IP:
HOST=192.168.x.x AUTH_USER=admin AUTH_PASS=tu_pass ./scripts/integration_test.sh
```

Cubre 27 escenarios: auth (401), happy path por endpoint (200 + `actionApplied`), validaciones (422 amount fuera de rango / tipo equivocado, 400 JSON malformado), 404 en rutas inexistentes y los assets estáticos del frontend.

---

## Verificación

### Tests automáticos
```bash
pio test -e native   # 108/108 deben pasar
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
- [x] `scripts/integration_test.sh` 27/27 PASS
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

## Roadmap

- **Fase 2.5** ✅ — sprites reales para EGG, KID, ADULT (variante base), OLD, DEAD + animación EAT por etapa + animaciones de transición (eclosión, evolve).
- **Fase 3** ✅ — WiFi STA + OTA + AsyncWebServer + REST `/api/v1/*` + frontend HTML/CSS/JS desde LittleFS.
- **Fase 4** — gestión de energía (auto-dim/off del OLED, modem sleep WiFi) para llegar a 6+ h con LiPo 500 mAh.

### Backlog específico (no perder)

- **ActionMenu en pantalla** — menú con iconos navegable con 1 botón. Permite ejecutar todas las acciones sin WiFi.
- **Sprites faltantes** — Adult sad, Old happy/sad. Baja prioridad (placeholders idle son coherentes con los estados emocionales contenidos de adultos serios y ancianos lentos).
- **AdultType variants** — sprites distintos según `attentionScore` (athletic / normal / obese / grumpy).
- **`/api/v1/reset`** — endpoint para forzar resurrección (huevo nuevo) desde la web. Planeado pero no implementado en Fase 3.
- **`actionApplied` de `sleep`** — actualmente reporta `amount: 1`/`0` en lugar de `value: true/false` (cosmético).

Planes completos en `docs/fase-1-plan.md`, `docs/fase-2-plan.md` y `docs/fase-3-plan.md`. Plan maestro en `docs/plan.md`.
