// =============================================================================
// StatsEngine.cpp — implementación de decay y acciones.
//
// Decisiones de diseño documentadas aquí:
//
// 1. Decay fraccional con acumulador float:
//    Se suma dtSec * rate al acumulador. Cuando acc >= 1.0 se resta 1.0 y se
//    aplica 1 punto entero al stat. Esto garantiza que tasas bajas (0.03 pts/s)
//    no se pierden a ticks de 1 Hz — el sobrante fraccional se lleva al siguiente tick.
//
// 2. Hunger SUBE con el tiempo (más hambre), las acciones la BAJAN (sacian).
//    Happiness BAJA con el tiempo (más triste), las acciones la SUBEN.
//    Health BAJA con el tiempo, las acciones la SUBEN.
//    Weight BAJA por metabolismo; SUBE al alimentar.
//    Discipline BAJA con el tiempo; SUBE con applyDiscipline.
//
// 3. Health condicional:
//    Health solo decae si hunger >= 90 O happiness <= 10 O weight <= 10 O weight >= 90.
//    Si isSick, el decay de health se multiplica por healthDecayWhenSickMul (×5).
//
//    EXTENSIÓN FASE 2 — isSick fuera de zona de peligro:
//    Adicionalmente, si isSick=true y NO se cumple ninguna condición de peligro,
//    se aplica un decay de health = rate × multiplicador (×5). Decisión intencional:
//    la enfermedad tiene consecuencias permanentes aunque el resto de stats estén
//    sanos. Sin esto, isSick solo importaría combinado con hambre/tristeza, lo que
//    haría irrelevante la mecánica de enfermedad como vector de muerte propio.
//
// 4. isSleeping paraliza hunger, happiness, discipline decay. Solo health regenera
//    lentamente (+0.002 × dt/1000 por segundo).
//
// 5. Las acciones (feed/play) resetean el acumulador del stat correspondiente
//    para evitar que una fracción acumulada se aplique inmediatamente en el siguiente
//    tick y cause un doble descuento justo tras una acción del usuario.
//
// 6. El flag dirty_ solo se marca si cambia el valor entero del stat (uint8_t),
//    no por cambios en los acumuladores fraccionales. Así no se genera ruido en
//    EventBus en los ticks donde aún no se acumuló 1 punto entero.
//
// 7. Aleatoriedad inyectable: sRand01_ apunta por defecto a defaultRand01_ pero
//    puede sustituirse en tests con setRandomSource() para comportamiento determinista.
// =============================================================================

#include "StatsEngine.h"
#include "Logger.h"
#include <stdlib.h>   // rand(), RAND_MAX

// -----------------------------------------------------------------------------
// Constante interna: límite de clamp de dtMs para evitar overflow tras meses offline.
// -----------------------------------------------------------------------------
static constexpr uint32_t kMaxDtMs = 86400000UL;

// Intervalo en ms para actualizar attentionScore (10 segundos)
static constexpr uint32_t kAttentionUpdateIntervalMs = 10000UL;

// -----------------------------------------------------------------------------
// Función de aleatoriedad por defecto: rand() normalizado a [0, 1).
// Compatible con host-native y ESP8266 sin Arduino.h.
// -----------------------------------------------------------------------------
static float defaultRand01_() {
    return static_cast<float>(rand()) / (static_cast<float>(RAND_MAX) + 1.0f);
}

// Definición del puntero estático
float (*StatsEngine::sRand01_)() = &defaultRand01_;

// -----------------------------------------------------------------------------
// Constructores
// -----------------------------------------------------------------------------
StatsEngine::StatsEngine()
    : stats_(), rates_() {}

StatsEngine::StatsEngine(const DecayRates& rates)
    : stats_(), rates_(rates) {}

// -----------------------------------------------------------------------------
// setRandomSource — para tests deterministas
// -----------------------------------------------------------------------------
void StatsEngine::setRandomSource(float (*src)()) {
    sRand01_ = src ? src : &defaultRand01_;
}

// -----------------------------------------------------------------------------
// tick — avanza la simulación dtMs milisegundos
// -----------------------------------------------------------------------------
void StatsEngine::tick(uint32_t dtMs) {
    if (dtMs == 0) return;

    // Clamp a 24h para proteger contra saltos temporales enormes al cargar estado
    // guardado hace semanas/meses con el dispositivo apagado.
    if (dtMs > kMaxDtMs) dtMs = kMaxDtMs;

    // -------------------------------------------------------------------------
    // Modo durmiendo: solo health regenera lentamente, nada más decae.
    // -------------------------------------------------------------------------
    if (stats_.isSleeping) {
        healthAcc_ += static_cast<float>(dtMs) * 0.002f;
        while (healthAcc_ >= 1.0f) {
            healthAcc_ -= 1.0f;
            uint8_t prev = stats_.health;
            stats_.health = clamp_(static_cast<int>(stats_.health) + 1);
            if (stats_.health != prev) dirty_ = true;
        }
        return;
    }

    float dtSec = static_cast<float>(dtMs) / 1000.0f;

    // --- hunger: sube con el tiempo ---
    hungerAcc_ += dtSec * rates_.hungerPerSec;
    while (hungerAcc_ >= 1.0f) {
        hungerAcc_ -= 1.0f;
        uint8_t prev = stats_.hunger;
        stats_.hunger = clamp_(static_cast<int>(stats_.hunger) + 1);
        if (stats_.hunger != prev) dirty_ = true;
    }

    // --- happiness: baja con el tiempo ---
    happinessAcc_ += dtSec * rates_.happinessPerSec;
    while (happinessAcc_ >= 1.0f) {
        happinessAcc_ -= 1.0f;
        uint8_t prev = stats_.happiness;
        stats_.happiness = clamp_(static_cast<int>(stats_.happiness) - 1);
        if (stats_.happiness != prev) dirty_ = true;
    }

    // --- weight: baja por metabolismo ---
    weightAcc_ += dtSec * rates_.weightDecayPerSec;
    while (weightAcc_ >= 1.0f) {
        weightAcc_ -= 1.0f;
        uint8_t prev = stats_.weight;
        stats_.weight = clamp_(static_cast<int>(stats_.weight) - 1);
        if (stats_.weight != prev) dirty_ = true;
    }

    // --- discipline: baja con el tiempo ---
    disciplineAcc_ += dtSec * rates_.disciplinePerSec;
    while (disciplineAcc_ >= 1.0f) {
        disciplineAcc_ -= 1.0f;
        uint8_t prev = stats_.discipline;
        stats_.discipline = clamp_(static_cast<int>(stats_.discipline) - 1);
        if (stats_.discipline != prev) dirty_ = true;
    }

    // --- health: decae condicionalmente ---
    // Condiciones de peligro: muy hambriento, muy triste, o peso extremo.
    const bool isHealthEndangered = (stats_.hunger >= 90)
                                 || (stats_.happiness <= 10)
                                 || (stats_.weight <= 10)
                                 || (stats_.weight >= 90);

    if (isHealthEndangered) {
        float healthMul = 1.0f;
        if (stats_.isSick) healthMul *= rates_.healthDecayWhenSickMul;
        healthAcc_ += dtSec * rates_.healthPerSec * healthMul;
        while (healthAcc_ >= 1.0f) {
            healthAcc_ -= 1.0f;
            uint8_t prev = stats_.health;
            stats_.health = clamp_(static_cast<int>(stats_.health) - 1);
            if (stats_.health != prev) dirty_ = true;
        }
    } else if (!stats_.isSick) {
        // Resetear acumulador cuando no hay peligro y no está enfermo para evitar
        // que se acumule silenciosamente y descargue de golpe al entrar en peligro.
        healthAcc_ = 0.0f;
    } else {
        // Está enfermo pero no en condición de peligro: health aún decae (×5)
        healthAcc_ += dtSec * rates_.healthPerSec * rates_.healthDecayWhenSickMul;
        while (healthAcc_ >= 1.0f) {
            healthAcc_ -= 1.0f;
            uint8_t prev = stats_.health;
            stats_.health = clamp_(static_cast<int>(stats_.health) - 1);
            if (stats_.health != prev) dirty_ = true;
        }
    }

    // --- Enfermedad espontánea por cacas presentes ---
    if (stats_.poopCount > 0 && !stats_.isSick) {
        float probSick = static_cast<float>(stats_.poopCount)
                       * rates_.sicknessProbPerPoop
                       * dtSec;
        if (sRand01_() < probSick) {
            stats_.isSick = true;
            dirty_ = true;
        }
    }

    // --- Actualizar attentionScore pasivamente ---
    updateAttentionScore_(dtMs);
}

// -----------------------------------------------------------------------------
// updateAttentionScore_ — cada 10s ajusta attentionScore según estado del pet
// -----------------------------------------------------------------------------
void StatsEngine::updateAttentionScore_(uint32_t dtMs) {
    attentionAcc_ += static_cast<float>(dtMs);

    while (attentionAcc_ >= static_cast<float>(kAttentionUpdateIntervalMs)) {
        attentionAcc_ -= static_cast<float>(kAttentionUpdateIntervalMs);

        bool isHealthy  = (stats_.hunger < 70)
                       && (stats_.happiness > 30)
                       && (stats_.health > 50);
        bool isNeglected = (stats_.hunger >= 90)
                        || (stats_.happiness <= 10)
                        || stats_.isSick;

        if (isHealthy && !isNeglected) {
            if (stats_.attentionScore < 1000) {
                stats_.attentionScore++;
                dirty_ = true;
            }
        } else if (isNeglected) {
            if (stats_.attentionScore > 0) {
                stats_.attentionScore--;
                dirty_ = true;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// applyFeed — sacia hunger, añade peso, puede spawnar caca
// -----------------------------------------------------------------------------
void StatsEngine::applyFeed(uint8_t amount) {
    if (amount == 0) return;
    if (amount > 100) amount = 100;

    if (stats_.isSleeping) {
        LOG_DEBUG("applyFeed ignorado: tamagochi dormido");
        return;
    }

    uint8_t prev = stats_.hunger;
    stats_.hunger = clamp_(static_cast<int>(stats_.hunger) - amount);
    hungerAcc_ = 0.0f;
    if (stats_.hunger != prev) dirty_ = true;

    // Peso: solo sube si el feed es significativo (>10 pts)
    if (amount > 10) {
        uint8_t prevW = stats_.weight;
        stats_.weight = clamp_(static_cast<int>(stats_.weight)
                             + static_cast<int>(rates_.weightDeltaPerFeed));
        if (stats_.weight != prevW) dirty_ = true;
    }

    onFeedTriggered_();
}

// -----------------------------------------------------------------------------
// onFeedTriggered_ — intenta spawnar caca probabilísticamente
// -----------------------------------------------------------------------------
void StatsEngine::onFeedTriggered_() {
    if (stats_.poopCount >= kMaxPoopCount) return;
    if (sRand01_() < rates_.poopSpawnPerHungerEat) {
        stats_.poopCount++;
        dirty_ = true;
    }
}

// -----------------------------------------------------------------------------
// applyPlay — jugar sube happiness, pero también da hambre (coste energético).
// Coste: hunger += amount/2 (jugar quema energía → necesidad de comer).
// Ignorado si está durmiendo, consistente con applyFeed.
// -----------------------------------------------------------------------------
void StatsEngine::applyPlay(uint8_t amount) {
    if (stats_.isSleeping) {
        LOG_DEBUG("applyPlay ignorado: tamagochi dormido");
        return;
    }
    uint8_t prevHappy  = stats_.happiness;
    uint8_t prevHunger = stats_.hunger;

    stats_.happiness = clamp_(static_cast<int>(stats_.happiness) + amount);
    stats_.hunger    = clamp_(static_cast<int>(stats_.hunger) + amount / 2);

    happinessAcc_ = 0.0f;
    hungerAcc_    = 0.0f;

    if (stats_.happiness != prevHappy || stats_.hunger != prevHunger) {
        dirty_ = true;
    }
}

// -----------------------------------------------------------------------------
// applyClean — resetea poopCount a 0
// -----------------------------------------------------------------------------
void StatsEngine::applyClean() {
    if (stats_.poopCount == 0) return;
    stats_.poopCount = 0;
    dirty_ = true;
    LOG_INFO("Clean: poop=0");
}

// -----------------------------------------------------------------------------
// applyMedicate — cura isSick, health +20
// -----------------------------------------------------------------------------
void StatsEngine::applyMedicate() {
    if (!stats_.isSick) {
        LOG_DEBUG("Medicate ignorado: no estaba enfermo");
        return;
    }
    stats_.isSick = false;
    stats_.health = clamp_(static_cast<int>(stats_.health) + 20);
    dirty_ = true;
}

// -----------------------------------------------------------------------------
// applyDiscipline — discipline += amt, happiness -= 5 (coste social)
// -----------------------------------------------------------------------------
void StatsEngine::applyDiscipline(uint8_t amt) {
    if (amt == 0) return;
    if (amt > 100) amt = 100;
    stats_.discipline = clamp_(static_cast<int>(stats_.discipline) + amt);
    // Disciplinar baja un poco happiness — coste social inevitable
    stats_.happiness  = clamp_(static_cast<int>(stats_.happiness) - 5);
    dirty_ = true;
}

// -----------------------------------------------------------------------------
// applySleep — activa o desactiva el modo sueño
// -----------------------------------------------------------------------------
void StatsEngine::applySleep(bool on) {
    if (stats_.isSleeping == on) return;
    stats_.isSleeping = on;
    dirty_ = true;
}

// -----------------------------------------------------------------------------
// resetForNewLife — stats a defaults v2 para un nuevo ciclo de vida
// -----------------------------------------------------------------------------
void StatsEngine::resetForNewLife() {
    stats_.hunger        = 50;
    stats_.happiness     = 50;
    stats_.health        = 100;
    stats_.discipline    = 50;
    stats_.weight        = 50;
    stats_.poopCount     = 0;
    stats_.isSick        = false;
    stats_.isSleeping    = false;
    stats_.attentionScore = 500;

    // Resetear acumuladores
    hungerAcc_     = 0.0f;
    happinessAcc_  = 0.0f;
    healthAcc_     = 0.0f;
    weightAcc_     = 0.0f;
    disciplineAcc_ = 0.0f;
    attentionAcc_  = 0.0f;

    dirty_ = true;
}

// -----------------------------------------------------------------------------
// consumeDirtyFlag
// -----------------------------------------------------------------------------
bool StatsEngine::consumeDirtyFlag() {
    bool was = dirty_;
    dirty_ = false;
    return was;
}

// -----------------------------------------------------------------------------
// listAvailableActions — filtro de acciones aplicables al estado actual.
//
// Reglas (alineadas con preconditions de los apply*):
//   - Feed:       !isSleeping            (applyFeed ignora si dormido)
//   - Play:       !isSleeping            (applyPlay ignora si dormido)
//   - Clean:      poopCount > 0          (applyClean es no-op si poop=0)
//   - Medicate:   isSick                 (applyMedicate es no-op si !sick)
//   - Discipline: siempre                (sin precondición en dominio)
//   - Lights:     siempre                (toggle luces on/off — label dinámico en UI;
//                                          TODO(sleep→lights): próximo commit desacopla de isSleeping)
//   - Stats:      siempre (último)       (no es acción real, abre StatsView)
// -----------------------------------------------------------------------------
uint8_t StatsEngine::listAvailableActions(ActionKind out[kMaxActionMenuItems]) const {
    uint8_t n = 0;

    if (!stats_.isSleeping)   { out[n++] = ActionKind::Feed; }
    if (!stats_.isSleeping)   { out[n++] = ActionKind::Play; }
    if (stats_.poopCount > 0) { out[n++] = ActionKind::Clean; }
    if (stats_.isSick)        { out[n++] = ActionKind::Medicate; }

    // Discipline y Lights son incondicionales — Lights alterna en la UI según lightsOn.
    out[n++] = ActionKind::Discipline;
    out[n++] = ActionKind::Lights;

    // Stats siempre como última entrada — el HUD inferior la usa para abrir StatsView.
    out[n++] = ActionKind::Stats;

    return n;
}

// -----------------------------------------------------------------------------
// clamp_ — clamp seguro a [0, 100] desde int
// -----------------------------------------------------------------------------
uint8_t StatsEngine::clamp_(int value) {
    if (value < 0)   return 0;
    if (value > 100) return 100;
    return static_cast<uint8_t>(value);
}
