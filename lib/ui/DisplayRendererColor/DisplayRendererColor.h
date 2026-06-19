/**
 * DisplayRendererColor.h — renderer a color para ESP32-S3 + TFT JD9853 240×296.
 *
 * FASE 6: sprites reales por etapa de vida (egg/baby/kid/adult/old/ghost).
 * Todas las etapas se precargan en PSRAM al boot (~478 KB).
 * framesForMood_() resuelve (lifeStage_, mood) a la animación correcta,
 * con fallback a idle de la etapa si el mood no tiene animación propia.
 *
 * Layout portrait 240×296 (safe-area con cards redondeadas):
 *   Margen físico   kEdgeInset=8 px por todos los bordes (curva del cristal IPS)
 *   Corner safe     kCornerSafe=28 px: clearance diagonal para esquinas del cristal
 *   HUD top         texto flotante h=52: BABY en (28,28), edad en (28,46), WiFi en (212,28)
 *   Personaje       y=(8+52)..(296-8-60) : sprite 48×48 × 4 = 192×192, centrado
 *   HUD bottom card x=8, y=228, w=224, h=60, r=10 : label acción (size 2) + cursor dots
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
    // triggerActionAnimation: reproduce la animación asociada a la acción
    // durante ~1000 ms y luego vuelve al mood calculado por computeMood_().
    // Mapeo acción → MoodColor (con fallback a idle si la etapa no tiene ese mood):
    //   Feed       → Eat   (come)
    //   Play       → Happy (alegre)
    //   Heal       → Happy
    //   Medicate   → Happy
    //   Clean      → Idle  (limpieza neutra; borra overlay caca visualmente)
    //   Discipline → Sad   (consecuencia negativa)
    //   Sleep/Stats→ sin one-shot (Sleep lo gestiona isSleeping)
    void triggerActionAnimation(ActionKind action);

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
    // animFrame_ avanza según millis() y el fps declarado en el manifest
    // (desacoplado del render FPS para que rendes más rápidos no aceleren el sprite).
    uint32_t animFrame_        = 0;
    uint32_t lastAnimTickMs_   = 0;  // millis() del último avance de animFrame_

    // Timers de animaciones one-shot (ms restantes, decrementados con millis()).
    // lastRenderMs_ se usa para calcular el delta entre renders y drenar los timers.
    uint32_t lastRenderCallMs_      = 0;

    // Timer unificado de acción one-shot (Feed/Play/Heal/Clean/Medicate/Discipline).
    // Mientras actionAnimRemainingMs_ > 0, drawSprite_() usa actionAnimMood_ en lugar
    // del mood calculado por computeMood_().
    uint32_t  actionAnimRemainingMs_ = 0;
    MoodColor actionAnimMood_        = MoodColor::Idle;

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

    // ── Safe-area: márgenes para respetar las esquinas físicas redondeadas ───
    // El cristal IPS 240×296 tiene esquinas redondeadas que recortan el contenido
    // si se pinta a sangre. Estas constantes definen el layout de cards para que
    // ningún texto ni elemento quede oculto por la curvatura del panel.

    // Dimensiones visibles del panel (el GRAM nativo es 240×320 con row_offset=12).
    static constexpr int16_t kScreenW = 240;
    static constexpr int16_t kScreenH = 296;

    // Margen desde el borde físico hasta el borde exterior de cualquier card.
    // 8 px da margen suficiente para la curvatura de esquina del cristal IPS.
    static constexpr int16_t kEdgeInset = 8;

    // Radio de esquinas de las cards (coincide visualmente con la curvatura del panel).
    static constexpr int16_t kCornerR = 10;

    // Clearance diagonal para elementos en las esquinas del cristal IPS.
    // El panel físico tiene un radio de curvatura real de ~30 px; 28 px de margen
    // desde cada borde garantiza que texto e iconos queden fuera del cuarto de
    // círculo recortado. Afinable en placa si el radio real difiere.
    static constexpr int16_t kCornerSafe = 28;

    // Sangrado de texto dentro de la card: kEdgeInset + margen interior de 8 px.
    static constexpr int16_t kSafeTextX = kEdgeInset + 8;

    // Altura de la card del HUD superior (etapa + edad).
    // Aumentada a 52 px para acomodar la fila de texto desplazada hacia kCornerSafe
    // (BABY en y=28, edad en y=46 → texto termina en ~54; HUD top delimita zoneTop
    // en kEdgeInset + kHudTopH = 60, dejando ~6 px de gap con el sprite centrado).
    static constexpr int16_t kHudTopH = 52;

    // Altura de la card del HUD inferior (label de acción + cursor dots).
    static constexpr int16_t kHudBottomH = 60;

    // Duración mínima de animación one-shot de acción (ms).
    // ~1000 ms da tiempo a ver el ciclo de animación completo (~8 fps × 5 frames = 625 ms
    // para eat; el resto son 4 frames, ~500 ms). Usamos 1200 ms con margen.
    static constexpr uint32_t kActionAnimDurationMs = 1200;

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
