#pragma once
#include <stdint.h>

// =============================================================================
// StatsEngine — lógica pura de stats + decay. Sin Arduino.h.
// Compilable en host-native para tests.
//
// Semántica de stats:
//   hunger     0=saciado,      100=hambriento  → el decay SUBE hunger
//   happiness  0=triste,       100=feliz        → el decay BAJA happiness
//   health     0=muerto,       100=sano         → el decay BAJA health
//              (health solo decae cuando se cumplen condiciones de peligro)
//   discipline 0=indisciplinado, 100=obediente  → el decay BAJA discipline
//   weight     0=anoréxico,    50=sano, 100=obeso → metabolismo BAJA weight
//   poopCount  0-9 cacas visibles acumuladas
//   isSick     flag de enfermedad
//   isSleeping flag de sueño — paraliza hunger/happiness/discipline decay
//   attentionScore 0-1000, métrica acumulada que dirige la evolución
// =============================================================================

// Número máximo de cacas visibles simultáneas
static constexpr uint8_t kMaxPoopCount = 9;

struct Stats {
    // Fase 1
    uint8_t hunger     = 50;   // 0 = saciado, 100 = hambriento
    uint8_t happiness  = 50;   // 0 = triste,  100 = feliz
    uint8_t health     = 100;  // 0 = muerto,  100 = sano

    // Fase 2
    uint8_t  discipline     = 50;    // 0 = indisciplinado, 100 = obediente
    uint8_t  weight         = 50;    // 0 = anoréxico, 50 = sano, 100 = obeso
    uint8_t  poopCount      = 0;     // # cacas visibles, 0-9
    bool     isSick         = false;
    bool     isSleeping     = false;
    uint16_t attentionScore = 500;   // 0-1000
};

struct DecayRates {
    // Puntos por segundo. Aplicados vía acumulador fraccional (float) para no
    // perder precisión a tasas bajas como 0.03 pts/s.

    // Fase 1
    float hungerPerSec    = 0.05f;   // hunger sube ~1 pt cada 20 s
    float happinessPerSec = 0.03f;   // happiness baja ~1 pt cada 33 s
    float healthPerSec    = 0.01f;   // health baja ~1 pt cada 100 s (condicional)

    // Fase 2
    float weightDeltaPerFeed     = 1.0f;    // cada feed añade +1 peso (si amount>10)
    float weightDecayPerSec      = 0.005f;  // peso baja lentamente por metabolismo
    float disciplinePerSec       = 0.01f;   // disciplina baja sin acciones
    float poopSpawnPerHungerEat  = 0.30f;   // 30% prob de caca al alimentar
    float sicknessProbPerPoop    = 0.005f;  // 0.5% por segundo y por caca presente
    float healthDecayWhenSickMul = 5.0f;    // si sick, health decay × 5
};

class StatsEngine {
public:
    StatsEngine();
    explicit StatsEngine(const DecayRates& rates);

    // Avanza dt milisegundos de simulación. dt=0 es no-op.
    // dt se clampa a 24h (86400000 ms) para evitar overflow al arrancar tras meses offline.
    void tick(uint32_t dtMs);

    // --- Acciones Fase 1 ---
    void applyFeed(uint8_t amount);   // reduce hunger (sacia). Clamp a 0. Genera peso y caca.
    void applyPlay(uint8_t amount);   // sube happiness en amount. Clamp a 100.
    void applyHeal(uint8_t amount);   // sube health en amount. Clamp a 100.

    // --- Acciones Fase 2 ---
    void applyClean();                  // resetea poopCount a 0
    void applyMedicate();               // cura isSick, health +20
    void applyDiscipline(uint8_t amt);  // discipline += amt; happiness -= 5 (coste social)
    void applySleep(bool on);           // setea isSleeping flag — paraliza decay

    // Pasivo: resetea todos los stats a defaults v2 (para nuevo ciclo de vida)
    void resetForNewLife();

    const Stats& stats() const { return stats_; }
    void setStats(const Stats& s) { stats_ = s; }   // para carga desde Persistence y tests

    // Devuelve true si algún stat entero cambió desde la última llamada.
    // Limpia el flag internamente — consumo único por ciclo.
    bool consumeDirtyFlag();

    // --- Inyección de aleatoriedad para tests deterministas ---
    // Default: rand() / RAND_MAX. Tests pueden sustituir con función controlada.
    static void setRandomSource(float (*src)());

private:
    Stats      stats_;
    DecayRates rates_;

    // Acumuladores fraccionales: evitan perder fracciones de punto entre ticks.
    float hungerAcc_     = 0.0f;
    float happinessAcc_  = 0.0f;
    float healthAcc_     = 0.0f;
    float weightAcc_     = 0.0f;     // acumula pérdida de peso por metabolismo
    float disciplineAcc_ = 0.0f;     // acumula pérdida de disciplina
    float attentionAcc_  = 0.0f;     // acumula tiempo para actualizar attentionScore (ms)

    bool dirty_ = false;

    // Función de aleatoriedad inyectable — puntero a función estático
    static float (*sRand01_)();

    // Clamp seguro a [0, 100] desde int (evita wrap-around de uint8_t).
    static uint8_t clamp_(int value);

    // Invocada desde applyFeed para intentar spawn de caca probabilísticamente
    void onFeedTriggered_();

    // Actualiza attentionScore cada 10s según condiciones del tamagochi
    void updateAttentionScore_(uint32_t dtMs);
};
