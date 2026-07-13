/**
 * DisplayRendererColor.cpp — renderer a color para ESP32-S3 + TFT JD9853.
 *
 * FASE 6: sprites reales por etapa de vida.
 * FASE 4 (menú): icono + label en HUD inferior.
 *
 * Decisiones de diseño:
 *
 * 1. Archivos en lib/ui/DisplayRendererColor/ — no toca DisplayRenderer.(h|cpp).
 *    El mono (esp12e) sigue compilando sin cambios.
 *
 * 2. kSpriteLoad[]: tabla estática con UUID, folder y animaciones a cargar
 *    por etapa. loadSprites_() itera sobre ella y popula spritesByStage_[].
 *
 * 3. Precarga de 6 etapas al boot:
 *      egg   × 1 anim (idle×6)      =  27 KB
 *      baby  × 5 anims              = 106 KB
 *      kid   × 5 anims              = 106 KB
 *      adult × 5 anims              = 106 KB
 *      old   × 5 anims              = 106 KB
 *      ghost × 1 anim (idle×6)      =  27 KB
 *      Total                        ≈ 478 KB  (de ~1.9 MB PSRAM disponible)
 *
 * 4. framesForMood_(stage, mood): O(1), sin LittleFS. Fallback a idle de la
 *    etapa si el mood no tiene frames (e.g. egg/ghost solo tienen idle).
 *
 * 5. drawDeadScreen_(): usa spritesByStage_[Dead].frames[Idle] (ghost real).
 *
 * 6. Iconos del menú de acciones (loadMenuIcons_):
 *    Feed→ic-apple, Play→ic-ball, Discipline→ic-disc, Sleep→ic-moon,
 *    Heal→ic-heal, Stats→ic-stats. Todos 24×24, 1 frame "idle".
 *    Clean y Medicate usan primitivas (drawMenuIconPrimitive_): sus assets
 *    anim-broom/anim-syringe son 48×48 — no caben en el HUD de 60 px junto
 *    al texto size 2 (que requiere 16 px + márgenes). Se dibujan como iconos
 *    simbólicos con las mismas primitivas que los overlay badges.
 *    Si loadMenuIcons_() falla (LittleFS no flasheado), drawHudBottom_() cae
 *    a label solo — el menú sigue navegable.
 */

#include "DisplayRendererColor.h"
#include "Logger.h"
#include <string.h>
#include <stdio.h>

// ─── Colores RGB565 usados en el renderer ─────────────────────────────────────
// Definidos localmente para no depender del orden de include de Arduino_GFX_Library.

static constexpr uint16_t COL_BLACK    = 0x0000;
static constexpr uint16_t COL_WHITE    = 0xFFFF;
static constexpr uint16_t COL_RED      = 0xF800;
static constexpr uint16_t COL_GREEN    = 0x07E0;
static constexpr uint16_t COL_AMBER    = 0xFD60;  // naranja-ámbar: R=31, G=43, B=0
static constexpr uint16_t COL_BLUE     = 0x001F;
static constexpr uint16_t COL_CYAN     = 0x07FF;
static constexpr uint16_t COL_MAGENTA  = 0xF81F;
static constexpr uint16_t COL_YELLOW   = 0xFFE0;
static constexpr uint16_t COL_DARKGRAY = 0x4208;
static constexpr uint16_t COL_GRAY     = 0x7BEF;
static constexpr uint16_t COL_PURPLE   = 0x780F;

// Fondo de pantalla — negro puro.
static constexpr uint16_t COL_BG = 0x0000;

// ─── IDs de los iconos de estado (banda superior) ─────────────────────────────
//
// Sprites 24×24 px, animación "idle" de 1 frame. Se escalan ×2 (48×48) en pantalla.
// Los IDs deben coincidir con el campo "id" del manifest.json.
// Orden: 0=hunger, 1=poop, 2=sick — coincide con kStatusIconOrder[].
// UUIDs reales del manifest (feature/color-assets — fuente de verdad).
static constexpr const char* kIconHungerId = "5965792e-71e6-4b56-9624-5ec6800f0767";
static constexpr const char* kIconPoopId   = "bbe58be2-24f8-4f02-b791-01035929519c";
static constexpr const char* kIconSickId   = "6d0893e1-1910-4dc5-9dc5-9049c144cb9d";

// ─── Tabla de carga de sprites por etapa ──────────────────────────────────────
//
// Fase 6: cada etapa tiene su propio sprite real (folder corto ≤31 chars).
// El campo "id" es el UUID del manifest (búsqueda en SpriteStore).
// El campo "anims" lista las animaciones a cargar en orden MoodColor:
//   índice 0 = Idle, 1 = Happy, 2 = Sad, 3 = Eat, 4 = Sleep.
// nullptr en un slot = sin animación propia → framesForMood_() cae a Idle.
//
// Egg  y Ghost solo tienen "idle"; el resto tienen 5 animaciones completas.
// ─────────────────────────────────────────────────────────────────────────────

struct StageLoadSpec {
    LifeStage   stage;
    const char* spriteId;
    // Animación por MoodColor index (nullptr = no cargar, usar idle como fallback)
    const char* animForMood[kMoodCount];  // [Idle, Happy, Sad, Eat, Sleep]
};

static const StageLoadSpec kSpriteLoad[] = {
    {
        LifeStage::Egg,
        "b89aa51e-e6d1-4208-b282-48c9b1b1f991",
        { "idle", nullptr, nullptr, nullptr, nullptr }
    },
    {
        LifeStage::Baby,
        "797ec06d-c81f-4f02-882a-df63976879b6",
        { "idle", "happy", "sad", "eat", "sleep" }
    },
    {
        LifeStage::Kid,
        "0be78b1f-d025-4c87-a246-d12303f79200",
        { "idle", "happy", "sad", "eat", "sleep" }
    },
    {
        LifeStage::Adult,
        "3d94524d-e9f3-4e4f-a2ae-ae7ac2683a87",
        { "idle", "happy", "sad", "eat", "sleep" }
    },
    {
        LifeStage::Old,
        "a115f4d7-e98e-4692-afd5-85264c646cd1",
        { "idle", "happy", "sad", "eat", "sleep" }
    },
    {
        LifeStage::Dead,
        "f5f69423-d2ea-48bd-9845-23672bffa540",
        { "idle", nullptr, nullptr, nullptr, nullptr }
    },
};

// ─── Tabla de carga de iconos de menú ─────────────────────────────────────────
//
// Cada entrada mapea un ActionKind a su sprite icon en LittleFS.
// Índice del array = índice en menuIcons_[] (ver kMenuIconSlot enumeración).
// Clean y Medicate NO están en esta tabla — se pintan con primitivas.
//
// UUIDs coinciden con el manifest. Los .bin se generan por el pipeline de assets
// (tools/export-esp32.mjs) y se flashean vía `pio -t uploadfs`.
// ─────────────────────────────────────────────────────────────────────────────

struct MenuIconSpec {
    ActionKind  action;
    const char* spriteId;  // UUID del manifest
};

// Orden fijo que define el índice en menuIcons_[].
// Usar menuIconSlot_() para resolver ActionKind → índice.
// UUIDs reales del manifest (feature/color-assets — fuente de verdad).
static const MenuIconSpec kMenuIconSpecs[] = {
    { ActionKind::Feed,       "cdccb195-6db4-43fd-886c-093d8c04b541" },  // ic-apple
    { ActionKind::Play,       "1bd2767f-f5dd-478f-b2bb-94e3d8c0f871" },  // ic-ball
    { ActionKind::Discipline, "7793e238-9159-465a-9965-5d8c83458a5a" },  // ic-disc
    { ActionKind::Sleep,      "2196aed8-2ebe-4dba-a7c6-58ea65a1ce97" },  // ic-moon
    { ActionKind::Heal,       "db816c90-28ad-4ffe-80bc-832517c57b29" },  // ic-heal
    { ActionKind::Stats,      "03fc9466-8147-4c78-8c2b-b8932e355103" },  // ic-stats
};
static constexpr uint8_t kMenuIconSpecCount =
    sizeof(kMenuIconSpecs) / sizeof(kMenuIconSpecs[0]);

// Resuelve ActionKind → índice en menuIcons_[].
// Devuelve 0xFF si la acción no tiene asset (Clean, Medicate → primitivas).
static uint8_t menuIconSlot_(ActionKind action) {
    for (uint8_t i = 0; i < kMenuIconSpecCount; i++) {
        if (kMenuIconSpecs[i].action == action) return i;
    }
    return 0xFF;  // sin slot: Clean o Medicate
}

// Devuelve true si la acción tiene icono dibujado con primitivas.
static bool menuIconIsPrimitive_(ActionKind action) {
    return action == ActionKind::Clean || action == ActionKind::Medicate;
}

// ─── Etiquetas de etapa de vida ───────────────────────────────────────────────
static const char* lifeStageLabel(LifeStage stage) {
    switch (stage) {
        case LifeStage::Egg:   return "EGG";
        case LifeStage::Baby:  return "BABY";
        case LifeStage::Kid:   return "KID";
        case LifeStage::Adult: return "ADULT";
        case LifeStage::Old:   return "OLD";
        case LifeStage::Dead:  return "DEAD";
    }
    return "?";
}

// ─── Etiquetas de acción para HUD inferior ───────────────────────────────────
static const char* actionLabelColor(ActionKind action, bool sleeping) {
    switch (action) {
        case ActionKind::Feed:       return "FEED";
        case ActionKind::Play:       return "PLAY";
        case ActionKind::Clean:      return "CLEAN";
        case ActionKind::Medicate:   return "MED";
        case ActionKind::Discipline: return "DISC";
        case ActionKind::Sleep:      return sleeping ? "WAKE" : "SLEEP";
        case ActionKind::Heal:       return "HEAL";
        case ActionKind::Stats:      return "STATS";
    }
    return "?";
}

// =============================================================================
// Constructor
// =============================================================================

DisplayRendererColor::DisplayRendererColor(Arduino_GFX& canvas, EventBus& bus,
                                           SpriteStore& sprites)
    : canvas_(canvas), bus_(bus), sprites_(sprites)
{
    memset(spritesByStage_, 0, sizeof(spritesByStage_));
    memset(menuIcons_,      0, sizeof(menuIcons_));
}

// =============================================================================
// begin() — carga sprites y resetea estado visual.
// =============================================================================

void DisplayRendererColor::begin() {
    animFrame_              = 0;
    lastAnimTickMs_         = millis();
    lastRenderCallMs_       = millis();
    actionAnimRemainingMs_  = 0;
    actionAnimMood_         = MoodColor::Idle;
    hatchAnimRemainingMs_   = 0;
    evolveAnimRemainingMs_  = 0;
    // Canal overlay: resetear estado antes de cargar sprites
    overlayAnimFrames_      = nullptr;
    overlayAnimRemainingMs_ = 0;
    overlayAnimFrame_       = 0;
    overlayAnimLastTickMs_  = millis();
    visibleCount_           = 0;
    selectedIndex_          = 0;
    spritesLoaded_          = loadSprites_();
    if (!spritesLoaded_) {
        LOG_ERROR("DisplayRendererColor::begin() — no se pudieron cargar sprites");
    }
    // Los iconos se cargan después — un fallo aquí no es fatal (degradación suave).
    menuIconsLoaded_        = loadMenuIcons_();
}

// =============================================================================
// loadSprites_ — precarga los sprites de las 6 etapas de vida en PSRAM.
//
// Etapa      Anims  Frames  Bytes
// egg        1      6       27 648
// baby       5      23     106 368
// kid        5      23     106 368
// adult      5      23     106 368
// old        5      23     106 368
// ghost      1      6       27 648
// Total      22     104    480 768  ≈ 470 KB
//
// Con ~1.9 MB de PSRAM disponible, caben con margen amplio.
// Los slots sin animación propia (nullptr) se dejan a count=0;
// framesForMood_() cae a Idle de la misma etapa como fallback.
// =============================================================================

bool DisplayRendererColor::loadSprites_() {
    bool allOk = true;
    static constexpr size_t kNumStages =
        sizeof(kSpriteLoad) / sizeof(kSpriteLoad[0]);

    for (size_t si = 0; si < kNumStages; si++) {
        const StageLoadSpec& spec = kSpriteLoad[si];
        uint8_t stageIdx = static_cast<uint8_t>(spec.stage);
        StageSprites& dst = spritesByStage_[stageIdx];

        LOG_INFO("Cargando etapa %u (id=%.8s...)", stageIdx, spec.spriteId);

        for (uint8_t mi = 0; mi < kMoodCount; mi++) {
            const char* animName = spec.animForMood[mi];
            if (animName == nullptr) {
                // Sin animación propia — framesForMood_() usará idle como fallback
                continue;
            }
            bool ok = sprites_.loadAnimation(spec.spriteId, animName,
                                             dst.frames[mi]);
            if (!ok) {
                LOG_ERROR("Fallo cargando stage=%u mood=%u anim=%s",
                          stageIdx, mi, animName);
                allOk = false;
            }
        }
    }

    // ── Iconos de estado (banda superior) ─────────────────────────────────────
    // Sprites 24×24 escalados ×2 (48×48) en pantalla. Se cargan los 3 iconos aquí
    // para que drawStatusIconsTop_() los tenga disponibles en O(1) sin LittleFS.
    const char* iconIds[kStatusIconCount] = {
        kIconHungerId, kIconPoopId, kIconSickId
    };
    for (uint8_t i = 0; i < kStatusIconCount; i++) {
        bool ok = sprites_.loadAnimation(iconIds[i], "idle", statusIcons_[i]);
        if (!ok) {
            LOG_ERROR("Fallo cargando icono estado idx=%u id=%.10s... (degradacion por icono)", i, iconIds[i]);
            // Solo dejamos ese slot con count=0 — los otros iconos siguen disponibles.
            // drawStatusIconsTop_() comprueba statusIcons_[idx].count > 0 por icono,
            // por lo que el fallback con primitivas se activa solo para este índice.
            allOk = false;
        }
    }

    // ── Sprites de overlay de acción (48×48, precargados una sola vez) ───────
    // Se usan como segundo canal de animación encima del personaje.
    // UUIDs reales del manifest (feature/color-assets — fuente de verdad).
    memset(&overlayBroom_,   0, sizeof(overlayBroom_));
    memset(&overlaySyringe_, 0, sizeof(overlaySyringe_));
    memset(&overlayBall_,    0, sizeof(overlayBall_));

    if (!sprites_.loadAnimation("6c900d53-e937-4c89-8789-457db84c7517", "broom",   overlayBroom_)) {
        LOG_ERROR("Overlay: fallo cargando broom (Clean)");
        allOk = false;
    }
    if (!sprites_.loadAnimation("4b24e182-adb8-486c-b4bd-7133927b7af6", "syringe", overlaySyringe_)) {
        LOG_ERROR("Overlay: fallo cargando syringe (Heal/Medicate)");
        allOk = false;
    }
    if (!sprites_.loadAnimation("29bf7686-c479-4176-ac8d-4e8460100d83", "ball",    overlayBall_)) {
        LOG_ERROR("Overlay: fallo cargando ball (Play)");
        allOk = false;
    }

    LOG_INFO("DisplayRendererColor: precarga completa. PSRAM libre: %u B",
             ESP.getFreePsram());
    return allOk;
}

// =============================================================================
// loadMenuIcons_ — precarga los 6 iconos de menú (24×24, 1 frame idle).
//
// Se llama desde begin() justo después de loadSprites_().
// Si el LittleFS no está flasheado (dev sin assets) devuelve false; en ese
// caso drawHudBottom_() cae a label solo — degradación suave.
//
// Uso de PSRAM por icono: 24×24×2 = 1 152 B. Total 6 iconos ≈ 7 KB.
// =============================================================================

bool DisplayRendererColor::loadMenuIcons_() {
    memset(menuIcons_, 0, sizeof(menuIcons_));
    bool allOk = true;

    for (uint8_t i = 0; i < kMenuIconSpecCount; i++) {
        bool ok = sprites_.loadAnimation(kMenuIconSpecs[i].spriteId, "idle",
                                         menuIcons_[i]);
        if (!ok) {
            LOG_WARN("loadMenuIcons_: fallo cargando icono accion=%u id=%.8s...",
                     static_cast<unsigned>(kMenuIconSpecs[i].action),
                     kMenuIconSpecs[i].spriteId);
            allOk = false;
        }
    }

    LOG_INFO("loadMenuIcons_: %s. PSRAM libre: %u B",
             allOk ? "OK" : "parcial",
             ESP.getFreePsram());
    return allOk;
}

// =============================================================================
// Setters
// =============================================================================

void DisplayRendererColor::setScene(SceneColor scene)         { scene_ = scene; }
void DisplayRendererColor::setStats(const Stats& stats)       { stats_ = stats; }
void DisplayRendererColor::setOtaProgress(uint8_t pct)        { otaProgress_ = pct; }
void DisplayRendererColor::setLifeStage(LifeStage stage)      { lifeStage_ = stage; }
void DisplayRendererColor::setAdultType(uint8_t adultType)    { adultType_ = adultType; }
void DisplayRendererColor::setLastDeathCause(uint8_t cause)   { lastDeathCause_ = cause; }
void DisplayRendererColor::setAgeMinutes(uint32_t mins)       { ageMinutes_ = mins; }
void DisplayRendererColor::setDeathCount(uint16_t count)      { deathCount_ = count; }
void DisplayRendererColor::setWifiState(WifiState state)      { wifiState_ = state; }

// setBattery: el renderer no lee EventBus/dominio (patrón setters existente) —
// BatteryMonitor decodifica su evento en main_esp32.cpp y llama este setter.
void DisplayRendererColor::setBattery(uint8_t percent, bool charging, bool valid) {
    batteryPercent_  = percent;
    batteryCharging_ = charging;
    batteryValid_    = valid;
}

// =============================================================================
// Triggers de animación one-shot
// =============================================================================

// Mapeo acción → MoodColor para la animación one-shot del personaje (canal 1)
// y → overlay sprite para el canal de overlay (canal 2).
//
// Canal 1 (mood del personaje):
//   Feed       → Eat   (masticar — único que no tiene overlay)
//   Play       → Happy
//   Heal       → Happy
//   Medicate   → Happy
//   Discipline → Sad
//   Clean      → Idle  (badge de caca ya desapareció por applyClean())
//
// Canal 2 (overlay encima del personaje):
//   Clean      → broom   (escoba)
//   Heal       → syringe (jeringa)
//   Medicate   → syringe (jeringa — misma animación que Heal)
//   Play       → ball    (pelota)
//   Feed       → sin overlay (la animación de comer es suficiente)
//
// framesForMood_() aplicará fallback a Idle si la etapa no tiene ese mood
// (e.g. Egg/Ghost no tienen happy/sad/eat — muestran idle igualmente).
void DisplayRendererColor::triggerActionAnimation(ActionKind action) {
    MoodColor mood = MoodColor::Idle;
    // Pointer al ColorSpriteFrames del overlay, nullptr si esta acción no tiene.
    const ColorSpriteFrames* overlay = nullptr;

    switch (action) {
        case ActionKind::Feed:
            // Solo cambia el mood a Eat; sin overlay para no tapar la animación de comer.
            mood = MoodColor::Eat;
            break;
        case ActionKind::Play:
            mood    = MoodColor::Happy;
            overlay = (overlayBall_.count > 0) ? &overlayBall_ : nullptr;
            break;
        case ActionKind::Heal:
            mood    = MoodColor::Happy;
            overlay = (overlaySyringe_.count > 0) ? &overlaySyringe_ : nullptr;
            break;
        case ActionKind::Medicate:
            mood    = MoodColor::Happy;
            overlay = (overlaySyringe_.count > 0) ? &overlaySyringe_ : nullptr;
            break;
        case ActionKind::Discipline:
            mood = MoodColor::Sad;
            break;
        case ActionKind::Clean:
            // Idle: la animación neutral refleja la limpieza; el badge de caca
            // desaparece porque poopCount ya fue reseteado por applyClean().
            mood    = MoodColor::Idle;
            overlay = (overlayBroom_.count > 0) ? &overlayBroom_ : nullptr;
            break;
        case ActionKind::Sleep:
        case ActionKind::Stats:
            // Sleep lo gestiona isSleeping (permanente); Stats no es una acción.
            return;
    }

    // Canal 1: mood del personaje
    actionAnimMood_        = mood;
    actionAnimRemainingMs_ = kActionAnimDurationMs;

    // Canal 2: overlay one-shot (puede ser nullptr si la acción no lo usa)
    overlayAnimFrames_      = overlay;
    overlayAnimRemainingMs_ = (overlay != nullptr) ? kActionAnimDurationMs : 0u;
    overlayAnimFrame_       = 0;
    overlayAnimLastTickMs_  = millis();

    LOG_INFO("[anim] triggerActionAnimation action=%u mood=%u overlay=%s dur=%ums",
             static_cast<unsigned>(action),
             static_cast<unsigned>(mood),
             overlay ? "yes" : "no",
             static_cast<unsigned>(kActionAnimDurationMs));
}

void DisplayRendererColor::triggerHatchAnimation() {
    hatchAnimRemainingMs_ = 2400;
}

void DisplayRendererColor::triggerEvolveAnimation() {
    evolveAnimRemainingMs_ = 1200;
}

// =============================================================================
// Menú de acciones
// =============================================================================

void DisplayRendererColor::refreshActionMenu(const StatsEngine& engine) {
    ActionKind previous = (visibleCount_ > 0 && selectedIndex_ < visibleCount_)
                        ? visibleActions_[selectedIndex_]
                        : ActionKind::Feed;

    visibleCount_ = engine.listAvailableActions(visibleActions_);

    selectedIndex_ = 0;
    for (uint8_t i = 0; i < visibleCount_; ++i) {
        if (visibleActions_[i] == previous) {
            selectedIndex_ = i;
            break;
        }
    }
}

void DisplayRendererColor::moveCursor() {
    if (visibleCount_ == 0) return;
    selectedIndex_ = (selectedIndex_ + 1) % visibleCount_;
}

ActionKind DisplayRendererColor::currentSelectedAction() const {
    if (visibleCount_ == 0)              return ActionKind::Stats;
    if (selectedIndex_ >= visibleCount_) return ActionKind::Stats;
    return visibleActions_[selectedIndex_];
}

// =============================================================================
// render() — punto de entrada del harness. Compone frame completo y hace flush.
// =============================================================================

void DisplayRendererColor::render() {
    uint32_t now = millis();

    // ── Delta desde el último render (para drenar timers one-shot) ──────────
    // Usamos millis() real en lugar de kRenderPeriodMs fijo, así los timers
    // no dependen de la cadencia del harness (correcto a 5, 10 o 15 FPS).
    uint32_t dtMs = (lastRenderCallMs_ == 0) ? 0u : (now - lastRenderCallMs_);
    lastRenderCallMs_ = now;

    // ── Decrementar timers one-shot ──────────────────────────────────────────
    if (actionAnimRemainingMs_ > 0) {
        actionAnimRemainingMs_ = (actionAnimRemainingMs_ > dtMs)
                               ? (actionAnimRemainingMs_ - dtMs) : 0u;
    }
    if (hatchAnimRemainingMs_ > 0) {
        hatchAnimRemainingMs_ = (hatchAnimRemainingMs_ > dtMs)
                              ? (hatchAnimRemainingMs_ - dtMs) : 0u;
    }
    if (evolveAnimRemainingMs_ > 0) {
        evolveAnimRemainingMs_ = (evolveAnimRemainingMs_ > dtMs)
                               ? (evolveAnimRemainingMs_ - dtMs) : 0u;
    }
    // Canal overlay: drenar timer y apagar cuando expire
    if (overlayAnimRemainingMs_ > 0) {
        overlayAnimRemainingMs_ = (overlayAnimRemainingMs_ > dtMs)
                                ? (overlayAnimRemainingMs_ - dtMs) : 0u;
        if (overlayAnimRemainingMs_ == 0) {
            overlayAnimFrames_ = nullptr;  // apagar overlay al expirar
        }
    }

    // ── Avanzar frame del overlay con su propio fps (desacoplado del personaje) ─
    // El overlay usa un contador de frame propio (overlayAnimFrame_) con el fps
    // declarado en sus ColorSpriteFrames, igual que el canal principal del personaje.
    if (overlayAnimFrames_ != nullptr && overlayAnimFrames_->fps > 0) {
        uint32_t overlayMsPerFrame = 1000u / overlayAnimFrames_->fps;
        if (now - overlayAnimLastTickMs_ >= overlayMsPerFrame) {
            overlayAnimLastTickMs_ = now;
            overlayAnimFrame_++;
        }
    }

    // ── Avanzar animFrame_ según fps del sprite activo (desacoplado del FPS de render) ─
    // Determinar los fps de la animación activa. El manifest declara fps por animación
    // (8 fps para la mayoría; 2 fps para el egg). Leemos el fps del ColorSpriteFrames
    // actual para calcular cuántos ms deben pasar por frame.
    // Si no hay sprites cargados, usamos 8 fps como defecto.
    uint8_t spriteFps = 8;
    {
        MoodColor activeMood = (actionAnimRemainingMs_ > 0) ? actionAnimMood_ : computeMood_();
        const ColorSpriteFrames* activeFrames = framesForMood_(lifeStage_, activeMood);
        if (activeFrames != nullptr && activeFrames->fps > 0) {
            spriteFps = activeFrames->fps;
        }
    }
    uint32_t msPerFrame = 1000u / spriteFps;  // e.g. 8fps → 125ms, 2fps → 500ms

    if (now - lastAnimTickMs_ >= msPerFrame) {
        lastAnimTickMs_ = now;
        animFrame_++;
    }

    // ── Forzar DeadScreen cuando el tamagochi está muerto ────────────────────
    if (lifeStage_ == LifeStage::Dead && scene_ != SceneColor::OtaProgress) {
        scene_ = SceneColor::DeadScreen;
    }

    // ── Parpadeo durante animación de evolución ──────────────────────────────
    // Alterna visible/invisible cada 200 ms (independiente del render FPS).
    // En los slots "oscuros" pintamos fondo y hacemos flush sin dibujar el personaje.
    static constexpr uint32_t kEvolveBlikPeriodMs = 200;
    if (evolveAnimRemainingMs_ > 0) {
        uint32_t tickIdx = (evolveAnimRemainingMs_ / kEvolveBlikPeriodMs);
        if ((tickIdx % 2) == 0) {
            canvas_.fillScreen(COL_BG);
            canvas_.flush();
            return;
        }
        // Tick visible: continúa al switch de escenas abajo
    }

    // ── Despachar escena ─────────────────────────────────────────────────────
    switch (scene_) {
        case SceneColor::IdleMain:    drawIdleMain_();    break;
        case SceneColor::StatsView:   drawStatsView_();   break;
        case SceneColor::OtaProgress: drawOtaProgress_(); break;
        case SceneColor::DeadScreen:  drawDeadScreen_();  break;
    }
}

// =============================================================================
// computeMood_ — prioridad: Sleep > Sad > Happy > Idle.
// La animación one-shot de acción (actionAnimRemainingMs_) se consulta en
// drawSprite_() antes de llamar a computeMood_(), por lo que aquí no la
// necesitamos. Sleep se pasa a framesForMood_() con fallback a idle si la
// etapa no tiene animación propia de sueño.
// =============================================================================

MoodColor DisplayRendererColor::computeMood_() const {
    if (stats_.isSleeping)         return MoodColor::Sleep;
    if (stats_.happiness <= 20)    return MoodColor::Sad;
    if (stats_.happiness >= 80)    return MoodColor::Happy;
    return MoodColor::Idle;
}

// =============================================================================
// framesForMood_ — devuelve el buffer de animación para (stage, mood).
//
// Prioridad:
//   1. isSleeping → Sleep (índice 4); si count==0, cae a Idle de la etapa.
//   2. mood recibido (Idle/Happy/Sad/Eat).
//   3. Fallback: Idle de la misma etapa (siempre debe existir).
// =============================================================================

const ColorSpriteFrames* DisplayRendererColor::framesForMood_(LifeStage stage,
                                                               MoodColor mood) const {
    if (!spritesLoaded_) return nullptr;

    uint8_t stageIdx = static_cast<uint8_t>(stage);
    if (stageIdx >= 6) stageIdx = static_cast<uint8_t>(LifeStage::Baby);

    const StageSprites& ss = spritesByStage_[stageIdx];
    uint8_t moodIdx = static_cast<uint8_t>(mood);

    // Try the requested mood animation; fall back to Idle of same stage.
    if (moodIdx < kMoodCount) {
        const ColorSpriteFrames* candidate = &ss.frames[moodIdx];
        if (candidate->count > 0) return candidate;
    }

    // Fallback: idle de la etapa (siempre presente para las 6 etapas)
    return &ss.frames[static_cast<uint8_t>(MoodColor::Idle)];
}

// =============================================================================
// drawSpriteScaled_ — helper estático. Reutiliza la lógica del bringup.
//
// Pintado nearest-neighbor: por cada pixel fuente sx×sy, rellena un cuadrado
// scale×scale en el destino. Transparencia: salta el keyColor.
// =============================================================================

void DisplayRendererColor::drawSpriteScaled_(Arduino_GFX* display,
                                             int16_t destX, int16_t destY,
                                             uint16_t srcW, uint16_t srcH,
                                             const uint16_t* pixels,
                                             uint8_t scale,
                                             uint16_t keyColor) {
    for (uint16_t sy = 0; sy < srcH; sy++) {
        for (uint16_t sx = 0; sx < srcW; sx++) {
            uint16_t color = pixels[sy * srcW + sx];
            if (color == keyColor) continue;
            display->fillRect(
                destX + sx * scale,
                destY + sy * scale,
                scale, scale,
                color
            );
        }
    }
}

// =============================================================================
// drawHudTop_ — HUD superior: WiFi (sup-der) + batería (sup-izq).
//
// D1: la banda superior se reserva para los iconos de estado grandes
// (drawStatusIconsTop_). Etapa y edad se muestran en StatsView.
// WiFi y batería son los únicos iconos que permanecen aquí para no perder
// los indicadores de conectividad/energía.
// =============================================================================

void DisplayRendererColor::drawHudTop_() {
    drawWifiIcon_();
    drawBatteryIcon_();
}

// =============================================================================
// drawWifiIcon_ — icono WiFi en esquina superior derecha.
//
// Centro en (kScreenW-kCornerSafe, kCornerSafe) para quedar fuera del cuarto
// de círculo recortado por el cristal. Dibuja 3 arcos concéntricos mediante
// círculos rellenos superpuestos (drawArc no disponible en todos los drivers).
// =============================================================================

void DisplayRendererColor::drawWifiIcon_() {
    if (wifiState_ == WifiState::Disabled)   return;
    if (scene_ == SceneColor::OtaProgress)   return;

    // Parpadeo en Connecting: frames pares = visible, impares = invisible.
    if (wifiState_ == WifiState::Connecting && (animFrame_ % 2) != 0) return;

    uint16_t color = (wifiState_ == WifiState::Connected) ? COL_GREEN : COL_AMBER;

    // Centro del icono desplazado kCornerSafe desde cada borde para quedar
    // fuera del cuarto de círculo de la esquina superior-derecha del cristal.
    // Los radios de los fillCircle son relativos a (cx, cy), por lo que basta
    // con mover el centro; no hay que ajustar los radios.
    const int16_t cx = kScreenW - kCornerSafe, cy = kCornerSafe;

    // Arco exterior (radio 6) — solo parte superior con fillCircle parcial
    canvas_.fillCircle(cx, cy, 7, color);
    // "Hueco" interior dejando solo el contorno exterior — COL_BG porque el
    // fondo ahora es azul (sin card gris debajo).
    canvas_.fillCircle(cx, cy, 5, COL_BG);
    // Arco medio (radio 4)
    canvas_.fillCircle(cx, cy, 4, color);
    canvas_.fillCircle(cx, cy, 2, COL_BG);
    // Arco pequeño (radio 1) — punto de señal
    canvas_.fillCircle(cx, cy, 1, color);

    // Si desconectado, tachar con X pequeña
    if (wifiState_ == WifiState::Disconnected) {
        canvas_.drawLine(cx - 4, cy - 4, cx + 4, cy + 4, COL_RED);
        canvas_.drawLine(cx - 4, cy + 4, cx + 4, cy - 4, COL_RED);
    }
}

// =============================================================================
// drawBatteryIcon_ — icono de batería en esquina superior izquierda.
//
// Primitivas, NO sprites: LittleFS está al límite de capacidad (commit
// a238adf). Espejo del WiFi (sup-der) en la esquina sup-izq.
//
// Guards: oculto si !batteryValid_ (sin hardware soldado — ver
// docs/hardware-battery.md), en OtaProgress (mismo patrón que drawWifiIcon_),
// en StatsView (esa escena muestra el % como texto en el footer, ver
// drawStatsView_, para no duplicar la información) y en DeadScreen (esa
// escena reutiliza drawHudTop_ solo para el WiFi — sin este guard el icono de
// batería se pintaba encima del layout de la pantalla RIP, colisionando con
// el texto "DEAD"/edad de la esquina superior izquierda). Parpadeo si batería
// crítica (<=10%) y sin cargar — mismo patrón que WiFi Connecting.
//
// Geometría: cuerpo drawRect(12,22,22,12) + borne fillRect(34,25,3,6) — el
// bloque completo se desplazó 4px a la izquierda respecto a la versión
// original (cuerpo en x=16, borne acabando en x=41) porque con 3 status icons
// activos (drawStatusIconsTop_) el bloque centrado arranca en x=40 y el borne
// original colisionaba 1px con él (right edge 41 > 40). Con el desplazamiento,
// el borne acaba en x=37, sin solape.
// Relleno interior 18×8 en (14,24), ancho proporcional a batteryPercent_
// (mínimo 1 px si percent>0, para que "1%" siga siendo visible).
// Colores por umbral: COL_GREEN >50 / COL_AMBER 21-50 / COL_RED <=20.
// Rayo de carga (2 fillTriangle amarillos) si batteryCharging_ — vértices con
// y<=32 para quedar dentro del interior del cuerpo (el borde inferior del
// drawRect ocupa la fila y=33; un vértice en y=33 pintaba sobre ese borde).
// =============================================================================

void DisplayRendererColor::drawBatteryIcon_() {
    if (!batteryValid_)                    return;
    if (scene_ == SceneColor::OtaProgress) return;
    if (scene_ == SceneColor::StatsView)   return;
    if (scene_ == SceneColor::DeadScreen)  return;

    // Parpadeo si batería crítica y no está cargando (patrón WiFi Connecting).
    if (batteryPercent_ <= 10 && !batteryCharging_ && (animFrame_ % 2) != 0) return;

    uint16_t fillColor;
    if      (batteryPercent_ > 50) fillColor = COL_GREEN;
    else if (batteryPercent_ > 20) fillColor = COL_AMBER;
    else                            fillColor = COL_RED;

    // Cuerpo + borne, blancos.
    canvas_.drawRect(12, 22, 22, 12, COL_WHITE);
    canvas_.fillRect(34, 25, 3, 6, COL_WHITE);

    // Relleno interior proporcional al percent (mínimo 1 px si percent>0).
    int16_t fillW = static_cast<int16_t>(batteryPercent_) * 18 / 100;
    if (fillW <= 0 && batteryPercent_ > 0) fillW = 1;
    if (fillW > 0) {
        canvas_.fillRect(14, 24, fillW, 8, fillColor);
    }

    // Rayo de carga: 2 triángulos amarillos superpuestos formando un zigzag.
    // Vértices con y<=32 — el borde inferior del cuerpo (drawRect) ocupa la
    // fila y=33; antes el segundo triángulo llegaba a y=33 y pintaba encima.
    if (batteryCharging_) {
        canvas_.fillTriangle(23, 23, 18, 29, 23, 29, COL_YELLOW);
        canvas_.fillTriangle(21, 26, 21, 32, 26, 26, COL_YELLOW);
    }
}

// =============================================================================
// drawStatusIconsTop_ — iconos de estado grandes centrados en la banda superior.
//
// Muestra hasta 3 iconos 24×24 escalados ×2 (48×48 px) en la franja superior.
// Solo los needs activos aparecen; el bloque se centra horizontalmente en 240 px.
//
//   Orden fijo izq→der: ic-hunger (cubiertos) · ic-poop (caca) · ic-sick (cruz)
//   Umbrales: hunger>=80, poopCount>0, isSick
//   Gap entre iconos: kIconGap = 8 px
//   Y de los iconos: centrado verticalmente en la banda [kEdgeInset, kEdgeInset+kHudTopH]
//
// Fallback si los sprites no están cargados: dibuja primitivas simples
// (mismo estilo que los badges antiguos) para no perder la información de estado.
// =============================================================================

void DisplayRendererColor::drawStatusIconsTop_() {
    // Determinar cuáles needs están activos y en qué orden.
    // activeIdx[] guarda los índices en statusIcons_[] de los iconos a mostrar.
    static constexpr int16_t kIconSize = 48;   // 24px fuente × 2 escala
    static constexpr int16_t kIconGap  = 8;    // gap horizontal entre iconos
    static constexpr int16_t kIconSrcW = 24;   // ancho fuente del sprite

    uint8_t activeIdx[kStatusIconCount];
    uint8_t activeCount = 0;

    // Orden: hunger(0) · poop(1) · sick(2) — siempre este orden si activos
    if (stats_.hunger >= 80 && !stats_.isSleeping) activeIdx[activeCount++] = 0;
    if (stats_.poopCount > 0)                      activeIdx[activeCount++] = 1;
    if (stats_.isSick)                             activeIdx[activeCount++] = 2;

    if (activeCount == 0) return;  // ningún need activo — banda vacía

    // Centro vertical de la banda superior
    const int16_t bandCenterY = kEdgeInset + kHudTopH / 2;
    const int16_t iconY = bandCenterY - kIconSize / 2;

    // Ancho total del bloque centrado
    const int16_t blockW = activeCount * kIconSize + (activeCount - 1) * kIconGap;
    int16_t iconX = (kScreenW - blockW) / 2;

    for (uint8_t i = 0; i < activeCount; i++) {
        uint8_t idx = activeIdx[i];

        if (statusIcons_[idx].count > 0 && statusIcons_[idx].w > 0) {
            // Sprite real: escalar ×2 (24px → 48px) con colorkey
            const uint16_t* pixels = statusIcons_[idx].frames[0];
            if (pixels != nullptr) {
                drawSpriteScaled_(&canvas_, iconX, iconY,
                                  kIconSrcW, kIconSrcW,
                                  pixels, 2, kKeyColor);
            }
        } else {
            // Fallback con primitivas si el sprite no se pudo cargar en runtime
            switch (idx) {
                case 0:  // hunger — signo "!" ámbar
                    canvas_.fillRect(iconX + 18, iconY + 4,  12, 22, COL_AMBER);
                    canvas_.fillRect(iconX + 18, iconY + 30, 12, 10, COL_AMBER);
                    break;
                case 1:  // poop — círculo marrón
                    {
                        static constexpr uint16_t COL_BROWN = 0x6220;
                        canvas_.fillCircle(iconX + 24, iconY + 24, 16, COL_BROWN);
                        canvas_.fillCircle(iconX + 24, iconY + 24, 10, COL_BG);
                        canvas_.fillCircle(iconX + 24, iconY + 24,  6, COL_BROWN);
                    }
                    break;
                case 2:  // sick — cruz roja
                    canvas_.fillRect(iconX + 18, iconY + 4,  12, 40, COL_RED);
                    canvas_.fillRect(iconX + 4,  iconY + 18, 40, 12, COL_RED);
                    break;
                default:
                    break;
            }
        }

        iconX += kIconSize + kIconGap;
    }
}

// =============================================================================
// drawMenuIconPrimitive_ — icono simbólico con primitivas para Clean/Medicate.
//
// Se usa cuando la acción no tiene asset .bin dedicado de 24×24 en LittleFS.
//   Clean:    escoba esquemática (mango diagonal + cerdas)
//   Medicate: cápsula ovalada dividida al centro (2 mitades de color)
//
// cx/cy: centro del área de icono 24×24 px.
// =============================================================================

void DisplayRendererColor::drawMenuIconPrimitive_(ActionKind action,
                                                  int16_t cx, int16_t cy) {
    if (action == ActionKind::Clean) {
        // Escoba: mango diagonal (línea) de arriba-derecha a abajo-izquierda.
        // Cerdas: pequeño rectángulo en el extremo inferior.
        static constexpr uint16_t COL_WOOD  = 0xC640;  // marrón claro RGB565
        static constexpr uint16_t COL_STRAW = 0xFFE0;  // amarillo (paja/cerdas)
        // Mango — 3 líneas paralelas para dar grosor (diagonal /)
        canvas_.drawLine(cx - 8, cy + 8, cx + 6, cy - 8, COL_WOOD);
        canvas_.drawLine(cx - 7, cy + 8, cx + 7, cy - 8, COL_WOOD);
        canvas_.drawLine(cx - 6, cy + 8, cx + 8, cy - 8, COL_WOOD);
        // Cerdas — bloque en el extremo inferior-izquierdo
        canvas_.fillRect(cx - 12, cy + 4, 12, 6, COL_STRAW);
        // Detalle: separaciones verticales en las cerdas
        canvas_.drawLine(cx - 8, cy + 4, cx - 8, cy + 10, COL_BG);
        canvas_.drawLine(cx - 4, cy + 4, cx - 4, cy + 10, COL_BG);

    } else if (action == ActionKind::Medicate) {
        // Cápsula: óvalo horizontal dividido en dos mitades de color.
        // Mitad izquierda: roja; mitad derecha: blanca.
        // Implementado como dos fillRect dentro de un marco drawRoundRect.
        const int16_t rx = cx - 10, ry = cy - 5, rw = 20, rh = 10;
        canvas_.fillRoundRect(rx, ry, rw / 2, rh, 4, COL_RED);
        canvas_.fillRoundRect(rx + rw / 2, ry, rw / 2, rh, 4, COL_WHITE);
        // Marco exterior
        canvas_.drawRoundRect(rx, ry, rw, rh, 4, COL_GRAY);
        // Línea divisoria central
        canvas_.drawFastVLine(cx, ry, rh, COL_GRAY);
    }
}

// =============================================================================
// drawHudBottom_ — HUD inferior: icono + label de la acción seleccionada.
//
// Layout (60 px de alto, cardY = kScreenH - kEdgeInset - kHudBottomH):
//   Bloque [icono 24×24 | gap 6 | label size 2] centrado horizontalmente.
//   Icono centrado verticalmente en la mitad superior (y = cardY + 8..32).
//   Label centrado verticalmente en la mitad superior (y = cardY + 14).
//   Cursor dots en la franja inferior (y = cardY + 44).
//
// Si el sprite icon no está cargado (LittleFS no flasheado), se omite el icono
// y el label se centra en x normal — degradación sin crash.
// =============================================================================

void DisplayRendererColor::drawHudBottom_() {
    // Origen Y de la zona HUD inferior
    const int16_t cardY = kScreenH - kEdgeInset - kHudBottomH;

    if (visibleCount_ == 0) return;

    uint8_t    cursor = (selectedIndex_ < visibleCount_) ? selectedIndex_ : 0u;
    ActionKind action = visibleActions_[cursor];

    // ── Determinar si hay icono disponible para esta acción ──────────────────
    const bool hasPrimitive = menuIconIsPrimitive_(action);
    uint8_t    iconSlot     = menuIconSlot_(action);
    const bool hasSpriteIcon = !hasPrimitive
                             && menuIconsLoaded_
                             && iconSlot < kMenuIconCount
                             && menuIcons_[iconSlot].count > 0
                             && menuIcons_[iconSlot].w > 0;
    const bool hasIcon = hasPrimitive || hasSpriteIcon;

    // ── Dimensiones del bloque icono + label ─────────────────────────────────
    // Icono siempre renderizado en 24×24 px (escala ×1 para sprite, área fija
    // para primitivas). Label size 2: cada char es 12 px ancho, 16 px alto.
    const char*   label     = actionLabelColor(action, stats_.isSleeping);
    const int16_t charW     = 12;
    const int16_t labelW    = static_cast<int16_t>(strlen(label)) * charW;
    const int16_t iconW     = 24;
    const int16_t iconGap   = 6;   // separación icono → label

    // Ancho total del bloque: icono + gap + texto (o solo texto si sin icono).
    int16_t blockW = hasIcon ? (iconW + iconGap + labelW) : labelW;
    // Clamp: no puede exceder el ancho seguro de la card.
    const int16_t maxBlockW = kScreenW - 2 * kSafeTextX;
    if (blockW > maxBlockW) blockW = maxBlockW;

    // Coordenada X del inicio del bloque (centrado en pantalla).
    int16_t blockX = (kScreenW - blockW) / 2;
    if (blockX < kSafeTextX) blockX = kSafeTextX;

    // ── Centro Y del contenido (zona superior de la card) ────────────────────
    // La card tiene 60 px. Reservamos ~16 px en la franja inferior para los
    // cursor dots. El contenido (max 24 px de icono) se centra en los 44 px
    // superiores: contentCY = cardY + 44/2 = cardY + 22.
    const int16_t contentAreaH = kHudBottomH - 16;  // 44 px para contenido
    const int16_t contentCY    = cardY + contentAreaH / 2;

    // ── Dibujar icono ─────────────────────────────────────────────────────────
    if (hasIcon) {
        const int16_t iconX  = blockX;
        const int16_t iconCX = iconX + iconW / 2;
        const int16_t iconCY = contentCY;

        if (hasPrimitive) {
            // Icono con primitivas (Clean / Medicate)
            drawMenuIconPrimitive_(action, iconCX, iconCY);
        } else {
            // Icono cargado desde SpriteStore — frame 0 (estático)
            const ColorSpriteFrames& ic = menuIcons_[iconSlot];
            const uint16_t* pixels = ic.frames[0];
            if (pixels != nullptr) {
                // Esquina superior-izquierda del área de icono centrada en iconCX/iconCY
                const int16_t destX = iconCX - static_cast<int16_t>(ic.w) / 2;
                const int16_t destY = iconCY - static_cast<int16_t>(ic.h) / 2;
                drawSpriteScaled_(&canvas_, destX, destY,
                                  ic.w, ic.h, pixels,
                                  /*scale=*/1, kKeyColor);
            }
        }
    }

    // ── Dibujar label ────────────────────────────────────────────────────────
    // Centrado verticalmente respecto al contenido (16 px alto, size 2).
    canvas_.setTextColor(COL_WHITE);
    canvas_.setTextSize(2);
    const int16_t labelX = hasIcon ? (blockX + iconW + iconGap) : blockX;
    // Centrar verticalmente el texto (16 px) dentro de contentAreaH
    const int16_t labelY = contentCY - 8;  // -8 = mitad de la altura de la fuente
    canvas_.setCursor(labelX, labelY);
    canvas_.print(label);

    // ── Cursor dots ──────────────────────────────────────────────────────────
    // Franja inferior de la card: 6 px de alto, a 10 px del borde inferior.
    if (visibleCount_ > 1) {
        const int16_t dotSize = 6;
        const int16_t dotGap  = 3;
        int16_t totalW = visibleCount_ * (dotSize + dotGap) - dotGap;
        int16_t startX = (kScreenW - totalW) / 2;
        int16_t dotY   = cardY + kHudBottomH - 10 - dotSize;

        for (uint8_t i = 0; i < visibleCount_; i++) {
            int16_t dotX = startX + i * (dotSize + dotGap);
            if (i == cursor) {
                canvas_.fillRect(dotX, dotY, dotSize, dotSize, COL_WHITE);
            } else {
                canvas_.drawRect(dotX, dotY, dotSize, dotSize, COL_GRAY);
            }
        }
    }
}

// =============================================================================
// drawSprite_ — pinta el personaje en la zona central.
//
// Zona de personaje: y=40..231 (192 px de alto = 48×4), centrado en x.
// Si los sprites están cargados, pinta el frame apropiado para el mood actual.
// Si no hay sprites, muestra un placeholder (rectángulo con "?").
// =============================================================================

void DisplayRendererColor::drawSprite_() {
    // Zona de personaje: entre el borde inferior de la card top y el borde superior
    // de la card bottom, ambas dentro de la safe-area.
    //   Top card termina en: kEdgeInset + kHudTopH
    //   Bottom card empieza en: kScreenH - kEdgeInset - kHudBottomH
    //   Gap total disponible: (kScreenH - kEdgeInset - kHudBottomH) - (kEdgeInset + kHudTopH)
    // El sprite se centra en ese gap usando su tamaño real (w/h * kScale).
    const int16_t zoneTop    = kEdgeInset + kHudTopH;
    const int16_t zoneBottom = kScreenH - kEdgeInset - kHudBottomH;
    const int16_t zoneH      = zoneBottom - zoneTop;

    // Prioridad de selección de mood:
    //   1. Animación one-shot de acción activa (actionAnimRemainingMs_ > 0)
    //   2. Mood calculado por computeMood_() (isSleeping → Sleep, etc.)
    MoodColor mood = (actionAnimRemainingMs_ > 0) ? actionAnimMood_ : computeMood_();
    const ColorSpriteFrames* frames = framesForMood_(lifeStage_, mood);
    const int16_t spriteW = (frames && frames->w > 0) ? frames->w * kScale : 48 * kScale;
    const int16_t spriteH = (frames && frames->h > 0) ? frames->h * kScale : 48 * kScale;

    // Centrado horizontal en 240 px y vertical en la zona libre, con el tamaño
    // REAL del sprite — antes se usaba un 192 fijo que descentraba al cambiar kScale.
    const int16_t spriteX = (kScreenW - spriteW) / 2;
    const int16_t spriteY = zoneTop + (zoneH - spriteH) / 2;

    if (frames != nullptr && frames->count > 0 && frames->w > 0) {
        uint8_t frameIdx = static_cast<uint8_t>(animFrame_ % frames->count);
        const uint16_t* pixels = frames->frames[frameIdx];
        if (pixels != nullptr) {
            drawSpriteScaled_(&canvas_,
                              spriteX, spriteY,
                              frames->w, frames->h,
                              pixels,
                              kScale,
                              kKeyColor);
        }
    } else {
        // Fallback: rectángulo con "?" cuando no hay sprites
        canvas_.drawRect(spriteX, spriteY, spriteW, spriteH, COL_GRAY);
        canvas_.setTextColor(COL_GRAY);
        canvas_.setTextSize(4);
        canvas_.setCursor(spriteX + spriteW / 2 - 12, spriteY + spriteH / 2 - 16);
        canvas_.print("?");
    }

    // ── Canal overlay: pinta el sprite de acción encima del personaje ────────
    // El overlay se renderiza a escala 2 (48×48 → 96×96 px) para ser visible
    // sin tapar el cuerpo completo del personaje (192×192 px a escala 4).
    // Posición por tipo de acción (elegida para resultar natural):
    //   broom   (Clean)           → pies del personaje, alineado al borde izq
    //                               del sprite: la escoba barre desde abajo-izq.
    //   syringe (Heal/Medicate)   → costado derecho a media altura: el médico
    //                               aplica la inyección en el brazo.
    //   ball    (Play)            → borde inferior-derecho: la pelota rebota
    //                               frente al personaje.
    if (overlayAnimRemainingMs_ > 0 && overlayAnimFrames_ != nullptr
            && overlayAnimFrames_->count > 0) {

        constexpr uint8_t kOverlayScale = 2;  // 48×48 → 96×96 px

        // Seleccionar frame cíclico con el contador propio del overlay
        uint8_t ovFrameIdx = static_cast<uint8_t>(
            overlayAnimFrame_ % overlayAnimFrames_->count);
        const uint16_t* ovPixels = overlayAnimFrames_->frames[ovFrameIdx];

        if (ovPixels != nullptr) {
            const int16_t ovW = static_cast<int16_t>(overlayAnimFrames_->w) * kOverlayScale;
            const int16_t ovH = static_cast<int16_t>(overlayAnimFrames_->h) * kOverlayScale;

            int16_t ovX, ovY;

            if (overlayAnimFrames_ == &overlayBroom_) {
                // Escoba: pies del personaje, esquina inferior izquierda del sprite.
                // spriteX es el borde izquierdo del personaje (24 px desde el borde
                // de pantalla); anclar el overlay ahí para que la escoba quede
                // visualmente a los pies del bicho.
                ovX = spriteX;
                ovY = spriteY + spriteH - ovH;
            } else if (overlayAnimFrames_ == &overlaySyringe_) {
                // Jeringa: costado derecho a media altura del personaje.
                // Se posiciona solapando el brazo derecho (lado derecho del sprite).
                ovX = spriteX + spriteW - ovW / 2;
                ovY = spriteY + (spriteH - ovH) / 2;
            } else {
                // Pelota (ball): esquina inferior-derecha, delante del personaje.
                ovX = spriteX + spriteW - ovW;
                ovY = spriteY + spriteH - ovH;
            }

            drawSpriteScaled_(&canvas_,
                              ovX, ovY,
                              overlayAnimFrames_->w, overlayAnimFrames_->h,
                              ovPixels,
                              kOverlayScale,
                              kKeyColor);
        }
    }
}

// =============================================================================
// drawOverlayBadges_ — overlay de sueño sobre el personaje.
//
// D1: sick/hungry/poop se muestran como iconos grandes en la banda superior
// (drawStatusIconsTop_). Solo la "Z" de sueño permanece aquí como overlay
// pequeño sobre el personaje, ya que contextualmente va con el sprite dormido.
// =============================================================================

void DisplayRendererColor::drawOverlayBadges_() {
    if (!stats_.isSleeping) return;

    // "Z" flotante en la esquina superior derecha del área de personaje.
    // badgeX/badgeY se sitúan dentro del safe-area y sobre el sprite.
    const int16_t badgeX = kScreenW - kEdgeInset - 30;
    const int16_t badgeY = kEdgeInset + kHudTopH + 8;
    drawBadgeSleep_(badgeX, badgeY);
}

// Badge: "Z" cian parpadeante para sueño — overlay pequeño sobre el personaje.
void DisplayRendererColor::drawBadgeSleep_(int16_t cx, int16_t cy) {
    // Parpadea al ritmo del sprite (animFrame_/2 % 2)
    if ((animFrame_ / 2) % 2 == 0) {
        canvas_.setTextColor(COL_CYAN);
        canvas_.setTextSize(2);
        canvas_.setCursor(cx - 6, cy - 8);
        canvas_.print("Z");
    }
}

// =============================================================================
// drawStatBar_ — barra de stats coloreada por umbral.
//
// Verde  >60  — stat saludable
// Ámbar  30-60 — stat en advertencia
// Rojo   <30  — stat crítico
//
// Para hunger (donde alto=malo), el color se invierte:
// el llamante puede pasar (100 - hunger) como value para la barra.
// =============================================================================

void DisplayRendererColor::drawStatBar_(int16_t x, int16_t y,
                                        int16_t barW, int16_t barH,
                                        int value, int valueMax,
                                        const char* label) {
    // Etiqueta a la izquierda
    canvas_.setTextColor(COL_WHITE);
    canvas_.setTextSize(1);
    canvas_.setCursor(x, y + 1);
    canvas_.print(label);

    // Coordenadas de la barra (tras la etiqueta de 5 chars × 6px = 30px)
    const int16_t bx = x + 30;
    const int16_t by = y - 6;

    // Clampar valor
    if (value < 0)        value = 0;
    if (value > valueMax) value = valueMax;

    // Color según nivel
    uint16_t fillColor;
    int pct = (valueMax > 0) ? (value * 100 / valueMax) : 0;
    if      (pct > 60) fillColor = COL_GREEN;
    else if (pct > 30) fillColor = COL_AMBER;
    else               fillColor = COL_RED;

    // Marco
    canvas_.drawRect(bx, by, barW, barH, COL_GRAY);

    // Relleno
    int fillWidth = (barW > 2 && valueMax > 0) ? (value * (barW - 2)) / valueMax : 0;
    if (fillWidth > 0) {
        canvas_.fillRect(bx + 1, by + 1, fillWidth, barH - 2, fillColor);
    }

    // Valor numérico tras la barra
    char numBuf[6];
    snprintf(numBuf, sizeof(numBuf), "%3d", value);
    canvas_.setTextColor(COL_WHITE);
    canvas_.setCursor(bx + barW + 3, y + 1);
    canvas_.print(numBuf);
}

// =============================================================================
// drawIdleMain_ — pantalla principal: HUD top + personaje + overlays + HUD bottom.
// =============================================================================

void DisplayRendererColor::drawIdleMain_() {
    // Fondo de toda la pantalla
    canvas_.fillScreen(COL_BG);

    // D1: iconos de estado grandes en banda superior + WiFi en esquina
    drawStatusIconsTop_();
    drawHudTop_();

    drawSprite_();
    // Overlay de sueño ("Z") — único badge que permanece sobre el personaje
    drawOverlayBadges_();
    drawHudBottom_();

    // Flush: vuelca el framebuffer completo al panel en un único bloque SPI.
    canvas_.flush();
}

// =============================================================================
// drawStatsView_ — vista densa con las stats.
//
// D1: etapa de vida y edad se muestran aquí (se movieron desde drawHudTop_).
// Layout portrait 240×296 (safe-area):
//   Etapa (size 2) en kCornerSafe,kCornerSafe  +  edad (size 1) bajo la etapa
//   WiFi (drawHudTop_) en esquina sup-der
//   Título "STATS" a kSafeTextX, bajo la card top
//   Barras de stats a kSafeTextX con ancho ajustado al safe-area
//   Flags booleanos, edad y deathCount también a kSafeTextX
// =============================================================================

void DisplayRendererColor::drawStatsView_() {
    canvas_.fillScreen(COL_BG);

    // ── Etapa de vida + edad en esquina superior izquierda ────────────────────
    // D1: movidas desde drawHudTop_ (que ahora solo contiene el WiFi).
    // kCornerSafe en x e y garantiza que ningún píxel quede dentro de la
    // zona curvada de la esquina superior-izquierda del cristal.
    canvas_.setTextColor(COL_WHITE);
    canvas_.setTextSize(2);
    canvas_.setCursor(kCornerSafe, kCornerSafe);
    canvas_.print(lifeStageLabel(lifeStage_));

    char headerAgeBuf[12];
    if (ageMinutes_ < 60) {
        snprintf(headerAgeBuf, sizeof(headerAgeBuf), "%um", static_cast<unsigned>(ageMinutes_));
    } else {
        uint32_t h = ageMinutes_ / 60;
        uint32_t m = ageMinutes_ % 60;
        if (h > 99) h = 99;
        snprintf(headerAgeBuf, sizeof(headerAgeBuf), "%uh%um",
                 static_cast<unsigned>(h), static_cast<unsigned>(m));
    }
    canvas_.setTextColor(COL_GRAY);
    canvas_.setTextSize(1);
    canvas_.setCursor(kCornerSafe, kCornerSafe + 18);
    canvas_.print(headerAgeBuf);

    // WiFi en esquina sup-der (sigue en drawHudTop_)
    drawHudTop_();

    // Título — respeta la safe-area en X y arranca bajo la card superior
    const int16_t contentStartY = kEdgeInset + kHudTopH + 8;
    canvas_.setTextColor(COL_YELLOW);
    canvas_.setTextSize(2);
    canvas_.setCursor(kSafeTextX, contentStartY);
    canvas_.print("STATS");
    // Línea separadora dentro de la safe-area horizontal
    canvas_.drawFastHLine(kEdgeInset, contentStartY + 18, kScreenW - 2 * kEdgeInset, COL_GRAY);

    // Barras de stats — ancho ajustado para caber en safe-area.
    // barW ocupa desde (kSafeTextX + 30) hasta (kScreenW - kEdgeInset - 30):
    // con kSafeTextX=16 y kEdgeInset=8 → barra de ~156 px.
    const int16_t barW = kScreenW - kSafeTextX - 30 - kEdgeInset - 30;
    const int16_t barH = 8;
    const int16_t x0   = kSafeTextX;
    int16_t       rowY = contentStartY + 34;
    const int16_t rowH = 20;

    drawStatBar_(x0, rowY, barW, barH,
                 100 - stats_.hunger, 100, "Hamb");  // invertida: lleno = bien
    rowY += rowH;
    drawStatBar_(x0, rowY, barW, barH, stats_.happiness,  100,  "Anim");
    rowY += rowH;
    drawStatBar_(x0, rowY, barW, barH, stats_.health,     100,  "Salu");
    rowY += rowH;
    drawStatBar_(x0, rowY, barW, barH, stats_.discipline, 100,  "Disc");
    rowY += rowH;
    drawStatBar_(x0, rowY, barW, barH, stats_.weight,     100,  "Peso");
    rowY += rowH;
    // attentionScore 0-1000 → mapear a 0-100 para la barra
    drawStatBar_(x0, rowY, barW, barH,
                 stats_.attentionScore / 10, 100, "Aten");
    rowY += rowH + 8;

    // Separador dentro de la safe-area
    canvas_.drawFastHLine(kEdgeInset, rowY - 4, kScreenW - 2 * kEdgeInset, COL_GRAY);

    // Flags booleanos — a kSafeTextX para no tocar el borde físico
    canvas_.setTextColor(COL_WHITE);
    canvas_.setTextSize(1);
    char flagBuf[40];
    snprintf(flagBuf, sizeof(flagBuf), "Enf:%c  Dor:%c  Caca:%u",
             stats_.isSick     ? 'Y' : 'N',
             stats_.isSleeping ? 'Y' : 'N',
             static_cast<unsigned>(stats_.poopCount));
    canvas_.setCursor(kSafeTextX, rowY);
    canvas_.print(flagBuf);

    // Edad
    rowY += 16;
    char ageBuf[16];
    if (ageMinutes_ >= 60) {
        snprintf(ageBuf, sizeof(ageBuf), "Edad: %uh %um",
                 static_cast<unsigned>(ageMinutes_ / 60),
                 static_cast<unsigned>(ageMinutes_ % 60));
    } else {
        snprintf(ageBuf, sizeof(ageBuf), "Edad: %um",
                 static_cast<unsigned>(ageMinutes_));
    }
    canvas_.setCursor(kSafeTextX, rowY);
    canvas_.print(ageBuf);

    // DeathCount — clampado para no salir de la safe-area inferior
    rowY += 16;
    if (rowY < kScreenH - kEdgeInset - 8) {
        char ripBuf[20];
        snprintf(ripBuf, sizeof(ripBuf), "RIPs: %u", static_cast<unsigned>(deathCount_));
        canvas_.setCursor(kSafeTextX, rowY);
        canvas_.print(ripBuf);
    }

    // Batería — solo si hay lectura válida (icono del HUD se oculta en esta
    // escena, ver drawBatteryIcon_). Mismo estilo tipográfico (size 1) que el
    // header de edad de arriba; " CHG" en amarillo si está cargando.
    if (batteryValid_) {
        rowY += 16;
        if (rowY < kScreenH - kEdgeInset - 8) {
            char batBuf[12];
            snprintf(batBuf, sizeof(batBuf), "Bat: %u%%", static_cast<unsigned>(batteryPercent_));
            canvas_.setTextColor(COL_WHITE);
            canvas_.setTextSize(1);
            canvas_.setCursor(kSafeTextX, rowY);
            canvas_.print(batBuf);
            if (batteryCharging_) {
                canvas_.setTextColor(COL_YELLOW);
                canvas_.print(" CHG");
            }
        }
    }

    canvas_.flush();
}

// =============================================================================
// drawOtaProgress_ — barra de progreso OTA.
//
// Safe-area: título y barra respetan kEdgeInset para no tocar el borde físico.
// =============================================================================

void DisplayRendererColor::drawOtaProgress_() {
    canvas_.fillScreen(COL_BLACK);

    // Título centrado horizontalmente, posicionado en el tercio superior
    canvas_.setTextColor(COL_WHITE);
    canvas_.setTextSize(2);
    // "Updating..." = 11 chars × 12 px = 132 px
    const int16_t titleX = (kScreenW - 132) / 2;
    canvas_.setCursor(titleX, kScreenH / 4);
    canvas_.print("Updating...");

    // Porcentaje — tamaño 3, centrado
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%u%%", static_cast<unsigned>(otaProgress_));
    canvas_.setTextSize(3);
    // Fuente size 3: 18 px por char
    int16_t pctX = (kScreenW - static_cast<int16_t>(strlen(pctBuf)) * 18) / 2;
    canvas_.setCursor(pctX, kScreenH / 4 + 40);
    canvas_.print(pctBuf);

    // Barra de progreso dentro de la safe-area:
    // x=kEdgeInset, w=kScreenW-2*kEdgeInset, height=20, centrada verticalmente
    const int16_t barX = kEdgeInset;
    const int16_t barY = kScreenH / 2 + 10;
    const int16_t barW = kScreenW - 2 * kEdgeInset;
    const int16_t barH = 20;
    canvas_.drawRect(barX, barY, barW, barH, COL_WHITE);
    int fillW = (static_cast<int>(otaProgress_) * (barW - 2)) / 100;
    if (fillW > 0) {
        canvas_.fillRect(barX + 1, barY + 1, fillW, barH - 2, COL_GREEN);
    }

    canvas_.flush();
}

// =============================================================================
// drawDeadScreen_ — pantalla RIP con causa, edad y contador de muertes.
//
// Layout portrait 240×296 (safe-area):
//   HUD top card (reutilizado — etapa "DEAD" + edad)
//   Ghost sprite escalado ×2 (96×96 px) centrado en zona libre
//   "RIP" grande centrado en x
//   Causa, edad y RIPs a kSafeTextX (fuera del borde físico)
// =============================================================================

void DisplayRendererColor::drawDeadScreen_() {
    canvas_.fillScreen(COL_BLACK);

    // Etapa "DEAD" + edad en esquina superior izquierda (igual que StatsView).
    // drawHudTop_ solo tiene WiFi; aquí reproducimos el texto de etapa/edad.
    canvas_.setTextColor(COL_RED);
    canvas_.setTextSize(2);
    canvas_.setCursor(kCornerSafe, kCornerSafe);
    canvas_.print("DEAD");

    char deadAgeBuf[12];
    if (ageMinutes_ < 60) {
        snprintf(deadAgeBuf, sizeof(deadAgeBuf), "%um", static_cast<unsigned>(ageMinutes_));
    } else {
        uint32_t h = ageMinutes_ / 60;
        uint32_t m = ageMinutes_ % 60;
        if (h > 99) h = 99;
        snprintf(deadAgeBuf, sizeof(deadAgeBuf), "%uh%um",
                 static_cast<unsigned>(h), static_cast<unsigned>(m));
    }
    canvas_.setTextColor(COL_GRAY);
    canvas_.setTextSize(1);
    canvas_.setCursor(kCornerSafe, kCornerSafe + 18);
    canvas_.print(deadAgeBuf);

    // WiFi en esquina sup-der
    drawHudTop_();

    // Zona libre bajo la card top: arrancar en kEdgeInset + kHudTopH
    const int16_t ghostZoneTop = kEdgeInset + kHudTopH + 4;

    // Sprite ghost escalado ×2 (96×96 px) centrado horizontalmente.
    const ColorSpriteFrames* ghostFrames =
        framesForMood_(LifeStage::Dead, MoodColor::Idle);

    if (ghostFrames != nullptr && ghostFrames->count > 0 && ghostFrames->w > 0) {
        uint8_t frameIdx = static_cast<uint8_t>(animFrame_ % ghostFrames->count);
        const uint16_t* pixels = ghostFrames->frames[frameIdx];
        if (pixels != nullptr) {
            const int16_t dX = (kScreenW - static_cast<int16_t>(ghostFrames->w) * 2) / 2;
            drawSpriteScaled_(&canvas_, dX, ghostZoneTop,
                              ghostFrames->w, ghostFrames->h,
                              pixels, 2, kKeyColor);
        }
    } else {
        // Fallback: calavera con primitivas si el ghost no se cargó
        const int16_t skullCX = kScreenW / 2;
        const int16_t skullCY = ghostZoneTop + 30;
        canvas_.fillCircle(skullCX, skullCY, 30, COL_GRAY);
        canvas_.fillRect(skullCX - 20, skullCY + 10, 40, 20, COL_GRAY);
        canvas_.fillCircle(skullCX - 11, skullCY - 2, 6, COL_BLACK);  // ojo izq
        canvas_.fillCircle(skullCX + 11, skullCY - 2, 6, COL_BLACK);  // ojo der
    }

    // "RIP" grande centrado — fuente size 4 (24×32 px/char), 3 chars = 72 px
    // Se coloca tras el sprite (~96 px de alto desde ghostZoneTop)
    const int16_t ripY = ghostZoneTop + 100;
    canvas_.setTextColor(COL_RED);
    canvas_.setTextSize(4);
    canvas_.setCursor((kScreenW - 72) / 2, ripY);
    canvas_.print("RIP");

    // Causa de muerte — a kSafeTextX para no tocar el borde físico
    const char* causeText;
    switch (lastDeathCause_) {
        case 1:  causeText = "De hambre";   break;
        case 2:  causeText = "Enfermo";     break;
        case 3:  causeText = "Vejez";       break;
        case 4:  causeText = "Tristeza";    break;
        case 5:  causeText = "Desconocida"; break;
        default: causeText = "Causa N/A";   break;
    }
    canvas_.setTextColor(COL_WHITE);
    canvas_.setTextSize(1);
    canvas_.setCursor(kSafeTextX, ripY + 40);
    canvas_.print(causeText);

    // Edad
    char ageBuf[20];
    if (ageMinutes_ >= 60) {
        snprintf(ageBuf, sizeof(ageBuf), "Vivio: %uh %um",
                 static_cast<unsigned>(ageMinutes_ / 60),
                 static_cast<unsigned>(ageMinutes_ % 60));
    } else {
        snprintf(ageBuf, sizeof(ageBuf), "Vivio: %um",
                 static_cast<unsigned>(ageMinutes_));
    }
    canvas_.setCursor(kSafeTextX, ripY + 56);
    canvas_.print(ageBuf);

    // Contador de muertes totales
    char ripBuf[20];
    snprintf(ripBuf, sizeof(ripBuf), "Total RIPs: %u", static_cast<unsigned>(deathCount_));
    canvas_.setCursor(kSafeTextX, ripY + 72);
    canvas_.print(ripBuf);

    canvas_.flush();
}
