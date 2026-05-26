#include "DisplayRenderer.h"
#include <Arduino.h>
#include "config.h"
#include "sprites_generated.h"

// =============================================================================
// Sprites BABY — generados desde assets/sprites/baby_idle_*.png con
// tools/slice_sheet.py. Definidos en include/sprites_generated.h como arrays
// PROGMEM. Aquí solo agrupamos los frames de cada animación en tablas
// para indexar por animFrame_.
// =============================================================================

static const unsigned char* const kBabyIdleFrames[2] = {
    kSpriteBabyIdle0,
    kSpriteBabyIdle1
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
    // Copia interna — usada en drawStatsView_() para pintar valores reales.
    // Se actualiza vía onStatsChanged en main.cpp cada vez que el bus emite StatsChanged.
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
// drawXBMx2 — pinta un sprite XBM escalado x2 (cada pixel renderizado como 2×2).
// U8g2 no expone drawXBMP con escala. Esta función recorre los bits del bitmap
// PROGMEM (LSB first) y pinta cuadrados 2×2 para los activos.
// Coste: O(w*h) drawBox calls. Para 32×32 = 1024 calls — aceptable a 5 FPS.
// -----------------------------------------------------------------------------
static void drawXBMx2(U8G2* u8g2, int x, int y, int w, int h, const uint8_t* bitmap) {
    int bytesPerRow = (w + 7) / 8;
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            uint8_t byte = pgm_read_byte(bitmap + (row * bytesPerRow) + (col / 8));
            if (byte & (1 << (col % 8))) {
                u8g2->drawBox(x + col * 2, y + row * 2, 2, 2);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// drawIdleMain_ — pantalla principal en portrait (64 wide × 128 tall).
// Sprite BABY 32×32 escalado a 64×64 centrado en ancho completo, texto debajo.
// -----------------------------------------------------------------------------
void DisplayRenderer::drawIdleMain_() {
    u8g2_->clearBuffer();

    // Sprite BABY 64×64 (32×32 escalado x2) ocupando el ancho entero, y=4..68.
    drawXBMx2(u8g2_, 0, 4, 32, 32, kBabyIdleFrames[animFrame_]);

    // Texto "Tamagochi" centrado bajo el sprite.
    // Fuente 6×10, 9 chars × 6 px = 54 → x = (64-54)/2 = 5.
    u8g2_->setFont(u8g2_font_6x10_tf);
    u8g2_->drawStr(5, 90, "Tamagochi");

    u8g2_->sendBuffer();
}

// -----------------------------------------------------------------------------
// drawStatsView_ — pantalla de stats en portrait con barras de progreso.
//
// Layout (64 wide × 128 tall, fuente 6×10):
//   y=10        STATS  (centrado)
//   y=13        ─────
//   y=24..36    Hambre  +  barra 64×8
//   y=54..66    Animo   +  barra 64×8
//   y=84..96    Salud   +  barra 64×8
// -----------------------------------------------------------------------------
void DisplayRenderer::drawStatsView_() {
    u8g2_->clearBuffer();
    u8g2_->setFont(u8g2_font_6x10_tf);

    // Título centrado (5 chars × 6 px = 30 → x = 17).
    u8g2_->drawStr(17, 10, "STATS");
    u8g2_->drawHLine(0, 13, 64);

    // Helper local: dibuja "etiqueta" + barra de progreso 64×8 según value 0-100.
    auto drawStatBar = [this](const char* label, uint8_t value, int yLabel, int yBar) {
        u8g2_->drawStr(0, yLabel, label);
        u8g2_->drawFrame(0, yBar, 64, 8);
        int fillWidth = (static_cast<int>(value) * 62) / 100;
        if (fillWidth > 0) {
            u8g2_->drawBox(1, yBar + 1, fillWidth, 6);
        }
    };

    drawStatBar("Hambre", stats_.hunger,    32, 36);
    drawStatBar("Animo",  stats_.happiness, 62, 66);
    drawStatBar("Salud",  stats_.health,    92, 96);

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
