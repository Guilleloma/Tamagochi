#include "DisplayRenderer.h"
#include <Arduino.h>
#include "config.h"
#include "sprites_generated.h"

// =============================================================================
// SpriteFrames — agrupa los frames de una animación para indexar por Mood.
// =============================================================================

struct SpriteFrames {
    const uint8_t* frames[3];   // nullptr si el slot no aplica
    uint8_t        frameCount;  // 1, 2 o 3
};

// =============================================================================
// kSpritesByStage[LifeStage][Mood] — tabla de 6 × 4 entradas.
//
// Política de placeholders (plan §1.1):
//   Todas las etapas sin sprites reales apuntan a kSpriteBabyIdle0/1.
//   Se sustituyen en commits separados ("Assets: sprites <Etapa> generados")
//   cuando Guillermo entregue el arte definitivo.
//
// Orden de filas: Egg(0), Baby(1), Kid(2), Adult(3), Old(4), Dead(5)
// Orden de cols:  Idle(0), Happy(1), Sad(2), Eat(3)
// =============================================================================

static const SpriteFrames kSpritesByStage[6][4] = {

    // -------------------------------------------------------------------------
    // Egg (0) — sprites reales (4 frames: quieto, leve vibración, vibración, grieta)
    // El huevo no tiene moods narrativos (no come, no se entristece). Mapeo:
    //   Idle  → frames 0+1 (huevo quieto + leve vibración)
    //   Happy → frames 2+3 (vibración fuerte y grieta = "vivo, listo para eclosionar")
    //   Sad   → frame 0 solo (huevo inerte)
    //   Eat   → frames 0+1 (idem idle; el huevo no come, pero la API requiere algo)
    // -------------------------------------------------------------------------
    {
        { {kSpriteEgg0, kSpriteEgg1, nullptr},     2 },  // Idle
        { {kSpriteEgg2, kSpriteEgg3, nullptr},     2 },  // Happy
        { {kSpriteEgg0, nullptr,     nullptr},     1 },  // Sad
        { {kSpriteEgg0, kSpriteEgg1, nullptr},     2 },  // Eat
    },

    // -------------------------------------------------------------------------
    // Baby (1) — sprites reales disponibles
    // -------------------------------------------------------------------------
    {
        { {kSpriteBabyIdle0,  kSpriteBabyIdle1,  nullptr},          2 },  // Idle
        { {kSpriteBabyHappy0, kSpriteBabyHappy1, kSpriteBabyHappy2}, 3 }, // Happy
        { {kSpriteBabySad0,   kSpriteBabySad1,   nullptr},          2 },  // Sad
        { {kSpriteBabyEat0,   kSpriteBabyEat1,   kSpriteBabyEat2},  3 }, // Eat
    },

    // -------------------------------------------------------------------------
    // Kid (2) — PLACEHOLDER hasta tener sprites Kid
    // -------------------------------------------------------------------------
    {
        { {kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2 },  // Idle   — placeholder
        { {kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2 },  // Happy  — placeholder
        { {kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2 },  // Sad    — placeholder
        { {kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2 },  // Eat    — placeholder
    },

    // -------------------------------------------------------------------------
    // Adult (3) — PLACEHOLDER hasta tener sprites Adult
    // -------------------------------------------------------------------------
    {
        { {kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2 },  // Idle   — placeholder
        { {kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2 },  // Happy  — placeholder
        { {kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2 },  // Sad    — placeholder
        { {kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2 },  // Eat    — placeholder
    },

    // -------------------------------------------------------------------------
    // Old (4) — PLACEHOLDER hasta tener sprites Old
    // -------------------------------------------------------------------------
    {
        { {kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2 },  // Idle   — placeholder
        { {kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2 },  // Happy  — placeholder
        { {kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2 },  // Sad    — placeholder
        { {kSpriteBabyIdle0, kSpriteBabyIdle1, nullptr}, 2 },  // Eat    — placeholder
    },

    // -------------------------------------------------------------------------
    // Dead (5) — PLACEHOLDER hasta tener sprite lápida (kSpriteDeadIdle0).
    // Se usa kSpriteBabySad0/1: transmite estado triste mejor que idle.
    // -------------------------------------------------------------------------
    {
        { {kSpriteBabySad0, kSpriteBabySad1, nullptr}, 2 },  // Idle — placeholder (sad > idle en pantalla muerte)
        { {kSpriteBabySad0, kSpriteBabySad1, nullptr}, 2 },  // Happy — no aplica para Dead; placeholder
        { {kSpriteBabySad0, kSpriteBabySad1, nullptr}, 2 },  // Sad   — placeholder
        { {kSpriteBabySad0, kSpriteBabySad1, nullptr}, 2 },  // Eat   — no aplica para Dead; placeholder
    },
};

// =============================================================================
// Implementación
// =============================================================================

DisplayRenderer::DisplayRenderer(U8G2& u8g2, EventBus& bus)
    : u8g2_(&u8g2), bus_(&bus)
{}

void DisplayRenderer::begin() {
    u8g2_->begin();
}

void DisplayRenderer::setScene(Scene scene) {
    scene_ = scene;
}

void DisplayRenderer::setStats(const Stats& stats) {
    stats_ = stats;
}

void DisplayRenderer::setOtaProgress(uint8_t pct) {
    otaProgress_ = pct;
}

void DisplayRenderer::setLifeStage(LifeStage stage) {
    lifeStage_ = stage;
}

void DisplayRenderer::setAdultType(uint8_t adultType) {
    adultType_ = adultType;
}

void DisplayRenderer::setLastDeathCause(uint8_t cause) {
    lastDeathCause_ = cause;
}

void DisplayRenderer::setAgeMinutes(uint32_t mins) {
    ageMinutes_ = mins;
}

void DisplayRenderer::setDeathCount(uint16_t count) {
    deathCount_ = count;
}

void DisplayRenderer::triggerEatAnimation() {
    // 3 frames × 800 ms = 2400 ms de animación EAT one-shot.
    eatAnimRemainingMs_ = 2400;
}

// -----------------------------------------------------------------------------
// computeMood_ — determina el mood actual del Tamagochi.
// Prioridad: Eat (one-shot timer) > Idle (dormido) > Sad > Happy > Idle.
// -----------------------------------------------------------------------------
Mood DisplayRenderer::computeMood_() const {
    if (eatAnimRemainingMs_ > 0)   return Mood::Eat;
    if (stats_.isSleeping)         return Mood::Idle;
    if (stats_.happiness <= 20)    return Mood::Sad;
    if (stats_.happiness >= 80)    return Mood::Happy;
    return Mood::Idle;
}

// -----------------------------------------------------------------------------
// render — frame principal a 5 FPS. Gestiona el timer de animación,
// fuerza Scene::DeadScreen cuando el Tamagochi está muerto, y delega
// en las funciones de escena.
// -----------------------------------------------------------------------------
void DisplayRenderer::render() {
    // Decrementar timer EAT one-shot: cada render = 200 ms (5 FPS).
    if (eatAnimRemainingMs_ > 0) {
        eatAnimRemainingMs_ = (eatAnimRemainingMs_ > 200u) ? (eatAnimRemainingMs_ - 200u) : 0u;
    }

    // Avanza frame de animación cada 2 ticks (400 ms).
    animTickCount_++;
    if (animTickCount_ >= 2) {
        animTickCount_ = 0;
        animFrame_++;
        // animFrame_ se mantiene sin módulo fijo aquí; drawIdleMain_ lo indexa
        // con % sf.frameCount según el número real de frames del mood actual.
    }

    // Forzar DeadScreen cuando el Tamagochi está muerto, salvo que esté
    // en mitad de una actualización OTA (OtaProgress tiene prioridad absoluta).
    if (lifeStage_ == LifeStage::Dead && scene_ != Scene::OtaProgress) {
        scene_ = Scene::DeadScreen;
    }

    switch (scene_) {
        case Scene::IdleMain:    drawIdleMain_();    break;
        case Scene::StatsView:   drawStatsView_();   break;
        case Scene::OtaProgress: drawOtaProgress_(); break;
        case Scene::DeadScreen:  drawDeadScreen_();  break;
    }
}

// -----------------------------------------------------------------------------
// drawXBMx2 — pinta un sprite XBM escalado x2 (cada pixel → cuadrado 2×2).
// U8g2 no expone drawXBMP con escala. Recorre bits del bitmap PROGMEM
// (LSB first) y dibuja drawBox(2,2) para los bits activos.
// Coste: O(w×h) drawBox calls. Para 32×32 = 1024 calls — aceptable a 5 FPS.
// -----------------------------------------------------------------------------
static void drawXBMx2(U8G2* u8g2, int x, int y, int w, int h,
                       const uint8_t* bitmap) {
    const int bytesPerRow = (w + 7) / 8;
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
// drawIdleMain_ — pantalla principal portrait (64 wide × 128 tall).
//
// Selecciona sprite según etapa de vida (kSpritesByStage) y mood calculado
// con computeMood_(). Indexa el frame con animFrame_ % frameCount para
// soportar animaciones de 1, 2 o 3 frames sin overflow.
// -----------------------------------------------------------------------------
void DisplayRenderer::drawIdleMain_() {
    u8g2_->clearBuffer();

    Mood               mood = computeMood_();
    int                stageIdx = static_cast<int>(lifeStage_);
    int                moodIdx  = static_cast<int>(mood);
    const SpriteFrames& sf  = kSpritesByStage[stageIdx][moodIdx];

    // Guardia defensiva: si frameCount es 0 (no debería ocurrir con la tabla
    // actual, pero evita UB en producción).
    uint8_t frameIdx = (sf.frameCount > 0)
                     ? (animFrame_ % sf.frameCount)
                     : 0u;

    const uint8_t* sprite = sf.frames[frameIdx];
    if (sprite != nullptr) {
        // Sprite 32×32 escalado ×2 → 64×64, ancho completo, y=4..68.
        drawXBMx2(u8g2_, 0, 4, 32, 32, sprite);
    }

    // Texto "Tamagotchi" centrado bajo el sprite (marca con T).
    // Fuente 6×10, 10 chars × 6 px = 60 → x = (64-60)/2 = 2.
    u8g2_->setFont(u8g2_font_6x10_tf);
    u8g2_->drawStr(2, 85, "Tamagotchi");

    // Contador de edad — fuente 6×10 (igual que "Tamagotchi") para legibilidad.
    // Formato: "Xm" si <60 min, "XhYm" si >=60 min. Cap a 99h para caber.
    char ageBuf[8];
    if (ageMinutes_ < 60) {
        snprintf(ageBuf, sizeof(ageBuf), "%um", static_cast<unsigned>(ageMinutes_));
    } else {
        uint32_t hours   = ageMinutes_ / 60;
        uint32_t minutes = ageMinutes_ % 60;
        if (hours > 99) hours = 99;
        snprintf(ageBuf, sizeof(ageBuf), "%uh%um", static_cast<unsigned>(hours), static_cast<unsigned>(minutes));
    }
    u8g2_->setFont(u8g2_font_6x10_tf);
    u8g2_->drawStr(0, 100, ageBuf);

    // Overlays asíncronos encima del buffer ya construido — antes del sendBuffer.
    drawOverlays_();

    u8g2_->sendBuffer();
}

// -----------------------------------------------------------------------------
// drawOverlays_ — dibuja overlays de condición encima del sprite base.
//
// Layout portrait 64×128. El sprite del personaje ocupa x=0..63, y=4..67.
// Zona libre arriba: y=0..3 (mínima). Zona libre abajo: y=68..127.
//
// Overlays disponibles (plan §6.4):
//
//   Sick        16×16 — top-left  (x=4, y=4). Prioridad máxima.
//   SleepingZ   16×16 — top, animado 800ms. x=42 normal; x=24 si Sick también activo.
//   Hungry      16×16 — parpadeo 600ms — x=48, y=68 (justo bajo el sprite).
//   Poop        16×16 — esquina inf-izquierda, hasta 3 cacas separadas 18px en x.
//                        y=110 (última fila libre antes de borde, sin solapar texto "Tamagochi").
//
// Nota sobre el texto "Tamagochi": desplazado a y=85 (desde y=90) para dejar
// espacio en y=88..104 para el overlay Hungry, y y=108..124 para las cacas.
// -----------------------------------------------------------------------------
void DisplayRenderer::drawOverlays_() {
    const bool sickShown  = stats_.isSick;
    const bool sleepShown = stats_.isSleeping;

    // -------------------------------------------------------------------------
    // Sick (top-left) — prioridad sobre SleepingZ en la misma zona.
    // -------------------------------------------------------------------------
    if (sickShown) {
        u8g2_->drawXBMP(4, 4, 16, 16, kSpriteOverlaySick);
    }

    // -------------------------------------------------------------------------
    // SleepingZ (top, animado cada 800 ms = 4 ticks a 5 FPS).
    // Si Sick también activo, Z se desplaza a x=24 para no solapar.
    // -------------------------------------------------------------------------
    if (sleepShown) {
        // (animFrame_ / 4) % 2 → cambia cada 4 ticks = 800 ms
        const uint8_t* zSprite = ((animFrame_ / 4) % 2 == 0)
                                 ? kSpriteOverlaySleepZ0
                                 : kSpriteOverlaySleepZ1;
        const int zX = sickShown ? 24 : 42;
        u8g2_->drawXBMP(zX, 0, 16, 16, zSprite);
    }

    // -------------------------------------------------------------------------
    // Hungry (parpadeo cada 600 ms = 3 ticks a 5 FPS).
    // Solo visible cuando hunger >= 80 y el Tamagochi no está dormido.
    // Posición: x=48, y=68 (inmediatamente bajo el sprite del personaje).
    // -------------------------------------------------------------------------
    if (stats_.hunger >= 80 && !stats_.isSleeping) {
        // visible los ticks pares → mitad del tiempo encendido
        if ((animFrame_ / 3) % 2 == 0) {
            u8g2_->drawXBMP(48, 68, 16, 16, kSpriteOverlayHungry0);
        }
    }

    // -------------------------------------------------------------------------
    // Poop (esquina inferior-izquierda).
    // Máximo 3 cacas visibles aunque poopCount sea mayor.
    // Cada caca 16×16, separadas 18px en x. y=110 deja margen al borde inferior.
    // -------------------------------------------------------------------------
    int poopVisible = stats_.poopCount;
    if (poopVisible > 3) poopVisible = 3;
    for (int i = 0; i < poopVisible; ++i) {
        u8g2_->drawXBMP(i * 18, 110, 16, 16, kSpriteOverlayPoop);
    }
}

// -----------------------------------------------------------------------------
// drawStatsView_ — pantalla de stats con barras de progreso.
//
// Layout (64 wide × 128 tall, fuente 6×10):
//   y=10        STATS  (centrado)
//   y=13        ─────
//   y=24..36    Hambre  + barra 64×8
//   y=54..66    Animo   + barra 64×8
//   y=84..96    Salud   + barra 64×8
//
// Al final, si TURBO_MODE activo, dibuja banner "T" como recordatorio.
// -----------------------------------------------------------------------------
void DisplayRenderer::drawStatsView_() {
    u8g2_->clearBuffer();
    u8g2_->setFont(u8g2_font_6x10_tf);

    // Título centrado (5 chars × 6 px = 30 → x = 17).
    u8g2_->drawStr(17, 10, "STATS");
    u8g2_->drawHLine(0, 13, 64);

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

#ifdef TURBO_MODE
    drawTurboBanner_();
#endif

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

// -----------------------------------------------------------------------------
// drawDeadScreen_ — pantalla RIP mostrada cuando lifeStage_ == Dead.
//
// Layout portrait 64×128:
//   Sprite Dead (placeholder sad) arriba, escalado x2 (64×64) en y=2..66.
//   "RIP" grande centrado a y=78.
//   Causa de muerte a y=90 según lastDeathCause_.
//   Edad formateada a y=102.
//   Contador de muertes a y=114.
// -----------------------------------------------------------------------------
void DisplayRenderer::drawDeadScreen_() {
    u8g2_->clearBuffer();

    // Sprite dead (placeholder): usar entrada Dead/Idle de la tabla.
    {
        const SpriteFrames& sf = kSpritesByStage[static_cast<int>(LifeStage::Dead)]
                                                 [static_cast<int>(Mood::Idle)];
        uint8_t frameIdx = (sf.frameCount > 0) ? (animFrame_ % sf.frameCount) : 0u;
        if (sf.frames[frameIdx] != nullptr) {
            drawXBMx2(u8g2_, 0, 2, 32, 32, sf.frames[frameIdx]);
        }
    }

    // "RIP" centrado. Fuente 6×10: 3 chars × 6px = 18 → x = (64-18)/2 = 23.
    u8g2_->setFont(u8g2_font_6x10_tf);
    u8g2_->drawStr(23, 78, "RIP");

    // Causa de muerte según código (plan §1.3).
    const char* causeText;
    switch (lastDeathCause_) {
        case 1:  causeText = "De hambre"; break;
        case 2:  causeText = "Enfermo";   break;
        case 3:  causeText = "Vejez";     break;
        case 4:  causeText = "Triste";    break;
        default: causeText = "Causa: ?";  break;
    }
    u8g2_->setFont(u8g2_font_5x7_tf);
    u8g2_->drawStr(0, 90, causeText);

    // Edad formateada: NNh NNm si > 60 min, si no "NN min".
    char ageBuf[16];
    if (ageMinutes_ >= 60) {
        uint32_t h = ageMinutes_ / 60;
        uint32_t m = ageMinutes_ % 60;
        snprintf(ageBuf, sizeof(ageBuf), "%luh %lum",
                 static_cast<unsigned long>(h),
                 static_cast<unsigned long>(m));
    } else {
        snprintf(ageBuf, sizeof(ageBuf), "%lu min",
                 static_cast<unsigned long>(ageMinutes_));
    }
    u8g2_->drawStr(0, 102, ageBuf);

    // Contador de muertes totales.
    char ripBuf[16];
    snprintf(ripBuf, sizeof(ripBuf), "RIPs: %u", deathCount_);
    u8g2_->drawStr(0, 114, ripBuf);

    u8g2_->sendBuffer();
}

// -----------------------------------------------------------------------------
// drawTurboBanner_ — banner visual "T" en StatsView cuando TURBO_MODE activo.
// Posición top-right del portrait (64 wide): x=58 deja margen de 2px al borde.
// Fuente 4×6 px.
// -----------------------------------------------------------------------------
#ifdef TURBO_MODE
void DisplayRenderer::drawTurboBanner_() {
    u8g2_->setFont(u8g2_font_4x6_tr);
    u8g2_->drawStr(58, 6, "T");
}
#endif
