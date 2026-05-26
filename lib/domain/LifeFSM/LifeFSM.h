#pragma once
// =============================================================================
// LifeFSM — máquina de estados de vida del Tamagochi.
//
// 6 etapas: Egg → Baby → Kid → Adult → Old → Dead
// Publica LifeStateChanged en EventBus en cada transición.
//
// Sin Arduino.h: compilable en host-native para tests.
// El LifeFSM NO toca stats directamente — lee Stats const-ref para decidir.
//
// Payload de LifeStateChanged:
//   bits [15..8] = LifeStage (nueva etapa)
//   bits  [7..0] = AdultType (relevante solo en transición a Adult)
//
// Umbrales en minutos subjetivos. En TURBO_MODE, el ageAccMs_ acumula más
// rápido porque tick() recibe dtMs * LIFE_FSM_TIME_SCALE (ver config.h).
// =============================================================================
#include <stdint.h>
#include "Persistence.h"   // LifeStage enum

class EventBus;
struct Stats;

// AdultType — tipo de adulto según attentionScore acumulada durante etapa Kid.
// Persiste en PersistedState.adultType.
enum AdultType : uint8_t {
    AdultNormal   = 0,
    AdultAthletic = 1,
    AdultObese    = 2,
    AdultGrumpy   = 3
};

class LifeFSM {
public:
    // Umbrales de transición en minutos subjetivos de ageMinutes_.
    static constexpr uint32_t kEggHatchMin    = 5;      // 5 min → Baby
    static constexpr uint32_t kBabyToKidMin   = 60;     // 60 min → Kid (con attentionScore >= 300)
    static constexpr uint32_t kKidToAdultMin  = 240;    // 4h → Adult
    static constexpr uint32_t kAdultToOldMin  = 720;    // 12h → Old
    static constexpr uint32_t kOldToDeadMin   = 1440;   // 24h → Dead (vejez)

    // Umbrales wall-clock (milisegundos) — NO escalan con TURBO_MODE.
    static constexpr uint32_t kDeathSustainMs  = 60000;   // 60s con health==0 → muerte
    static constexpr uint32_t kResurrectionMs  = 300000;  // 5min en Dead → nuevo Egg

    explicit LifeFSM(EventBus& bus);

    // --- Setters para carga desde Persistence al boot ---
    void setStage(LifeStage stage);
    void setAgeMinutes(uint32_t minutes);
    void setDeathSustainMs(uint16_t ms);

    // --- Getters ---
    LifeStage stage()          const { return stage_; }
    uint32_t  ageMinutes()     const { return ageMinutes_; }
    uint16_t  deathSustainMs() const { return deathSustainMs_; }
    uint8_t   lastDeathCause() const { return lastDeathCause_; }
    AdultType adultType()      const { return adultType_; }

    // Llamado desde el main loop tras cada StatsEngine::tick().
    // dtMs: delta de tiempo real en milisegundos.
    // stats: estado actual de StatsEngine (const-ref, no modifica).
    // attentionScore: campo de stats pasado por separado para claridad de firma.
    void tick(uint32_t dtMs, const Stats& stats, uint16_t attentionScore);

private:
    EventBus* bus_;
    LifeStage stage_          = LifeStage::Egg;
    uint32_t  ageMinutes_     = 0;
    uint32_t  ageAccMs_       = 0;       // acumulador fraccional de ms → minutos
    uint16_t  deathSustainMs_ = 0;       // ms acumulados con health==0
    uint32_t  deadElapsedMs_  = 0;       // ms transcurridos en estado Dead
    uint8_t   lastDeathCause_ = 0;       // 0=none, 1=hunger, 2=sickness, 3=oldAge, 4=neglect, 5=unknown
    AdultType adultType_      = AdultNormal;

    // --- Helpers privados ---

    // Punto único de transición. Actualiza stage_, resetea contadores,
    // persiste lastDeathCause_ y publica LifeStateChanged.
    // deathCause solo se guarda cuando newStage == Dead.
    void transitionTo_(LifeStage newStage, uint8_t deathCause = 0);

    // Establece adultType_ y llama a transitionTo_(Adult).
    void transitionToAdult_(AdultType type);

    // Evalúa transiciones por tiempo/atención según stage_ actual.
    void evaluateTransitions_(const Stats& stats, uint16_t attentionScore);

    // Determina AdultType en función del attentionScore al evolucionar Kid → Adult.
    AdultType computeAdultType_(uint16_t attentionScore) const;

    // Infiere causa de muerte a partir del estado de stats cuando health cae a 0.
    // 1=hunger, 2=sickness, 4=neglect, 5=unknown.
    uint8_t inferDeathCause_(const Stats& s) const;
};
