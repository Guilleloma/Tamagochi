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
//
// 3. Health condicional:
//    Health solo decae si hunger >= 90 (casi hambriento al límite) O happiness <= 10
//    (casi deprimido). Fuera de esas condiciones, estar bien comido y contento
//    mantiene la salud estable.
//
// 4. Las acciones (feed/play/heal) resetean el acumulador del stat correspondiente
//    para evitar que una fracción acumulada se aplique inmediatamente en el siguiente
//    tick y cause un doble descuento justo tras una acción del usuario.
//
// 5. El flag dirty_ solo se marca si cambia el valor entero del stat (uint8_t),
//    no por cambios en los acumuladores fraccionales. Así no se genera ruido en
//    EventBus en los ticks donde aún no se acumuló 1 punto entero.
// =============================================================================

#include "StatsEngine.h"

// -----------------------------------------------------------------------------
// Constante interna: límite de clamp de dtMs para evitar overflow tras meses offline.
// 24 horas en milisegundos.
// -----------------------------------------------------------------------------
static constexpr uint32_t kMaxDtMs = 86400000UL;

StatsEngine::StatsEngine()
    : stats_(), rates_() {}

StatsEngine::StatsEngine(const DecayRates& rates)
    : stats_(), rates_(rates) {}

void StatsEngine::tick(uint32_t dtMs) {
    if (dtMs == 0) return;

    // Clamp a 24h para proteger contra saltos temporales enormes al cargar estado
    // guardado hace semanas/meses con el dispositivo apagado.
    if (dtMs > kMaxDtMs) dtMs = kMaxDtMs;

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

    // --- health: solo decae si hunger >= 90 o happiness <= 10 ---
    // Estar muy hambriento o muy triste daña la salud; en condiciones normales
    // la salud se mantiene estable.
    const bool isHealthEndangered = (stats_.hunger >= 90) || (stats_.happiness <= 10);
    if (isHealthEndangered) {
        healthAcc_ += dtSec * rates_.healthPerSec;
        while (healthAcc_ >= 1.0f) {
            healthAcc_ -= 1.0f;
            uint8_t prev = stats_.health;
            stats_.health = clamp_(static_cast<int>(stats_.health) - 1);
            if (stats_.health != prev) dirty_ = true;
        }
    } else {
        // Resetear acumulador cuando no hay peligro para evitar que se acumule
        // silenciosamente durante periodos safe y luego descargue de golpe.
        healthAcc_ = 0.0f;
    }
}

void StatsEngine::applyFeed(uint8_t amount) {
    uint8_t prev = stats_.hunger;
    stats_.hunger = clamp_(static_cast<int>(stats_.hunger) - amount);
    // Resetear acumulador: el usuario acaba de saciar, no tiene sentido
    // aplicar hambre adicional en el siguiente tick por fracciones pre-acumuladas.
    hungerAcc_ = 0.0f;
    if (stats_.hunger != prev) dirty_ = true;
}

void StatsEngine::applyPlay(uint8_t amount) {
    uint8_t prev = stats_.happiness;
    stats_.happiness = clamp_(static_cast<int>(stats_.happiness) + amount);
    happinessAcc_ = 0.0f;
    if (stats_.happiness != prev) dirty_ = true;
}

void StatsEngine::applyHeal(uint8_t amount) {
    uint8_t prev = stats_.health;
    stats_.health = clamp_(static_cast<int>(stats_.health) + amount);
    healthAcc_ = 0.0f;
    if (stats_.health != prev) dirty_ = true;
}

bool StatsEngine::consumeDirtyFlag() {
    bool was = dirty_;
    dirty_ = false;
    return was;
}

uint8_t StatsEngine::clamp_(int value) {
    if (value < 0)   return 0;
    if (value > 100) return 100;
    return static_cast<uint8_t>(value);
}
