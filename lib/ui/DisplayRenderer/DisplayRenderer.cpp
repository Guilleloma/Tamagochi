#include "DisplayRenderer.h"
#include <Arduino.h>
#include "config.h"
#include "sprites_generated.h"

// Forward declaration de drawXBMx2 / drawXBMx4 (helpers de pintado escalado).
// La implementación está más abajo en el archivo, pero render() y los drawers
// las necesitan antes.
static void drawXBMx2(U8G2* u8g2, int x, int y, int w, int h, const uint8_t* bitmap);
static void drawXBMx4(U8G2* u8g2, int x, int y, int w, int h, const uint8_t* bitmap);

// Forward declarations de los helpers de animación de acciones (Fase 4.2).
static uint8_t actionAnimFrameCount(ActionKind action);
static const uint8_t* actionAnimFrame(ActionKind action, uint8_t frameIdx);

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

void DisplayRenderer::triggerActionAnimation(ActionKind action) {
    uint8_t frames = actionAnimFrameCount(action);
    if (frames == 0) return;  // acción sin animación de sustitución

    actionAnimKind_         = action;
    actionAnimTotalMs_      = static_cast<uint32_t>(frames) * 600u;  // 600 ms por frame
    actionAnimRemainingMs_  = actionAnimTotalMs_;
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
    if (actionAnimRemainingMs_ > 0) {
        actionAnimRemainingMs_ = (actionAnimRemainingMs_ > 200u) ? (actionAnimRemainingMs_ - 200u) : 0u;
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
// drawXBMx4 — pinta un sprite XBM escalado x4 (cada pixel → cuadrado 4×4).
// Usado para los frames de animación de acciones (16×16 → 64×64) que sustituyen
// al sprite del personaje durante la ejecución de Play/Clean/Discipline.
// -----------------------------------------------------------------------------
static void drawXBMx4(U8G2* u8g2, int x, int y, int w, int h,
                       const uint8_t* bitmap) {
    const int bytesPerRow = (w + 7) / 8;
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            uint8_t byte = pgm_read_byte(bitmap + (row * bytesPerRow) + (col / 8));
            if (byte & (1 << (col % 8))) {
                u8g2->drawBox(x + col * 4, y + row * 4, 4, 4);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// actionAnimFrameCount — número de frames de animación por acción.
// 0 indica que la acción no tiene animación de sustitución.
// -----------------------------------------------------------------------------
static uint8_t actionAnimFrameCount(ActionKind action) {
    switch (action) {
        case ActionKind::Play:       return 4;
        case ActionKind::Clean:      return 2;
        case ActionKind::Discipline: return 2;
        default:                     return 0;
    }
}

// -----------------------------------------------------------------------------
// actionAnimFrame — sprite XBM del frame N de la animación de una acción.
// Devuelve nullptr si la acción no tiene frames o el índice es inválido.
// -----------------------------------------------------------------------------
static const uint8_t* actionAnimFrame(ActionKind action, uint8_t frameIdx) {
    switch (action) {
        case ActionKind::Play:
            switch (frameIdx % 4) {
                case 0: return kSpriteActionPlay0;
                case 1: return kSpriteActionPlay1;
                case 2: return kSpriteActionPlay2;
                case 3: return kSpriteActionPlay3;
            }
            return kSpriteActionPlay0;
        case ActionKind::Clean:
            return (frameIdx % 2) ? kSpriteActionClean1 : kSpriteActionClean0;
        case ActionKind::Discipline:
            return (frameIdx % 2) ? kSpriteActionDiscipline1 : kSpriteActionDiscipline0;
        default:
            return nullptr;
    }
}

// -----------------------------------------------------------------------------
// drawIdleMain_ — pantalla principal portrait (64 wide × 128 tall).
//
// Layout (sprite centrado verticalmente con 16 px de aire arriba y abajo):
//   HUD top      (y=0..15)  : edad izq | estado rotante centro | WiFi 8×6 der
//   Aire         (y=16..31) : 16 px libres
//   Personaje    (y=32..95) : sprite 32×32 escalado ×2 → 64×64
//   Aire         (y=96..111): 16 px libres
//   HUD inferior (y=112..127): icono 16×16 + label en una fila (drawActionHud_)
// -----------------------------------------------------------------------------
void DisplayRenderer::drawIdleMain_() {
    // Dormido con las luces apagadas (correcto, fiel al 97): solo zZZ a
    // oscuras — sin HUD, sin sprite. isSleeping && lightsOn (molesto, el
    // usuario dejó la luz encendida) sigue el render normal de abajo.
    if (stats_.isSleeping && !stats_.lightsOn) {
        drawLightsOffSleep_();
        return;
    }

    u8g2_->clearBuffer();

    // ---- HUD top: edad arriba-izquierda ----
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
    u8g2_->drawStr(0, 11, ageBuf);

    // ---- Personaje: sprite 64×64 centrado en y=32 ----
    // Si hay animación de acción activa (Play/Clean/Disc), sustituimos el
    // sprite del personaje por el frame de acción correspondiente escalado ×4
    // (16×16 → 64×64). Si no, render normal del personaje según mood.
    if (actionAnimRemainingMs_ > 0) {
        uint8_t frames = actionAnimFrameCount(actionAnimKind_);
        if (frames > 0) {
            uint32_t elapsedMs = actionAnimTotalMs_ - actionAnimRemainingMs_;
            uint8_t  frameIdx  = static_cast<uint8_t>((elapsedMs / 600u) % frames);
            const uint8_t* actionSprite = actionAnimFrame(actionAnimKind_, frameIdx);
            if (actionSprite != nullptr) {
                drawXBMx4(u8g2_, 0, 32, 16, 16, actionSprite);
            }
        }
    } else {
        Mood               mood = computeMood_();
        int                stageIdx = static_cast<int>(lifeStage_);
        int                moodIdx  = static_cast<int>(mood);
        const SpriteFrames& sf  = kSpritesByStage[stageIdx][moodIdx];

        uint8_t frameIdx = (sf.frameCount > 0)
                         ? (animFrame_ % sf.frameCount)
                         : 0u;

        const uint8_t* sprite = sf.frames[frameIdx];
        if (sprite != nullptr) {
            drawXBMx2(u8g2_, 0, 32, 32, 32, sprite);
        }
    }

    // ---- Overlays HUD: estado rotante a la derecha ----
    drawOverlays_();

    // ---- WiFi: esquina sup-der del HUD ----
    drawWifiIcon_();

    // ---- HUD inferior: acción activa + dots + hint ----
    drawActionHud_();

    u8g2_->sendBuffer();
}

// -----------------------------------------------------------------------------
// drawLightsOffSleep_ — pantalla oscura mientras duerme con las luces
// apagadas (descanso correcto, fiel al 97): rastro de 3 "z" de tamaño
// creciente ascendiendo en diagonal (arriba-derecha) desde la zona donde
// estaría la cabeza del personaje — sin iconos de estado ni sprite. El
// panel es 1-bit (SSD1306, sin escala de grises): en vez del fundido de
// color del renderer color, cada "z" (posición y fuente fijas) aparece y
// desaparece por VISIBILIDAD escalonada dentro de su ventana del ciclo —
// la pequeña nace, luego la mediana, luego la grande, con hueco breve antes
// de reiniciar — mismo efecto de "rastro ascendente" adaptado a 1-bit.
// El HUD inferior (drawActionHud_) SÍ se mantiene para que el usuario pueda
// ver y alcanzar la acción "LUZ ON" y reencenderlas — si no, quedaría sin
// forma de despertar la pantalla. NO toca ningún pin de backlight (el OLED
// SSD1306 no lo tiene; en el color sí existe TFT_BL y tampoco se toca, ver
// DisplayRendererColor::drawLightsOffSleep_).
// -----------------------------------------------------------------------------
void DisplayRenderer::drawLightsOffSleep_() {
    u8g2_->clearBuffer();

    struct ZSlot {
        const uint8_t* font;
        const char*    glyph;
        uint8_t        x;
        uint8_t        y;   // baseline
    };
    // Posiciones fijas: pequeña cerca de la "cabeza", mediana y grande
    // subiendo en diagonal hacia arriba-derecha (mismo sentido que el color).
    static const ZSlot kSlots[3] = {
        { u8g2_font_6x10_tf,  "z", 24, 58 },
        { u8g2_font_7x14B_tf, "Z", 32, 44 },
        { u8g2_font_10x20_tf, "Z", 38, 26 },
    };
    static constexpr uint8_t kSlotCount   = 3;
    static constexpr uint8_t kPeriod      = 24;                    // frames/ciclo
    static constexpr uint8_t kStagger     = kPeriod / kSlotCount;  // 8 frames entre slots
    static constexpr uint8_t kVisibleSpan = 14;                    // frames visible por slot

    for (uint8_t i = 0; i < kSlotCount; i++) {
        uint8_t local = static_cast<uint8_t>((animFrame_ + i * kStagger) % kPeriod);
        if (local >= kVisibleSpan) continue;   // fuera de ventana -> invisible este frame

        u8g2_->setFont(kSlots[i].font);
        u8g2_->drawStr(kSlots[i].x, kSlots[i].y, kSlots[i].glyph);
    }

    // HUD inferior visible/operable — permite ver y seleccionar "LUZ ON"
    // para reencender sin quedar atrapado en la pantalla a oscuras.
    // drawActionHud_() no llama a sendBuffer() por su cuenta (mismo patrón
    // que drawIdleMain_).
    drawActionHud_();

    // WiFi (esquina superior derecha) — info de SISTEMA, se mantiene visible
    // igual que el HUD inferior (el ESP8266 no tiene icono de batería).
    drawWifiIcon_();

    u8g2_->sendBuffer();
}

// -----------------------------------------------------------------------------
// drawOverlays_ — estado rotante 16×16 en el HUD top, posición fija (x=32, y=0).
//
// Construye la lista de estados activos en orden de prioridad y muestra uno
// cada 1.5 s (≈ 7 ticks a 5 FPS) cuando hay más de uno. Si solo hay uno, se
// queda fijo. Si no hay ninguno, no se pinta nada.
//
// Estados (orden de aparición en la rotación):
//   1. Sick      — kSpriteOverlaySick      cuando stats_.isSick
//   2. SleepingZ — kSpriteOverlaySleepZ0/1 alterna cada 800 ms si isSleeping
//   3. Hungry    — kSpriteOverlayHungry0   cuando hunger >= 80 && !isSleeping
//   4. Poop      — kSpriteOverlayPoop      cuando poopCount > 0
// -----------------------------------------------------------------------------
void DisplayRenderer::drawOverlays_() {
    const uint8_t* statuses[4];
    uint8_t        count = 0;

    if (stats_.isSick) {
        statuses[count++] = kSpriteOverlaySick;
    }
    if (stats_.isSleeping) {
        statuses[count++] = ((animFrame_ / 4) % 2 == 0)
                          ? kSpriteOverlaySleepZ0
                          : kSpriteOverlaySleepZ1;
    }
    if (stats_.hunger >= 80 && !stats_.isSleeping) {
        statuses[count++] = kSpriteOverlayHungry0;
    }
    if (stats_.poopCount > 0) {
        statuses[count++] = kSpriteOverlayPoop;
    }

    if (count == 0) return;

    // Rotación cada 7 ticks (≈ 1.4 s a 5 FPS). Si solo hay 1, idx siempre 0.
    uint8_t idx = (count > 1) ? ((animFrame_ / 7) % count) : 0;
    u8g2_->drawXBMP(32, 0, 16, 16, statuses[idx]);
}

// -----------------------------------------------------------------------------
// drawWifiIcon_ — icono WiFi 8×6 px en esquina superior derecha del HUD.
//
// Posición: x=56..63, y=5..10 (centrado vertical en HUD de 16 px de alto).
// Se pinta sobre cualquier escena excepto OtaProgress.
//
// Política de visibilidad:
//   Disabled     → no se pinta nada.
//   Connected    → kSpriteWifiOn fijo.
//   Disconnected → kSpriteWifiOff fijo.
//   Connecting   → kSpriteWifiOff con blink ~400 ms (animFrame_ par = visible).
// -----------------------------------------------------------------------------
void DisplayRenderer::drawWifiIcon_() {
    if (wifiState_ == WifiState::Disabled) return;
    if (scene_ == Scene::OtaProgress)      return;

    const uint8_t* sprite;
    if (wifiState_ == WifiState::Connected) {
        sprite = kSpriteWifiOn;
    } else {
        sprite = kSpriteWifiOff;
        if (wifiState_ == WifiState::Connecting && (animFrame_ % 2) != 0) {
            return;
        }
    }

    u8g2_->drawXBMP(56, 5, 8, 6, sprite);
}

// -----------------------------------------------------------------------------
// drawStatsView_ — vista densa con las 9 stats en una sola pantalla.
//
// Layout portrait 64×128, fuente `u8g2_font_5x7_tf` (5 px ancho × 7 px alto):
//   y=7    "STATS"  + icono WiFi a la derecha (drawWifiIcon_ pinta y=5..10)
//   y=10   línea separadora horizontal
//   y=18   "Hamb" + barra 24×5 + valor numérico (0-100)
//   y=26   "Anim" + barra 24×5 + valor numérico
//   y=34   "Salu" + barra 24×5 + valor numérico
//   y=42   "Disc" + barra 24×5 + valor numérico
//   y=50   "Peso" + barra 24×5 + valor numérico
//   y=58   "Aten" + barra 24×5 + valor numérico (0-1000)
//   y=62   línea separadora
//   y=70   "Enf:Y/N  Dor:Y/N"
//   y=78   "Caca:N"
//   y=88   "Edad: NhNm"
//   y=100  hint footer "click=Back"
//
// Las barras (24 px de ancho, label de 4 chars × 5 px = 20 px, valor a la derecha)
// caben en los 64 px lógicos del portrait. Valores entre 0 y 100 mapean a 0..22 px
// de relleno; attentionScore (0-1000) mapea a 0..22 px dividiendo por ~46.
// -----------------------------------------------------------------------------
void DisplayRenderer::drawStatsView_() {
    u8g2_->clearBuffer();
    u8g2_->setFont(u8g2_font_5x7_tf);

    // Título y separador.
    u8g2_->drawStr(0, 7, "STATS");
    u8g2_->drawHLine(0, 10, 64);

    // Helper: dibuja "Label barra valor" en una fila.
    // - label: ≤4 chars (5×7 font → 4×5=20 px)
    // - bar: x=22..45 (24 px de ancho, 5 px de alto)
    // - value: x=48 (hasta 4 chars: "1000\0")
    auto drawRow = [this](const char* label, int value, int valueMax, int y) {
        u8g2_->drawStr(0, y, label);

        // Marco de la barra.
        int barX = 22;
        int barY = y - 6;
        u8g2_->drawFrame(barX, barY, 24, 6);

        // Relleno: clampa value al rango [0, valueMax] y mapea a 0..22 px.
        if (value < 0)         value = 0;
        if (value > valueMax)  value = valueMax;
        int fillWidth = (valueMax > 0) ? (value * 22) / valueMax : 0;
        if (fillWidth > 0) {
            u8g2_->drawBox(barX + 1, barY + 1, fillWidth, 4);
        }

        // Valor numérico a la derecha.
        char buf[6];
        snprintf(buf, sizeof(buf), "%d", value);
        u8g2_->drawStr(48, y, buf);
    };

    drawRow("Hamb", stats_.hunger,         100,  18);
    drawRow("Anim", stats_.happiness,      100,  26);
    drawRow("Salu", stats_.health,         100,  34);
    drawRow("Disc", stats_.discipline,     100,  42);
    drawRow("Peso", stats_.weight,         100,  50);
    drawRow("Aten", stats_.attentionScore, 1000, 58);

    // Separador antes de la sección de flags.
    u8g2_->drawHLine(0, 62, 64);

    // Flags compactos: enfermedad y sueño en una línea.
    char flagsBuf[20];
    snprintf(flagsBuf, sizeof(flagsBuf), "Enf:%c Dor:%c",
             stats_.isSick     ? 'Y' : 'N',
             stats_.isSleeping ? 'Y' : 'N');
    u8g2_->drawStr(0, 70, flagsBuf);

    // Cacas en línea propia (puede crecer hasta "Caca:9").
    char poopBuf[10];
    snprintf(poopBuf, sizeof(poopBuf), "Caca:%u", static_cast<unsigned>(stats_.poopCount));
    u8g2_->drawStr(0, 78, poopBuf);

    // Edad formateada — mismo formato que drawIdleMain_ (NhNm o NNm).
    char ageBuf[12];
    if (ageMinutes_ >= 60) {
        uint32_t hours   = ageMinutes_ / 60;
        uint32_t minutes = ageMinutes_ % 60;
        if (hours > 99) hours = 99;
        snprintf(ageBuf, sizeof(ageBuf), "Edad:%uh%um",
                 static_cast<unsigned>(hours),
                 static_cast<unsigned>(minutes));
    } else {
        snprintf(ageBuf, sizeof(ageBuf), "Edad:%um",
                 static_cast<unsigned>(ageMinutes_));
    }
    u8g2_->drawStr(0, 88, ageBuf);

    // Hint footer: short click cierra esta vista y vuelve al HUD principal.
    u8g2_->drawStr(0, 100, "click=Back");

#ifdef TURBO_MODE
    drawTurboBanner_();
#endif

    drawWifiIcon_();

    u8g2_->sendBuffer();
}

// -----------------------------------------------------------------------------
// actionLabel — etiqueta del HUD inferior para una ActionKind. Lights alterna
// según lightsOn (NO isSleeping — el sueño es automático). Stats es la
// entrada a StatsView. Mayúsculas: contrastan con el resto de fuentes
// pequeñas y dan presencia. "LUZ" (no "LUCES"): a 6px/char con fuente
// u8g2_font_6x10_tf, el label arranca en x=18 sobre 64px de pantalla — solo
// caben ~7-8 chars ("LUZ OFF" = 7 chars = 42px, cabe con margen).
// -----------------------------------------------------------------------------
static const char* actionLabel(ActionKind action, bool lightsOn) {
    switch (action) {
        case ActionKind::Feed:       return "FEED";
        case ActionKind::Play:       return "PLAY";
        case ActionKind::Clean:      return "CLEAN";
        case ActionKind::Medicate:   return "MED";
        case ActionKind::Discipline: return "DISC";
        case ActionKind::Lights:     return lightsOn ? "LUZ OFF" : "LUZ ON";
        case ActionKind::Stats:      return "STATS";
    }
    return "?";
}

// -----------------------------------------------------------------------------
// actionIcon — sprite XBM 16×16 asociado a cada ActionKind. Los iconos viven
// en sprites_generated.h y se generan desde assets/sprites/action_*.png.
// -----------------------------------------------------------------------------
static const uint8_t* actionIcon(ActionKind action) {
    switch (action) {
        case ActionKind::Feed:       return kSpriteActionFeed;
        case ActionKind::Play:       return kSpriteActionPlay;
        case ActionKind::Clean:      return kSpriteActionClean;
        case ActionKind::Medicate:   return kSpriteActionMedicate;
        case ActionKind::Discipline: return kSpriteActionDiscipline;
        case ActionKind::Lights:     return kSpriteActionSleep;
        case ActionKind::Stats:      return kSpriteActionStats;
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// drawActionHud_ — HUD inferior compacto en una sola fila (Fase 4.2 refinado).
//
// Ocupa solo y=112..127 (16 px). Deja toda la franja y=80..111 vacía para que
// el bicho respire visualmente — el sprite es el protagonista.
//   x=0..15  y=112..127  icono 16×16 de la acción activa
//   x=18..   baseline y=124  label fuente 6×10 alineado al icono
// -----------------------------------------------------------------------------
void DisplayRenderer::drawActionHud_() {
    if (visibleCount_ == 0) return;

    // Clamp defensivo: si las preconditions reducen la lista entre frames y el
    // cursor quedó fuera, lo replegamos al primer item para el render actual.
    uint8_t cursor = (selectedIndex_ < visibleCount_) ? selectedIndex_ : 0;
    ActionKind action = visibleActions_[cursor];

    // Icono 16×16 anclado abajo-izquierda.
    const uint8_t* icon = actionIcon(action);
    if (icon != nullptr) {
        u8g2_->drawXBMP(0, 112, 16, 16, icon);
    }

    // Label 6×10 a la derecha del icono, centrado verticalmente respecto a él.
    // Centro vertical del icono = y=120 → baseline 6×10 a y=124 deja el cap entre y=115..124.
    const char* label = actionLabel(action, stats_.lightsOn);
    u8g2_->setFont(u8g2_font_6x10_tf);
    u8g2_->drawStr(18, 124, label);
}

// -----------------------------------------------------------------------------
// refreshActionMenu — recalcula visibleActions_ a partir del StatsEngine actual.
//
// Política de cursor (Fase 4.1): preserva la posición del cursor si la acción
// previa sigue presente en la nueva lista. Si desapareció (p.ej. Medicate ya no
// aplica porque el bicho se curó), el cursor se replega al primer item.
// Esta política aporta continuidad visual al usuario tras ejecutar acciones.
// -----------------------------------------------------------------------------
void DisplayRenderer::refreshActionMenu(const StatsEngine& engine) {
    // Recordar la acción seleccionada antes del recálculo (si existía).
    ActionKind previous = (visibleCount_ > 0 && selectedIndex_ < visibleCount_)
                        ? visibleActions_[selectedIndex_]
                        : ActionKind::Feed;

    visibleCount_ = engine.listAvailableActions(visibleActions_);

    // Buscar la acción anterior en la nueva lista para preservar el cursor.
    selectedIndex_ = 0;
    for (uint8_t i = 0; i < visibleCount_; ++i) {
        if (visibleActions_[i] == previous) {
            selectedIndex_ = i;
            break;
        }
    }
}

// -----------------------------------------------------------------------------
// moveCursor — avanza el cursor al siguiente item visible (circular).
// -----------------------------------------------------------------------------
void DisplayRenderer::moveCursor() {
    if (visibleCount_ == 0) return;
    selectedIndex_ = (selectedIndex_ + 1) % visibleCount_;
}

// -----------------------------------------------------------------------------
// currentSelectedAction — devuelve la acción bajo el cursor. Stats es el item
// garantizado por listAvailableActions, así que sirve como fallback seguro.
// -----------------------------------------------------------------------------
ActionKind DisplayRenderer::currentSelectedAction() const {
    if (visibleCount_ == 0)              return ActionKind::Stats;
    if (selectedIndex_ >= visibleCount_) return ActionKind::Stats;
    return visibleActions_[selectedIndex_];
}

// -----------------------------------------------------------------------------
// drawOtaProgress_ — pantalla de progreso OTA (Fase 3 hito 2).
//
// Layout portrait 64×128:
//   y=12        "Updating..." centrado (fuente 5×7)
//   y=30        "X%" centrado (porcentaje numérico, fuente 6×10)
//   y=50..58    Barra de progreso: frame 64×8 + relleno proporcional
//
// La barra ocupa x=0..63, y=50..57 (64px de ancho).
// Relleno: fillWidth = otaProgress_ * 62 / 100 (px dentro del marco de 62 px).
//
// El icono WiFi NO se pinta aquí — en pantalla OTA la prioridad visual es máxima.
// drawWifiIcon_() tiene guard: if (scene_ == Scene::OtaProgress) return.
// -----------------------------------------------------------------------------
void DisplayRenderer::drawOtaProgress_() {
    u8g2_->clearBuffer();

    // Texto "Updating..." — fuente 5×7, 11 chars × 5px = 55 → x = (64-55)/2 ≈ 4
    u8g2_->setFont(u8g2_font_5x7_tf);
    u8g2_->drawStr(4, 20, "Updating...");

    // Porcentaje numérico centrado — fuente 6×10
    // Hasta 3 dígitos + "%" = 4 chars × 6px = 24 → x = (64-24)/2 = 20
    u8g2_->setFont(u8g2_font_6x10_tf);
    char pctBuf[6];
    snprintf(pctBuf, sizeof(pctBuf), "%u%%", static_cast<unsigned>(otaProgress_));
    // Centrar dinámicamente: medir ancho en chars × 6 px
    uint8_t pctLen = static_cast<uint8_t>(strlen(pctBuf));
    int pctX = (64 - pctLen * 6) / 2;
    if (pctX < 0) pctX = 0;
    u8g2_->drawStr(pctX, 44, pctBuf);

    // Barra de progreso: marco exterior 64×8 en y=50, relleno proporcional.
    u8g2_->drawFrame(0, 50, 64, 8);
    int fillWidth = (static_cast<int>(otaProgress_) * 62) / 100;
    if (fillWidth > 0) {
        u8g2_->drawBox(1, 51, fillWidth, 6);
    }

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
