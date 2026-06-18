/**
 * DisplayRendererColor.cpp — renderer a color para ESP32-S3 + TFT JD9853.
 *
 * Decisiones de diseño:
 *
 * 1. Archivos nuevos en lib/ui/DisplayRendererColor/ — no toca DisplayRenderer.(h|cpp).
 *    Razón: el mono sigue compilando en esp12e sin cambios. Evitar ifdef platform
 *    que contaminarían ambos renderers.
 *
 * 2. WifiState se incluye desde WifiManager.h directamente. El .h es portable
 *    (sin ESP8266WiFi.h); solo el .cpp arrastra WiFi. WifiManager está en lib_ignore
 *    para el build esp32s3, así que el .cpp no se compila. El .h sí es accesible
 *    porque lib_extra_dirs incluye lib/infra.
 *
 * 3. Persistence.h solo aporta el enum LifeStage — también portable. Su .cpp
 *    incluye LittleFsStorage que usa <LittleFS.h>. En esp32s3 LittleFS está
 *    disponible como dependencia, así que sacar Persistence de lib_ignore.
 *    LittleFsStorage.cpp se compila solo si LittleFS existe en target — ok para esp32s3.
 *
 * 4. EventBus y StatsEngine son puramente portables (no incluyen Arduino.h).
 *    Se eliminan de lib_ignore en esp32s3.
 *
 * 5. LifeFSM depende de Persistence.h (portátil) y EventBus (portátil).
 *    Sin dependencias ESP8266. Se puede sacar de lib_ignore también.
 *
 * 6. Logger.h es header-only con guard #ifdef ARDUINO → Serial / printf. Portable.
 *    Se saca de lib_ignore.
 *
 * 7. Tabla de sprites kSpritesByStageColor: todas las etapas apuntan a "wiz"
 *    (único sprite disponible en Fase 2). Documentado como placeholder hasta Fase 6.
 *
 * 8. render() tick period: el renderer asume kRenderPeriodMs para decrementar
 *    timers de animación. Si el harness corre a fps diferente, los timers se
 *    agotan a ritmo distinto — aceptable para Fase 2.
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

// Fondo de pantalla — azul oscuro, más agradable que negro puro.
static constexpr uint16_t COL_BG = 0x0010;

// ─── Tabla de mapeo sprites por (LifeStage, Mood) ────────────────────────────
//
// FASE 2 PLACEHOLDER: todas las etapas apuntan al único sprite disponible "wiz".
// Animaciones reales por etapa se añaden en Fase 6 cuando lleguen los assets.
//
// Mapeado de moods a animaciones disponibles en "wiz":
//   Idle   → "walk"    (no hay idle dedicado; walk transmite movimiento)
//   Happy  → "happy"
//   Sad    → "sick"    (sick visualmente comunica tristeza/malestar)
//   Eat    → "eat"
//
// Los overlays de estado (isSleeping → "sleep") se gestionan aparte en
// drawOverlayBadges_ + drawSprite_ (la animación sleep sustituye al personaje
// cuando isSleeping está activo).
//
// Orden de filas: Egg(0), Baby(1), Kid(2), Adult(3), Old(4), Dead(5)
// Orden de cols:  Idle(0), Happy(1), Sad(2), Eat(3)
// ─────────────────────────────────────────────────────────────────────────────

static const SpriteMapping kSpritesByStageColor[6][4] = {
    // Egg — usa walk/sick como placeholder hasta tener sprite de huevo
    {
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "walk" },  // Idle
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "walk" },  // Happy
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "sick" },  // Sad
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "eat"  },  // Eat
    },
    // Baby (1)
    {
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "walk"  },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "happy" },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "sick"  },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "eat"   },
    },
    // Kid (2)
    {
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "walk"  },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "happy" },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "sick"  },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "eat"   },
    },
    // Adult (3)
    {
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "walk"  },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "happy" },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "sick"  },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "eat"   },
    },
    // Old (4)
    {
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "walk"  },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "walk"  },  // Happy → walk (viejo cansado)
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "sick"  },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "eat"   },
    },
    // Dead (5) — usa sick como proxy visual de estado muerto hasta tener ghost sprite
    {
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "sick" },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "sick" },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "sick" },
        { "0e273289-be9b-4048-a2c3-acb89579dcdc", "sick" },
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
    memset(&animWalk_,  0, sizeof(animWalk_));
    memset(&animHappy_, 0, sizeof(animHappy_));
    memset(&animSick_,  0, sizeof(animSick_));
    memset(&animEat_,   0, sizeof(animEat_));
    memset(&animSleep_, 0, sizeof(animSleep_));
}

// =============================================================================
// begin() — carga sprites y resetea estado visual.
// =============================================================================

void DisplayRendererColor::begin() {
    animFrame_            = 0;
    animTickCount_        = 0;
    eatAnimRemainingMs_   = 0;
    hatchAnimRemainingMs_ = 0;
    evolveAnimRemainingMs_= 0;
    visibleCount_         = 0;
    selectedIndex_        = 0;
    spritesLoaded_        = loadSprites_();
    if (!spritesLoaded_) {
        LOG_ERROR("DisplayRendererColor::begin() — no se pudieron cargar sprites");
    }
}

// =============================================================================
// loadSprites_ — carga las 5 animaciones del sprite "wiz" en PSRAM.
//
// Todas las animaciones se cargan una vez en begin() y se mantienen en PSRAM
// durante todo el ciclo de vida del renderer. Coste estimado:
//   walk:    8 frames × 48×48×2 = 36 864 B  ≈ 36 KB
//   happy:   4 frames × 48×48×2 = 18 432 B  ≈ 18 KB
//   sick:    4 frames × 48×48×2 = 18 432 B  ≈ 18 KB
//   eat:     5 frames × 48×48×2 = 23 040 B  ≈ 22 KB
//   sleep:   4 frames × 48×48×2 = 18 432 B  ≈ 18 KB
//   Total ≈ 114 KB — cabe holgadamente en los ~1.9 MB de PSRAM disponibles.
// =============================================================================

bool DisplayRendererColor::loadSprites_() {
    bool ok = true;

    ok &= sprites_.loadAnimation(kSpriteId, "walk",  animWalk_);
    ok &= sprites_.loadAnimation(kSpriteId, "happy", animHappy_);
    ok &= sprites_.loadAnimation(kSpriteId, "sick",  animSick_);
    ok &= sprites_.loadAnimation(kSpriteId, "eat",   animEat_);
    ok &= sprites_.loadAnimation(kSpriteId, "sleep", animSleep_);

    if (ok) {
        LOG_INFO("DisplayRendererColor: sprites cargados OK. PSRAM libre: %u B",
                 ESP.getFreePsram());
    } else {
        LOG_ERROR("DisplayRendererColor: fallo al cargar uno o mas sprites");
    }
    return ok;
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

void DisplayRendererColor::triggerEatAnimation() {
    // eat tiene 5 frames a 8 fps → 5 × 125 ms = 625 ms.
    // Usamos un timer un poco más largo (1500 ms) para que el ciclo se vea completo.
    eatAnimRemainingMs_ = 1500;
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
    // ── Decrementar timers one-shot ──────────────────────────────────────────
    // Cada render() corresponde a kRenderPeriodMs ms de tiempo subjetivo.
    // El harness puede llamar a render() a cualquier cadencia; la duración
    // visual de las animaciones escala con ella.
    if (eatAnimRemainingMs_ > 0) {
        eatAnimRemainingMs_ = (eatAnimRemainingMs_ > kRenderPeriodMs)
                            ? (eatAnimRemainingMs_ - kRenderPeriodMs) : 0u;
    }
    if (hatchAnimRemainingMs_ > 0) {
        hatchAnimRemainingMs_ = (hatchAnimRemainingMs_ > kRenderPeriodMs)
                              ? (hatchAnimRemainingMs_ - kRenderPeriodMs) : 0u;
    }
    if (evolveAnimRemainingMs_ > 0) {
        evolveAnimRemainingMs_ = (evolveAnimRemainingMs_ > kRenderPeriodMs)
                               ? (evolveAnimRemainingMs_ - kRenderPeriodMs) : 0u;
    }

    // ── Avanzar frame de animación cada 2 ticks ──────────────────────────────
    // A 8 FPS, 2 ticks = 250 ms ≈ 4 cambios/s. Suficiente para animaciones fluidas.
    animTickCount_++;
    if (animTickCount_ >= 2) {
        animTickCount_ = 0;
        animFrame_++;
    }

    // ── Forzar DeadScreen cuando el tamagochi está muerto ────────────────────
    if (lifeStage_ == LifeStage::Dead && scene_ != SceneColor::OtaProgress) {
        scene_ = SceneColor::DeadScreen;
    }

    // ── Parpadeo durante animación de evolución ──────────────────────────────
    // Cada 200 ms alterna visible/invisible. En los ticks "oscuros" pintamos
    // fondo y hacemos flush, devolviendo sin dibujar el personaje.
    if (evolveAnimRemainingMs_ > 0) {
        uint32_t tickIdx = (evolveAnimRemainingMs_ / kRenderPeriodMs);
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
// computeMood_ — prioridad: Eat > Sleeping (→ Idle) > Sad > Happy > Idle.
// =============================================================================

MoodColor DisplayRendererColor::computeMood_() const {
    if (eatAnimRemainingMs_ > 0)   return MoodColor::Eat;
    if (stats_.isSleeping)         return MoodColor::Idle;
    if (stats_.happiness <= 20)    return MoodColor::Sad;
    if (stats_.happiness >= 80)    return MoodColor::Happy;
    return MoodColor::Idle;
}

// =============================================================================
// framesForMood_ — devuelve el puntero al buffer de animación apropiado.
//
// Si isSleeping está activo, devuelve animSleep_ independientemente del mood
// (el personaje duerme aunque el mood computado sea Idle por otras razones).
// =============================================================================

const ColorSpriteFrames* DisplayRendererColor::framesForMood_(MoodColor mood) const {
    if (!spritesLoaded_) return nullptr;

    // Sleeping tiene precedencia visual sobre cualquier mood
    if (stats_.isSleeping) return &animSleep_;

    switch (mood) {
        case MoodColor::Idle:  return &animWalk_;
        case MoodColor::Happy: return &animHappy_;
        case MoodColor::Sad:   return &animSick_;
        case MoodColor::Eat:   return &animEat_;
    }
    return &animWalk_;
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
// Layout y=0..39 (40 px):
//   x=4   y=15   "BABY" o "KID" etc. (etapa) — fuente size 2 (12×16 px por char)
//   x=4   y=35   "Xh Ym" o "Zm" (edad)       — fuente size 1 (6×8 px)
//   icono WiFi esquina derecha (x=224, y=4)
// =============================================================================

void DisplayRendererColor::drawHudTop_() {
    // Fondo del HUD top — banda oscura para separar del personaje
    canvas_.fillRect(0, 0, 240, 40, COL_DARKGRAY);

    // Etapa de vida
    canvas_.setTextColor(COL_WHITE);
    canvas_.setTextSize(2);
    canvas_.setCursor(4, 6);
    canvas_.print(lifeStageLabel(lifeStage_));

    // Edad
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
    canvas_.setTextSize(1);
    canvas_.setCursor(4, 28);
    canvas_.print(ageBuf);

    drawWifiIcon_();
}

// =============================================================================
// drawWifiIcon_ — icono WiFi 16×12 px en esquina superior derecha.
//
// Dibuja 3 arcos concéntricos con primitivas Arduino_GFX (drawArc no disponible
// en todos los drivers; usamos rectángulos y círculos rellenos).
// =============================================================================

void DisplayRendererColor::drawWifiIcon_() {
    if (wifiState_ == WifiState::Disabled)   return;
    if (scene_ == SceneColor::OtaProgress)   return;

    // Parpadeo en Connecting: frames pares = visible, impares = invisible.
    if (wifiState_ == WifiState::Connecting && (animFrame_ % 2) != 0) return;

    uint16_t color = (wifiState_ == WifiState::Connected) ? COL_GREEN : COL_AMBER;

    // Icono simplificado: 3 píxeles superpuestos centrales + punto
    // usando fillCircle con radios 6, 4, 2, 1 a x=226, y=8
    const int16_t cx = 226, cy = 16;

    // Arco exterior (radio 6) — solo parte superior con fillCircle parcial
    canvas_.fillCircle(cx, cy, 7, color);
    // "Hueco" interior dejando solo el contorno exterior
    canvas_.fillCircle(cx, cy, 5, COL_DARKGRAY);
    // Arco medio (radio 4)
    canvas_.fillCircle(cx, cy, 4, color);
    canvas_.fillCircle(cx, cy, 2, COL_DARKGRAY);
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
// Layout y=240..295 (56 px):
//   Fondo oscuro
//   Label de la acción en el cursor — fuente size 2, centrado.
//   Indicadores de cursor: pequeños cuadrados para cada acción visible,
//   el seleccionado relleno, los demás vacíos.
// =============================================================================

void DisplayRendererColor::drawHudBottom_() {
    // Fondo del HUD inferior
    canvas_.fillRect(0, 240, 240, 56, COL_DARKGRAY);

    // Línea separadora
    canvas_.drawFastHLine(0, 241, 240, COL_GRAY);

    if (visibleCount_ == 0) return;

    uint8_t cursor = (selectedIndex_ < visibleCount_) ? selectedIndex_ : 0u;
    ActionKind action = visibleActions_[cursor];

    // Label de la acción seleccionada, centrado
    const char* label = actionLabelColor(action, stats_.isSleeping);
    canvas_.setTextColor(COL_WHITE);
    canvas_.setTextSize(2);
    int16_t labelX = (240 - static_cast<int16_t>(strlen(label)) * 12) / 2;
    if (labelX < 4) labelX = 4;
    canvas_.setCursor(labelX, 248);
    canvas_.print(label);

    // Indicadores de posición de cursor: puntos cuadrados de 6×6 px centrados
    // Máximo 8 items → total width 8*(6+2) = 64 px, centrados en 240 → x_start=88
    if (visibleCount_ > 1) {
        const int16_t dotSize = 6;
        const int16_t dotGap  = 3;
        int16_t totalW  = visibleCount_ * (dotSize + dotGap) - dotGap;
        int16_t startX  = (240 - totalW) / 2;
        int16_t dotY    = 282;

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
    // Sprite 48×48 × 4 = 192×192 px centrado en 240×192
    const int16_t spriteW = animWalk_.w > 0 ? animWalk_.w * kScale : 192;
    const int16_t spriteH = animWalk_.h > 0 ? animWalk_.h * kScale : 192;
    const int16_t spriteX = (240 - spriteW) / 2;
    const int16_t spriteY = 40;

    MoodColor mood = computeMood_();
    const ColorSpriteFrames* frames = framesForMood_(mood);

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
            return;
        }
    }

    // Fallback: rectángulo con "?" cuando no hay sprites
    canvas_.drawRect(spriteX, spriteY, spriteW, spriteH, COL_GRAY);
    canvas_.setTextColor(COL_GRAY);
    canvas_.setTextSize(4);
    canvas_.setCursor(spriteX + spriteW / 2 - 12, spriteY + spriteH / 2 - 16);
    canvas_.print("?");
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
    int16_t badgeX = 200;  // esquina derecha de la zona personaje
    int16_t badgeY = 48;   // primera posición y
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
// Layout portrait 240×296:
//   y=0..39    HUD top (reutilizado)
//   y=48..     filas de stats: label + barra color + valor
//   y=210..    flags booleanos (sick, sleeping, poop)
//   y=270..    footer "→ STATS"
// =============================================================================

void DisplayRendererColor::drawStatsView_() {
    canvas_.fillScreen(COL_BG);

    drawHudTop_();

    // Título
    canvas_.setTextColor(COL_YELLOW);
    canvas_.setTextSize(2);
    canvas_.setCursor(4, 48);
    canvas_.print("STATS");
    canvas_.drawFastHLine(0, 66, 240, COL_GRAY);

    // Barras de stats
    // Nota: hunger se invierte (100-hunger) para que "lleno" = barra llena.
    // barW = 140 px, x = 4 → barra va de x=34 a x=174, valor a x=177
    const int16_t barW = 140;
    const int16_t barH = 8;
    const int16_t x0   = 4;
    int16_t       rowY = 82;
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

    // Separador
    canvas_.drawFastHLine(0, rowY - 4, 240, COL_DARKGRAY);

    // Flags booleanos
    canvas_.setTextColor(COL_WHITE);
    canvas_.setTextSize(1);
    char flagBuf[40];
    snprintf(flagBuf, sizeof(flagBuf), "Enf:%c  Dor:%c  Caca:%u",
             stats_.isSick     ? 'Y' : 'N',
             stats_.isSleeping ? 'Y' : 'N',
             static_cast<unsigned>(stats_.poopCount));
    canvas_.setCursor(4, rowY);
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
    canvas_.setCursor(4, rowY);
    canvas_.print(ageBuf);

    // DeathCount
    rowY += 16;
    char ripBuf[20];
    snprintf(ripBuf, sizeof(ripBuf), "RIPs: %u", static_cast<unsigned>(deathCount_));
    canvas_.setCursor(4, rowY);
    canvas_.print(ripBuf);

    canvas_.flush();
}

// =============================================================================
// drawOtaProgress_ — barra de progreso OTA (placeholder Fase 2).
// =============================================================================

void DisplayRendererColor::drawOtaProgress_() {
    canvas_.fillScreen(COL_BLACK);

    canvas_.setTextColor(COL_WHITE);
    canvas_.setTextSize(2);
    canvas_.setCursor(20, 80);
    canvas_.print("Updating...");

    // Porcentaje
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%u%%", static_cast<unsigned>(otaProgress_));
    canvas_.setTextSize(3);
    int16_t pctX = (240 - static_cast<int16_t>(strlen(pctBuf)) * 18) / 2;
    canvas_.setCursor(pctX, 130);
    canvas_.print(pctBuf);

    // Barra de progreso: x=10..229, y=170, height=20
    canvas_.drawRect(10, 170, 220, 20, COL_WHITE);
    int fillW = (static_cast<int>(otaProgress_) * 218) / 100;
    if (fillW > 0) {
        canvas_.fillRect(11, 171, fillW, 18, COL_GREEN);
    }

    canvas_.flush();
}

// =============================================================================
// drawDeadScreen_ — pantalla RIP con causa, edad y contador de muertes.
//
// Layout portrait 240×296:
//   y=0..39     HUD top (usa espacio del personaje para texto grande)
//   y=50..90    Sprite personaje (muerto → animSick_) escalado ×2 (96×96 px)
//               Centrado en x.
//   y=100       "RIP" grande centrado — fuente size 3 (18×24 px/char)
//   y=140       Causa de muerte
//   y=170       Edad formateada
//   y=195       Contador de muertes totales
// =============================================================================

void DisplayRendererColor::drawDeadScreen_() {
    canvas_.fillScreen(COL_BLACK);

    // HUD top — mantiene el nombre de etapa "DEAD" y la edad
    drawHudTop_();

    // Sprite del muerto: usamos animSick_ escalado ×2 (no ×4) para que quede
    // arriba y deje espacio para el texto. 48×48 ×2 = 96×96 px.
    if (spritesLoaded_ && animSick_.count > 0 && animSick_.w > 0) {
        uint8_t frameIdx = static_cast<uint8_t>(animFrame_ % animSick_.count);
        const uint16_t* pixels = animSick_.frames[frameIdx];
        if (pixels != nullptr) {
            const int16_t dW = animSick_.w * 2;
            const int16_t dH = animSick_.h * 2;
            const int16_t dX = (240 - dW) / 2;
            drawSpriteScaled_(&canvas_, dX, 50,
                              animSick_.w, animSick_.h,
                              pixels, 2, kKeyColor);
        }
    } else {
        // Placeholder: calavera con primitivas
        canvas_.fillCircle(120, 80, 30, COL_GRAY);
        canvas_.fillRect(100, 90, 40, 20, COL_GRAY);
        canvas_.fillCircle(109, 78, 6, COL_BLACK);  // ojo izq
        canvas_.fillCircle(131, 78, 6, COL_BLACK);  // ojo der
    }

    // "RIP" grande centrado — fuente size 4 (24×32 px/char), 3 chars = 72 px
    canvas_.setTextColor(COL_RED);
    canvas_.setTextSize(4);
    canvas_.setCursor((240 - 72) / 2, 160);
    canvas_.print("RIP");

    // Causa de muerte
    const char* causeText;
    switch (lastDeathCause_) {
        case 1:  causeText = "De hambre";  break;
        case 2:  causeText = "Enfermo";    break;
        case 3:  causeText = "Vejez";      break;
        case 4:  causeText = "Tristeza";   break;
        case 5:  causeText = "Desconocida"; break;
        default: causeText = "Causa N/A";  break;
    }
    canvas_.setTextColor(COL_WHITE);
    canvas_.setTextSize(1);
    canvas_.setCursor(4, 210);
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
    canvas_.setCursor(4, 230);
    canvas_.print(ageBuf);

    // Contador
    char ripBuf[20];
    snprintf(ripBuf, sizeof(ripBuf), "Total RIPs: %u", static_cast<unsigned>(deathCount_));
    canvas_.setCursor(4, 250);
    canvas_.print(ripBuf);

    canvas_.flush();
}
