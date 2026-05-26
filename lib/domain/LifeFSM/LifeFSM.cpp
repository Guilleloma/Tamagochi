// =============================================================================
// LifeFSM.cpp — implementación de la máquina de estados de vida.
//
// Ver LifeFSM.h para contrato completo y semántica de cada método.
// Sin Arduino.h: compilable en host-native para tests.
// =============================================================================
#include "LifeFSM.h"
#include "EventBus.h"
#include "StatsEngine.h"   // struct Stats

// -----------------------------------------------------------------------------
// Helper local: suma saturante a uint16_t con cap explícito.
// Evita desbordamiento cuando dtMs acumula sobre deathSustainMs_ (uint16_t).
// cap debe caber en uint16_t (máximo 65535).
// -----------------------------------------------------------------------------
static uint16_t saturatingAddU16(uint16_t a, uint32_t b, uint32_t cap) {
    uint32_t sum = static_cast<uint32_t>(a) + b;
    if (sum > cap) {
        return static_cast<uint16_t>(cap);
    }
    return static_cast<uint16_t>(sum);
}

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
LifeFSM::LifeFSM(EventBus& bus) : bus_(&bus) {}

// -----------------------------------------------------------------------------
// Setters para carga desde Persistence al boot.
// No publican eventos — la primera transición real ocurre en el loop.
// -----------------------------------------------------------------------------
void LifeFSM::setStage(LifeStage stage) {
    stage_ = stage;
}

void LifeFSM::setAgeMinutes(uint32_t minutes) {
    ageMinutes_ = minutes;
}

void LifeFSM::setDeathSustainMs(uint16_t ms) {
    deathSustainMs_ = ms;
}

// -----------------------------------------------------------------------------
// tick() — punto de entrada principal, llamado por el main loop.
//
// Si estamos en Dead: solo acumula deadElapsedMs_ y eventualmente resurrecta.
// Si estamos en cualquier otra etapa:
//   1. Acumula ageAccMs_ → convierte a minutos enteros en ageMinutes_.
//   2. Gestiona el sustain timer de health==0 (muerte por agotamiento).
//   3. Evalúa transiciones por tiempo/atención.
//
// Nota: el LifeFSM no controla el decay de StatsEngine. Cuando stage es Egg
// o Dead, main.cpp llama a StatsEngine::setDecayPaused(true). El LifeFSM
// simplemente no evalúa muerte en esos estados (Egg no tiene health drop;
// Dead está congelado).
// -----------------------------------------------------------------------------
void LifeFSM::tick(uint32_t dtMs, const Stats& stats, uint16_t attentionScore) {
    // --- Rama Dead: solo cuenta resurrección ---
    if (stage_ == LifeStage::Dead) {
        deadElapsedMs_ += dtMs;
        if (deadElapsedMs_ >= kResurrectionMs) {
            transitionTo_(LifeStage::Egg);
        }
        return;
    }

    // --- Avance de edad ---
    ageAccMs_ += dtMs;
    if (ageAccMs_ >= 60000u) {
        ageMinutes_ += ageAccMs_ / 60000u;
        ageAccMs_   %= 60000u;
    }

    // --- Death sustain timer (no aplica en Egg: los huevos no tienen health drop) ---
    if (stage_ != LifeStage::Egg) {
        if (stats.health == 0) {
            deathSustainMs_ = saturatingAddU16(deathSustainMs_, dtMs, kDeathSustainMs);
            if (deathSustainMs_ >= static_cast<uint16_t>(kDeathSustainMs)) {
                uint8_t cause = inferDeathCause_(stats);
                transitionTo_(LifeStage::Dead, cause);
                return;
            }
        } else {
            // Health recuperado: resetear el sustain timer
            deathSustainMs_ = 0;
        }
    }

    // --- Transiciones por tiempo y atención ---
    evaluateTransitions_(stats, attentionScore);
}

// -----------------------------------------------------------------------------
// evaluateTransitions_() — evalúa la etapa actual y transiciona si corresponde.
//
// Baby → Kid:
//   - Transición normal: ageMinutes >= kBabyToKidMin Y attentionScore >= 300.
//   - Transición forzada: ageMinutes >= kBabyToKidMin * 3/2 (evita Baby eterno).
//
// Kid → Adult:
//   - Se determina AdultType según attentionScore.
//
// Old → Dead:
//   - Causa 3 = oldAge.
// -----------------------------------------------------------------------------
void LifeFSM::evaluateTransitions_(const Stats& stats, uint16_t attentionScore) {
    (void)stats;   // stats no se usa directamente aquí, pero se mantiene en firma para extensibilidad

    switch (stage_) {
        case LifeStage::Egg:
            if (ageMinutes_ >= kEggHatchMin) {
                transitionTo_(LifeStage::Baby);
            }
            break;

        case LifeStage::Baby:
            if (ageMinutes_ >= kBabyToKidMin && attentionScore >= 300) {
                transitionTo_(LifeStage::Kid);
            } else if (ageMinutes_ >= (kBabyToKidMin * 3u / 2u)) {
                // Forzado: evita Baby eterno por attentionScore bajo
                transitionTo_(LifeStage::Kid);
            }
            break;

        case LifeStage::Kid:
            if (ageMinutes_ >= kKidToAdultMin) {
                AdultType type = computeAdultType_(attentionScore);
                transitionToAdult_(type);
            }
            break;

        case LifeStage::Adult:
            if (ageMinutes_ >= kAdultToOldMin) {
                transitionTo_(LifeStage::Old);
            }
            break;

        case LifeStage::Old:
            if (ageMinutes_ >= kOldToDeadMin) {
                transitionTo_(LifeStage::Dead, /*oldAge=*/3);
            }
            break;

        default:
            break;
    }
}

// -----------------------------------------------------------------------------
// computeAdultType_() — determina el tipo de adulto por attentionScore.
//
// >= 800 → Athletic (cuidado excelente)
// 500-799 → Normal
// 200-499 → Grumpy (cuidado mediocre)
// < 200   → Obese  (descuidado, sobrealimentado)
// -----------------------------------------------------------------------------
AdultType LifeFSM::computeAdultType_(uint16_t attentionScore) const {
    if (attentionScore >= 800) return AdultAthletic;
    if (attentionScore >= 500) return AdultNormal;
    if (attentionScore >= 200) return AdultGrumpy;
    return AdultObese;
}

// -----------------------------------------------------------------------------
// inferDeathCause_() — infiere la causa de muerte desde el estado de stats.
//
// Se llama solo cuando health==0 se sostiene durante kDeathSustainMs.
// Prioridad: sickness > hunger > neglect > unknown.
// -----------------------------------------------------------------------------
uint8_t LifeFSM::inferDeathCause_(const Stats& s) const {
    if (s.isSick)          return 2;   // sickness
    if (s.hunger >= 90)    return 1;   // hunger
    if (s.happiness <= 10) return 4;   // neglect (tristeza extrema)
    return 5;                           // unknown
}

// -----------------------------------------------------------------------------
// transitionTo_() — punto único de cambio de etapa.
//
// Garantías:
//   - ageMinutes_ se resetea a 0 al volver a Egg (resurrección).
//   - deadElapsedMs_ se resetea al salir de Dead (implícito: solo se entra
//     por transitionTo_(Egg) desde la rama Dead en tick()).
//   - deathSustainMs_ se resetea en cada transición.
//   - lastDeathCause_ se guarda SOLO cuando newStage == Dead.
//   - Publica LifeStateChanged con payload codificado.
//
// Payload: (newStage << 8) | adultType_
//   El adultType_ es relevante solo al transicionar a Adult, pero se incluye
//   siempre para que el handler de main.cpp no tenga que recordar el valor anterior.
// -----------------------------------------------------------------------------
void LifeFSM::transitionTo_(LifeStage newStage, uint8_t deathCause) {
    if (newStage == LifeStage::Egg) {
        // Resurrección: reset completo de contadores de edad
        ageMinutes_    = 0;
        ageAccMs_      = 0;
        deadElapsedMs_ = 0;
    }

    if (newStage == LifeStage::Dead) {
        lastDeathCause_ = deathCause;
    }

    deathSustainMs_ = 0;
    stage_ = newStage;

    // Publicar evento de cambio de etapa
    // payload high byte = newStage, low byte = adultType_
    int32_t payload = (static_cast<int32_t>(newStage) << 8) | static_cast<int32_t>(adultType_);
    bus_->publish({ EventType::LifeStateChanged, payload });
}

// -----------------------------------------------------------------------------
// transitionToAdult_() — establece el tipo de adulto y llama a transitionTo_.
// -----------------------------------------------------------------------------
void LifeFSM::transitionToAdult_(AdultType type) {
    adultType_ = type;
    transitionTo_(LifeStage::Adult);
}
