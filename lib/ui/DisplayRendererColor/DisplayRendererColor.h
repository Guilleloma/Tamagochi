/**
 * DisplayRendererColor.h — renderer a color para ESP32-S3 + TFT JD9853 240×296.
 *
 * FASE 6: sprites reales por etapa de vida (egg/baby/kid/adult/old/ghost).
 * Todas las etapas se precargan en PSRAM al boot (~478 KB).
 * framesForMood_() resuelve (lifeStage_, mood) a la animación correcta,
 * con fallback a idle de la etapa si el mood no tiene animación propia.
 *
 * Layout portrait 240×296:
 *   HUD top       y=0..39   : nombre/etapa + edad + icono WiFi
 *   Personaje     y=40..231  : sprite 48×48 escalado ×4 = 192×192, centrado en x
 *   HUD inferior  y=232..295 : acción seleccionada (icono + label)
 *
 * Constructor: DisplayRendererColor(Arduino_GFX& canvas, EventBus& bus, SpriteStore& sprites)
 * render()    : compone frame completo y llama canvas.flush().
 */

#pragma once

#include <Arduino_GFX_Library.h>
#include "EventBus.h"       // EventBus (core, portable)
#include "StatsEngine.h"    // Stats, ActionKind, kMaxActionMenuItems
#include "Persistence.h"    // LifeStage enum
#include "WifiState.h"      // WifiState enum — header propio sin deps de plataforma.
                            // WifiManager.h incluye este mismo header en esp12e.
#include "SpriteStore.h"    // ColorSpriteFrames

// =============================================================================
// MoodColor — estado emocional visible. Misma semántica que en el renderer mono.
// =============================================================================
enum class MoodColor : uint8_t {
    Idle  = 0,
    Happy = 1,
    Sad   = 2,
    Eat   = 3,
    Sleep = 4   // overlay de dormido — precede a todos los moods
};

// Número de moods posibles (tamaño del eje mood en StageSprites).
static constexpr uint8_t kMoodCount = 5;

// =============================================================================
// SceneColor — escena activa en el display color.
// =============================================================================
enum class SceneColor : uint8_t {
    IdleMain,
    StatsView,
    OtaProgress,
    DeadScreen
};

// =============================================================================
// SpriteMapping — par (spriteId, animationName) que resuelve el SpriteStore.
// =============================================================================
struct SpriteMapping {
    const char* spriteId;    // campo "id" del manifest
    const char* animation;   // nombre de animación ("idle", "eat", etc.)
};

// =============================================================================
// StageSprites — buffers PSRAM de todas las animaciones de una etapa de vida.
//
// Indexados por MoodColor: frames[Idle]=0, Happy=1, Sad=2, Eat=3, Sleep=4.
// Los moods sin animación propia dejan count=0; framesForMood_() cae a idle.
// =============================================================================
struct StageSprites {
    ColorSpriteFrames frames[kMoodCount];  // one per MoodColor value
};

// =============================================================================
// DisplayRendererColor
// =============================================================================
class DisplayRendererColor {
public:
    /**
     * @param canvas   Arduino_Canvas (framebuffer en PSRAM). render() llama flush().
     * @param bus      EventBus — actualmente no suscribe eventos en Fase 2.
     * @param sprites  SpriteStore ya inicializado (begin() llamado en setup).
     */
    DisplayRendererColor(Arduino_GFX& canvas, EventBus& bus, SpriteStore& sprites);

    // Resetea el estado interno (no hace flush).
    void begin();

    // ── Setters de estado ────────────────────────────────────────────────────
    void setScene(SceneColor scene);
    void setStats(const Stats& stats);
    void setOtaProgress(uint8_t pct);
    void setLifeStage(LifeStage stage);
    void setAdultType(uint8_t adultType);
    void setLastDeathCause(uint8_t cause);
    void setAgeMinutes(uint32_t mins);
    void setDeathCount(uint16_t count);
    void setWifiState(WifiState state);

    // ── Triggers de animación one-shot ───────────────────────────────────────
    void triggerEatAnimation();     // 5 frames × 125 ms ≈ 625 ms
    void triggerHatchAnimation();   // reservado (Fase 3)
    void triggerEvolveAnimation();  // parpadeo 1200 ms (Fase 3)

    // ── Menú de acciones ─────────────────────────────────────────────────────
    // En Fase 2 el menú se pobla con datos falsos; refreshActionMenu usará StatsEngine real en Fase 3.
    void refreshActionMenu(const StatsEngine& engine);
    void moveCursor();
    ActionKind currentSelectedAction() const;

    /**
     * Compone el frame completo sobre el canvas y llama canvas.flush().
     * Llamar desde el harness a la cadencia de frame (~8-12 FPS).
     * Cada llamada = 1 tick de animación (200 ms si se llama a 5 FPS,
     * o adaptado al deltaMs si se llama al fps real del harness).
     */
    void render();

private:
    Arduino_GFX& canvas_;
    EventBus&    bus_;
    SpriteStore& sprites_;

    // ── Estado visible ───────────────────────────────────────────────────────
    SceneColor scene_          = SceneColor::IdleMain;
    Stats      stats_;
    uint8_t    otaProgress_    = 0;
    LifeStage  lifeStage_      = LifeStage::Baby;
    uint8_t    adultType_      = 0;
    uint8_t    lastDeathCause_ = 0;
    uint32_t   ageMinutes_     = 0;
    uint16_t   deathCount_     = 0;
    WifiState  wifiState_      = WifiState::Disabled;

    // ── Animación ────────────────────────────────────────────────────────────
    // animFrame_ avanza 1 cada render() — los drawers lo usan para calcular
    // el frame del sprite y el parpadeo de overlays.
    uint32_t animFrame_       = 0;
    uint32_t animTickCount_   = 0;  // ticks para avance lento (cada 2 ticks = 1 frame)

    // Timers de animaciones one-shot (ms restantes, decrementados por render).
    // render() asume que se llama aproximadamente cada kRenderPeriodMs ms.
    uint32_t eatAnimRemainingMs_    = 0;
    uint32_t hatchAnimRemainingMs_  = 0;
    uint32_t evolveAnimRemainingMs_ = 0;

    // ── Cache de animaciones por etapa — precargadas en PSRAM al boot ───────
    // spritesByStage_[stage] contiene los ColorSpriteFrames de todos los moods
    // de esa etapa. Precargamos las 6 etapas al boot (~478 KB en PSRAM).
    // El acceso en render() es O(1) sin lecturas LittleFS.
    StageSprites spritesByStage_[6];   // índice = LifeStage enum value (0-5)
    bool spritesLoaded_ = false;

    // Color key de transparencia (0xF81F = magenta en RGB565).
    static constexpr uint16_t kKeyColor = 0xF81F;

    // Escala de renderizado: 48×48 × 4 = 192×192 px.
    static constexpr uint8_t kScale = 4;

    // Período nominal entre renders (ms) — usado para decrementar timers.
    // No es crítico: si el harness va más rápido, los timers sólo se drenan antes.
    static constexpr uint32_t kRenderPeriodMs = 125;  // ~8 FPS

    // ── Menú de acciones ─────────────────────────────────────────────────────
    ActionKind visibleActions_[kMaxActionMenuItems] = {};
    uint8_t    visibleCount_  = 0;
    uint8_t    selectedIndex_ = 0;

    // ── Helpers internos ─────────────────────────────────────────────────────
    MoodColor computeMood_() const;
    bool      loadSprites_();

    // Devuelve el puntero al ColorSpriteFrames correcto para (stage, mood).
    // Fallback a idle de la etapa si el mood no tiene frames cargados.
    // Devuelve nullptr si los sprites no están cargados.
    const ColorSpriteFrames* framesForMood_(LifeStage stage, MoodColor mood) const;

    void drawIdleMain_();
    void drawStatsView_();
    void drawOtaProgress_();
    void drawDeadScreen_();

    void drawHudTop_();      // nombre/etapa + edad + WiFi
    void drawHudBottom_();   // acción seleccionada
    void drawSprite_();      // personaje centrado
    void drawOverlayBadges_(); // iconos de estado sobre el personaje (sick/sleep/hungry/poop)
    void drawWifiIcon_();    // icono WiFi esquina sup-der

    // Dibuja el sprite escalado con color key. Reutiliza drawSpriteScaled de bringup.
    static void drawSpriteScaled_(Arduino_GFX* display,
                                  int16_t destX, int16_t destY,
                                  uint16_t srcW, uint16_t srcH,
                                  const uint16_t* pixels,
                                  uint8_t scale,
                                  uint16_t keyColor);

    // Dibuja una barra horizontal coloreada por umbral (Stats view).
    void drawStatBar_(int16_t x, int16_t y, int16_t barW, int16_t barH,
                      int value, int valueMax, const char* label);

    // Overlay badge: pequeño icono simbólico sobre el personaje.
    void drawBadgeSick_(int16_t cx, int16_t cy);
    void drawBadgeSleep_(int16_t cx, int16_t cy);
    void drawBadgeHungry_(int16_t cx, int16_t cy);
    void drawBadgePoop_(int16_t cx, int16_t cy);
};
