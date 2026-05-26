# Fase 2 — Tamagochi clásico completo (Plan de implementación)

> **Autor:** SuperDev (arquitectura)
> **Ejecutor:** backend-expert
> **Branch:** `feature/fase-2` (ya creada desde `main` post-Fase 1)
> **Estimación:** 4-5 sesiones de desarrollo
> **Pre-requisitos:** Fase 1 mergeada en `main` (commit `75e3dd9`) y verificada en hardware

Este documento es la **fuente única de verdad** para implementar Fase 2. backend-expert debe seguirlo de arriba abajo, en el orden de commits propuesto en la sección 11. Cualquier desviación debe consultarse con Maestro antes de ejecutarse.

---

## 0. Resumen ejecutivo

Fase 1 dejó el firmware en "**Tamagochi mínimo jugable**" con un único `BABY` animado, 3 stats con decay, persistencia rotativa y EventBus. Fase 2 lo convierte en un **Tamagochi clásico de los 90**:

- **5 etapas de vida**: EGG → BABY → KID → ADULT → OLD, con DEAD como sexto estado terminal
- **6 stats**: hunger, happiness, health (Fase 1) **+** discipline, weight, attentionScore
- **3 flags de condición**: isSick, isSleeping, poopCount (entero, no flag)
- **Acciones nuevas**: clean, medicate, discipline, sleep on/off (encima de feed)
- **Lógica de evolución** dirigida por `attentionScore` — el cuidado decide qué tipo de adulto sale
- **Muerte y resurrección automática** — health=0 sostenido durante N min → DEAD → tras Y min de wall-clock vuelve a EGG
- **Animaciones de 2-3 frames** por etapa y por mood (idle, happy, sad, eat)
- **Overlays asíncronos**: SICK, POOP, SLEEPING_Z, HUNGRY se renderizan encima del sprite base sin interrumpir su animación
- **TURBO_MODE x60-120**: flag de compilación que comprime un ciclo de vida completo en 1-2 horas reales para validación
- **Schema de persistencia v2**: 12 bytes nuevos, `migrate v1→v2` mantiene los huevos de Fase 1 vivos

Verificación de aceptación: ciclo completo vida-muerte ejecutado en una tarde con turbo; muerte provocada por hambre **y** por enfermedad; sprites nítidos a 5 FPS; **65+ tests host pasando** (32 actuales + ~30-35 nuevos).

---

## 1. Validaciones previas y decisiones críticas

Estas decisiones cierran ambigüedades del plan maestro **antes** de que backend-expert escriba la primera línea. Si alguna se quiere revisar, hacerlo ahora — cambiarlas a mitad de Fase 2 cuesta refactor cruzado.

### 1.1. Estrategia de sprites faltantes (EGG / KID / ADULT / OLD / DEAD)

**Decisión: no bloquear. Implementar con `kSpriteBabyIdle0/1` como placeholder global.**

| Aspecto | Política |
|---|---|
| ¿Bloqueamos código hasta que Guillermo entregue sprites? | **NO** |
| Sprites placeholder | `kSpriteBabyIdle0/1` para todas las etapas no diseñadas, `kSpriteBabyHappy*` para mood happy, etc. |
| Cómo se conecta etapa → sprite | Tabla `kSpritesByStage[LifeStage][MoodFrame]` en `DisplayRenderer.cpp` — sustituir cada entrada cuando llegue el asset definitivo |
| Visualmente, ¿se distingue una etapa de otra? | **No, hasta tener sprites reales.** Para el playtest interno hay un indicador textual en `StatsView` ("Stage: KID") y un overlay numérico de 1px en esquina inferior derecha (mock-stage indicator, ifdef-able) |
| Commits de sprites reales | Van en commits separados (`Assets: sprites EGG generados`, `Assets: sprites KID generados`...) — intercalados cuando Guillermo los entregue, **no bloqueantes** del resto del plan |
| Pipeline | Igual que Fase 1: PNG generado vía Nano Banana → `tools/slice_sheet.py` → `tools/png_to_xbm.py --invert` → `include/sprites_generated.h` regenerado |

**Justificación:** la lógica de Fase 2 (LifeFSM, decay extendido, overlays, muerte) es **independiente** del aspecto visual de cada etapa. Bloquear el código por assets ralentiza sin ganancia. El sprite BABY como placeholder mantiene la pantalla siempre algo coherente.

**Convención de nombres** para los sprites cuando lleguen (Guillermo la respeta en el pipeline):

| Etapa | Sprites mínimos | Nombres en `kSprites*` |
|---|---|---|
| EGG | idle 0/1 | `kSpriteEggIdle0`, `kSpriteEggIdle1` |
| KID | idle 0/1, happy 0/1/2, sad 0/1, eat 0/1/2 | `kSpriteKidIdle0`, ... (mismo patrón que BABY) |
| ADULT | idle 0/1, happy 0/1/2, sad 0/1, eat 0/1/2 | `kSpriteAdultIdle0`, ... |
| OLD | idle 0/1, happy 0/1, sad 0/1, eat 0/1/2 | `kSpriteOldIdle0`, ... |
| DEAD | idle 0 (lápida) | `kSpriteDeadIdle0` |
| **Total mínimo** | **~40 sprites nuevos** | ~5 KB de Flash |

**Overlays ya disponibles** (Fase 1): `kSpriteOverlayHungry0/1`, `kSpriteOverlayPoop`, `kSpriteOverlaySick`, `kSpriteOverlaySleepZ0/1`. Se reusan tal cual.

### 1.2. TURBO_MODE — flag de compilación, jamás runtime

**Decisión: flag de compilación `-D TURBO_MODE` que multiplica el ritmo de decay por ~60-120. Nunca activable en runtime. Nunca se mergea a `main` con turbo activo.**

| Aspecto | Política |
|---|---|
| Activación | Compilando contra el env `[env:esp12e_turbo]` (NO el env `esp12e` de producción) |
| Mecanismo en código | `#ifdef TURBO_MODE` en `config.h` y `LifeFSM.cpp` — escala `STATS_TICK_MS` (de 1000 a 16) y los **umbrales temporales** de la FSM (no escala `PERSIST_AUTOSAVE_MS` para no machacar flash) |
| Factor | Por defecto **60×** (1 hora de juego real = 1 minuto de turbo). El factor es `#define TURBO_FACTOR 60` ajustable a 120 si se quiere comprimir más |
| Guard nº1 — banner en pantalla | Si `TURBO_MODE` activo, banner permanente "TURBO" en `StatsView` (4-6px arriba a la derecha). Imposible olvidarse |
| Guard nº2 — log al boot | `LOG_WARN("=== TURBO_MODE ACTIVE — factor %u — NO PROD ===", TURBO_FACTOR)` en cada arranque, también en INFO=3 por defecto |
| Guard nº3 — `FIRMWARE_VERSION` | Con turbo activo, `FIRMWARE_VERSION` cambia a `0x02FE` (sufijo `FE` = "feature-eval") en vez de `0x0200`. Distingue binarios en cualquier inspección |
| Guard nº4 — CI/QA gate | QA verifica que el binario que se mergea a `main` se construyó con `pio run -e esp12e` (no `esp12e_turbo`) inspeccionando el log de build. Documentado en sección 13 |
| Wall-clock vs. simulación | Los timers de UI (`RENDER_FRAME_MS`, `ANIM_FRAME_MS`, `PERSIST_AUTOSAVE_MS`) NUNCA se escalan. Solo el **tiempo subjetivo del Tamagochi** (decay + transiciones LifeFSM) |

**Por qué no runtime:** un flag persistido que active turbo en producción es exactamente lo que termina haciendo que un usuario reciba un Tamagochi con vida de 6 minutos. Un flag de compilación es **imposible de activar accidentalmente** desde la app/UI.

### 1.3. Política de muerte y resurrección

**Decisión: muerte es automática tras umbral sostenido; resurrección es automática tras Y minutos de wall-clock; sin interacción del usuario en ninguno de los dos casos.**

| Aspecto | Política |
|---|---|
| ¿Cuándo se entra en DEAD? | `health == 0` sostenido durante `kDeathSustainMs = 60 000 ms` (1 min wall-clock, 1 s con turbo×60). Se evita una muerte instantánea si el usuario justo abre la pantalla con stats al límite |
| Causa de muerte loguada | Se persiste `lastDeathCauseCode` en `PersistedState` v2: `0=none, 1=hunger, 2=sickness, 3=oldAge, 4=neglect, 5=unknown`. Usado por DisplayRenderer para mostrar mensaje en pantalla DEAD |
| ¿Qué se ve en pantalla? | Sprite `kSpriteDeadIdle0` (lápida o ánima — Guillermo decide diseño) + texto "RIP — causa" abajo |
| ¿Qué pasa con los stats al morir? | Se **congelan** (decay paralizado por el LifeFSM cuando está en DEAD). Quedan los últimos valores como "epitafio" |
| ¿Cuándo resurrecta? | Automáticamente tras `kResurrectionMs = 5 * 60 * 1000 ms` (5 min wall-clock; **no se escala con turbo**, para que el playtester pueda ver la pantalla DEAD aunque sea brevemente). Tras `kResurrectionMs` el LifeFSM transiciona DEAD → EGG y resetea stats a defaults |
| ¿Hay opción manual? | **Sí, secundaria**: long-press en pantalla DEAD acelera resurrección a 5 s. **No** se documenta como feature principal — solo "easter egg" para devs |
| Contador de muertes | `deathCount` se incrementa en cada transición → DEAD. Se persiste y se muestra en `StatsView` ("RIPs: 3") |

**Justificación de los umbrales:**
- 1 min sostenido evita ruido por "el usuario casi tuvo tiempo de alimentar". 5 min de DEAD da margen para apreciar la muerte sin frustrar al jugador eternamente.
- Resurrección automática elimina el caso "Tamagochi muerto durante meses porque el dueño no sabe cómo revivirlo".

### 1.4. Persistence schema migration v1 → v2

**Decisión: añadir 12 bytes nuevos en bloque al final de la zona payload (antes del CRC). `migrate_v1_to_v2()` rellena con defaults coherentes. Magic se mantiene `0xCAFE`. `version` salta de 1 a 2.**

Detalle exhaustivo en sección 3. **Premisa de diseño:** ningún usuario (excepto Guillermo) tiene firmware de Fase 1 desplegado, así que migración fallida no es catastrófica — pero la queremos correcta porque sienta el precedente para futuras versiones.

### 1.5. Naming convention (heredado de Fase 1)

Sin cambios respecto a Fase 1. Resumen de lo que aplica especialmente en Fase 2:

| Elemento | Convención | Ejemplo Fase 2 |
|---|---|---|
| Carpetas de módulos en `lib/` | PascalCase | `lib/domain/LifeFSM/` |
| Enums | PascalCase con `enum class` (o `enum : uint8_t` si necesita ser POD para persistir) | `enum LifeStage : uint8_t { ... }` |
| Constantes C++ (`constexpr`) | `k` + PascalCase | `constexpr uint32_t kDeathSustainMs = 60000;` |
| Tests | `test/test_<modulo>/test_<modulo>.cpp` | `test/test_life_fsm/test_life_fsm.cpp` |

Reglas globales aplicables sin excepción: `rules/code-style.md`, `rules/error-handling.md`, `rules/security.md`, `rules/testing.md`, `rules/git-workflow.md`.

### 1.6. Política de errores y logging (heredado)

- **Nunca swallow errors**: cualquier fallo de migración v1→v2, transición FSM imposible, sprite no encontrado, CRC mismatch tras roundtrip se loguea con `LOG_WARN` o `LOG_ERROR`.
- **Fail fast**: `applyClean()`, `applyMedicate()`, etc. validan input al inicio y rechazan amounts fuera de rango (1-100).
- **Nunca loguear PII** (cumple `rules/security.md`) — pero Fase 2 no introduce nueva PII. Sin secrets nuevos.

### 1.7. Política de testing (heredado de Fase 1)

Tests **obligatorios** para mergear Fase 2:

- Migración `v1→v2`: roundtrip con datos sintéticos, asserts campo a campo.
- StatsEngine extendido: cada nueva acción (clean, medicate, discipline, sleep), cada nuevo decay (weight, discipline, poop spawn rate), cada regla condicional (sickness eleva decay de health, sleep paraliza decay de hunger).
- LifeFSM: cada transición esperada y cada transición **prohibida** (no debe poder ir BABY → ADULT saltándose KID).
- Edge cases: health=0 durante 30 s (NO mata), health=0 durante 70 s (sí mata con turbo desactivado en tests).

Framework: Unity (igual que Fase 1).

---

## 2. Árbol completo de archivos

### 2.1. Nuevos

```
Tamagochi/
├── include/
│   ├── config.h                          [MODIFICAR] añadir TURBO_FACTOR, kDeathSustainMs, kResurrectionMs, umbrales LifeFSM
│   └── sprites_generated.h               [REGENERAR] cuando lleguen assets nuevos (no bloquea)
├── lib/
│   ├── domain/
│   │   ├── StatsEngine/                  [MODIFICAR] extender Stats, DecayRates, acciones
│   │   ├── Persistence/                  [MODIFICAR] schema v2 + migrate_v1_to_v2
│   │   └── LifeFSM/                      [NUEVO]
│   │       ├── LifeFSM.h
│   │       └── LifeFSM.cpp
│   └── ui/
│       └── DisplayRenderer/              [MODIFICAR] tabla sprites por etapa, overlays asíncronos
├── src/
│   └── main.cpp                          [MODIFICAR] cablear LifeFSM, suscribir a StatsChanged
├── test/
│   ├── test_stats_engine/                [MODIFICAR] añadir tests para los nuevos campos y acciones
│   ├── test_persistence/                 [MODIFICAR] añadir tests de migración v1→v2
│   └── test_life_fsm/                    [NUEVO]
│       └── test_life_fsm.cpp
├── platformio.ini                        [MODIFICAR] añadir [env:esp12e_turbo]
└── docs/
    └── fase-2-plan.md                    [este archivo]
```

### 2.2. Modificaciones detalladas

**`platformio.ini`** — añadir env `esp12e_turbo` que hereda de `esp12e` y añade `-DTURBO_MODE -DTURBO_FACTOR=60`. **Modificar `[env:native]` `build_src_filter`** para incluir `lib/domain/LifeFSM/` y añadir `-Ilib/domain/LifeFSM` a los `build_flags`.

**`include/config.h`** — añadir constantes nuevas sin tocar las existentes. Resumen al final de sección 4.

**`src/main.cpp`** — añadir construcción de `LifeFSM`, suscripción a `StatsChanged` y `StatsTick`, publicación de `LifeStateChanged` consumido por `DisplayRenderer`. Estimado: +50 líneas sobre los ~240 actuales.

**`lib/domain/StatsEngine/`** — extender `Stats`, `DecayRates`, y añadir 4 acciones nuevas. Pasa de ~120 líneas .cpp a ~250.

**`lib/domain/Persistence/Persistence.h`** — extender `PersistedState` a v2, `kCurrentVersion=2`, añadir constantes para los enum values de `LifeStage` y `DeathCause`.

**`lib/ui/DisplayRenderer/`** — añadir tabla de sprites por etapa, lógica de overlays, animaciones HAPPY/SAD/EAT, escena DEAD.

---

## 3. Modelo de datos extendido (Persistence v2)

### 3.1. Struct `PersistedState` v2

```cpp
struct PersistedState {
    // === Bloque heredado de v1 (idéntico layout) ===
    uint16_t magic;               // [0..2]   0xCAFE
    uint16_t version;             // [2..4]   2 en Fase 2
    uint32_t bornAtEpoch;         // [4..8]   0 hasta NTP (Fase 1.5)
    uint32_t lastSavedAtEpoch;    // [8..12]
    uint32_t ageMinutes;          // [12..16]
    uint8_t  lifeStage;           // [16]     LifeStage enum
    uint8_t  hunger;              // [17]
    uint8_t  happiness;           // [18]
    uint8_t  health;              // [19]
    uint16_t deathCount;          // [20..22]
    uint16_t reserved;            // [22..24] padding v1 — en v2 sigue siendo 0

    // === Bloque nuevo v2 (12 bytes) ===
    uint8_t  discipline;          // [24]     0-100, 50 default
    uint8_t  weight;              // [25]     0-100, 50 default (50 = sano)
    uint8_t  poopCount;           // [26]     0-9 visibles a la vez
    uint8_t  flags;               // [27]     bit0=isSick, bit1=isSleeping, bit2..7 reservados
    uint16_t attentionScore;      // [28..30] 0-1000, 500 default — métrica acumulada que dirige la evolución
    uint16_t deathSustainMs;      // [30..32] cuenta atrás de health=0 — persistido para no perderlo en power-loss
    uint8_t  lastDeathCauseCode;  // [32]     0=none, 1=hunger, 2=sickness, 3=oldAge, 4=neglect, 5=unknown
    uint8_t  adultType;           // [33]     0=normal, 1=athletic, 2=obese, 3=grumpy (poblado al evolucionar a Adult; default 0)
    uint16_t reservedV2b;         // [34..36] padding, debe ser 0

    // === CRC ===
    uint32_t crc32;               // [36..40] CRC32 sobre bytes [0..36]
};
static_assert(sizeof(PersistedState) == 40, "PersistedState v2 debe ser exactamente 40 bytes");
```

**Total: 40 bytes** (era 28 en v1). Cabe holgadamente en cualquier slot LittleFS.

### 3.2. Cómputo CRC32

Algoritmo idéntico al de Fase 1 (`computeCrc32`, polynomial 0xEDB88320, table-less). Cambia solo el rango cubierto: `crc32 = computeCrc32(&state, offsetof(PersistedState, crc32))` que ahora son 36 bytes en vez de 24.

### 3.3. Migrate v1 → v2

```cpp
bool Persistence::migrate(PersistedState& s) {
    if (s.version == kCurrentVersion) return true;
    if (s.version > kCurrentVersion) {
        LOG_ERROR("Persist: version %u > current %u — descartando", s.version, kCurrentVersion);
        return false;
    }
    if (s.version == 1) {
        return migrate_v1_to_v2_(s);
    }
    LOG_WARN("Persist: version desconocida %u — descartando", s.version);
    return false;
}

bool Persistence::migrate_v1_to_v2_(PersistedState& s) {
    // Premisa: los bytes [0..24] vienen ya rellenos por el read del slot v1.
    // Los bytes [24..40] están sin inicializar (basura del stack/buffer) — los reescribimos a defaults.
    s.discipline         = 50;
    s.weight             = 50;
    s.poopCount          = 0;
    s.flags              = 0;          // no sick, no sleeping
    s.attentionScore     = 500;        // neutro
    s.deathSustainMs     = 0;
    s.lastDeathCauseCode = 0;
    s.adultType          = 0;
    s.reservedV2b        = 0;
    s.version            = kCurrentVersion;
    // NB: el caller (load()) recalculará el CRC tras la migración antes del próximo save.
    LOG_INFO("Persist: migrado v1->v2 (defaults para campos nuevos)");
    return true;
}
```

**Punto crítico:** los bytes [24..40] que existen en el buffer al leer un slot v1 son **basura**. El read leyó solo 28 bytes pero el struct ocupa 40. Implementación correcta de `load()`:

```cpp
LoadResult Persistence::load(PersistedState& out) {
    memset(&out, 0, sizeof(out));   // crítico: garantiza que los bytes "extra" arranquen a 0
    // ... resto del algoritmo igual que Fase 1 ...
    // Cuando se lee un slot v1, solo se leen 28 bytes — los bytes [28..40] quedan a 0 por el memset.
}
```

Sin ese `memset`, los campos nuevos podrían tener basura aleatoria que luego pasa por `migrate_v1_to_v2_`. El memset garantiza una base limpia.

### 3.4. Política de tamaño de read

Al leer un slot, **determinar tamaño en función de la versión leída**:

```cpp
// Leer primero los 4 bytes de cabecera (magic + version) para saber qué tamaño cargar
struct Header { uint16_t magic; uint16_t version; };
Header hdr;
if (!storage_.read(path, &hdr, sizeof(hdr))) continue;
if (hdr.magic != kPersistMagic) continue;

size_t blobSize = (hdr.version == 1) ? 28
                : (hdr.version == 2) ? 40
                : 0;
if (blobSize == 0) continue;   // versión desconocida → skip

memset(&out, 0, sizeof(out));
if (!storage_.read(path, &out, blobSize)) continue;
```

**Simplificación aceptable** si `IStorage` no soporta offsets/partials: leer siempre con `sizeof(PersistedState)` y aceptar que las versiones antiguas dejen padding sin leer **mientras `IStorage::read` no falle si el archivo es más pequeño de lo pedido**. Política para Fase 2: `IStorage::read(path, buf, size)` devuelve `true` si lee al menos los bytes que existen en el archivo, rellenando con 0 el resto. **Actualizar `InMemoryStorage` y `LittleFsStorage`** para cumplir.

### 3.5. Enum `LifeStage`

```cpp
enum LifeStage : uint8_t {
    Egg   = 0,
    Baby  = 1,   // ojo: en Fase 1 Baby=1, se mantiene
    Kid   = 2,
    Adult = 3,
    Old   = 4,
    Dead  = 5
};
```

**Compatibilidad con Fase 1:** los blobs v1 tienen `lifeStage = 1` (Baby). Tras migración v1→v2, el valor se conserva (sigue siendo Baby). El usuario de Fase 1 entra en Fase 2 con su Baby intacto. **Esto es por diseño.**

### 3.6. Defaults de un huevo nuevo

`Persistence::fillDefaults` se modifica:

```cpp
void Persistence::fillDefaults(PersistedState& s) {
    memset(&s, 0, sizeof(s));
    s.magic            = kPersistMagic;
    s.version          = kCurrentVersion;
    s.lifeStage        = LifeStage::Egg;     // ← antes era Baby; ahora Egg para que la FSM corra el ciclo completo
    s.hunger           = 0;                  // huevos no comen
    s.happiness        = 100;                // huevos son felices
    s.health           = 100;
    s.discipline       = 50;
    s.weight           = 50;
    s.attentionScore   = 500;
    // resto a 0 (memset)
}
```

**Cambio respecto a Fase 1:** un huevo nuevo arranca como `Egg`, no `Baby`. Tras `kEggHatchMs` (definido en sección 5) eclosiona a `Baby`.

---

## 4. StatsEngine extendido

### 4.1. Struct `Stats` v2

```cpp
struct Stats {
    // Fase 1
    uint8_t hunger     = 50;
    uint8_t happiness  = 50;
    uint8_t health     = 100;

    // Fase 2
    uint8_t discipline = 50;   // 0 = indisciplinado, 100 = obediente
    uint8_t weight     = 50;   // 0 = anoréxico, 50 = sano, 100 = obeso
    uint8_t poopCount  = 0;    // # cacas visibles, 0-9
    bool    isSick     = false;
    bool    isSleeping = false;
    uint16_t attentionScore = 500;  // 0-1000, métrica de "qué tan bien lo cuidas"
};
```

### 4.2. Struct `DecayRates` v2

```cpp
struct DecayRates {
    // Fase 1
    float hungerPerSec    = 0.05f;
    float happinessPerSec = 0.03f;
    float healthPerSec    = 0.01f;   // solo cuando hunger>=90 o happiness<=10

    // Fase 2
    float weightDeltaPerFeed     = 1.0f;    // cada feed añade +1 peso
    float weightDecayPerSec      = 0.005f;  // peso baja lentamente por metabolismo
    float disciplinePerSec       = 0.01f;   // sin disciplinar baja
    float poopSpawnPerHungerEat  = 0.30f;   // 30% prob de spawn de caca por feed
    float sicknessProbPerPoop    = 0.005f;  // 0.5% por segundo y por caca presente
    float healthDecayWhenSickMul = 5.0f;    // si sick, health decay × 5
};
```

**Justificación de los ratios:**
- `weightDeltaPerFeed = 1` → un feed normal (~20 hunger) sube peso 1 punto. Engordar a 100 requiere ~50 feeds. Realista.
- `weightDecayPerSec = 0.005` → en 10 minutos baja 3 puntos sin acciones. Mantiene cierta presión hacia el equilibrio.
- `poopSpawnPerHungerEat` → probabilidad **al alimentar**, no continuo. Comer → quizá cacas. Las cacas se acumulan hasta `kMaxPoopCount = 9`.
- `sicknessProbPerPoop` → 0.5% por segundo y por caca; con 3 cacas son 1.5% por segundo = ~63% en 1 minuto de quedar enfermo. Suficiente presión.
- `healthDecayWhenSickMul = 5` → enfermar acelera muerte por health.

### 4.3. API pública nueva

```cpp
class StatsEngine {
public:
    // ... (todo lo de Fase 1 sigue) ...

    // Nuevas acciones — todas con clamp y dirty flag
    void applyClean();                  // resetea poopCount a 0
    void applyMedicate();               // cura isSick, health +20
    void applyDiscipline(uint8_t amt);  // discipline += amt
    void applySleep(bool on);           // setea isSleeping flag — paraliza decay de hunger/happiness/discipline

    // Pasivos — el LifeFSM o el main los llaman tras transiciones
    void resetForNewLife();             // huevo nuevo: stats a defaults v2 + isSick=false, isSleeping=false, etc.
    void onPoopTick();                  // candidato a generar enfermedad; llamado desde tick() si poopCount > 0
    void onFeedTriggered();             // candidato a generar caca; llamado desde applyFeed()
};
```

### 4.4. Lógica de tick extendida (pseudo)

```
tick(dtMs):
  if (isSleeping):
      // dormido: solo health regenera lentamente, nada más decae
      healthAcc += dtMs * 0.002f
      apply integer part of healthAcc to health
      return
  
  // Acumuladores fraccionarios (igual que Fase 1)
  hungerAcc      += dtMs / 1000.0f * rates.hungerPerSec
  happinessAcc   += dtMs / 1000.0f * rates.happinessPerSec
  weightAcc      -= dtMs / 1000.0f * rates.weightDecayPerSec      // weight BAJA por metabolismo
  disciplineAcc  -= dtMs / 1000.0f * rates.disciplinePerSec       // discipline BAJA
  
  // Health decay condicionado
  healthMultiplier = 1.0f
  if (isSick): healthMultiplier *= rates.healthDecayWhenSickMul
  if (hunger >= 90 || happiness <= 10 || weight <= 10 || weight >= 90):
      healthAcc += dtMs / 1000.0f * rates.healthPerSec * healthMultiplier
  
  // Aplicar enteros y clamp 0-100
  applyAccumulators_()
  
  // Enfermedad espontánea por cacas presentes
  if (poopCount > 0 && !isSick):
      probSick = poopCount * rates.sicknessProbPerPoop * (dtMs / 1000.0f)
      if (rand01() < probSick): isSick = true; dirty_ = true
  
  // AttentionScore se actualiza pasivamente: si hunger<70 && happiness>30 && health>50 → +1 cada 10 s.
  // Si hunger>=90 || happiness<=10 || isSick → -1 cada 10 s.
  // Clamp 0-1000.
  updateAttentionScore_(dtMs)
```

### 4.5. `applyFeed` ampliado

```cpp
void StatsEngine::applyFeed(uint8_t amount) {
    if (amount == 0) return;
    if (amount > 100) amount = 100;
    if (isSleeping) {
        LOG_DEBUG("applyFeed ignorado: tamagochi dormido");
        return;
    }
    stats_.hunger = clamp_(stats_.hunger - amount);
    stats_.weight = clamp_(stats_.weight + (amount > 10 ? 1 : 0));
    onFeedTriggered_();
    dirty_ = true;
}

void StatsEngine::onFeedTriggered_() {
    if (stats_.poopCount >= kMaxPoopCount) return;
    if (rand01() < rates_.poopSpawnPerHungerEat) {
        stats_.poopCount++;
        dirty_ = true;
    }
}
```

**Determinismo en tests:** `rand01()` se inyecta vía `setRandomSource(std::function<float()>)` o se sustituye en build time por `#ifdef UNIT_TEST` para usar una secuencia controlable. Decidir patrón al implementar — recomendación: función global `static float (*sRand01)() = &defaultRand01;` con setter.

### 4.6. `applyClean`, `applyMedicate`, `applyDiscipline`, `applySleep`

```cpp
void StatsEngine::applyClean() {
    if (stats_.poopCount == 0) return;
    stats_.poopCount = 0;
    dirty_ = true;
    LOG_INFO("Clean: poop=0");
}

void StatsEngine::applyMedicate() {
    if (!stats_.isSick) {
        LOG_DEBUG("Medicate ignorado: no estaba enfermo");
        return;
    }
    stats_.isSick = false;
    stats_.health = clamp_(stats_.health + 20);
    dirty_ = true;
}

void StatsEngine::applyDiscipline(uint8_t amt) {
    if (amt == 0) return;
    if (amt > 100) amt = 100;
    stats_.discipline = clamp_(stats_.discipline + amt);
    // disciplinar baja un poco happiness — coste social
    stats_.happiness = clamp_(stats_.happiness - 5);
    dirty_ = true;
}

void StatsEngine::applySleep(bool on) {
    if (stats_.isSleeping == on) return;
    stats_.isSleeping = on;
    dirty_ = true;
}
```

### 4.7. Tests nuevos (estimación: +12-15 tests)

```
test_clean_resets_poop_count
test_clean_noop_when_no_poop
test_medicate_cures_sickness_and_heals
test_medicate_noop_when_healthy
test_discipline_increases_and_drops_happiness
test_discipline_clamps_at_100
test_sleep_paralyzes_decay
test_sleep_off_resumes_decay
test_feed_spawns_poop_with_seeded_rng
test_feed_increases_weight
test_feed_ignored_when_sleeping
test_weight_decays_over_time
test_health_decay_x5_when_sick
test_sickness_probability_grows_with_poop
test_attention_score_rises_when_healthy
test_attention_score_drops_when_neglected
```

---

## 5. LifeFSM — máquina de estados de vida

### 5.1. Responsabilidad y patrón

`LifeFSM` mantiene la etapa actual, recibe ticks (vía `StatsTick`) y transiciona automáticamente. Publica `LifeStateChanged` cuando ocurre una transición. **No toca stats directamente** — eso es responsabilidad del StatsEngine. El FSM lee `Stats` const-ref para decidir.

```cpp
// lib/domain/LifeFSM/LifeFSM.h
#pragma once
#include <stdint.h>
#include "Persistence.h"   // para LifeStage enum

class EventBus;
struct Stats;

class LifeFSM {
public:
    LifeFSM(EventBus& bus);

    // Estado inicial — cargado desde Persistence al boot.
    void setStage(LifeStage stage);
    void setAgeMinutes(uint32_t minutes);
    void setDeathSustainMs(uint16_t ms);
    LifeStage stage() const { return stage_; }
    uint32_t ageMinutes() const { return ageMinutes_; }
    uint16_t deathSustainMs() const { return deathSustainMs_; }

    // Llamado desde main loop tras cada StatsTick. Lee los stats actuales para decidir.
    void tick(uint32_t dtMs, const Stats& stats, uint16_t attentionScore);

    // Para tests: causa de la última muerte (0 si no hay muerte)
    uint8_t lastDeathCause() const { return lastDeathCause_; }

private:
    EventBus* bus_;
    LifeStage stage_         = LifeStage::Egg;
    uint32_t  ageMinutes_    = 0;
    uint32_t  ageAccMs_      = 0;
    uint16_t  deathSustainMs_= 0;     // se incrementa cuando health==0, se resetea cuando >0
    uint32_t  deadElapsedMs_ = 0;     // tiempo en estado DEAD
    uint8_t   lastDeathCause_= 0;

    void transitionTo_(LifeStage newStage, uint8_t deathCause = 0);
    void evaluateTransitions_(const Stats& stats, uint16_t attentionScore);
};
```

### 5.2. Tabla de transiciones

| De | A | Condición |
|---|---|---|
| `Egg` | `Baby` | `ageMinutes >= kEggHatchMin` (default 5 min wall-clock; con turbo×60 = 5 s) |
| `Baby` | `Kid` | `ageMinutes >= kBabyToKidMin` (default 60 min) **Y** `attentionScore >= 300` |
| `Kid` | `Adult` | `ageMinutes >= kKidToAdultMin` (default 240 min = 4h) — el tipo de adulto (sano/obeso/triste) depende de `attentionScore` |
| `Adult` | `Old` | `ageMinutes >= kAdultToOldMin` (default 720 min = 12h) |
| `Old` | `Dead` | `ageMinutes >= kOldToDeadMin` (default 1440 min = 24h) → cause = `oldAge` |
| **cualquiera** (no `Dead`) | `Dead` | `health == 0` sostenido durante `kDeathSustainMs` (60 000 ms) → cause = `hunger`/`sickness`/`neglect` según último motivo de health drop |
| `Dead` | `Egg` | `deadElapsedMs >= kResurrectionMs` (300 000 ms = 5 min wall-clock; **NO escala con turbo**) |

**Reglas adicionales:**
- En `Egg`, decay está pausado (los huevos no tienen hambre). Sólo cuenta `ageMinutes`. Esto se implementa por convención: cuando stage == Egg, `main.cpp` no propaga `StatsTick` al StatsEngine — o bien `StatsEngine::tick()` detecta vía un setter `pauseDecay(true)` y se vuelve no-op.
- **Recomendado:** un método explícito `StatsEngine::setDecayPaused(bool)` que se invoca tras `LifeStateChanged` cuando la nueva etapa es Egg o Dead.
- En `Dead`, decay también está pausado. Stats congelados como epitafio.
- `kBabyToKidMin` con `attentionScore < 300` **retrasa** la transición pero no la bloquea — tras `kBabyToKidMin * 1.5` se fuerza la transición (evita Tamagochis-baby eternos).

### 5.3. Lógica de evolución dirigida por attentionScore

Cuando transiciona `Kid → Adult`, el FSM publica además un payload en `LifeStateChanged`:

```cpp
// Event payload codifica el "tipo" de adulto
// payload high byte = LifeStage::Adult (3)
// payload low  byte = AdultType: 0=normal, 1=athletic, 2=obese, 3=grumpy
```

`AdultType` se decide así:

| `attentionScore` final en Kid | AdultType resultante |
|---|---|
| >= 800 | `athletic` (rey del cuidado) |
| 500-799 | `normal` |
| 200-499 | `grumpy` |
| < 200 | `obese` (descuidado, sobrealimentado) |

`AdultType` se persiste en el campo `reservedV2` de `PersistedState` v2 (renombrarlo a `adultType` en la implementación — está documentado en sección 3.1 como reservado pero se puede usar para esto). **Nota de implementación:** al implementar, renombrar `reservedV2` → `adultType` para coherencia.

### 5.4. Pseudo-código de `tick`

```cpp
void LifeFSM::tick(uint32_t dtMs, const Stats& stats, uint16_t attentionScore) {
    if (stage_ == LifeStage::Dead) {
        deadElapsedMs_ += dtMs;
        if (deadElapsedMs_ >= kResurrectionMs) {
            transitionTo_(LifeStage::Egg);
        }
        return;
    }
    
    ageAccMs_ += dtMs;
    if (ageAccMs_ >= 60000) {
        ageMinutes_ += ageAccMs_ / 60000;
        ageAccMs_   %= 60000;
    }
    
    // Vigilancia de muerte
    if (stats.health == 0) {
        deathSustainMs_ = saturating_add(deathSustainMs_, dtMs);
        if (deathSustainMs_ >= kDeathSustainMs) {
            uint8_t cause = inferDeathCause_(stats);
            transitionTo_(LifeStage::Dead, cause);
            return;
        }
    } else {
        deathSustainMs_ = 0;
    }
    
    // Transiciones por edad/atención
    evaluateTransitions_(stats, attentionScore);
}

void LifeFSM::evaluateTransitions_(const Stats& stats, uint16_t attentionScore) {
    switch (stage_) {
        case LifeStage::Egg:
            if (ageMinutes_ >= kEggHatchMin) transitionTo_(LifeStage::Baby);
            break;
        case LifeStage::Baby:
            if (ageMinutes_ >= kBabyToKidMin && attentionScore >= 300)
                transitionTo_(LifeStage::Kid);
            else if (ageMinutes_ >= kBabyToKidMin * 3 / 2)   // forzado
                transitionTo_(LifeStage::Kid);
            break;
        case LifeStage::Kid:
            if (ageMinutes_ >= kKidToAdultMin) {
                AdultType t = computeAdultType_(attentionScore);
                transitionToAdult_(t);   // helper que codifica el payload
            }
            break;
        case LifeStage::Adult:
            if (ageMinutes_ >= kAdultToOldMin) transitionTo_(LifeStage::Old);
            break;
        case LifeStage::Old:
            if (ageMinutes_ >= kOldToDeadMin) transitionTo_(LifeStage::Dead, /*oldAge=*/3);
            break;
        default: break;
    }
}
```

### 5.5. Inferencia de causa de muerte

```cpp
uint8_t LifeFSM::inferDeathCause_(const Stats& s) {
    if (s.isSick) return 2;          // sickness
    if (s.hunger >= 90) return 1;    // hunger
    if (s.happiness <= 10) return 4; // neglect (por tristeza extrema)
    return 5;                         // unknown
}
```

### 5.6. Tests del LifeFSM (estimación: +12-15 tests)

```
test_initial_stage_is_egg
test_egg_hatches_to_baby_after_threshold
test_egg_does_not_hatch_below_threshold
test_baby_to_kid_blocked_by_low_attention
test_baby_to_kid_forced_after_extended_time
test_kid_to_adult_at_threshold
test_kid_to_adult_picks_athletic_with_high_attention
test_kid_to_adult_picks_obese_with_low_attention
test_adult_to_old_at_threshold
test_old_to_dead_at_threshold_with_cause_old_age
test_any_to_dead_when_health_zero_sustained
test_health_zero_short_does_not_kill
test_dead_to_egg_after_resurrection_delay
test_dead_state_pauses_age_counter
test_publishes_life_state_changed_event_on_transition
test_death_cause_inferred_from_stats
```

---

## 6. DisplayRenderer extendido

### 6.1. Tabla sprite por etapa y mood

```cpp
enum class Mood : uint8_t { Idle, Happy, Sad, Eat };

struct SpriteFrames {
    const uint8_t* frames[3];   // nullptr si no aplica
    uint8_t frameCount;         // 1, 2 o 3
};

// Tabla rellenada en DisplayRenderer.cpp
static const SpriteFrames kSpritesByStage[6 /*LifeStage*/][4 /*Mood*/] = {
    // Egg
    { {{kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2},   // Idle — placeholder con BABY
      {{kSpriteBabyIdle0, nullptr, nullptr}, 1},            // Happy
      {{kSpriteBabyIdle0, nullptr, nullptr}, 1},            // Sad
      {{kSpriteBabyIdle0, nullptr, nullptr}, 1} },          // Eat (no aplica para Egg)
    // Baby (sprites reales disponibles)
    { {{kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2},
      {{kSpriteBabyHappy0, kSpriteBabyHappy1, kSpriteBabyHappy2}, 3},
      {{kSpriteBabySad0, kSpriteBabySad1, nullptr}, 2},
      {{kSpriteBabyEat0, kSpriteBabyEat1, kSpriteBabyEat2}, 3} },
    // Kid, Adult, Old, Dead — placeholders por ahora
    // ... (mismo patrón, todos apuntando a kSpriteBaby* hasta que llegue arte)
};
```

**Cuando llegue cada batch de sprites:** sustituir las entradas de la fila correspondiente. La estructura no cambia.

### 6.2. Selección automática de mood

`DisplayRenderer` recibe `Stats` actualizados vía `setStats()` (heredado de Fase 1) y `LifeStage` vía nuevo `setLifeStage(LifeStage)`. Cada `render()` calcula el mood:

```cpp
Mood DisplayRenderer::computeMood_() const {
    if (eatAnimRemainingMs_ > 0) return Mood::Eat;          // animación one-shot tras feed
    if (stats_.isSleeping)       return Mood::Idle;         // sleeping usa idle + overlay Z
    if (stats_.happiness <= 20)  return Mood::Sad;
    if (stats_.happiness >= 80)  return Mood::Happy;
    return Mood::Idle;
}
```

### 6.3. Animación EAT one-shot

Cuando llega `ButtonLongPress` (feed), main publica `StatsChanged`. Adicionalmente publica un evento nuevo `EventType::FeedAnimationRequested` que el DisplayRenderer captura y arranca un timer interno (`eatAnimRemainingMs_ = 2400`, 3 frames × 800 ms). Mientras esté >0, `computeMood_` devuelve `Eat`. Al expirar, vuelve a Idle/Happy/Sad según stats.

### 6.4. Overlays asíncronos

Los overlays se dibujan **encima del sprite base** sin alterar la animación principal. Política de qué overlay se muestra cuándo:

| Overlay | Sprite | Cuándo se muestra | Posición |
|---|---|---|---|
| Sleeping Z | `kSpriteOverlaySleepZ0/1` (animado) | `stats.isSleeping == true` | Sobre la cabeza (top-right del sprite) |
| Sick | `kSpriteOverlaySick` | `stats.isSick == true` | Sobre la cabeza (top-left) |
| Hungry | `kSpriteOverlayHungry0/1` (animado lento) | `stats.hunger >= 80 && !isSleeping` | Esquina superior derecha global |
| Poop | `kSpriteOverlayPoop` | `stats.poopCount > 0` | Esquina inferior izquierda; se dibuja **N veces** según `poopCount` clamp(1..3) — 3 sprites uno al lado del otro como máximo (limitación de espacio) |

**Coexistencia:** múltiples overlays pueden coexistir. SleepingZ y Sick pueden mostrarse a la vez si el Tamagochi está enfermo y dormido. La posición fija evita que se solapen entre sí. Si hay conflicto (ej. SleepingZ + Sick ambos top), Sick gana prioridad (top-left) y Z se mueve a top-center.

### 6.5. Escena DEAD

Nueva `Scene::DeadScreen`:

```
+------------------------+
|       _____            |
|      |     |           |  ← sprite kSpriteDeadIdle0 (lápida)
|      | RIP |           |
|     /_______\          |
|                        |
|   "Murió de hambre"    |  ← texto según lastDeathCauseCode
|   Edad: 3h 42m         |
|   RIPs totales: 5      |
+------------------------+
```

Se activa automáticamente cuando `LifeFSM` publica `LifeStateChanged` con `stage == Dead`. La escena previa (IdleMain o StatsView) se reemplaza. Al resurrectar (DEAD → EGG), se vuelve a `Scene::IdleMain`.

### 6.6. Banner TURBO

Cuando `TURBO_MODE` está definido, dibujar permanentemente "T" pequeña (4×6 px) en `StatsView` arriba a la derecha. Sirve como recordatorio visual constante de que no es firmware de producción. Implementación:

```cpp
#ifdef TURBO_MODE
void DisplayRenderer::drawTurboBanner_() {
    u8g2_->setFont(u8g2_font_4x6_tr);
    u8g2_->drawStr(120, 6, "T");
}
#endif
```

### 6.7. Cambios en `DisplayRenderer.h`

```cpp
class DisplayRenderer {
public:
    // ... API Fase 1 ...
    
    // Nuevos en Fase 2
    void setLifeStage(LifeStage stage);
    void setAdultType(uint8_t adultType);   // si se quiere variar sprite por tipo
    void triggerEatAnimation();             // arranca el one-shot
};
```

---

## 7. Loop principal en `main.cpp`

### 7.1. Construcción de singletons

```cpp
static EventBus        bus;
static StatsEngine     statsEngine;
static LittleFsStorage storage;
static Persistence     persistence(storage);
static LifeFSM         lifeFSM(bus);         // ← NUEVO
static InputHandler    input(bus, PIN_BUTTON);
static DisplayRenderer renderer(u8g2, bus);
```

### 7.2. setup() — añadidos

```cpp
void setup() {
    // ... boot, OLED, LittleFS, load (igual que Fase 1) ...
    
    // Aplicar estado cargado tanto a StatsEngine como a LifeFSM
    PersistedState loaded;
    LoadResult lr = persistence.load(loaded);
    applyToStatsEngine(loaded, statsEngine);
    lifeFSM.setStage(static_cast<LifeStage>(loaded.lifeStage));
    lifeFSM.setAgeMinutes(loaded.ageMinutes);
    lifeFSM.setDeathSustainMs(loaded.deathSustainMs);
    
    // Sincronizar el renderer con la etapa inicial
    renderer.setLifeStage(lifeFSM.stage());
    
    // Si estamos en Egg o Dead, pausar el decay del engine
    statsEngine.setDecayPaused(
        lifeFSM.stage() == LifeStage::Egg || lifeFSM.stage() == LifeStage::Dead
    );
    
    // EventBus wiring nuevo
    bus.subscribe(EventType::LifeStateChanged, onLifeStateChanged);
    // ... resto igual ...
    
#ifdef TURBO_MODE
    LOG_WARN("=== TURBO_MODE ACTIVE — factor %u — NO PROD ===", TURBO_FACTOR);
#endif
}

static void onLifeStateChanged(const Event& e) {
    LifeStage newStage = static_cast<LifeStage>((e.payload >> 8) & 0xFF);
    uint8_t   subtype  = e.payload & 0xFF;
    renderer.setLifeStage(newStage);
    if (newStage == LifeStage::Adult) renderer.setAdultType(subtype);
    statsEngine.setDecayPaused(newStage == LifeStage::Egg || newStage == LifeStage::Dead);
    if (newStage == LifeStage::Dead) renderer.setScene(Scene::DeadScreen);
    if (newStage == LifeStage::Egg && previousStage_ == LifeStage::Dead) {
        // Resurrección — reset stats
        statsEngine.resetForNewLife();
        renderer.setScene(Scene::IdleMain);
    }
    previousStage_ = newStage;
    bus.publish({EventType::PersistRequested, 0});   // guardar cambio importante
}
```

### 7.3. loop() — integrar tick del LifeFSM

```cpp
void loop() {
    input.loop();
    uint32_t now = millis();
    
    if (now - lastStatsTickMs >= STATS_TICK_MS) {
        uint32_t dt = now - lastStatsTickMs;
        statsEngine.tick(dt);
        lifeFSM.tick(dt, statsEngine.stats(), statsEngine.stats().attentionScore);  // ← NUEVO
        lastStatsTickMs = now;
        if (statsEngine.consumeDirtyFlag()) bus.publish({EventType::StatsChanged, 0});
    }
    
    // ... render, persist, heap check (igual que Fase 1) ...
}
```

### 7.4. Persistencia tras transición

Cada `LifeStateChanged` dispara `PersistRequested`. Razones:
- Evita perder progreso en cambio de etapa por power-loss inmediato.
- Las transiciones son raras (5-7 por vida) — coste de flash despreciable.
- La función `fillFromEngine()` (helper en `main.cpp`) debe rellenar **todos los campos v2** desde StatsEngine y LifeFSM. Crucial: incluir `lifeStage`, `deathSustainMs`, `attentionScore`.

### 7.5. Helpers `applyToStatsEngine` / `fillFromEngine` actualizados

```cpp
static void applyToStatsEngine(const PersistedState& s, StatsEngine& engine) {
    Stats stats;
    stats.hunger          = s.hunger;
    stats.happiness       = s.happiness;
    stats.health          = s.health;
    stats.discipline      = s.discipline;
    stats.weight          = s.weight;
    stats.poopCount       = s.poopCount;
    stats.isSick          = (s.flags & 0x01) != 0;
    stats.isSleeping      = (s.flags & 0x02) != 0;
    stats.attentionScore  = s.attentionScore;
    engine.setStats(stats);
}

static void fillFromEngine(PersistedState& s, const StatsEngine& engine, const LifeFSM& fsm) {
    const Stats& st = engine.stats();
    s.magic          = Persistence::kPersistMagic;
    s.version        = Persistence::kCurrentVersion;
    s.lifeStage      = static_cast<uint8_t>(fsm.stage());
    s.ageMinutes     = fsm.ageMinutes();
    s.hunger         = st.hunger;
    s.happiness      = st.happiness;
    s.health         = st.health;
    s.discipline     = st.discipline;
    s.weight         = st.weight;
    s.poopCount      = st.poopCount;
    s.flags          = (st.isSick ? 0x01 : 0) | (st.isSleeping ? 0x02 : 0);
    s.attentionScore = st.attentionScore;
    s.deathSustainMs = fsm.deathSustainMs();
    s.lastDeathCauseCode = fsm.lastDeathCause();
    s.adultType      = currentAdultType_;   // global capturado en onLifeStateChanged
    // CRC lo calcula Persistence::save()
}
```

---

## 8. TURBO_MODE — implementación detallada

### 8.1. `platformio.ini` — nuevo env

```ini
[env:esp12e_turbo]
extends = env:esp12e
build_flags =
    ${env:esp12e.build_flags}
    -D TURBO_MODE
    -D TURBO_FACTOR=60
```

### 8.2. `config.h` — todas las constantes escaladas

```cpp
#ifdef TURBO_MODE
  #ifndef TURBO_FACTOR
    #define TURBO_FACTOR 60
  #endif
  #define STATS_TICK_MS         (1000 / TURBO_FACTOR)   // 16 ms con factor 60
  #define LIFE_FSM_TIME_SCALE   TURBO_FACTOR             // multiplica el ageMinutes "subjetivo"
#else
  #define STATS_TICK_MS         1000
  #define LIFE_FSM_TIME_SCALE   1
#endif

// Umbrales LifeFSM (en minutos subjetivos — escalables)
#define EGG_HATCH_MIN           5
#define BABY_TO_KID_MIN         60
#define KID_TO_ADULT_MIN        240
#define ADULT_TO_OLD_MIN        720
#define OLD_TO_DEAD_MIN         1440

// Wall-clock (NO escalan)
#define DEATH_SUSTAIN_MS        60000
#define RESURRECTION_MS         (5UL * 60UL * 1000UL)
#define PERSIST_AUTOSAVE_MS     (5UL * 60UL * 1000UL)
```

**Cómo aplica el escalado en LifeFSM:** los umbrales `EGG_HATCH_MIN`, etc. **no se modifican** — siguen siendo minutos subjetivos. Lo que sube es la frecuencia con la que `lifeFSM.tick(dt, ...)` se llama (porque `STATS_TICK_MS` es 60× más corto). En cada `tick`, `ageAccMs_ += dt` se incrementa con `dt` real (~16ms) pero el loop principal lo invoca 60× más a menudo. **Resultado neto: `ageMinutes` avanza 60× más rápido en wall-clock**.

Si el aproach quiere ser más explícito en el FSM (no dependiente del tick rate): añadir factor:

```cpp
void LifeFSM::tick(uint32_t dtMs, ...) {
    ageAccMs_ += dtMs * LIFE_FSM_TIME_SCALE;   // ← multiplica explícitamente
    // ...
}
```

**Decisión recomendada:** usar la versión **explícita** con `LIFE_FSM_TIME_SCALE`. Razón: si el tick rate cambia (Fase 4 podría reducir polling para ahorrar batería), el FSM mantiene comportamiento esperado. Es más auditable.

### 8.3. Pantalla con TURBO activo

Ya cubierto en sección 6.6. Banner "T" permanente en `StatsView`.

### 8.4. Guard contra producción accidental

```cpp
// En main.cpp, en setup():
#ifdef TURBO_MODE
    LOG_WARN("=================================================");
    LOG_WARN("== TURBO_MODE ACTIVE — factor %u           ==", TURBO_FACTOR);
    LOG_WARN("== ESTE BINARIO NO DEBE LLEGAR A PRODUCCION    ==");
    LOG_WARN("=================================================");
    static_assert(FIRMWARE_VERSION == 0x02FE, "TURBO build must use FIRMWARE_VERSION 0x02FE");
#else
    static_assert(FIRMWARE_VERSION == 0x0200, "Prod build must use FIRMWARE_VERSION 0x0200");
#endif
```

QA check al revisar PR: `grep -r TURBO_MODE platformio.ini` y verificar que **el env por defecto** no tiene el flag. CI/CD verifica que `pio run -e esp12e` (sin sufijo `_turbo`) es el binario que se distribuye.

---

## 9. Tests host-native — listado completo

Estado base: **32 tests pasando en Fase 1**.

### 9.1. Nuevos tests StatsEngine (+12-15 tests)

(Ya enumerados en sección 4.7)

### 9.2. Nuevos tests LifeFSM (+12-15 tests)

(Ya enumerados en sección 5.6)

### 9.3. Nuevos tests Persistence (+5-7 tests)

```
test_persistedstate_v2_is_exactly_40_bytes
test_save_load_roundtrip_v2
test_migrate_v1_to_v2_fills_defaults
test_migrate_v1_to_v2_preserves_v1_fields
test_load_v1_blob_returns_migrated_status
test_load_v2_blob_returns_ok_status
test_load_unknown_version_v3_returns_migration_failed
```

### 9.4. Tests para infra del bus (opcional, +2-3 tests)

Si Fase 1 no testeó EventBus, sumar:

```
test_eventbus_subscribe_and_publish
test_eventbus_max_subscribers_rejects_new
test_eventbus_publish_no_subscribers_does_not_crash
```

### 9.5. Total estimado

| Categoría | Fase 1 | Fase 2 | Delta |
|---|---|---|---|
| StatsEngine | 14 | 26-29 | +12-15 |
| Persistence | 17 | 22-24 | +5-7 |
| LifeFSM | 0 | 12-15 | +12-15 |
| EventBus | 0-1 | 2-3 | +2-3 |
| **Total** | **~32** | **~62-71** | **+30-40** |

Target: pasar de 32 a **al menos 60 tests verdes** al final de Fase 2.

### 9.6. Determinismo en tests del rand01

Para los tests de `applyFeed` (poop spawn) y de `tick` (sickness probability), usar un mock RNG con secuencia controlada:

```cpp
// En el setup del test
StatsEngine engine;
static float gMockRandSeq[10] = {0.1f, 0.9f, 0.5f, ...};
static int   gMockRandIdx = 0;
engine.setRandomSource([]() { return gMockRandSeq[gMockRandIdx++ % 10]; });
```

---

## 10. Sprites faltantes — pipeline reusado

Mismo pipeline de Fase 1, sin cambios:

```bash
# 1. Guillermo genera PNGs vía "Nano Banana" (LLM image gen) — un PNG por etapa+mood
# 2. Si vienen como sheet, slicear:
python3 tools/slice_sheet.py assets/sprites_raw/kid_sheet.png \
    --tiles 8 --tile-size 32 --out-prefix kid_idle_ \
    --output assets/sprites/

# 3. Convertir todos a XBM y regenerar el header:
python3 tools/png_to_xbm.py --invert assets/sprites include/sprites_generated.h

# 4. Verificar visualmente:
pio run -e esp12e -t upload
```

**Backend-expert no espera assets para avanzar.** Los placeholders BABY mantienen la pantalla coherente. Cada commit de assets es independiente.

### 10.1. Ejemplo de commit de assets

```
Assets: sprites KID generados (idle/happy/sad/eat)
```

Cambios: `assets/sprites/kid_*.png` + `include/sprites_generated.h` regenerado + actualización de la tabla `kSpritesByStage[Kid][...]` en `DisplayRenderer.cpp`.

---

## 11. Hitos / commits atómicos sugeridos

Cada commit debe ser **<500 líneas**, pasar `pio run -e esp12e` y (cuando aplique) `pio test -e native`. Orden recomendado:

| # | Tipo | Título sugerido | Archivos clave | Dependencias |
|---|---|---|---|---|
| 1 | Feature + Test | `Feature: PersistedState v2 + migrate v1->v2 + tests` | `lib/domain/Persistence/*`, `test/test_persistence/*` | — |
| 2 | Feature + Test | `Feature: StatsEngine extendido (discipline/weight/poop/sick/sleeping) + tests` | `lib/domain/StatsEngine/*`, `test/test_stats_engine/*` | #1 (usa LifeStage enum del header de Persistence) |
| 3 | Feature + Test | `Feature: LifeFSM con 6 etapas y transiciones + tests` | `lib/domain/LifeFSM/*`, `test/test_life_fsm/*`, `platformio.ini` (env native include LifeFSM) | #1, #2 |
| 4 | Feature | `Feature: TURBO_MODE flag + env esp12e_turbo` | `platformio.ini`, `include/config.h` | #3 (escala los timings del FSM) |
| 5 | Refactor | `Refactor: DisplayRenderer con tabla sprites por etapa (BABY placeholder)` | `lib/ui/DisplayRenderer/*` | #3 (necesita LifeStage enum) |
| 6 | Feature | `Feature: overlays asíncronos SICK/POOP/SLEEPING_Z/HUNGRY` | `lib/ui/DisplayRenderer/*` | #5 |
| 7 | Feature | `Feature: muerte y resurrección automática + escena DEAD` | `lib/ui/DisplayRenderer/*`, `src/main.cpp` | #3, #5, #6 |
| 8 | Feature | `Feature: animaciones HAPPY/SAD/EAT one-shot tras feed` | `lib/ui/DisplayRenderer/*`, `src/main.cpp` | #5 |
| 9 | Feature | `Feature: cablear LifeFSM en main + persist en transiciones` | `src/main.cpp` | #1-#8 |
| 10 | Docs | `Docs: README Fase 2 + plan cierre` | `README.md`, `docs/fase-2-plan.md` (notas finales) | — |

**Commits intercalados de assets** (insertar cuando Guillermo entregue cada batch):

| Asset commit | Cuándo | Pre-requisitos |
|---|---|---|
| `Assets: sprites EGG generados` | Cuando esté el PNG | Pipeline tools/ (ya existe) |
| `Assets: sprites KID generados` | Idem | Idem |
| `Assets: sprites ADULT (4 tipos) generados` | Idem | Idem; coordinar `AdultType` ↔ sprite |
| `Assets: sprites OLD generados` | Idem | Idem |
| `Assets: sprite DEAD generado` | Idem | Idem |

**Estos commits son no-bloqueantes** — el firmware funciona con BABY como placeholder global desde el commit #5.

### 11.1. Estrategia de PR

Una sola PR `feature/fase-2 → main` al final de los commits 1-10. Asset commits pueden ir mezclados entre los hitos sin ningún problema. QA valida el conjunto.

---

## 12. Riesgos específicos de Fase 2

| # | Riesgo | Mitigación |
|---|---|---|
| 1 | **RAM**: añadir LifeFSM (~50 bytes), Stats extendido (~10 bytes), overlays activos en DisplayRenderer (~20 bytes), banner TURBO. Total estimado: **<200 bytes RAM extra**. Margen actual (60% libre = ~50 KB) cubre con muchísima holgura | Monitorear `ESP.getFreeHeap()` igual que Fase 1. Sin AsyncWebServer aún. Sin riesgo real |
| 2 | **Flash**: sprites de 5 etapas × ~5 sprites/etapa × 128 bytes = **~3.2 KB extra de Flash en PROGMEM**. Footprint actual 29.6% de 1 MB → margen amplio | Sin acción necesaria. Si en algún momento se acerca al 70%, considerar comprimir XBM con RLE |
| 3 | **Complejidad LifeFSM**: muchas transiciones, fácil meter bugs (transición ilegal, doble transición, race con persistencia) | Tests exhaustivos (sección 5.6). Implementar `transitionTo_` como única entrada para cambiar `stage_` — asserta transiciones legales |
| 4 | **Migración v1→v2 mal hecha rompe estados v1**: Guillermo tiene huevos vivos en Fase 1 | Tests específicos: `test_migrate_v1_to_v2_preserves_v1_fields`. memset(&out, 0) antes de read. Validar visualmente en hardware con un dump v1 conocido antes de mergear |
| 5 | **TURBO_MODE accidentalmente mergeado a main** = vida de 6 minutos en producción | Guards múltiples (sección 8.4): `FIRMWARE_VERSION` distinto, banner pantalla, warning boot, env nombre distinto, asserción al compilar |
| 6 | **Determinismo RNG en tests**: spawn de caca y sickness usan rand → tests flaky si no se mockea | Patrón `setRandomSource` (sección 9.6). En producción usa `random()` de Arduino; en tests, secuencia fija |
| 7 | **Bytes basura en buffer al cargar v1 con struct v2**: si no se hace memset, los campos nuevos tienen valores impredecibles | `memset(&out, 0, sizeof(out))` al inicio de `load()`. Cubierto en sección 3.3 |
| 8 | **AdultType solo persiste en `reservedV2`**: si más tarde se quiere usar `reservedV2` para otra cosa, problema | Renombrar `reservedV2` → `adultType` en el header desde el commit #1. Sin reservar campos "por si acaso" |
| 9 | **Overlays solapados (Sick + Sleep + Hungry)**: caos visual | Política de prioridad fija (sección 6.4). Solo 2 overlays a la vez como máximo, posiciones físicas separadas |
| 10 | **Escena DEAD tapa la posibilidad de feed/clean/sleep** — si el usuario quiere "salvarlo" no puede | Por diseño DEAD es DEAD. El long-press en DEAD acelera resurrección (sección 1.3). No hay zombi |
| 11 | **`attentionScore` se vuelve dominante y sesga toda la dinámica** — un usuario con score 999 nunca ve fallos | Clamp 0-1000. Decay también baja el score si neglect. Atención: balance numérico requiere validar en hardware con turbo durante un par de "vidas" |
| 12 | **Persistencia más frecuente** (cada `LifeStateChanged` salva) — desgaste flash | Solo 5-7 transiciones por vida + autosave 5 min. Total ~12 saves/vida. LittleFS wear-leveling absorbe. Sin riesgo real |
| 13 | **Race condition en setup() entre load + setStage + decayPaused**: si `LifeStateChanged` se publica antes de que main termine setup, el handler accede a estado a medias | En setup() **no se publica** `LifeStateChanged` — la primera transición ocurre en el primer tick del loop. Suscribir handlers al final de setup() |
| 14 | **Polución de `EventType` enum**: ya tiene 11 valores, Fase 2 añade `FeedAnimationRequested`. Sobrepasa los 16 → no hay riesgo (es uint8_t), pero conviene revisar `kMaxSubscribersPerEvent` por footprint | Cada nuevo EventType añade ~32 bytes (8 ptr × 4 max subs). No crítico. Validar `sizeof(EventBus)` < 1 KB |

---

## 13. Checklist final para backend-expert

Antes de abrir PR (lo coordina QA + CI/CD):

- [ ] Todos los commits respetan el formato `Prefix: descripción` (cumple `rules/git-workflow.md`)
- [ ] Ningún commit supera 500 líneas
- [ ] `pio run -e esp12e` compila limpio con `FIRMWARE_VERSION == 0x0200`
- [ ] `pio run -e esp12e_turbo` compila limpio con `FIRMWARE_VERSION == 0x02FE`
- [ ] `pio test -e native` pasa al 100% con **al menos 60 tests verdes**
- [ ] Migración v1 → v2 verificada con un blob v1 generado en Fase 1 (cargar, migrar, guardar, recargar)
- [ ] `static_assert(sizeof(PersistedState) == 40)` presente en header
- [ ] `static_assert(sizeof(PersistedState) == 28)` ELIMINADO del header — sustituido por el de 40
- [ ] `enum LifeStage { Egg=0, Baby=1, Kid=2, Adult=3, Old=4, Dead=5 }` con valores explícitos
- [ ] Ningún `#include <Arduino.h>` en `lib/domain/LifeFSM/`
- [ ] `lib/domain/LifeFSM/` incluido en `[env:native]` build flags y src filter
- [ ] Banner TURBO visible en pantalla cuando `-DTURBO_MODE`
- [ ] LOG_WARN al boot cuando `-DTURBO_MODE`
- [ ] Ciclo completo de vida (EGG → ... → DEAD → EGG) verificado en hardware con turbo (debe caber en <2h)
- [ ] Muerte por hambre verificada en hardware (no comer hasta hunger=100 → health → 0 → DEAD)
- [ ] Muerte por enfermedad verificada en hardware (no limpiar → sick → health → 0 → DEAD)
- [ ] Resurrección automática verificada (esperar 5 min en DEAD → EGG)
- [ ] Power cycle 5× durante distintas etapas — estado conserva etapa y stats
- [ ] README actualizado con lista de etapas, acciones nuevas, y nota sobre TURBO_MODE
- [ ] Sprites placeholder (BABY) funcionando para todas las etapas no diseñadas
- [ ] No hay `#warning` ni `TODO crítico` sin justificar en el código

Si **alguna** marca queda sin cumplir, no se mergea. QA lo bloqueará.

---

## 14. Lo que NO entra en Fase 2 (anti-scope creep)

- WiFi STA + ArduinoOTA (Hito 8 de Fase 1 — **diferido por decisión de Guillermo**)
- WiFiManager con portal cautivo → Fase 1.5
- `configTime()` / NTP → Fase 1.5
- AsyncWebServer + REST API → Fase 3
- HTTP Basic Auth, rate limiting → Fase 3
- PowerManager (auto-dim, sleep eléctrico real) → Fase 4
- OTA signed updates → Fase 4
- Reproducción / cría (más Tamagochis a la vez) → no planeado, fuera de scope
- Minijuegos para subir happiness → Fase 5 si surge

Si surge la tentación de "ya que estoy meto esto", **no**. Cada cosa en su fase.

---

## 15. Coordinación con otros agentes

- **frontend-expert (designer):** entrega los sprites EGG, KID, ADULT (×4 tipos opcionalmente), OLD, DEAD usando el pipeline existente. **No bloqueante** para el resto del trabajo de Fase 2 — backend-expert avanza con placeholders.
- **qa:** invocado tras los commits 1, 2, 3 (tests), tras commit 9 (cableado) y al final para validación completa (vida-muerte en hardware con turbo).
- **cicd:** invocado solo al final para merge `--no-ff` a `main` y tag `v0.3.0-fase2`. Antes del merge, verificar que el binario de `main` se compila con `pio run -e esp12e` (no turbo).
- **product-manager (Atlas):** no requiere intervención salvo cambio de scope.
- **superdev (yo):** disponible si surge un bloqueo de arquitectura. Notable: si Guillermo decide cambiar la política de muerte/resurrección durante implementación, debe consultarse antes de tocar LifeFSM.

---

## 16. Métricas de éxito de Fase 2

Al mergear, el firmware debe demostrar:

| Métrica | Objetivo |
|---|---|
| Tests host-native verdes | ≥ 60 |
| Etapas de vida implementadas | 6 (EGG, BABY, KID, ADULT, OLD, DEAD) |
| Stats persistidos | 6 (hunger, happiness, health, discipline, weight, attentionScore) + 3 flags (isSick, isSleeping, poopCount) |
| Acciones disponibles | 5 (feed, clean, medicate, discipline, sleep on/off) |
| Overlays funcionales | 4 (Sick, Poop, SleepingZ, Hungry) |
| Ciclo de vida en turbo×60 | ≤ 30 min wall-clock para EGG→OLD→DEAD→EGG |
| RAM libre tras boot | ≥ 32 KB (60% del total) |
| Flash usado | ≤ 40% del total |
| Migración v1→v2 sin pérdida | 100% de campos v1 conservados |
| Power cycle resiliente | 5/5 power cycles preservan etapa y stats |

---

Fin del plan.
