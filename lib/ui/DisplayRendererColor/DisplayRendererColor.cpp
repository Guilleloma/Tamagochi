/**
 * DisplayRendererColor.cpp — renderer a color para ESP32-S3 + TFT JD9853.
 *
 * FASE 6: sprites reales por etapa de vida.
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

    // ── Sprites de overlay de acción (48×48, precargados una sola vez) ───────
    // Se usan como segundo canal de animación encima del personaje.
    // IDs registrados en el manifest bajo las carpetas "anim-broom",
    // "anim-syringe" y "anim-ball" para respetar el límite LFS_NAME_MAX=31.
    memset(&overlayBroom_,   0, sizeof(overlayBroom_));
    memset(&overlaySyringe_, 0, sizeof(overlaySyringe_));
    memset(&overlayBall_,    0, sizeof(overlayBall_));

    // IDs cortos definidos en el manifest. Al estar dentro de los 31 chars del
    // tool-mklittlefs, el campo "id" y "folder" pueden coincidir en estos sprites.
    if (!sprites_.loadAnimation("anim-broom",   "broom",   overlayBroom_)) {
        LOG_ERROR("Overlay: fallo cargando broom (Clean)");
        allOk = false;
    }
    if (!sprites_.loadAnimation("anim-syringe", "syringe", overlaySyringe_)) {
        LOG_ERROR("Overlay: fallo cargando syringe (Heal/Medicate)");
        allOk = false;
    }
    if (!sprites_.loadAnimation("anim-ball",    "ball",    overlayBall_)) {
        LOG_ERROR("Overlay: fallo cargando ball (Play)");
        allOk = false;
    }

    LOG_INFO("DisplayRendererColor: precarga completa. PSRAM libre: %u B",
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
// drawHudTop_ — HUD superior: etapa de vida, edad, icono WiFi.
//
// Texto flotante sobre fondo azul (COL_BG), sin card de fondo.
//   Título (etapa, size 2)   en x=kCornerSafe, y=kCornerSafe
//   Subtítulo (edad, size 1) en x=kCornerSafe, y=kCornerSafe+18
//   Icono WiFi centrado en cx=kScreenW-kCornerSafe, cy=kCornerSafe
//
// kCornerSafe desplaza ambos elementos (texto e icono) lo suficiente para
// quedar fuera del cuarto de círculo recortado por la curvatura del cristal.
// =============================================================================

void DisplayRendererColor::drawHudTop_() {
    // Sin card de fondo — el texto flota sobre el azul de COL_BG (pintado por
    // drawIdleMain_ con fillScreen).

    // Etapa de vida — título size 2 (12×16 px/char).
    // kCornerSafe en x e y garantiza que ningún píxel quede dentro de la
    // zona curvada de la esquina superior-izquierda del cristal.
    canvas_.setTextColor(COL_WHITE);
    canvas_.setTextSize(2);
    canvas_.setCursor(kCornerSafe, kCornerSafe);
    canvas_.print(lifeStageLabel(lifeStage_));

    // Edad — subtítulo size 1 (6×8 px), a 18 px bajo el baseline del título
    // (1 línea size 2 = 16 px alto; 2 px de gap).
    char ageBuf[12];
    if (ageMinutes_ < 60) {
        snprintf(ageBuf, sizeof(ageBuf), "%um", static_cast<unsigned>(ageMinutes_));
    } else {
        uint32_t h = ageMinutes_ / 60;
        uint32_t m = ageMinutes_ % 60;
        if (h > 99) h = 99;
        snprintf(ageBuf, sizeof(ageBuf), "%uh%um",
                 static_cast<unsigned>(h), static_cast<unsigned>(m));
    }
    canvas_.setTextColor(COL_GRAY);
    canvas_.setTextSize(1);
    canvas_.setCursor(kCornerSafe, kCornerSafe + 18);
    canvas_.print(ageBuf);

    drawWifiIcon_();
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
// drawHudBottom_ — HUD inferior: acción activa (label + cursor dots).
//
// Texto flotante sobre fondo azul (COL_BG), sin card de fondo.
//   Label centrado (size 2) a cardY+8, respetando safe-area inferior.
//   Cursor dots centrados en x, a 10 px del borde inferior de la zona.
// =============================================================================

void DisplayRendererColor::drawHudBottom_() {
    // Origen Y de la zona HUD inferior — sin card de fondo; kEdgeInset marca
    // el margen mínimo respecto a la curvatura del borde físico.
    const int16_t cardY = kScreenH - kEdgeInset - kHudBottomH;

    if (visibleCount_ == 0) return;

    uint8_t cursor = (selectedIndex_ < visibleCount_) ? selectedIndex_ : 0u;
    ActionKind action = visibleActions_[cursor];

    // Label de la acción seleccionada — size 2 (12×16 px/char), centrado en x.
    // El clamp mínimo usa kSafeTextX para no salir de la card.
    const char* label = actionLabelColor(action, stats_.isSleeping);
    canvas_.setTextColor(COL_WHITE);
    canvas_.setTextSize(2);
    int16_t labelX = (kScreenW - static_cast<int16_t>(strlen(label)) * 12) / 2;
    if (labelX < kSafeTextX) labelX = kSafeTextX;
    // Y del label: tercio superior de la card (cardY + 8 px de margen interno)
    canvas_.setCursor(labelX, cardY + 8);
    canvas_.print(label);

    // Cursor dots — cuadrados 6×6 px centrados en x, en la parte baja de la card.
    // Se sitúan a 10 px del borde inferior de la card para quedar bien dentro.
    if (visibleCount_ > 1) {
        const int16_t dotSize = 6;
        const int16_t dotGap  = 3;
        int16_t totalW = visibleCount_ * (dotSize + dotGap) - dotGap;
        int16_t startX = (kScreenW - totalW) / 2;
        // dotY: borde inferior de la card menos margen interno (10 px) menos tamaño dot
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
    // El sprite 192×192 se centra verticalmente en ese gap.
    const int16_t zoneTop    = kEdgeInset + kHudTopH;
    const int16_t zoneBottom = kScreenH - kEdgeInset - kHudBottomH;
    const int16_t zoneH      = zoneBottom - zoneTop;
    // Centrado horizontal en 240 px y vertical en la zona libre.
    const int16_t spriteX = (kScreenW - 192) / 2;
    const int16_t spriteY = zoneTop + (zoneH - 192) / 2;

    // Prioridad de selección de mood:
    //   1. Animación one-shot de acción activa (actionAnimRemainingMs_ > 0)
    //   2. Mood calculado por computeMood_() (isSleeping → Sleep, etc.)
    MoodColor mood = (actionAnimRemainingMs_ > 0) ? actionAnimMood_ : computeMood_();
    const ColorSpriteFrames* frames = framesForMood_(lifeStage_, mood);
    const int16_t spriteW = (frames && frames->w > 0) ? frames->w * kScale : 192;
    const int16_t spriteH = (frames && frames->h > 0) ? frames->h * kScale : 192;

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
// drawOverlayBadges_ — iconos de estado activos sobre el personaje.
//
// Los badges se dibujan después del sprite, en la esquina superior derecha
// de la zona de personaje (x=192..239, y=40..80), de 20×20 px cada uno.
// Se apilan verticalmente si hay varios activos.
//
// Badges implementados con primitivas (sin assets separados):
//   SICK:    calavera simplificada (círculo + marcas ojos)
//   SLEEP:   "Z" grande
//   HUNGRY:  signo de exclamación (!)
//   POOP:    espiral (círculo concéntrico marrón)
// =============================================================================

void DisplayRendererColor::drawOverlayBadges_() {
    // badgeX: suficientemente a la izquierda del borde derecho para quedar
    // dentro del safe-area (kScreenW - kEdgeInset - 30 da 202 con inset=8).
    int16_t badgeX = kScreenW - kEdgeInset - 30;
    // badgeY: justo bajo la card superior (kEdgeInset + kHudTopH + gap)
    int16_t badgeY = kEdgeInset + kHudTopH + 8;
    const int16_t badgeStep = 26;

    if (stats_.isSick) {
        drawBadgeSick_(badgeX, badgeY);
        badgeY += badgeStep;
    }
    if (stats_.isSleeping) {
        drawBadgeSleep_(badgeX, badgeY);
        badgeY += badgeStep;
    }
    if (stats_.hunger >= 80 && !stats_.isSleeping) {
        drawBadgeHungry_(badgeX, badgeY);
        badgeY += badgeStep;
    }
    if (stats_.poopCount > 0) {
        drawBadgePoop_(badgeX, badgeY);
    }
}

// Badge: cruz roja (símbolo de enfermedad)
void DisplayRendererColor::drawBadgeSick_(int16_t cx, int16_t cy) {
    canvas_.fillRect(cx - 3, cy - 8, 6, 16, COL_RED);
    canvas_.fillRect(cx - 8, cy - 3, 16, 6, COL_RED);
}

// Badge: "Z" para dormir (blanco sobre fondo oscuro)
void DisplayRendererColor::drawBadgeSleep_(int16_t cx, int16_t cy) {
    // Parpadea al ritmo del sprite (animFrame_/2 % 2)
    if ((animFrame_ / 2) % 2 == 0) {
        canvas_.setTextColor(COL_CYAN);
        canvas_.setTextSize(2);
        canvas_.setCursor(cx - 6, cy - 8);
        canvas_.print("Z");
    }
}

// Badge: "!" ámbar para hambre
void DisplayRendererColor::drawBadgeHungry_(int16_t cx, int16_t cy) {
    canvas_.fillRect(cx - 3, cy - 8, 6, 10, COL_AMBER);
    canvas_.fillRect(cx - 3, cy + 4, 6, 5, COL_AMBER);
}

// Badge: espiral marrón para caca (círculos concéntricos)
void DisplayRendererColor::drawBadgePoop_(int16_t cx, int16_t cy) {
    static constexpr uint16_t COL_BROWN = 0x6220;
    canvas_.fillCircle(cx, cy, 8, COL_BROWN);
    canvas_.fillCircle(cx, cy, 5, COL_BG);
    canvas_.fillCircle(cx, cy, 3, COL_BROWN);
    // Número de cacas si hay más de 1
    if (stats_.poopCount > 1) {
        canvas_.setTextColor(COL_WHITE);
        canvas_.setTextSize(1);
        canvas_.setCursor(cx - 3, cy - 3);
        char buf[3];
        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(stats_.poopCount));
        canvas_.print(buf);
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

    drawHudTop_();
    drawSprite_();
    drawOverlayBadges_();
    drawHudBottom_();

    // Flush: vuelca el framebuffer completo al panel en un único bloque SPI.
    canvas_.flush();
}

// =============================================================================
// drawStatsView_ — vista densa con las stats.
//
// Layout portrait 240×296 (safe-area):
//   HUD top card (reutilizado — ya respeta safe-area)
//   Título "STATS" a kSafeTextX, bajo la card top
//   Barras de stats a kSafeTextX con ancho ajustado al safe-area
//   Flags booleanos, edad y deathCount también a kSafeTextX
// =============================================================================

void DisplayRendererColor::drawStatsView_() {
    canvas_.fillScreen(COL_BG);

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

    // HUD top — mantiene el nombre de etapa "DEAD" y la edad (ya usa safe-area)
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
