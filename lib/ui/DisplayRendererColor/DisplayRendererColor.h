/**
 * DisplayRendererColor.h — renderer a color para ESP32-S3 + TFT JD9853 240×296.
 *
 * FASE 6: sprites reales por etapa de vida (egg/baby/kid/adult/old/ghost).
 * Todas las etapas se precargan en PSRAM al boot (~486 KB).
 * framesForMood_() resuelve (lifeStage_, mood) a la animación correcta con la
 * cadena de fallback de MoodLogic (Jump→Happy→Idle; el resto → Idle) si la
 * etapa no tiene animación propia para ese mood.
 *
 * FASE 4 (menú): drawHudBottom_() pinta icono 24×24 + label de la acción
 * seleccionada, centrados como bloque horizontal en la card inferior.
 * Iconos para 5 acciones (Feed/Play/Discipline/Sleep/Stats) cargados vía
 * SpriteStore desde LittleFS (carpetas ic-apple, ic-ball, ic-disc, ic-moon,
 * ic-stats). Clean y Medicate usan primitivas dibujadas en render
 * (sin assets separados: anim-broom y anim-syringe son 48×48, no caben en
 * los 60 px del HUD junto al texto size 2; ver drawMenuIconPrimitive_()).
 *
 * Layout portrait 240×296 (safe-area con cards redondeadas):
 *   Margen físico   kEdgeInset=8 px por todos los bordes (curva del cristal IPS)
 *   Corner safe     kCornerSafe=28 px: clearance diagonal para esquinas del cristal
 *   HUD top (D1)    iconos de estado centrados h=52: ic-hunger · ic-poop · ic-sick
 *                   (24px×2 = 48px, gap=8, bloque centrado en 240px). WiFi en (212,28).
 *                   Etapa+edad movidas a StatsView.
 *   Personaje       y=(8+52)..(296-8-60) : sprite 48×48 × 3 = 144×144, centrado
 *   HUD bottom card x=8, y=228, w=224, h=60, r=10 : icono(24px) + label(size 2) + dots
 *
 * Stats en 2 páginas (la 1/2 ya ocupaba hasta y≈286, sin hueco para la red):
 *   StatsView   (1/2) barras + flags + edad + RIPs + batería
 *   NetworkView (2/2) estado WiFi, SSID, IP, hostname mDNS y RSSI — datos de
 *                     WifiInfo (WifiManager::info()) formateados con
 *                     NetInfoLogic en setWifiInfo(); render() solo imprime.
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
#include "MoodLogic.h"      // MoodColor, kMoodCount y cadena de fallback de moods
                            // (lógica pura, testeada en env:native)
#include "NetInfoLogic.h"   // NetStatus + formateo de SSID/IP/RSSI de la página
                            // de red (lógica pura, testeada en env:native)

// =============================================================================
// SceneColor — escena activa en el display color.
// =============================================================================
enum class SceneColor : uint8_t {
    IdleMain,
    StatsView,
    OtaProgress,
    DeadScreen,
    MessageView,  // mensaje de texto del padre (marquee) — ver showMessage()
    NetworkView   // Stats página 2/2: estado WiFi, SSID, IP, mDNS, RSSI. Añadida
                  // al final para no renumerar las escenas que se loguean.
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
// Indexados por MoodColor: frames[Idle]=0, Happy=1, Sad=2, Eat=3, Sleep=4,
// Jump=5. Los moods sin animación propia dejan count=0; framesForMood_()
// aplica la cadena de fallback de MoodLogic::resolveMood().
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

    // Página de red (NetworkView). Formatea y copia los textos AQUÍ (coste de
    // µs, sin I/O): render() solo los imprime. main_esp32.cpp la llama al
    // cambiar el estado WiFi, al entrar en la página y periódicamente
    // mientras está visible (RSSI). Nunca recibe contraseñas (ver WifiInfo).
    void setWifiInfo(const WifiInfo& info);

    void setBattery(uint8_t percent, bool charging, bool valid);

    // ── Triggers de animación one-shot ───────────────────────────────────────
    // triggerActionAnimation: reproduce UNA vez la animación asociada a la
    // acción, desde su frame 0, durante max(kActionAnimDurationMs, ciclo
    // completo) — 1200 ms para happy/sad, 1250 ms eat, 2000 ms el salto — y
    // luego vuelve al mood calculado por computeMood_().
    // Mapeo acción → MoodColor (MoodLogic::moodForAction + resolveMood):
    //   Feed       → Eat   (come)
    //   Play       → Jump  (salta; sin "jump" en la etapa → Happy → Idle)
    //   Medicate   → Happy
    //   Clean      → Idle  (limpieza neutra; borra overlay caca visualmente)
    //   Discipline → Sad   (consecuencia negativa)
    //   Lights/Stats → sin one-shot (el sueño lo gestiona isSleeping)
    void triggerActionAnimation(ActionKind action);

    void triggerHatchAnimation();   // reservado (Fase 3)
    void triggerEvolveAnimation();  // parpadeo 1200 ms (Fase 3)

    // ── Mensaje del padre (marquee) ──────────────────────────────────────────
    // showMessage: copia asciiText (ya sanitizado a ASCII por MessageBox — ver
    // main_esp32.cpp) a messageText_, resetea el scroll (offset/pasadas) y
    // arranca el timer de kMessageDurationMs. NO cambia scene_ — el caller
    // (main_esp32.cpp) decide cuándo mostrar MessageView.
    void showMessage(const char* asciiText);

    // dismissMessage: descarte inmediato (botón físico). Pone remaining a 0 —
    // el próximo render() ya ve messageActive()==false.
    void dismissMessage();

    // messageActive: true mientras quede tiempo Y no se hayan agotado las
    // pasadas de marquee permitidas (kMessageMaxPasses). El caller la usa para
    // saber cuándo restaurar la escena anterior.
    bool messageActive() const;

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

    // Batería — Unknown por defecto (icono oculto) hasta que BatteryMonitor
    // confirme lectura válida. Ver setBattery() / drawBatteryIcon_().
    uint8_t    batteryPercent_  = 0;
    bool       batteryCharging_ = false;
    bool       batteryValid_    = false;

    // ── Animación ────────────────────────────────────────────────────────────
    // animFrame_ avanza según millis() y el fps declarado en el manifest
    // (desacoplado del render FPS para que rendes más rápidos no aceleren el sprite).
    uint32_t animFrame_        = 0;
    uint32_t lastAnimTickMs_   = 0;  // millis() del último avance de animFrame_

    // Timers de animaciones one-shot (ms restantes, decrementados con millis()).
    // lastRenderMs_ se usa para calcular el delta entre renders y drenar los timers.
    uint32_t lastRenderCallMs_      = 0;

    // Timer unificado de acción one-shot (Feed/Play/Clean/Medicate/Discipline).
    // Mientras actionAnimRemainingMs_ > 0, drawSprite_() usa actionAnimMood_ en lugar
    // del mood calculado por computeMood_(), y elige el frame por el tiempo
    // transcurrido (actionAnimTotalMs_ - actionAnimRemainingMs_).
    // actionAnimTotalMs_: duración con la que se disparó el one-shot actual.
    uint32_t  actionAnimRemainingMs_ = 0;
    uint32_t  actionAnimTotalMs_     = 0;
    MoodColor actionAnimMood_        = MoodColor::Idle;

    uint32_t hatchAnimRemainingMs_  = 0;
    uint32_t evolveAnimRemainingMs_ = 0;

    // ── Mensaje del padre (marquee) ─────────────────────────────────────────
    // messageText_: copia local ya sanitizada a ASCII (ver showMessage()).
    // messageRemainingMs_: ms restantes antes de auto-descartar el mensaje;
    //   se drena en render() igual que el resto de timers one-shot. 0 = sin
    //   mensaje activo (ver messageActive()).
    // messageElapsedMs_: ms transcurridos desde showMessage() — alimenta
    //   MarqueeLogic::offsetPx() para el scroll (por TIEMPO, no por frame).
    // messageTextPxW_: ancho en px del texto completo a kMessageTextSize,
    //   calculado una vez en showMessage() (evita recalcular strlen*charW
    //   en cada render()).
    char     messageText_[97]      = {};
    uint32_t messageRemainingMs_   = 0;
    uint32_t messageElapsedMs_     = 0;
    uint16_t messageTextPxW_       = 0;

    // ── Cache de animaciones por etapa — precargadas en PSRAM al boot ───────
    // spritesByStage_[stage] contiene los ColorSpriteFrames de todos los moods
    // de esa etapa. Precargamos las 6 etapas al boot (~486 KB en PSRAM).
    // El acceso en render() es O(1) sin lecturas LittleFS.
    StageSprites spritesByStage_[6];   // índice = LifeStage enum value (0-5)
    bool spritesLoaded_ = false;

    // ── Iconos de estado (banda superior) — precargados en PSRAM al boot ────
    // Orden fijo: [0]=ic-hunger, [1]=ic-poop, [2]=ic-sick. Sprites 24×24, 1 frame idle.
    // Se renderizan escalados ×2 (48×48) centrados en la banda superior de IdleMain.
    static constexpr uint8_t kStatusIconCount = 3;
    ColorSpriteFrames statusIcons_[kStatusIconCount];
    // statusIconsLoaded_ eliminado: la degradación es por icono individual
    // (statusIcons_[i].count > 0 actúa como flag per-slot; ver Fix 1b).

    // ── Canal de overlay one-shot (escoba/jeringa/pelota) ───────────────────
    // Segundo canal de animación independiente del mood del personaje.
    // Se pinta ENCIMA del personaje durante la misma duración que el one-shot
    // del personaje (ver triggerActionAnimation) y luego se apaga solo.
    // Feed NO usa overlay (solo cambia mood a Eat).
    //
    // overlayAnimFrames_: puntero a los ColorSpriteFrames del overlay activo
    //   (apunta a uno de los 3 sprites precarados abajo). nullptr = sin overlay.
    // overlayAnimRemainingMs_: ms restantes de overlay; se drena en render().
    // overlayAnimFrame_: contador de frame propio del overlay, avanzado según
    //   el fps declarado en el manifest (desacoplado de animFrame_ del personaje).
    // overlayAnimLastTickMs_: millis() del último avance de overlayAnimFrame_.
    const ColorSpriteFrames* overlayAnimFrames_       = nullptr;
    uint32_t                 overlayAnimRemainingMs_   = 0;
    uint32_t                 overlayAnimFrame_         = 0;
    uint32_t                 overlayAnimLastTickMs_    = 0;

    // Sprites de overlay precargados en PSRAM al boot (~8 KB total):
    //   overlayBroom_   → Clean   (48×48, 6 frames)
    //   overlaySyringe_ → Medicate (48×48, 3 frames)
    //   overlayBall_    → Play    (48×48, 3 frames)
    ColorSpriteFrames overlayBroom_;
    ColorSpriteFrames overlaySyringe_;
    ColorSpriteFrames overlayBall_;

    // ── Iconos del menú de acciones (HUD inferior) ───────────────────────────
    // Iconos 24×24 RGB565, uno por ActionKind que tiene asset de icono dedicado.
    // Orden: Feed=0, Play=1, Discipline=2, Sleep=3, Stats=4.
    // Clean e Medicate se dibujan con primitivas (ver drawMenuIconPrimitive_()).
    // Cargados en loadMenuIcons_(); si falla, drawHudBottom_() omite el icono
    // para esa acción y pinta solo el label (degradación suave).
    static constexpr uint8_t kMenuIconCount = 5;
    ColorSpriteFrames menuIcons_[kMenuIconCount];
    bool menuIconsLoaded_ = false;

    // Color key de transparencia (0xF81F = magenta en RGB565).
    static constexpr uint16_t kKeyColor = 0xF81F;

    // Escala de renderizado: 48×48 × 3 = 144×144 px.
    // Con kScale=3 el personaje ocupa y≈60..204 (zona=236px) dejando ~12px de
    // aire respecto a la banda de iconos (y=60) y el HUD inferior (y=228).
    static constexpr uint8_t kScale = 3;

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

    // ── Página de red (NetworkView) ──────────────────────────────────────────
    // Valores (SSID / IP / host / RSSI) a textSize 2 en el ancho seguro
    // (kScreenW - 2*kSafeTextX = 208 px → 17 chars). SSID y hostname se
    // truncan con "..." a ese ancho (NetInfoLogic::fitText).
    static constexpr uint8_t kNetValueTextSize = 2;
    static constexpr size_t  kNetValueChars =
        NetInfoLogic::maxCharsForWidth(kScreenW - 2 * kSafeTextX, kNetValueTextSize);
    // Nº de páginas de Stats (StatsView=1, NetworkView=2) — indicador "n/2".
    static constexpr uint8_t kStatsPageCount = 2;

    // Textos de la página de red, ya formateados por setWifiInfo() (ver
    // NetInfoLogic). Declarados aquí (y no con el resto del estado visible)
    // porque su tamaño depende de kNetValueChars. Arrancan como "sin dato"
    // hasta el primer setWifiInfo().
    NetInfoLogic::NetStatus netStatus_ = NetInfoLogic::NetStatus::NotConfigured;
    char    netSsidText_[kNetValueChars + 1]     = "-";
    char    netHostText_[kNetValueChars + 1]     = "-";
    char    netIpText_[NetInfoLogic::kIpTextSize]     = "-";
    char    netRssiText_[NetInfoLogic::kRssiTextSize] = "-";
    bool    netIpAssigned_ = false;
    uint8_t netRssiBars_   = 0;

    // Altura de la card del HUD superior (etapa + edad).
    // Aumentada a 52 px para acomodar la fila de texto desplazada hacia kCornerSafe
    // (BABY en y=28, edad en y=46 → texto termina en ~54; HUD top delimita zoneTop
    // en kEdgeInset + kHudTopH = 60, dejando ~6 px de gap con el sprite centrado).
    static constexpr int16_t kHudTopH = 52;

    // Altura de la card del HUD inferior (label de acción + cursor dots).
    static constexpr int16_t kHudBottomH = 60;

    // Duración MÍNIMA de animación one-shot de acción (ms). La duración real es
    // MoodLogic::actionAnimDurationMs(): max(este mínimo, ciclo completo de la
    // animación a fps×SPRITE_ANIM_SLOWDOWN). Con slowdown 2 (250 ms/frame):
    // happy/sad 4 frames = 1000 ms → 1200; eat 5 frames → 1250; jump 8 → 2000.
    static constexpr uint32_t kActionAnimDurationMs = 1200;

    // ── Mensaje del padre (marquee) ──────────────────────────────────────────
    // kMessageDurationMs: duración máxima del mensaje en pantalla (60 s) —
    //   auto-descarte aunque el padre nunca vuelva a interactuar y el usuario
    //   no pulse el botón.
    // kMarqueeSpeedPxPerSec: velocidad de scroll horizontal. 60 px/s da un
    //   ritmo de lectura cómodo (a kMessageTextSize=3, ~3.3 chars/s).
    // kMessageTextSize: setTextSize() del texto del marquee (18 px/char de
    //   ancho — ver drawMessageView_()).
    // kMessageMaxPasses: nº máximo de vueltas completas del marquee antes de
    //   dar el mensaje por "leído" y descartarlo (con textos cortos, 60 s a
    //   60 px/s podría dar muchas más vueltas de las razonables).
    static constexpr uint32_t kMessageDurationMs     = 60000;
    static constexpr uint32_t kMarqueeSpeedPxPerSec   = 60;
    static constexpr uint8_t  kMessageTextSize        = 3;
    static constexpr uint8_t  kMessageMaxPasses       = 6;

    // ── Menú de acciones ─────────────────────────────────────────────────────
    ActionKind visibleActions_[kMaxActionMenuItems] = {};
    uint8_t    visibleCount_  = 0;
    uint8_t    selectedIndex_ = 0;

    // ── Helpers internos ─────────────────────────────────────────────────────
    MoodColor computeMood_() const;
    bool      loadSprites_();
    bool      loadMenuIcons_();

    // Devuelve el puntero al ColorSpriteFrames correcto para (stage, mood),
    // aplicando la cadena de fallback de MoodLogic (Jump→Happy→Idle, resto→Idle).
    // Devuelve nullptr si los sprites no están cargados.
    const ColorSpriteFrames* framesForMood_(LifeStage stage, MoodColor mood) const;

    // Buffers de la etapa (LifeStage fuera de rango → Baby).
    const StageSprites& stageSprites_(LifeStage stage) const;

    // Mood efectivo en `stage` según los frames cargados (MoodLogic::resolveMood).
    MoodColor resolveMoodForStage_(LifeStage stage, MoodColor mood) const;

    // Overlay del canal 2 para la acción (ball/syringe/broom); nullptr si no tiene
    // o si su sprite no se cargó.
    const ColorSpriteFrames* overlayForAction_(ActionKind action) const;

    void drawIdleMain_();
    void drawStatsView_();
    void drawNetworkView_();       // Stats 2/2 — red (ver setWifiInfo())
    void drawOtaProgress_();
    void drawDeadScreen_();
    void drawMessageView_();       // mensaje del padre — HUD + personaje + marquee

    // Pantalla oscura + "zZZ" flotante — isSleeping && !lightsOn (luces apagadas
    // correctamente). NO toca TFT_BL. Llamada desde drawIdleMain_() como guard.
    void drawLightsOffSleep_();

    void drawHudTop_();            // WiFi + batería en esquina sup (etapa/edad movidas a StatsView)
    void drawHudBottom_();         // acción seleccionada
    void drawSprite_();            // personaje centrado
    void drawStatusIconsTop_();    // iconos de estado grandes centrados en banda superior
    void drawOverlayBadges_();     // overlay sleep "Z" sobre el personaje
    void drawWifiIcon_();          // icono WiFi esquina sup-der
    void drawBatteryIcon_();       // icono batería esquina sup-izq (primitivas, ver .cpp)

    // Dibuja el sprite escalado con color key. Reutiliza drawSpriteScaled de bringup.
    static void drawSpriteScaled_(Arduino_GFX* display,
                                  int16_t destX, int16_t destY,
                                  uint16_t srcW, uint16_t srcH,
                                  const uint16_t* pixels,
                                  uint8_t scale,
                                  uint16_t keyColor);

    // Cabecera común a las 2 páginas de Stats: etapa + edad (sup-izq) y HUD
    // superior (WiFi sup-der).
    void drawStatsHeader_();
    // Título amarillo de la página + indicador "page/kStatsPageCount" a la
    // derecha + línea separadora. y = borde superior del título.
    void drawStatsTitle_(const char* title, uint8_t page, int16_t y);
    // Fila de la página de red: etiqueta gris (size 1) y valor (size 2) debajo.
    void drawNetRow_(const char* label, const char* value, uint16_t valueColor, int16_t y);
    // 4 barras de cobertura (0-4 rellenas) con la base en yBottom.
    void drawRssiBars_(int16_t x, int16_t yBottom, uint8_t bars);

    // Dibuja una barra horizontal coloreada por umbral (Stats view).
    void drawStatBar_(int16_t x, int16_t y, int16_t barW, int16_t barH,
                      int value, int valueMax, const char* label);

    // Overlay badge: "Z" animada de sueño sobre el personaje (única que permanece).
    void drawBadgeSleep_(int16_t cx, int16_t cy);

    // Icono de menú de acción dibujado con primitivas (sin asset .bin).
    // Usado para Clean y Medicate, cuyos assets 48×48 no caben en el HUD de 60 px.
    // cx/cy: centro del área de icono (24×24 px).
    void drawMenuIconPrimitive_(ActionKind action, int16_t cx, int16_t cy);
};
