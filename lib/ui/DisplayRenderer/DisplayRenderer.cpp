#include "DisplayRenderer.h"
#include <Arduino.h>
#include "config.h"

// =============================================================================
// Sprites dummy 32×32 — movidos de main.cpp en hito 5 (Fase 0 usaba kSpriteFrame0/1).
// Formato XBM: 1 bit por pixel, byte-packed, LSB first. bit=0 → pixel activo.
// En Fase 2 estos sprites serán sustituidos por los assets BABY del designer
// generados con tools/png_to_xbm.py → include/sprites_generated.h
// =============================================================================

// Frame 0: blob redondo con ojos abiertos y sonrisa
static const unsigned char PROGMEM kSpriteFrame0[128] = {
    0x00, 0xFF, 0xFF, 0x00,  // fila 0
    0x80, 0xFF, 0xFF, 0x01,  // fila 1
    0xC0, 0xFF, 0xFF, 0x03,  // fila 2
    0xE0, 0xFF, 0xFF, 0x07,  // fila 3
    0xF0, 0xFF, 0xFF, 0x0F,  // fila 4
    0xF8, 0xFF, 0xFF, 0x1F,  // fila 5
    0xFC, 0xFF, 0xFF, 0x3F,  // fila 6
    0xFC, 0xFF, 0xFF, 0x3F,  // fila 7
    0xFE, 0x99, 0x99, 0x7F,  // fila 8  — ojos (huecos en 0x99)
    0xFE, 0x99, 0x99, 0x7F,  // fila 9
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 10
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 11
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 12
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 13
    0xFF, 0xC3, 0xC3, 0xFF,  // fila 14 — sonrisa (huecos en 0xC3)
    0xFF, 0xE7, 0xE7, 0xFF,  // fila 15
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 16
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 17
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 18
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 19
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 20
    0xFF, 0xFF, 0xFF, 0xFF,  // fila 21
    0xFE, 0xFF, 0xFF, 0x7F,  // fila 22
    0xFE, 0xFF, 0xFF, 0x7F,  // fila 23
    0xFC, 0xFF, 0xFF, 0x3F,  // fila 24
    0xFC, 0xFF, 0xFF, 0x3F,  // fila 25
    0xF8, 0xFF, 0xFF, 0x1F,  // fila 26
    0xF0, 0xFF, 0xFF, 0x0F,  // fila 27
    0xE0, 0xFF, 0xFF, 0x07,  // fila 28
    0xC0, 0xFF, 0xFF, 0x03,  // fila 29
    0x80, 0xFF, 0xFF, 0x01,  // fila 30
    0x00, 0xFF, 0xFF, 0x00   // fila 31
};

// Frame 1: blob con ojos cerrados (líneas horizontales = parpadeo)
static const unsigned char PROGMEM kSpriteFrame1[128] = {
    0x00, 0xFF, 0xFF, 0x00,
    0x80, 0xFF, 0xFF, 0x01,
    0xC0, 0xFF, 0xFF, 0x03,
    0xE0, 0xFF, 0xFF, 0x07,
    0xF0, 0xFF, 0xFF, 0x0F,
    0xF8, 0xFF, 0xFF, 0x1F,
    0xFC, 0xFF, 0xFF, 0x3F,
    0xFC, 0xFF, 0xFF, 0x3F,
    0xFE, 0x01, 0x80, 0x7F,  // fila 8  — ojos cerrados (línea horizontal)
    0xFE, 0xFF, 0xFF, 0x7F,  // fila 9
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xC3, 0xC3, 0xFF,  // sonrisa igual que frame 0
    0xFF, 0xE7, 0xE7, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFE, 0xFF, 0xFF, 0x7F,
    0xFE, 0xFF, 0xFF, 0x7F,
    0xFC, 0xFF, 0xFF, 0x3F,
    0xFC, 0xFF, 0xFF, 0x3F,
    0xF8, 0xFF, 0xFF, 0x1F,
    0xF0, 0xFF, 0xFF, 0x0F,
    0xE0, 0xFF, 0xFF, 0x07,
    0xC0, 0xFF, 0xFF, 0x03,
    0x80, 0xFF, 0xFF, 0x01,
    0x00, 0xFF, 0xFF, 0x00
};

// Tabla de frames para iterar por índice
static const unsigned char* const kSpriteFrames[2] = {
    kSpriteFrame0,
    kSpriteFrame1
};

// =============================================================================
// Implementación
// =============================================================================

DisplayRenderer::DisplayRenderer(U8G2& u8g2, EventBus& bus)
    : u8g2_(&u8g2), bus_(&bus)
{}

void DisplayRenderer::begin() {
    // Inicializa el hardware de display. En SW I2C no se llama a Wire.begin().
    u8g2_->begin();
}

void DisplayRenderer::setScene(Scene scene) {
    scene_ = scene;
}

void DisplayRenderer::setStats(const Stats& stats) {
    // Copia interna — en hito 5 no se usa en drawIdleMain_, pero la API es vinculante.
    // TODO(hito-6): llamar a drawIdleMain_ con stats reales cuando se integre EventBus.
    stats_ = stats;
}

void DisplayRenderer::setOtaProgress(uint8_t pct) {
    // Almacena porcentaje OTA — no se usa en hito 5.
    // TODO(hito-8): mostrar overlay OtaProgress cuando se active OTAManager.
    otaProgress_ = pct;
}

void DisplayRenderer::render() {
    // Avanza la animación: alterna frame cada 2 ticks a 5 FPS = 400 ms = ANIM_FRAME_MS.
    // Dos ticks × 200 ms/tick = 400 ms — coincide exactamente con Fase 0.
    animTickCount_++;
    if (animTickCount_ >= 2) {
        animTickCount_ = 0;
        animFrame_ = (animFrame_ + 1) % 2;
    }

    switch (scene_) {
        case Scene::IdleMain:    drawIdleMain_();    break;
        case Scene::StatsView:   drawStatsView_();   break;
        case Scene::OtaProgress: drawOtaProgress_(); break;
    }
}

// -----------------------------------------------------------------------------
// drawIdleMain_ — idéntico al renderFrame() de Fase 0.
// Texto a la izquierda, sprite 32×32 a la derecha (x=96).
// -----------------------------------------------------------------------------
void DisplayRenderer::drawIdleMain_() {
    u8g2_->clearBuffer();

    u8g2_->setFont(u8g2_font_6x10_tf);
    u8g2_->drawStr(0, 10, "Hola Tamagochi");

    u8g2_->setFont(u8g2_font_5x7_tf);
    u8g2_->drawStr(0, 24, "Fase 0 - Bring-up");

    // Sprite 32x32 alineado a la derecha del OLED (128 - 32 = 96)
    u8g2_->drawXBMP(96, 16, 32, 32, kSpriteFrames[animFrame_]);

    u8g2_->sendBuffer();
}

// -----------------------------------------------------------------------------
// drawStatsView_ — stub hito 5. Se implementa en hito 6.
// -----------------------------------------------------------------------------
void DisplayRenderer::drawStatsView_() {
    u8g2_->clearBuffer();
    u8g2_->setFont(u8g2_font_5x7_tf);
    u8g2_->drawStr(0, 30, "TODO: StatsView");
    u8g2_->sendBuffer();
}

// -----------------------------------------------------------------------------
// drawOtaProgress_ — stub hito 5. Se implementa en hito 8.
// -----------------------------------------------------------------------------
void DisplayRenderer::drawOtaProgress_() {
    u8g2_->clearBuffer();
    u8g2_->setFont(u8g2_font_5x7_tf);
    u8g2_->drawStr(0, 30, "TODO: OTA");
    u8g2_->sendBuffer();
}
