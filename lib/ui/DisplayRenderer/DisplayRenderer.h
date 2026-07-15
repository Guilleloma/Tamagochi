#pragma once
#include <U8g2lib.h>
#include <stdint.h>
#include "../../domain/StatsEngine/StatsEngine.h"
#include "../../domain/Persistence/Persistence.h"   // LifeStage enum
#include "../../infra/WifiManager/WifiManager.h"    // WifiState enum

// =============================================================================
// DisplayRenderer — encapsula el dibujado de escenas en el OLED SSD1306.
//
// Escenas disponibles en hito 5:
//   IdleMain    — sprite animado multi-etapa + mood adaptativo
//   StatsView   — barras de progreso; stub hasta hito 6
//   OtaProgress — stub hasta hito 8
//   DeadScreen  — pantalla RIP; activa cuando lifeStage_ == Dead
//
// ANIMACIÓN: render() se llama a 5 FPS desde el scheduler de main.cpp.
// animFrame_ alterna cada 2 ticks (2 × 200 ms = 400 ms).
//
// TABLA DE SPRITES: kSpritesByStage[LifeStage][Mood] en DisplayRenderer.cpp.
// Etapas sin arte real usan kSpriteBabyIdle0/1 como placeholder; se sustituyen
// commit a commit cuando lleguen los assets definitivos.
//
// EAT ANIMATION: one-shot de 2400 ms (3 frames × 800 ms).
// Se arranca desde triggerEatAnimation() que main.cpp invoca tras cada feed.
// =============================================================================

class EventBus;

// -----------------------------------------------------------------------------
// Mood — estado emocional visible que selecciona la fila de sprites.
// -----------------------------------------------------------------------------
enum class Mood : uint8_t {
    Idle  = 0,
    Happy = 1,
    Sad   = 2,
    Eat   = 3
};

// -----------------------------------------------------------------------------
// Scene — escena activa en el display.
// -----------------------------------------------------------------------------
enum class Scene : uint8_t {
    IdleMain,    // sprite + HUD inferior con acciones (Fase 4.1)
    StatsView,   // vista densa de las 9 stats
    OtaProgress,
    DeadScreen   // activa automáticamente cuando lifeStage_ == Dead
};

class DisplayRenderer {
public:
    DisplayRenderer(U8G2& u8g2, EventBus& bus);

    void begin();
    void setScene(Scene scene);
    void setStats(const Stats& stats);
    void setOtaProgress(uint8_t pct);

    // Nuevos en hito 5 — Fase 2
    void setLifeStage(LifeStage stage);
    void setAdultType(uint8_t adultType);        // persiste el subtipo de adulto para selección de sprite futura
    void setLastDeathCause(uint8_t cause);       // código 0-5 para texto en DeadScreen
    void setAgeMinutes(uint32_t mins);           // edad en minutos, mostrada en DeadScreen
    void setDeathCount(uint16_t count);          // contador total de muertes, mostrado en DeadScreen
    void triggerEatAnimation();                  // arranca animación EAT one-shot (2400 ms)
    void triggerHatchAnimation();                // Egg → Baby: vibración + grieta 2400 ms
    void triggerEvolveAnimation();               // Baby→Kid/Kid→Adult/Adult→Old: parpadeo 1200 ms

    // Sustituye al sprite del personaje en IdleMain por la animación de la acción
    // ejecutada. Solo aplica a Play, Clean, Discipline — el resto no
    // tienen frames asociados y la llamada es no-op.
    void triggerActionAnimation(ActionKind action);

    // Actualiza el estado WiFi para el indicador de esquina superior derecha.
    // Disabled → sin icono. Connecting → parpadeo. Connected/Disconnected → icono fijo.
    void setWifiState(WifiState state);

    // -- HUD inferior de acciones en IdleMain (Fase 4.1) --
    // Recalcula la lista de acciones visibles a partir del estado actual de
    // StatsEngine. Si el cursor apuntaba a una acción que ya no aparece, se
    // clampea al primer item; si no, se preserva su posición (UX consistente
    // tras ejecutar una acción que cambia preconditions).
    void refreshActionMenu(const StatsEngine& engine);

    // Avanza el cursor al siguiente item visible (circular). No-op si no hay items.
    void moveCursor();

    // Devuelve la acción actualmente seleccionada en el cursor.
    // Si no hay items (no debería ocurrir — Stats siempre está), devuelve Stats.
    ActionKind currentSelectedAction() const;

    // Dibuja un frame completo. Llamar desde el scheduler a 5 FPS.
    void render();

private:
    U8G2*     u8g2_;
    EventBus* bus_;
    Scene     scene_              = Scene::IdleMain;
    Stats     stats_;
    uint8_t   otaProgress_        = 0;
    uint8_t   animFrame_          = 0;
    uint32_t  animTickCount_      = 0;

    // Estado de etapa y muerte — actualizado desde main.cpp vía setters
    LifeStage lifeStage_          = LifeStage::Baby;
    uint8_t   adultType_          = 0;
    uint8_t   lastDeathCause_     = 0;
    uint32_t  ageMinutes_         = 0;
    uint16_t  deathCount_         = 0;

    // Estado WiFi — icono 8×6 px en esquina superior derecha.
    WifiState wifiState_ = WifiState::Disabled;

    // Timers one-shot; decrementados 200 ms por render()
    uint32_t  eatAnimRemainingMs_    = 0;
    uint32_t  hatchAnimRemainingMs_  = 0;    // eclosión Egg → Baby
    uint32_t  evolveAnimRemainingMs_ = 0;    // transición de etapa adulta

    // Animación de acción ejecutada desde el HUD inferior — sustituye al sprite
    // del personaje en IdleMain mientras dure. Duración = frameCount × 600 ms.
    uint32_t  actionAnimRemainingMs_ = 0;
    uint32_t  actionAnimTotalMs_     = 0;
    ActionKind actionAnimKind_       = ActionKind::Feed;

    // Estado del menú de acciones físicas (Fase 4).
    // visibleActions_ contiene las acciones aplicables al estado actual del
    // tamagochi; visibleCount_ es su longitud real (≤ kMaxActionMenuItems).
    // selectedIndex_ se mantiene siempre en [0, visibleCount_) por moveCursor()
    // y refreshActionMenu() — la UI clampea en la frontera para evitar lecturas
    // fuera de rango si las preconditions cambian entre frames.
    ActionKind visibleActions_[kMaxActionMenuItems] = {};
    uint8_t    visibleCount_  = 0;
    uint8_t    selectedIndex_ = 0;

    Mood computeMood_() const;

    void drawIdleMain_();
    void drawLightsOffSleep_();   // pantalla oscura + "zZZ" — isSleeping && !lightsOn
    void drawOverlays_();    // overlays asíncronos sobre el sprite principal (hito 6)
    void drawWifiIcon_();    // icono WiFi 8×6 en esquina superior derecha
    void drawActionHud_();   // HUD inferior de acciones en IdleMain (Fase 4.1)
    void drawStatsView_();
    void drawOtaProgress_();
    void drawDeadScreen_();

#ifdef TURBO_MODE
    void drawTurboBanner_();
#endif
};
