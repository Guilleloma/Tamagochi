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
//
// Tope de crecimiento ("pausa de crecimiento", ver setGrowthCap()): etapa
// máxima alcanzable por edad. Sin tope por defecto → comportamiento original.
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

    // Sin tope de crecimiento. Dead como centinela: es la etapa final, así que
    // "se puede llegar hasta Dead por edad" equivale a no tener tope.
    static constexpr LifeStage kNoGrowthCap = LifeStage::Dead;

    explicit LifeFSM(EventBus& bus);

    // --- Setters crudos (tests / estado ya validado) ---
    // No aplican la degradación por tope: para cargar desde Persistence al
    // boot usar restoreLifeState().
    void setStage(LifeStage stage);
    void setAgeMinutes(uint32_t minutes);
    void setDeathSustainMs(uint16_t ms);

    // --- Tope de crecimiento ---
    // Con tope (cap < Dead):
    //   - no hay transición por edad hacia una etapa > cap (tampoco Old→Dead
    //     por vejez si cap <= Old);
    //   - ageMinutes se congela en (edad de entrada a la etapa cap+1) - 1, p.ej.
    //     cap=Baby → kBabyToKidMin - 1. Así, al quitar el tope, el pet sigue
    //     desde su etapa sin saltarse ninguna;
    //   - muerte por salud y resurrección como Egg no cambian.
    // Llamar ANTES de restoreLifeState() en el boot: no toca el estado actual.
    // Valores fuera de rango (> Dead) se tratan como kNoGrowthCap.
    void      setGrowthCap(LifeStage cap);
    LifeStage growthCap()    const { return growthCap_; }
    bool      hasGrowthCap() const { return growthCap_ != kNoGrowthCap; }

    // Carga etapa + edad desde Persistence (boot) aplicando el tope de forma
    // atómica. Si la etapa guardada supera el tope y no es Dead, la degrada al
    // tope con la edad de entrada a esa etapa (Baby → kEggHatchMin) y resetea
    // adultType (solo tiene sentido en Adult; se recalcula al volver a crecer).
    // Si no degrada pero la edad pasa del máximo bajo el tope, la clampa.
    // Los stats viven en StatsEngine: no se tocan.
    // No publica LifeStateChanged (igual que los setters de boot).
    // Devuelve true si degradó la etapa → el caller debe persistir el estado.
    bool restoreLifeState(LifeStage stage, uint32_t ageMinutes);

    // Edad (minutos subjetivos) a la que se entra en una etapa por edad:
    // Egg=0, Baby=kEggHatchMin, Kid=kBabyToKidMin, Adult=kKidToAdultMin,
    // Old=kAdultToOldMin, Dead=kOldToDeadMin (muerte por vejez).
    static uint32_t stageEntryAgeMin(LifeStage stage);

    // Escala de tiempo de la edad (dtMs × scale). Por defecto
    // LIFE_FSM_TIME_SCALE (1, o el factor turbo en builds TURBO_MODE).
    // Expuesto para poder testear el modo turbo en env:native. 0 se trata como 1.
    void setTimeScale(uint32_t scale);

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
    LifeStage growthCap_      = kNoGrowthCap;
    uint32_t  timeScale_;                // inicializado en el ctor (LIFE_FSM_TIME_SCALE)

    // --- Helpers privados ---

    // Edad máxima permitida bajo el tope actual. Solo válido con hasGrowthCap().
    uint32_t maxAgeUnderGrowthCap_() const;

    // Congela ageMinutes_ en maxAgeUnderGrowthCap_() si lo alcanza. No-op sin tope.
    void clampAgeToGrowthCap_();

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
