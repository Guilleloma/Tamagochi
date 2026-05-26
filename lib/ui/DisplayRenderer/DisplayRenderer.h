#pragma once
#include <U8g2lib.h>
#include <stdint.h>
#include "../../domain/StatsEngine/StatsEngine.h"

// =============================================================================
// DisplayRenderer — encapsula el dibujado de escenas en el OLED SSD1306.
//
// Escenas disponibles en hito 5:
//   IdleMain    — texto + sprite animado (comportamiento idéntico a Fase 0)
//   StatsView   — stub; se implementa en hito 6
//   OtaProgress — stub; se implementa en hito 8
//
// bus_ se almacena pero NO se usa en hito 5. En hito 6 se suscribirá a
// StatsChanged para actualizar stats_ de forma reactiva.
//
// ANIMACIÓN: render() se llama a 5 FPS desde el scheduler de main.cpp.
// animFrame_ alterna cada 2 ticks (2 × 200 ms = 400 ms = ANIM_FRAME_MS).
// =============================================================================

class EventBus;

enum class Scene : uint8_t {
    IdleMain,
    StatsView,
    OtaProgress
};

class DisplayRenderer {
public:
    DisplayRenderer(U8G2& u8g2, EventBus& bus);

    void begin();
    void setScene(Scene scene);
    void setStats(const Stats& stats);    // copia interna; no usada en IdleMain (hito 5)
    void setOtaProgress(uint8_t pct);     // 0-100; no usada en hito 5

    // Dibuja un frame completo. Llamar desde el scheduler a 5 FPS.
    void render();

private:
    U8G2*    u8g2_;
    EventBus* bus_;
    Scene    scene_         = Scene::IdleMain;
    Stats    stats_;
    uint8_t  otaProgress_  = 0;
    uint8_t  animFrame_    = 0;
    uint32_t animTickCount_ = 0;

    void drawIdleMain_();
    void drawStatsView_();
    void drawOtaProgress_();
};
