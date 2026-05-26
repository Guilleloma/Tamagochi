# Fase 1 — Core jugable offline (Plan de implementación)

> **Autor:** SuperDev (arquitectura)
> **Ejecutor:** backend-expert
> **Branch:** `feature/fase-1-core` (ya creada desde `main` post-Fase 0)
> **Estimación:** 4-5 sesiones de desarrollo
> **Pre-requisitos:** Fase 0 mergeada en `main` y verificada en hardware

Este documento es la **fuente única de verdad** para implementar Fase 1. backend-expert debe seguirlo de arriba abajo, en el orden de commits propuesto en la sección 11. Cualquier desviación debe consultarse con Maestro antes de ejecutarse.

---

## 0. Resumen ejecutivo

Fase 1 lleva el firmware de "muestra un sprite" (Fase 0) a "**Tamagochi mínimo jugable con persistencia y telemetría**":

- 1 etapa de vida (`BABY`)
- 3 stats: `hunger`, `happiness`, `health` (0-100, decay 1 Hz)
- Botón: **short click** → vista de stats / **long press** → alimentar
- Persistencia LittleFS rotativa con CRC32 + magic + migrate()
- EventBus pub/sub mínimo (desacopla input ↔ dominio ↔ render)
- Logger con niveles (DEBUG/INFO/WARN/ERROR) configurable en compile time
- ArduinoOTA con password (WiFi STA en LAN, fallback offline si no conecta)
- Tests host-native (`pio test -e native`) sobre `StatsEngine` y `Persistence`
- **Sin WebUI todavía** — eso es Fase 3
- **Sin WiFiManager / portal cautivo todavía** — eso es Fase 1.5

Verificaciones de aceptación: tests host verdes, 24h endurance sin reinicios ni leaks, 10× power cycle preservando estado, OTA flasheando en LAN.

---

## 1. Validaciones previas y decisiones críticas

### 1.1. OTA vs. "Sin WiFi de usuario" — resolución de la contradicción

El plan maestro dice **"Sin WiFi de usuario aún (solo OTA en LAN)"** y a la vez exige **ArduinoOTA con password**. La resolución acordada:

| Aspecto | Fase 1 | Fase 1.5 |
|---|---|---|
| Provisioning de credenciales WiFi | **Hardcoded** en `include/secrets.h` (gitignored) | WiFiManager con portal cautivo |
| Conexión WiFi | **STA** no bloqueante, timeout 15 s | STA + AP fallback |
| OTA activo | Solo si WiFi STA conectó | Igual |
| Comportamiento offline | Funciona standalone; OTA desactivado | Igual |

**Justificación:** OTA no puede existir sin pila WiFi viva. Pero el plan maestro tampoco quiere meter WiFiManager (con su portal cautivo y dependencias) hasta Fase 1.5. La solución es **WiFi STA hardcoded mediante secrets**, suficiente para validar OTA en LAN durante el ciclo de desarrollo de Fase 1, sin invertir aún en la complejidad de provisioning.

**Política de secrets (cumple `rules/security.md`):**

- `include/secrets.h` — **gitignored** — contiene `WIFI_SSID`, `WIFI_PASSWORD`, `OTA_PASSWORD`, `OTA_HOSTNAME`
- `include/secrets.h.example` — **versionado** — template con valores placeholder y comentarios
- `secrets.h` se crea manualmente la primera vez (no auto-generar) — instrucción documentada en README
- Si `secrets.h` no existe, `#error` claro en compilación: "Crea include/secrets.h desde secrets.h.example"
- Si las credenciales fallan WiFi → log WARN, OTA queda desactivado, Tamagochi sigue jugable

### 1.2. Naming convention (cumple `rules/code-style.md`)

| Elemento | Convención | Ejemplo |
|---|---|---|
| Carpetas de módulos en `lib/` | PascalCase | `lib/core/Logger/` |
| Archivos `.h` / `.cpp` | PascalCase, mismo nombre que el módulo | `Logger.h`, `Logger.cpp` |
| Clases y tipos | PascalCase | `class StatsEngine`, `struct PersistedState` |
| Métodos y variables | camelCase | `void applyDecay()`, `uint8_t hunger` |
| Constantes C (macros `#define`) | `kSnakeCase` o `UPPER_SNAKE_CASE` según contexto. Para timings y pines existentes mantener UPPER_SNAKE_CASE (compatibilidad con Fase 0) | `RENDER_FRAME_MS`, `OLED_SCL` |
| Constantes C++ (`constexpr`) | `k` + PascalCase | `constexpr uint16_t kPersistMagic = 0xCAFE` |
| Enums | PascalCase con `enum class`, valores en PascalCase | `enum class LifeStage { Egg, Baby, Kid }` |
| Booleans | prefijo `is`, `has`, `should` | `isSick`, `hasOtaConnected` |
| Archivos de tests | `test_<modulo>/test_<modulo>.cpp` | `test/test_stats_engine/test_stats_engine.cpp` |

### 1.3. Política de errores (cumple `rules/error-handling.md`)

- **Sin BD ni API en Fase 1**, así que la mayoría de la regla aplica indirectamente.
- **Nunca swallow errors**: todo `false`/error de `LittleFS`, `WiFi.begin`, CRC mismatch, etc. se loguea con `LOG_WARN` o `LOG_ERROR`.
- **Fail fast**: validar input en cada función pública del dominio (`StatsEngine::applyFeed(amount)` rechaza `amount > 100`).
- Errores en async (callbacks de OTA, etc.) capturados y logueados — nunca dejar el dispositivo en estado indefinido.
- **Nunca loguear** credenciales WiFi, password OTA, etc. (cumple `security.md`).

### 1.4. Política de testing (cumple `rules/testing.md`)

Tests **obligatorios** para mergear Fase 1:

- `StatsEngine`: decay, clamp 0-100, acción `feed` aumenta hunger correctamente, edge case con `dt=0`, edge case con `dt` enorme (overflow simulado), evolución a `DEAD` cuando `health == 0` durante > umbral.
- `Persistence`: serialize/deserialize roundtrip, CRC mismatch rechaza el blob, magic incorrecto rechaza el blob, rotación selecciona el más reciente, recuperación cuando 0/1/2/3 blobs corruptos.

**Lo que NO se testea host-native:** display, OneButton, WiFi, OTA (depende de Arduino-only). Para esos solo verificación manual en hardware.

**Framework:** Unity (built-in en PlatformIO `pio test`).

---

## 2. Árbol completo de archivos

### 2.1. Nuevos

```
Tamagochi/
├── include/
│   ├── config.h                          [MODIFICAR]
│   ├── secrets.h.example                 [NUEVO]  template versionado
│   └── secrets.h                         [NUEVO]  gitignored
├── lib/
│   ├── core/
│   │   ├── Logger/
│   │   │   └── Logger.h                  [NUEVO]  header-only macros
│   │   └── EventBus/
│   │       ├── EventBus.h                [NUEVO]
│   │       └── EventBus.cpp              [NUEVO]
│   ├── domain/
│   │   ├── StatsEngine/
│   │   │   ├── StatsEngine.h             [NUEVO]
│   │   │   └── StatsEngine.cpp           [NUEVO]
│   │   └── Persistence/
│   │       ├── Persistence.h             [NUEVO]  + interface IStorage
│   │       ├── Persistence.cpp           [NUEVO]
│   │       └── LittleFsStorage.cpp       [NUEVO]  implementación Arduino-only
│   ├── infra/
│   │   └── OTAManager/
│   │       ├── OTAManager.h              [NUEVO]
│   │       └── OTAManager.cpp            [NUEVO]
│   └── ui/
│       ├── InputHandler/
│       │   ├── InputHandler.h            [NUEVO]
│       │   └── InputHandler.cpp          [NUEVO]
│       └── DisplayRenderer/
│           ├── DisplayRenderer.h         [NUEVO]
│           └── DisplayRenderer.cpp       [NUEVO]
├── src/
│   └── main.cpp                          [REESCRIBIR]  delgado, orquestación
├── test/
│   ├── test_stats_engine/
│   │   └── test_stats_engine.cpp         [NUEVO]
│   └── test_persistence/
│       ├── test_persistence.cpp          [NUEVO]
│       └── InMemoryStorage.h             [NUEVO]  mock IStorage para host
├── platformio.ini                        [MODIFICAR]  añadir env:native
├── .gitignore                            [MODIFICAR]  añadir include/secrets.h
└── docs/
    └── fase-1-plan.md                    [este archivo]
```

### 2.2. Modificaciones

**`platformio.ini`** — añadir env `native` para tests host, mantener `esp12e` intacto, declarar dependencias nuevas (`ArduinoOTA` viene en core ESP8266, no se añade `lib_dep`).

**`.gitignore`** — añadir línea `include/secrets.h` (manteniendo todas las reglas actuales).

**`include/config.h`** — añadir constantes nuevas (timings de decay, persist intervals, WiFi timeout, etc.) sin romper las de Fase 0.

**`src/main.cpp`** — reescritura completa: pasa de ~240 líneas con todo en línea a ~150-180 líneas orquestando módulos.

---

## 3. API pública de cada módulo

> Las firmas son **vinculantes**. Si backend-expert necesita cambiar alguna, debe consultarlo. Los headers que aparecen aquí no incluyen guardas de inclusión por brevedad — todos llevan `#pragma once`.

### 3.1. `lib/core/Logger/Logger.h` — header-only

**Responsabilidad:** macros de logging con niveles configurables en compile time.

```cpp
#pragma once
#include <Arduino.h>

// Niveles: 0=NONE, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG
#ifndef LOG_LEVEL
  #define LOG_LEVEL 3
#endif

#define LOG_ERROR(fmt, ...) do { if (LOG_LEVEL >= 1) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_WARN(fmt, ...)  do { if (LOG_LEVEL >= 2) Serial.printf("[WARN ] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_INFO(fmt, ...)  do { if (LOG_LEVEL >= 3) Serial.printf("[INFO ] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_DEBUG(fmt, ...) do { if (LOG_LEVEL >= 4) Serial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); } while (0)
```

- **No-op cuando compila para `native`** (no hay `Arduino.h`). Solución: `#ifdef ARDUINO` envuelve todo el bloque y para native se redefinen las macros a `do{}while(0)` (o `printf` a stdout para tests).
- Las macros son `do { ... } while(0)` para que `if (x) LOG_INFO(...)` se comporte bien sin braces.
- **Nunca loguear** strings con credenciales (cumple `rules/security.md`).

### 3.2. `lib/core/EventBus/` — pub/sub mínimo

**Responsabilidad:** desacoplar productores de eventos (botón, timer) de consumidores (StatsEngine, DisplayRenderer).

**Patrón elegido:** observer con callbacks fijos, **sin allocación dinámica**. Máximo `kMaxSubscribersPerEvent = 4` suscriptores por tipo de evento. Sin queue intermedia — los handlers se invocan **síncronamente** desde `publish()`. Esto es crítico en ESP8266: las queues dinámicas fragmentan el heap.

**`EventBus.h`:**
```cpp
#pragma once
#include <stdint.h>

enum class EventType : uint8_t {
    ButtonShortClick,
    ButtonLongPress,
    ButtonDoubleClick,
    StatsTick,          // emitido 1Hz por main loop
    StatsChanged,       // emitido tras feed/decay
    LifeStateChanged,   // p.ej. transición a DEAD (Fase 2 lo usará más)
    PersistRequested,   // alguien pide guardar ya
    OtaStarted,
    OtaProgress,
    OtaFinished,
    OtaError,
    EventTypeCount
};

struct Event {
    EventType type;
    int32_t   payload;   // semántica depende del tipo; suficiente para Fase 1
};

using EventHandler = void(*)(const Event&);

class EventBus {
public:
    static constexpr uint8_t kMaxSubscribersPerEvent = 4;

    // Devuelve false si ya hay kMaxSubscribersPerEvent registrados para ese tipo
    bool subscribe(EventType type, EventHandler handler);

    // Invoca síncronamente a todos los handlers suscritos. Nunca falla.
    void publish(const Event& event);

private:
    EventHandler handlers_[(uint8_t)EventType::EventTypeCount][kMaxSubscribersPerEvent] = {};
    uint8_t      counts_[(uint8_t)EventType::EventTypeCount] = {};
};
```

- **Singleton vía referencia** (no patrón Singleton clásico) — main.cpp construye una instancia y la inyecta a quien la necesita.
- **No usar `std::function`** — añade ~200 bytes RAM por callback por tema de captures. Function pointers planos.
- Coste RAM: `EventTypeCount * 4 * sizeof(ptr) + EventTypeCount = ~50 bytes` por bus.

### 3.3. `lib/domain/StatsEngine/` — lógica pura, sin Arduino

**Responsabilidad:** stats, decay, acciones. **Cero dependencias de Arduino** (testeable en host).

**`StatsEngine.h`:**
```cpp
#pragma once
#include <stdint.h>

struct Stats {
    uint8_t hunger     = 50;  // 0 = saciado, 100 = hambriento
    uint8_t happiness  = 50;  // 0 = triste,  100 = feliz
    uint8_t health     = 100; // 0 = muerto,  100 = sano
};

struct DecayRates {
    // Unidades por segundo (puntos por segundo). Se aplican como acumulador
    // fraccional internamente para no perder precisión a 1 Hz.
    float hungerPerSec    = 0.05f;   // 50 pts en ~16 min — turbo lo multiplica
    float happinessPerSec = 0.03f;
    float healthPerSec    = 0.01f;   // baja sólo si hunger=100 o happiness=0
};

class StatsEngine {
public:
    StatsEngine();
    explicit StatsEngine(const DecayRates& rates);

    // Avanza dt milisegundos de simulación. dt=0 es no-op.
    // dt enorme se clampa a 24h para evitar overflow al cargar tras meses offline.
    void tick(uint32_t dtMs);

    void applyFeed(uint8_t amount);   // amount 1-100, reduce hunger
    void applyPlay(uint8_t amount);   // amount 1-100, sube happiness
    void applyHeal(uint8_t amount);   // amount 1-100, sube health

    const Stats& stats() const { return stats_; }
    void setStats(const Stats& s) { stats_ = s; }    // para tests y load

    // Devuelve true si las stats han cambiado desde la última llamada (uso para EventBus)
    bool consumeDirtyFlag();

private:
    Stats         stats_;
    DecayRates    rates_;
    float         hungerAcc_      = 0.0f;
    float         happinessAcc_   = 0.0f;
    float         healthAcc_      = 0.0f;
    bool          dirty_          = false;

    static uint8_t clamp_(int value);
};
```

- **Sin `Arduino.h`** — usa `stdint.h` y operaciones puras. Compilable en host.
- `dirty_` evita publicar `StatsChanged` cada tick si los puntos enteros no cambiaron.
- Decay de `health` está condicionado: solo decae si `hunger >= 90` o `happiness <= 10`. La lógica exacta se documenta en el `.cpp`.

### 3.4. `lib/domain/Persistence/` — LittleFS rotativo con CRC32

**Responsabilidad:** serializar/deserializar `PersistedState`, gestionar rotación de archivos, validar CRC32, ejecutar `migrate()` si la versión leída es distinta de la actual.

**Patrón clave:** la dependencia de LittleFS está **detrás de una interfaz `IStorage`**. Para tests host se inyecta un `InMemoryStorage`. Para producción se usa `LittleFsStorage`.

**`Persistence.h`:**
```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

// PersistedState — ver sección 4 para layout y bytes exactos
struct PersistedState {
    uint16_t magic;               // 0xCAFE
    uint16_t version;             // 1 en Fase 1
    uint32_t bornAtEpoch;         // 0 si nunca se sincronizó NTP
    uint32_t lastSavedAtEpoch;    // 0 si nunca se sincronizó NTP
    uint32_t ageMinutes;          // contador local, no depende de NTP
    uint8_t  lifeStage;           // enum LifeStage (Fase 1: siempre Baby = 1)
    uint8_t  hunger;
    uint8_t  happiness;
    uint8_t  health;
    uint16_t deathCount;          // veces que ha muerto desde fábrica
    uint16_t reserved;            // padding para alineamiento futuro
    uint32_t crc32;               // CRC32 de los bytes anteriores
};
static_assert(sizeof(PersistedState) == 28, "PersistedState debe ser exactamente 28 bytes");

// Interfaz de almacenamiento — permite mockear en tests host-native
class IStorage {
public:
    virtual ~IStorage() = default;
    virtual bool exists(const char* path) = 0;
    virtual bool read(const char* path, void* buffer, size_t size) = 0;
    virtual bool write(const char* path, const void* buffer, size_t size) = 0;
    virtual bool remove(const char* path) = 0;
};

enum class LoadResult : uint8_t {
    Ok,
    NoneFound,      // sin slot válido → estado inicial
    AllCorrupt,     // todos corruptos → estado inicial + LOG_ERROR
    Migrated,       // versión antigua, se migró correctamente
    MigrationFailed // versión incompatible → estado inicial
};

class Persistence {
public:
    static constexpr uint8_t  kSlotCount     = 4;
    static constexpr uint16_t kPersistMagic  = 0xCAFE;
    static constexpr uint16_t kCurrentVersion = 1;

    explicit Persistence(IStorage& storage);

    // Carga el slot más reciente válido. Si ninguno válido, llena out con defaults.
    LoadResult load(PersistedState& out);

    // Escribe en el siguiente slot rotativo. Calcula CRC. Tras fsync, borra el slot k-3.
    bool save(const PersistedState& state);

    // Aplica migrate() si versión leída != kCurrentVersion. Devuelve false si no es migrable.
    static bool migrate(PersistedState& state);

    // Cómputo CRC32 — public static para que los tests lo verifiquen
    static uint32_t computeCrc32(const void* data, size_t size);

    // Fija el estado inicial (huevo nuevo Fase 1 = bebé recién nacido)
    static void fillDefaults(PersistedState& state);

private:
    IStorage& storage_;
    uint8_t   nextSlot_ = 0;       // se calcula tras el load para que save() rote correctamente

    static const char* slotPath_(uint8_t slot);   // "/state_0.bin" ... "/state_3.bin"
};
```

**Implementaciones:**

- `Persistence.cpp` — lógica pura (load/save/migrate/CRC). Compilable en host. Solo `<string.h>` y `<stdint.h>`.
- `LittleFsStorage.cpp` — implementa `IStorage` envolviendo `LittleFS`. Solo se compila para `env:esp12e` (excluido en `env:native` por `build_src_filter`).

### 3.5. `lib/infra/OTAManager/` — ArduinoOTA con password

**Responsabilidad:** levantar WiFi STA no bloqueante, configurar ArduinoOTA con password, manejar callbacks. Si WiFi no conecta, deshabilita OTA pero no bloquea boot.

**`OTAManager.h`:**
```cpp
#pragma once
#include <stdint.h>

class EventBus;

class OTAManager {
public:
    OTAManager(EventBus& bus, const char* ssid, const char* password,
               const char* otaHostname, const char* otaPassword);

    // No bloquea. Intenta conectar WiFi STA con timeout configurable.
    // Devuelve true si WiFi+OTA quedaron operativos.
    bool begin(uint32_t wifiTimeoutMs = 15000);

    // Llamar en cada loop si begin() devolvió true. Si false, es no-op.
    void loop();

    bool isOtaActive() const { return otaActive_; }
    bool isWifiConnected() const;

private:
    EventBus*   bus_;
    const char* ssid_;
    const char* wifiPassword_;
    const char* otaHostname_;
    const char* otaPassword_;
    bool        otaActive_ = false;

    void registerOtaCallbacks_();
};
```

- **Nunca loguear** el contenido de `wifiPassword_` ni `otaPassword_`.
- `OTAManager::loop()` llama internamente a `ArduinoOTA.handle()` y `MDNS.update()` si aplica.
- En cada fase del OTA (start, progress, end, error) publica el `EventType::Ota*` correspondiente — DisplayRenderer puede pintar barra de progreso.

### 3.6. `lib/ui/InputHandler/` — wrapper de OneButton → EventBus

**Responsabilidad:** Configurar OneButton, traducir callbacks de botón a eventos del bus.

**`InputHandler.h`:**
```cpp
#pragma once
#include <OneButton.h>

class EventBus;

class InputHandler {
public:
    InputHandler(EventBus& bus, uint8_t pin, bool activeLow = true);

    void begin();
    void loop();   // delega a OneButton::tick()

private:
    EventBus*  bus_;
    OneButton  button_;

    static InputHandler* instance_;   // OneButton solo acepta function ptrs

    static void onShortClick_();
    static void onDoubleClick_();
    static void onLongPress_();
};
```

- `instance_` es necesario porque OneButton solo acepta function pointers C (no member functions). Patrón aceptado para singletons de input.
- **Solo un `InputHandler`** vivo a la vez en Fase 1 (un solo botón).

### 3.7. `lib/ui/DisplayRenderer/` — separa render del main

**Responsabilidad:** dibujar las escenas de Fase 1. Solo 2 escenas en esta fase: `IdleMain` (sprite + stats compactos) y `StatsView` (texto grande de los 3 stats).

**`DisplayRenderer.h`:**
```cpp
#pragma once
#include <U8g2lib.h>
#include <stdint.h>

struct Stats;   // fwd decl
class EventBus;

enum class Scene : uint8_t {
    IdleMain,
    StatsView,
    OtaProgress    // overlay durante OTA
};

class DisplayRenderer {
public:
    DisplayRenderer(U8G2& u8g2, EventBus& bus);

    void begin();
    void setScene(Scene scene);
    void setStats(const Stats& stats);    // copia interna
    void setOtaProgress(uint8_t pct);     // 0-100

    // Render una vez. Llamar desde el scheduler a 5 FPS.
    void render();

private:
    U8G2*       u8g2_;
    EventBus*   bus_;
    Scene       scene_       = Scene::IdleMain;
    Stats       stats_;
    uint8_t     otaProgress_ = 0;
    uint8_t     animFrame_   = 0;
    uint32_t    animTickCount_ = 0;

    void drawIdleMain_();
    void drawStatsView_();
    void drawOtaProgress_();
};
```

- En Fase 1 se reusan los sprites dummy de Fase 0 (`kSpriteFrame0`/`kSpriteFrame1`) movidos a `DisplayRenderer.cpp`. Los sprites BABY reales del designer entrarán cuando `tools/png_to_xbm.py` genere `include/sprites_generated.h` — dejar el include preparado pero ifdef-able.
- **Buffer full** (1KB RAM) — mantener el modo de Fase 0.

---

## 4. Modelo `PersistedState` Fase 1

### 4.1. Struct exacto

```cpp
struct PersistedState {
    uint16_t magic;               // [0..2]    0xCAFE
    uint16_t version;             // [2..4]    1
    uint32_t bornAtEpoch;         // [4..8]    0 hasta tener NTP (Fase 1.5)
    uint32_t lastSavedAtEpoch;    // [8..12]
    uint32_t ageMinutes;          // [12..16]
    uint8_t  lifeStage;           // [16]      Baby=1
    uint8_t  hunger;              // [17]
    uint8_t  happiness;           // [18]
    uint8_t  health;              // [19]
    uint16_t deathCount;          // [20..22]
    uint16_t reserved;            // [22..24]  padding, debe ser 0
    uint32_t crc32;               // [24..28]  CRC32 sobre bytes [0..24]
};
// Total: 28 bytes
```

`static_assert(sizeof(PersistedState) == 28)` en el header — falla la compilación si algún cambio rompe el layout.

### 4.2. CRC32

**Implementación:** **propia, minimal, table-less**. Cumple los requisitos sin meter una librería:

```cpp
// CRC32 IEEE polynomial 0xEDB88320 — sin tabla precomputada para ahorrar Flash.
// 28 bytes a CRC ~250 ciclos, ejecuta en <50 µs a 80 MHz → irrelevante.
uint32_t Persistence::computeCrc32(const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
        }
    }
    return ~crc;
}
```

**Razones para no usar `esp_crc32_le`:**
- Es API de ESP-IDF, no de Arduino core para ESP8266 (en ESP8266 Arduino no está expuesta limpiamente).
- Una implementación propia portable se ejecuta también en `env:native`, lo cual es el requisito real (los tests deben verificar el CRC).
- 28 bytes × CRC < 50 µs es irrelevante para el budget de 1 Hz.

**Cómputo:** `crc32 = computeCrc32(&state, offsetof(PersistedState, crc32))` — sobre los 24 bytes anteriores.

### 4.3. Política de versionado y `migrate()`

```cpp
bool Persistence::migrate(PersistedState& s) {
    if (s.version == kCurrentVersion) return true;
    if (s.version > kCurrentVersion) {
        LOG_ERROR("Persist: version %u > current %u — descartando estado", s.version, kCurrentVersion);
        return false;
    }
    // Migraciones futuras se encadenan aquí (v1 → v2 → v3...).
    // En Fase 1 solo existe la v1, así que cualquier version<1 es imposible.
    LOG_WARN("Persist: version desconocida %u — usando defaults", s.version);
    return false;
}
```

Política: si `migrate()` devuelve `false`, `load()` devuelve `MigrationFailed` y el llamante usa el estado inicial. **"Muerte digna + huevo nuevo"** en caso de incompatibilidad.

---

## 5. Esquema LittleFS rotativo

### 5.1. Algoritmo de escritura atómica

```
save(state):
  state.lastSavedAtEpoch = currentTime()
  state.crc32 = computeCrc32(&state, 24)
  slotToWrite = nextSlot_  (0..3 round-robin)
  path = "/state_{slotToWrite}.bin"
  ok = storage.write(path, &state, 28)   ← LittleFS fsync incluido en write+close
  if !ok:
      LOG_ERROR(...)
      return false
  oldestSlot = (slotToWrite + 1) % 4      ← el que se sobrescribirá la próxima vez
  storage.remove("/state_{oldestSlot}.bin")  ← OK si no existe
  nextSlot_ = (slotToWrite + 1) % 4
  return true
```

**Por qué borramos *tras* escribir y no antes:** si se corta la energía a mitad, **siempre queda al menos un slot válido del ciclo anterior**. El nuevo slot o se completó (ok) o no se escribió (fallo → quedará corrupto y se ignora en el siguiente load).

**Garantía de atomicidad:** LittleFS hace COW (copy-on-write) por defecto. Una escritura completa o no se aplica. Si la escritura se corta, el archivo previo (con ese mismo nombre) permanece intacto. Por eso usamos **slots distintos**: el archivo nuevo no choca con el anterior.

### 5.2. Algoritmo de lectura al boot

```
load(out):
  candidates = []
  for slot in 0..3:
      if storage.exists("/state_{slot}.bin"):
          tmp = read("/state_{slot}.bin")
          if tmp.magic != 0xCAFE: continue
          if tmp.crc32 != computeCrc32(&tmp, 24): continue
          candidates.append(tmp)
  if candidates.empty():
      fillDefaults(out)
      return NoneFound   (o AllCorrupt si encontramos archivos pero todos malos)
  newest = candidates.argmax(c => c.lastSavedAtEpoch)   ← desempate: ageMinutes
  if !migrate(newest):
      fillDefaults(out)
      return MigrationFailed
  out = newest
  nextSlot_ = (slotOfNewest + 1) % 4
  return Ok   (o Migrated si la versión cambió)
```

**Desempate cuando `lastSavedAtEpoch == 0`** (sin NTP nunca): usar `ageMinutes` como criterio. Si también empata, elegir el slot de índice mayor.

### 5.3. Nombres de archivos

- `/state_0.bin`, `/state_1.bin`, `/state_2.bin`, `/state_3.bin` — en raíz de LittleFS
- Nada más en LittleFS por ahora (en Fase 3 entrará `/index.html`, etc.)

### 5.4. Recuperación ante corrupción

| Escenario | Comportamiento |
|---|---|
| 0 slots existen (primer boot) | `NoneFound` → estado inicial (huevo Baby con stats default) |
| 1+ slots existen, 1+ válidos | `Ok` → carga el más reciente |
| 1+ slots existen, **todos corruptos** | `LOG_ERROR`, `AllCorrupt` → estado inicial |
| Slot válido pero versión > actual | `LOG_ERROR`, `MigrationFailed` → estado inicial |
| Slot válido pero migración necesaria | `migrate()` retorna true → `Migrated`, log INFO |

En todos los casos el firmware **arranca jugable**.

### 5.5. Mock `InMemoryStorage` para tests host-native

```cpp
// test/test_persistence/InMemoryStorage.h
#pragma once
#include "Persistence.h"
#include <map>
#include <vector>
#include <string>

class InMemoryStorage : public IStorage {
public:
    bool exists(const char* path) override {
        return files_.count(path) > 0;
    }
    bool read(const char* path, void* buffer, size_t size) override {
        auto it = files_.find(path);
        if (it == files_.end() || it->second.size() != size) return false;
        memcpy(buffer, it->second.data(), size);
        return true;
    }
    bool write(const char* path, const void* buffer, size_t size) override {
        files_[path] = std::vector<uint8_t>((const uint8_t*)buffer, (const uint8_t*)buffer + size);
        return true;
    }
    bool remove(const char* path) override {
        return files_.erase(path) > 0;
    }

    // Helpers para los tests
    void corruptFile(const char* path) {
        auto it = files_.find(path);
        if (it != files_.end() && !it->second.empty()) {
            it->second[0] ^= 0xFF;   // flip byte 0
        }
    }
    void truncateFile(const char* path, size_t newSize) {
        auto it = files_.find(path);
        if (it != files_.end()) it->second.resize(newSize);
    }
    bool simulateWriteFailure = false;
    size_t fileCount() const { return files_.size(); }

private:
    std::map<std::string, std::vector<uint8_t>> files_;
};
```

- **Sin allocación dinámica de `std::*` en producción** — `std::map` y `std::vector` solo en tests host (PC tiene RAM de sobra).
- Permite simular corrupción y fallos de escritura para edge cases.

---

## 6. EventBus mínimo

### 6.1. Patrón decidido

**Observer síncrono con function pointers planos.** Ya descrito en sección 3.2. Razones:

- **Sin allocación dinámica** — todo en stack/static. ESP8266 con AsyncWebServer (Fase 3) puede fragmentar el heap fácilmente; cualquier ahorro suma.
- **Sin queue intermedia** — un evento publicado se entrega en el mismo loop. Determinista.
- **Limitación aceptada:** los handlers no deben tardar más de 1-2 ms o se rompe el budget de 5 FPS. Documentado en el header.

### 6.2. Tipos de eventos en Fase 1

| Tipo | Productor | Consumidores |
|---|---|---|
| `ButtonShortClick` | InputHandler | main (cambia escena) |
| `ButtonLongPress` | InputHandler | main (acción feed) |
| `ButtonDoubleClick` | InputHandler | (reservado, Fase 2) |
| `StatsTick` | main (timer 1Hz) | StatsEngine (decay) |
| `StatsChanged` | main tras tick/acción | DisplayRenderer (refresca cache de stats) |
| `LifeStateChanged` | (no usado en Fase 1, reservado) | — |
| `PersistRequested` | main (timer 5min + on-action) | main (llama Persistence::save) |
| `Ota*` | OTAManager | DisplayRenderer (overlay), main (log) |

### 6.3. API uso típico

```cpp
EventBus bus;
bus.subscribe(EventType::ButtonShortClick, [](const Event& e){
    // ...
});
// no es lambda, function pointer:
static void onShort(const Event& e) { /* ... */ }
bus.subscribe(EventType::ButtonShortClick, onShort);
bus.publish({EventType::ButtonShortClick, 0});
```

**Las lambdas que capturan no son convertibles a function pointer** — solo lambdas sin capturas funcionan. Esto se documenta en el header.

---

## 7. Loop principal refinado de `main.cpp`

Pseudo-código completo. El `.cpp` real lo escribe backend-expert respetando esta orquestación.

```cpp
// ============================================================================
// Tamagochi — Fase 1: Core jugable offline
// ============================================================================
#include <Arduino.h>
#include <U8g2lib.h>
#include "config.h"
#include "secrets.h"
#include "Logger.h"
#include "EventBus.h"
#include "StatsEngine.h"
#include "Persistence.h"
#include "OTAManager.h"
#include "InputHandler.h"
#include "DisplayRenderer.h"

// Hardware
static U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, OLED_SCL, OLED_SDA, U8X8_PIN_NONE);

// Singletons (orden de construcción importa por las referencias)
static EventBus        bus;
static StatsEngine     statsEngine;
static LittleFsStorage storage;
static Persistence     persistence(storage);
static OTAManager      ota(bus, WIFI_SSID, WIFI_PASSWORD, OTA_HOSTNAME, OTA_PASSWORD);
static InputHandler    input(bus, PIN_BUTTON);
static DisplayRenderer renderer(u8g2, bus);

// Schedulers
static uint32_t lastStatsTickMs   = 0;
static uint32_t lastRenderMs      = 0;
static uint32_t lastSaveMs        = 0;
static uint32_t lastHeapCheckMs   = 0;
static Scene    currentScene      = Scene::IdleMain;

// ----------- Event handlers -----------
static void onShortClick(const Event&) {
    currentScene = (currentScene == Scene::IdleMain) ? Scene::StatsView : Scene::IdleMain;
    renderer.setScene(currentScene);
    LOG_INFO("Scene -> %s", currentScene == Scene::IdleMain ? "IdleMain" : "StatsView");
}

static void onLongPress(const Event&) {
    statsEngine.applyFeed(20);                 // -20 hunger
    bus.publish({EventType::StatsChanged, 0});
    bus.publish({EventType::PersistRequested, 0});
    LOG_INFO("Feed +20");
}

static void onStatsChanged(const Event&) {
    renderer.setStats(statsEngine.stats());
}

static void onPersistRequested(const Event&) {
    PersistedState s;
    fillFromEngine(s, statsEngine);            // helper en main, lee engine y rellena s
    if (!persistence.save(s)) {
        LOG_ERROR("Persist save failed");
    } else {
        LOG_DEBUG("Persist saved");
    }
}

// ----------- setup -----------
void setup() {
    Serial.begin(115200);
    LOG_INFO("Boot Fase 1, version=%u reset_reason=%s", FIRMWARE_VERSION, ESP.getResetReason().c_str());

    // OLED primero — pantalla de boot visible cuanto antes
    if (!u8g2.begin()) LOG_ERROR("OLED init failed");
    renderer.begin();
    renderer.setScene(Scene::IdleMain);

    // LittleFS + load
    if (!LittleFS.begin()) {
        LOG_ERROR("LittleFS begin failed — formato necesario o flash corrupta");
    }
    PersistedState loaded;
    LoadResult lr = persistence.load(loaded);
    LOG_INFO("Persist load: %s", loadResultName(lr));
    applyToEngine(loaded, statsEngine);

    // EventBus wiring
    bus.subscribe(EventType::ButtonShortClick,    onShortClick);
    bus.subscribe(EventType::ButtonLongPress,     onLongPress);
    bus.subscribe(EventType::StatsChanged,        onStatsChanged);
    bus.subscribe(EventType::PersistRequested,    onPersistRequested);

    // Input
    input.begin();

    // OTA (no bloqueante; offline graceful)
    if (ota.begin(WIFI_TIMEOUT_MS)) {
        LOG_INFO("OTA ready: %s", OTA_HOSTNAME);
    } else {
        LOG_WARN("OTA inactivo — WiFi no conectó, Tamagochi offline-only");
    }

    uint32_t now = millis();
    lastStatsTickMs = lastRenderMs = lastSaveMs = lastHeapCheckMs = now;

    LOG_INFO("Setup done. heap=%u", ESP.getFreeHeap());
}

// ----------- loop -----------
void loop() {
    input.loop();
    ota.loop();

    uint32_t now = millis();

    // Decay a 1 Hz
    if (now - lastStatsTickMs >= STATS_TICK_MS) {
        statsEngine.tick(now - lastStatsTickMs);
        lastStatsTickMs = now;
        if (statsEngine.consumeDirtyFlag()) {
            bus.publish({EventType::StatsChanged, 0});
        }
    }

    // Render a 5 FPS
    if (now - lastRenderMs >= RENDER_FRAME_MS) {
        renderer.render();
        lastRenderMs += RENDER_FRAME_MS;     // drift-free
    }

    // Persist autosave cada 5 min
    if (now - lastSaveMs >= PERSIST_AUTOSAVE_MS) {
        bus.publish({EventType::PersistRequested, 0});
        lastSaveMs = now;
    }

    // Heap watchdog cada 30s
    if (now - lastHeapCheckMs >= HEAP_CHECK_MS) {
        uint32_t heap = ESP.getFreeHeap();
        LOG_DEBUG("heap=%u", heap);
        if (heap < HEAP_PANIC_THRESHOLD) {
            LOG_ERROR("Heap crítico %u → reset planificado", heap);
            bus.publish({EventType::PersistRequested, 0});
            delay(100);
            ESP.restart();
        }
        lastHeapCheckMs = now;
    }

    yield();
}
```

### 7.1. Timings (en `config.h`)

```cpp
#ifdef TURBO_MODE
  #define STATS_TICK_MS        16     // ~60× turbo
#else
  #define STATS_TICK_MS        1000
#endif
#define RENDER_FRAME_MS        200    // 5 FPS (igual que Fase 0)
#define ANIM_FRAME_MS          400
#define FPS_REPORT_MS          5000
#define PERSIST_AUTOSAVE_MS    (5UL * 60UL * 1000UL)   // 5 min
#define HEAP_CHECK_MS          30000
#define HEAP_PANIC_THRESHOLD   8000   // bytes
#define WIFI_TIMEOUT_MS        15000
#define FIRMWARE_VERSION       0x0100  // 1.0 — usado en LOG_INFO de boot
```

---

## 8. ArduinoOTA en Fase 1

### 8.1. Setup

```cpp
bool OTAManager::begin(uint32_t wifiTimeoutMs) {
    WiFi.persistent(false);     // no escribe SSID en flash en cada conexión
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid_, wifiPassword_);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < wifiTimeoutMs) {
        delay(100);
        yield();
    }
    if (WiFi.status() != WL_CONNECTED) {
        LOG_WARN("WiFi timeout %ums — OTA deshabilitado", wifiTimeoutMs);
        WiFi.mode(WIFI_OFF);    // modem off para ahorrar mA
        return false;
    }

    LOG_INFO("WiFi conectado, IP=%s RSSI=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());

    ArduinoOTA.setHostname(otaHostname_);
    ArduinoOTA.setPassword(otaPassword_);   // REQUERIDO — sin password no se arranca
    registerOtaCallbacks_();
    ArduinoOTA.begin();
    otaActive_ = true;
    return true;
}
```

- **Password obligatoria** — si `otaPassword_` es `nullptr` o cadena vacía, abortar con `LOG_ERROR` y devolver false (cumple `rules/security.md`).
- Hostname configurable vía `OTA_HOSTNAME` en `secrets.h`; recomendado `"tamagochi"` → resuelve como `tamagochi.local` con mDNS.
- `WiFi.mode(WIFI_OFF)` cuando falla evita gasto de batería innecesario.

### 8.2. `secrets.h.example` (versionado)

```cpp
#pragma once

// Copia este archivo a "secrets.h" (mismo directorio) y rellena los valores.
// secrets.h está en .gitignore — NO se sube nunca al repo.

#define WIFI_SSID       "tu-ssid-aqui"
#define WIFI_PASSWORD   "tu-password-aqui"

// OTA — password obligatoria, mínimo 12 caracteres.
#define OTA_HOSTNAME    "tamagochi"
#define OTA_PASSWORD    "cambia-esta-password-larga"
```

Si `secrets.h` no existe, en `main.cpp` (o en `OTAManager.h`):

```cpp
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #error "Falta include/secrets.h — copia desde secrets.h.example y rellena credenciales"
#endif
```

### 8.3. Comportamiento si WiFi falla

- `LOG_WARN` claro en serie
- OLED muestra discretamente "Offline" en una esquina (8 px) — frontend del designer no lo cubre todavía, basta con texto crudo en Fase 1
- Loop principal sigue funcionando: stats decay, persistencia, botón
- En la próxima Fase 1.5 se añade reintento periódico

---

## 9. Tests host-native con `pio test -e native`

### 9.1. Configuración `platformio.ini`

Añadir al final del archivo (sin modificar `[env:esp12e]`):

```ini
[env:native]
platform = native
test_framework = unity
build_flags =
    -std=c++17
    -DUNIT_TEST
    -DLOG_LEVEL=0
build_src_filter =
    +<*>
    -<main.cpp>            ; main.cpp depende de Arduino — excluir
    -<../lib/infra/OTAManager/>
    -<../lib/ui/InputHandler/>
    -<../lib/ui/DisplayRenderer/>
    -<../lib/domain/Persistence/LittleFsStorage.cpp>
lib_compat_mode = off
lib_deps =
    ; Unity ya viene con PlatformIO. No librerías embedded.
```

**Justificación del `build_src_filter`:** los módulos que dependen de Arduino, U8g2, ArduinoOTA o LittleFS se **excluyen** del compile nativo. Solo se compila:
- `lib/core/EventBus/`
- `lib/core/Logger/` (header-only, no aporta TU)
- `lib/domain/StatsEngine/`
- `lib/domain/Persistence/Persistence.cpp` (NO `LittleFsStorage.cpp`)
- `test/test_*`

### 9.2. Lista de tests

#### `test/test_stats_engine/test_stats_engine.cpp`

```
test_default_stats_are_sane
test_tick_zero_dt_does_nothing
test_tick_one_second_applies_decay
test_tick_clamps_to_zero
test_tick_huge_dt_clamps_to_24h
test_feed_reduces_hunger
test_feed_clamps_at_zero
test_play_increases_happiness
test_play_clamps_at_100
test_heal_increases_health
test_health_decays_only_when_hungry_or_sad
test_dirty_flag_set_after_tick
test_dirty_flag_consumed
test_dirty_flag_not_set_if_no_integer_change
```

#### `test/test_persistence/test_persistence.cpp`

```
test_crc32_roundtrip
test_crc32_changes_on_byte_flip
test_fill_defaults_produces_valid_state
test_save_then_load_roundtrip
test_load_when_no_files_returns_none_found
test_load_when_all_files_corrupted_returns_all_corrupt
test_load_with_wrong_magic_skips_file
test_load_with_wrong_crc_skips_file
test_load_picks_newest_by_lastSavedAt
test_load_picks_newest_by_ageMinutes_when_epoch_zero
test_save_rotation_writes_to_next_slot
test_save_rotation_removes_oldest_after_four_writes
test_save_does_not_lose_old_state_on_simulated_power_loss
test_migrate_same_version_returns_true
test_migrate_higher_version_returns_false
test_load_unknown_version_returns_migration_failed
test_persistedstate_is_exactly_28_bytes
```

### 9.3. Estrategia de mocks

- `Persistence` recibe `IStorage&` por constructor → en tests se pasa `InMemoryStorage`.
- `StatsEngine` no necesita mock — es lógica pura.
- Para simular paso de tiempo, llamar `tick(dt)` con `dt` controlado por el test, sin depender de `millis()`.
- Tests **rápidos** (<1 s todos) — el endurance está en hardware, no aquí.

### 9.4. Cómo correr

```bash
pio test -e native                     # corre todos los tests host
pio test -e native -f test_stats*      # solo StatsEngine
pio test -e native -v                  # verbose, ver assertions
```

CI/CD podrá engancharse a este target en el futuro.

---

## 10. Plan de verificación de Fase 1

### 10.1. Endurance 24h

**Setup:**
- Flashear firmware Fase 1 final
- `secrets.h` con WiFi real (para que OTA esté activo y se pueda monitorizar)
- Dejar conectado por USB con `pio device monitor > endurance.log` capturando salida 24h

**Métricas a comprobar al final:**
- Sin reinicios espontáneos (ningún `[BOOT]` extra en el log)
- Heap mínimo > 8 KB (`LOG_DEBUG heap=...` debe mantenerse estable)
- FPS estable ~5
- Stats han decaído y se han persistido (verificar mirando timestamp del último save)
- Sin mensajes `[ERROR]` salvo los explicables

### 10.2. Power cycle 10×

**Setup:**
- Tamagochi en estado intermedio (stats no defaults)
- 10 veces: desconectar USB, esperar 5 s, reconectar, anotar stats al boot

**Métricas:**
- Las stats post-boot coinciden con las pre-power-off (margen +/- 1 punto por el tick que pudiera haber pasado)
- `LoadResult` siempre `Ok` (en el log)
- `deathCount` no se incrementa indebidamente
- LittleFS no se corrompe (`/state_*.bin` siguen leyéndose)

### 10.3. Modo TURBO_MODE

Flag de compilación, **nunca runtime**:

```bash
pio run -e esp12e_turbo -t upload
```

Para añadirlo, crear un env adicional en `platformio.ini` que herede de `esp12e`:

```ini
[env:esp12e_turbo]
extends = env:esp12e
build_flags =
    ${env:esp12e.build_flags}
    -DTURBO_MODE
```

Con `TURBO_MODE`, `STATS_TICK_MS` baja de 1000 a 16 (~60× más rápido), permitiendo validar decay completo en minutos en vez de horas. Útil para iterar en hardware sin esperar.

### 10.4. OTA en LAN

- Conectar el ESP a la WiFi del entorno de desarrollo
- `arp -a | grep tamagochi` o `ping tamagochi.local` para confirmar mDNS
- `pio run -e esp12e -t upload --upload-port tamagochi.local` (o IP) — debe pedir password OTA
- Verificar que el firmware nuevo arranca y carga el estado persistido

---

## 11. Hitos / commits atómicos sugeridos

Cada commit debe ser **<500 líneas** y pasar `pio run -e esp12e` (compila) tras aplicarlo. Los commits 2-4 deben además pasar `pio test -e native`.

| # | Tipo | Título sugerido | Archivos clave |
|---|---|---|---|
| 1 | Feature | `Feature: Logger y EventBus mínimos para Fase 1` | `lib/core/Logger/Logger.h`, `lib/core/EventBus/*` |
| 2 | Test | `Test: env native con Unity para tests host` | `platformio.ini` (añadir `[env:native]`) |
| 3 | Feature + Test | `Feature: Persistence LittleFS rotativo con CRC32 + tests host` | `lib/domain/Persistence/*`, `test/test_persistence/*` |
| 4 | Feature + Test | `Feature: StatsEngine con decay y acciones + tests host` | `lib/domain/StatsEngine/*`, `test/test_stats_engine/*` |
| 5 | Refactor | `Refactor: extraer DisplayRenderer e InputHandler de main.cpp` | `lib/ui/DisplayRenderer/*`, `lib/ui/InputHandler/*`, `src/main.cpp` |
| 6 | Feature | `Feature: EventBus integrado en main, InputHandler emite eventos` | `src/main.cpp` (suscripciones) |
| 7 | Feature | `Feature: Persistence integrada en main (load al boot, save 5min + on-action)` | `src/main.cpp`, `lib/domain/Persistence/LittleFsStorage.cpp` |
| 8 | Feature + Security | `Feature: WiFi STA + ArduinoOTA con password y secrets.h gitignored` | `lib/infra/OTAManager/*`, `include/secrets.h.example`, `.gitignore` |
| 9 | Docs + Test | `Docs: actualizar README con Fase 1 + endurance instructions` | `README.md`, posibles fixes menores |

**Orden crítico:** los commits 3-4 dependen del 1-2. El 5 no depende de persistencia. El 7 depende del 3. El 8 puede ir en paralelo al 5-7 si el ejecutor quiere, pero conviene dejarlo al final para no mezclar credenciales en commits inestables.

---

## 12. Riesgos específicos de Fase 1

| # | Riesgo | Mitigación |
|---|---|---|
| 1 | **RAM**: Fase 0 ya usa 37% (~30 KB). U8g2 full buffer = 1 KB. WiFi + OTA + AsyncTCP suman ~10-15 KB de heap fragmentable. | Monitorear con `ESP.getFreeHeap()` en `LOG_DEBUG` cada 30 s. Si baja de 8 KB, persist + restart. Sin AsyncWebServer aún (eso es Fase 3), debería sobrar margen. |
| 2 | **EventBus síncrono**: un handler lento bloquea el publish | Documentar en header. Handlers deben ser <2 ms. Persist se invoca dentro del handler, no en línea (5 min entre saves es aceptable). |
| 3 | **OneButton + InputHandler con static instance_** | Estándar en embedded. Solo un InputHandler vivo. Asserted en constructor (`if (instance_) panic()`). |
| 4 | **GPIO0 (FLASH) y reset boot**: si se mantiene presionado durante boot, entra en flash mode | OneButton ya lo gestiona en Fase 0. No tocar configuración. Documentar en README "no mantener pulsado el botón al encender". |
| 5 | **CRC32 sin tabla** = ~250 ciclos por byte | 28 bytes → <50 µs. Aceptable. Si más tarde se persisten payloads grandes, considerar tabla 256-byte (256 bytes Flash, 4× más rápido). |
| 6 | **LittleFS no formateado al primer flash** | `LittleFS.begin()` formatea automáticamente si está vacío. Verificar logueando `LittleFS.totalBytes()` al boot. |
| 7 | **WiFi.persistent(false)** crítico — sin esto, cada `WiFi.begin()` reescribe flash | Aplicado en `OTAManager::begin`. Comentado en código. |
| 8 | **ArduinoOTA password mínima**: sin password, ESP-12E flasheable por cualquiera en la LAN | `OTA_PASSWORD` obligatoria, validada al construir OTAManager. En PR review verificar que no está vacía ni es "1234". |
| 9 | **`secrets.h` se commitea por error** | `.gitignore` lo cubre. `pre-commit` opcional para grep `WIFI_PASSWORD`. Si pasa, **rotar credenciales WiFi y OTA inmediatamente** (cumple `rules/security.md`). |
| 10 | **Tests host-native fallan por includes de Arduino en headers** | Cada header de dominio NO incluye `<Arduino.h>`. Solo `<stdint.h>`, `<stddef.h>`, `<string.h>`. Si necesita logging usa `LOG_*` (que se vuelve no-op en native). |
| 11 | **Endurance: leak progresivo de heap** | Monitor `ESP.getMaxFreeBlockSize()` además de `getFreeHeap()`. Fragmentación es el enemigo real. |
| 12 | **NTP no sincronizado en Fase 1** → `bornAtEpoch=0`, `lastSavedAtEpoch=0` | Aceptado en Fase 1. Persistence usa `ageMinutes` como desempate. Fase 1.5 introduce `configTime()`. |
| 13 | **U8g2 SW I2C es lento** (~50ms/frame buffer flush) | Verificado en Fase 0 que 5 FPS es alcanzable. No subir el target. Si Fase 3 (Web) tira los FPS abajo, mover a page buffer. |
| 14 | **TURBO_MODE acelera todo, incluido el persist autosave** | `PERSIST_AUTOSAVE_MS` también se escala si se desea, o se deja en 5 min wall-clock. **Decisión: dejar en wall-clock** para no machacar flash innecesariamente al testear con turbo. |

---

## 13. Checklist final para backend-expert

Antes de abrir PR (lo coordina QA + CI/CD):

- [ ] Todos los commits respetan el formato `Prefix: descripción`
- [ ] Ningún commit supera 500 líneas
- [ ] `pio run -e esp12e` compila limpio
- [ ] `pio run -e esp12e_turbo` compila limpio
- [ ] `pio test -e native` pasa al 100%
- [ ] No hay `LOG_DEBUG` ruidoso en logs por defecto (level=3 INFO en producción)
- [ ] `include/secrets.h` NO está en el repo (verificar `git ls-files | grep secrets.h`)
- [ ] `include/secrets.h.example` SÍ está versionado
- [ ] README actualizado con instrucciones de Fase 1 + nota sobre `secrets.h`
- [ ] Test de endurance 24h ejecutado y logueado
- [ ] 10× power cycle ejecutado
- [ ] OTA probado al menos 1 vez exitosa en LAN
- [ ] `static_assert(sizeof(PersistedState) == 28)` presente en header
- [ ] Ningún `#include <Arduino.h>` en headers de `lib/domain/`

Si **alguna** marca queda sin cumplir, no se mergea. QA lo bloqueará.

---

## 14. Lo que NO entra en Fase 1 (anti-scope creep)

- WiFiManager con portal cautivo → Fase 1.5
- `configTime()` / NTP → Fase 1.5
- 5 etapas de vida (KID, ADULT, OLD, DEAD) → Fase 2
- `discipline`, `weight`, `poop`, `sick`, `sleeping` → Fase 2
- `LifeFSM` → Fase 2 (en Fase 1 `lifeStage` se persiste pero está fijo en Baby)
- Sprites BABY del designer → Fase 2 (Fase 1 reusa los dummy de Fase 0)
- AsyncWebServer + REST API → Fase 3
- HTTP Basic Auth, rate limiting → Fase 3
- PowerManager (auto-dim, sleep) → Fase 4
- OTA signed updates → Fase 4

Si surge la tentación de "ya que estoy meto esto", **no**. Cada cosa en su fase. La modularidad de capas core/domain/infra/ui permite añadir sin reescribir.

---

## 15. Coordinación con otros agentes

- **frontend-expert (designer):** ya entregó `docs/sprites-plan.md` y `tools/png_to_xbm.py`. **No se le requiere en Fase 1.** Sus sprites BABY entran en Fase 2.
- **qa:** invocado tras los commits 3, 4, 7 (con tests) y al final para endurance + power cycles.
- **cicd:** invocado solo al final para merge `--no-ff` a `main` y posible tag `v0.2.0-fase1`.
- **product-manager (Atlas):** no requiere intervención salvo cambio de scope.

---

Fin del plan.
