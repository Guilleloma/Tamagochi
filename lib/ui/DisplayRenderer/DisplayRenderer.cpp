#include "DisplayRenderer.h"
#include <Arduino.h>
#include "config.h"
#include "sprites_generated.h"

// Forward declaration de drawXBMx2 (helper de pintado escalado x2).
// La implementación está más abajo en el archivo, pero render() y
// los drawers la necesitan antes.
static void drawXBMx2(U8G2* u8g2, int x, int y, int w, int h, const uint8_t* bitmap);

// =============================================================================
// Sprites WiFi — bitmaps XBM 8×6 (6 bytes en formato LSB-first por fila).
//
// Diseño a mano, 8 columnas × 6 filas:
//
// kSpriteWifiOn (3 arcos concéntricos clásicos):
//   Fila 0:  _ _ _ X _ _ _ _   0x08  arco exterior — centrado en col 3
//   Fila 1:  _ _ X _ X _ _ _   0x14  arco exterior, dos flancos
//   Fila 2:  _ _ _ _ _ _ _ _   0x00  separación
//   Fila 3:  _ _ _ X _ _ _ _   0x08  arco medio
//   Fila 4:  _ _ _ _ _ _ _ _   0x00  separación
//   Fila 5:  _ _ _ X _ _ _ _   0x08  punto central (señal)
//
// kSpriteWifiOff — mismos arcos + línea X superpuesta:
//   Fila 0:  X _ _ X _ _ _ X  0x89
//   Fila 1:  _ X X _ X X _ _  0x36
//   Fila 2:  _ _ _ _ _ _ _ _  0x00
//   Fila 3:  _ _ _ X _ _ _ _  0x08
//   Fila 4:  _ _ _ _ _ _ _ _  0x00
//   Fila 5:  _ _ _ X _ _ _ _  0x08
//
// Posición en display portrait 64×128: esquina superior derecha → (56, 0).
// El sprite ocupa x=56..63, y=0..5 — sin solapar el sprite del personaje
// (que empieza en y=4 con un margen de 4px). La zona y=0..3 queda libre.
// =============================================================================

// XBM formato: byte[fila] = bits de col 0..7, bit 0 = columna izquierda (LSB first).
static const uint8_t kSpriteWifiOn[] PROGMEM = {
    0x08,  // fila 0: ...X....  arco exterior centro
    0x14,  // fila 1: ..X.X...  arco exterior flancos
    0x00,  // fila 2: ........  espacio
    0x08,  // fila 3: ...X....  arco medio
    0x00,  // fila 4: ........  espacio
    0x08,  // fila 5: ...X....  punto de señal
};

static const uint8_t kSpriteWifiOff[] PROGMEM = {
    0x89,  // fila 0: X..X...X  arco + X barra diagonal derecha (bit0, bit3, bit7)
    0x36,  // fila 1: .XX.XX..  arco flancos + X barra diagonal izquierda (bit1,2,4,5)
    0x00,  // fila 2: ........  espacio
    0x08,  // fila 3: ...X....  arco medio
    0x00,  // fila 4: ........  espacio
    0x08,  // fila 5: ...X....  punto de señal
};

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
    // El huevo NO tiene moods narrativos (no come, no se entristece, no está feliz).
    // Todos los moods usan los mismos 2 frames idle para coherencia visual.
    // Los frames 2-3 quedan reservados para una eventual animación de eclosión.
    // -------------------------------------------------------------------------
    {
        { {kSpriteEgg0, kSpriteEgg1, nullptr}, 2 },  // Idle
        { {kSpriteEgg0, kSpriteEgg1, nullptr}, 2 },  // Happy
        { {kSpriteEgg0, kSpriteEgg1, nullptr}, 2 },  // Sad
        { {kSpriteEgg0, kSpriteEgg1, nullptr}, 2 },  // Eat
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
    // Kid (2) — sprites reales: idle, happy, sad, eat.
    // -------------------------------------------------------------------------
    {
        { {kSpriteKidIdle0,  kSpriteKidIdle1,  nullptr},          2 },  // Idle
        { {kSpriteKidHappy0, kSpriteKidHappy1, kSpriteKidHappy2}, 3 },  // Happy
        { {kSpriteKidSad0,   kSpriteKidSad1,   nullptr},          2 },  // Sad
        { {kSpriteKidEat0,   kSpriteKidEat1,   kSpriteKidEat2},   3 },  // Eat
    },

    // -------------------------------------------------------------------------
    // Adult (3) — sprites reales: idle, happy, eat. Sad → reusa idle.
    // -------------------------------------------------------------------------
    {
        { {kSpriteAdultIdle0,  kSpriteAdultIdle1,  nullptr},            2 }, // Idle
        { {kSpriteAdultHappy0, kSpriteAdultHappy1, kSpriteAdultHappy2}, 3 }, // Happy
        { {kSpriteAdultIdle0,  kSpriteAdultIdle1,  nullptr},            2 }, // Sad → reuse idle
        { {kSpriteAdultEat0,   kSpriteAdultEat1,   kSpriteAdultEat2},   3 }, // Eat
    },

    // -------------------------------------------------------------------------
    // Old (4) — sprites reales: idle, eat. Happy/Sad → reusan idle (lentos).
    // -------------------------------------------------------------------------
    {
        { {kSpriteOldIdle0, kSpriteOldIdle1, nullptr},        2 },  // Idle
        { {kSpriteOldIdle0, kSpriteOldIdle1, nullptr},        2 },  // Happy → reuse idle
        { {kSpriteOldIdle0, kSpriteOldIdle1, nullptr},        2 },  // Sad   → reuse idle
        { {kSpriteOldEat0,  kSpriteOldEat1,  kSpriteOldEat2}, 3 },  // Eat
    },

    // -------------------------------------------------------------------------
    // Dead (5) — sprite ghost real. Sin moods narrativos: todos usan el fantasma.
    // -------------------------------------------------------------------------
    {
        { {kSpriteDeadGhost0, kSpriteDeadGhost1, nullptr}, 2 },  // Idle
        { {kSpriteDeadGhost0, kSpriteDeadGhost1, nullptr}, 2 },  // Happy (n/a)
        { {kSpriteDeadGhost0, kSpriteDeadGhost1, nullptr}, 2 },  // Sad   (n/a)
        { {kSpriteDeadGhost0, kSpriteDeadGhost1, nullptr}, 2 },  // Eat   (n/a)
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

void DisplayRenderer::setWifiState(WifiState state) {
    wifiState_ = state;
}

void DisplayRenderer::triggerEatAnimation() {
    // 3 frames × 800 ms = 2400 ms de animación EAT one-shot.
    eatAnimRemainingMs_ = 2400;
}

void DisplayRenderer::triggerHatchAnimation() {
    // 2400 ms de eclosión: frames 2-3 del huevo (vibración + grieta).
    hatchAnimRemainingMs_ = 2400;
}

void DisplayRenderer::triggerEvolveAnimation() {
    // 1200 ms de parpadeo del sprite nuevo (efecto "evoluciona").
    evolveAnimRemainingMs_ = 1200;
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
    // Decrementar timers de animaciones one-shot: cada render = 200 ms (5 FPS).
    if (eatAnimRemainingMs_ > 0) {
        eatAnimRemainingMs_ = (eatAnimRemainingMs_ > 200u) ? (eatAnimRemainingMs_ - 200u) : 0u;
    }
    if (hatchAnimRemainingMs_ > 0) {
        hatchAnimRemainingMs_ = (hatchAnimRemainingMs_ > 200u) ? (hatchAnimRemainingMs_ - 200u) : 0u;
    }
    if (evolveAnimRemainingMs_ > 0) {
        evolveAnimRemainingMs_ = (evolveAnimRemainingMs_ > 200u) ? (evolveAnimRemainingMs_ - 200u) : 0u;
    }

    // Animación de eclosión: dibujo dedicado, ignora lifeStage_ y mood.
    // Aunque la transición ya cambió lifeStage_ a Baby, queremos mostrar
    // el huevo rompiéndose unos segundos antes de revelar el bebé.
    if (hatchAnimRemainingMs_ > 0) {
        animTickCount_++;
        if (animTickCount_ >= 2) {
            animTickCount_ = 0;
            animFrame_ = (animFrame_ + 1) % 2;
        }
        u8g2_->clearBuffer();
        // Frames 2-3 del huevo: vibración fuerte + grieta.
        const uint8_t* sprite = (animFrame_ % 2) ? kSpriteEgg3 : kSpriteEgg2;
        drawXBMx2(u8g2_, 0, 4, 32, 32, sprite);
        u8g2_->setFont(u8g2_font_6x10_tf);
        u8g2_->drawStr(5, 85, "Eclosion");
        u8g2_->sendBuffer();
        return;
    }

    // Animación de evolve: parpadeo on/off del sprite normal cada 200 ms.
    // Mientras dure, alternamos render normal con clear-buffer.
    if (evolveAnimRemainingMs_ > 0) {
        if ((evolveAnimRemainingMs_ / 200u) % 2u != 0u) {
            u8g2_->clearBuffer();
            u8g2_->sendBuffer();
            return;
        }
        // Tick visible: cae al render normal (drawIdleMain_) tras este bloque.
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

    // Icono WiFi (8×6 px, esquina superior derecha). No interfiere con overlays
    // full-screen (SICK/POOP) — es un sprite de esquina, no un overlay de prioridad.
    drawWifiIcon_();

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
// drawWifiIcon_ — icono WiFi 8×6 px en esquina superior derecha (56, 0).
//
// Se pinta sobre cualquier escena excepto OtaProgress (donde la pantalla de
// actualización tiene prioridad absoluta).
//
// Política de visibilidad según plan §6.2:
//   Disabled     → no se pinta nada.
//   Connected    → kSpriteWifiOn fijo.
//   Disconnected → kSpriteWifiOff fijo.
//   Connecting   → kSpriteWifiOff con blink 500ms (animFrame_ par = visible).
//     A 5 FPS (200 ms/frame), animFrame_ % 2 alterna cada 2 frames = 400 ms.
//     Es la aproximación más cercana a 500ms sin timer dedicado.
// -----------------------------------------------------------------------------
void DisplayRenderer::drawWifiIcon_() {
    if (wifiState_ == WifiState::Disabled) return;
    if (scene_ == Scene::OtaProgress)      return;

    const uint8_t* sprite;
    if (wifiState_ == WifiState::Connected) {
        sprite = kSpriteWifiOn;
    } else {
        sprite = kSpriteWifiOff;
        // Blink cuando está intentando conectar: ocultar en frames impares.
        if (wifiState_ == WifiState::Connecting && (animFrame_ % 2) != 0) {
            return;
        }
    }

    // drawXBMP: x=56, y=0, w=8, h=6 — esquina superior derecha portrait 64×128.
    u8g2_->drawXBMP(56, 0, 8, 6, sprite);
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

    drawWifiIcon_();

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

    drawWifiIcon_();

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
