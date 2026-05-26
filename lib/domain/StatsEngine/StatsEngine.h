#pragma once
#include <stdint.h>

// =============================================================================
// StatsEngine — lógica pura de stats + decay. Sin Arduino.h.
// Compilable en host-native para tests.
//
// Semántica de stats:
//   hunger    0=saciado, 100=hambriento  → el decay SUBE hunger
//   happiness 0=triste,  100=feliz       → el decay BAJA happiness
//   health    0=muerto,  100=sano        → el decay BAJA health
//             (health solo decae si hunger>=90 o happiness<=10)
// =============================================================================

struct Stats {
    uint8_t hunger     = 50;   // 0 = saciado, 100 = hambriento
    uint8_t happiness  = 50;   // 0 = triste,  100 = feliz
    uint8_t health     = 100;  // 0 = muerto,  100 = sano
};

struct DecayRates {
    // Puntos por segundo. Aplicados vía acumulador fraccional (float) para no
    // perder precisión a tasas bajas como 0.03 pts/s.
    float hungerPerSec    = 0.05f;   // hunger sube ~1 pt cada 20 s
    float happinessPerSec = 0.03f;   // happiness baja ~1 pt cada 33 s
    float healthPerSec    = 0.01f;   // health baja ~1 pt cada 100 s (condicional)
};

class StatsEngine {
public:
    StatsEngine();
    explicit StatsEngine(const DecayRates& rates);

    // Avanza dt milisegundos de simulación. dt=0 es no-op.
    // dt se clampa a 24h (86400000 ms) para evitar overflow al arrancar tras meses offline.
    void tick(uint32_t dtMs);

    void applyFeed(uint8_t amount);   // reduce hunger en amount (sacia). Clamp a 0.
    void applyPlay(uint8_t amount);   // sube happiness en amount. Clamp a 100.
    void applyHeal(uint8_t amount);   // sube health en amount. Clamp a 100.

    const Stats& stats() const { return stats_; }
    void setStats(const Stats& s) { stats_ = s; }   // para carga desde Persistence y tests

    // Devuelve true si algún stat entero cambió desde la última llamada.
    // Limpia el flag internamente — consumo único por ciclo.
    bool consumeDirtyFlag();

private:
    Stats      stats_;
    DecayRates rates_;

    // Acumuladores fraccionales: evitan perder fracciones de punto entre ticks.
    // Cuando acc >= 1.0, se consume 1.0 y se aplica 1 punto de cambio al stat.
    float hungerAcc_    = 0.0f;
    float happinessAcc_ = 0.0f;
    float healthAcc_    = 0.0f;

    bool dirty_ = false;

    // Clamp seguro a [0, 100] desde int (evita wrap-around de uint8_t).
    static uint8_t clamp_(int value);
};
