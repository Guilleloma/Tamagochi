// =============================================================================
// test_life_fsm.cpp — tests para LifeFSM.
//
// Framework: Unity (pio test -e native).
// LifeFSM es lógica pura: no necesita mocks de hardware.
//
// Convenciones:
//   g_bus, g_fsm — instancias recreadas en setUp/tearDown.
//   advanceMinutes() — helper que simula N minutos con ticks de 60 000 ms.
//   g_lifeStateChangedCount — contador de eventos publicados.
//   g_lastLifeStatePayload — último payload capturado.
//
// Hito 3 (Fase 2): 16 tests para las 6 etapas de vida y transiciones.
// Pausa de crecimiento: tests 17+ para el tope de crecimiento (growth cap).
// =============================================================================

#include <unity.h>
#include "EventBus.h"
#include "LifeFSM.h"
#include "StatsEngine.h"   // struct Stats

// --- Estado global de test ---
static EventBus* g_bus = nullptr;
static LifeFSM*  g_fsm = nullptr;

static int     g_lifeStateChangedCount  = 0;
static int32_t g_lastLifeStatePayload   = 0;

static void onLifeStateChanged(const Event& e) {
    g_lifeStateChangedCount++;
    g_lastLifeStatePayload = e.payload;
}

void setUp() {
    g_bus = new EventBus();
    g_fsm = new LifeFSM(*g_bus);
    g_bus->subscribe(EventType::LifeStateChanged, onLifeStateChanged);
    g_lifeStateChangedCount = 0;
    g_lastLifeStatePayload  = 0;
}

void tearDown() {
    delete g_fsm; g_fsm = nullptr;
    delete g_bus; g_bus = nullptr;
}

// Helper: construye Stats sano por defecto (health=100, hunger=0, no sick)
static Stats healthyStats() {
    Stats s;
    s.health    = 100;
    s.hunger    = 0;
    s.happiness = 80;
    s.isSick    = false;
    return s;
}

// Helper: avanza N minutos completos de simulación (1 tick = 1 minuto = 60 000 ms)
static void advanceMinutes(LifeFSM& fsm, uint32_t minutes, const Stats& s, uint16_t attn) {
    for (uint32_t i = 0; i < minutes; i++) {
        fsm.tick(60000u, s, attn);
    }
}

// =============================================================================
// 1. test_initial_stage_is_egg
// Al construir LifeFSM, la etapa inicial es Egg.
// =============================================================================
void test_initial_stage_is_egg() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Egg),
                            static_cast<uint8_t>(g_fsm->stage()));
}

// =============================================================================
// 2. test_egg_hatches_to_baby_after_threshold
// Después de kEggHatchMin minutos, el Egg transiciona a Baby.
// =============================================================================
void test_egg_hatches_to_baby_after_threshold() {
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, LifeFSM::kEggHatchMin, s, 500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby),
                            static_cast<uint8_t>(g_fsm->stage()));
}

// =============================================================================
// 3. test_egg_does_not_hatch_below_threshold
// Con menos de kEggHatchMin minutos, el Egg no transiciona.
// =============================================================================
void test_egg_does_not_hatch_below_threshold() {
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, LifeFSM::kEggHatchMin - 1u, s, 500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Egg),
                            static_cast<uint8_t>(g_fsm->stage()));
}

// =============================================================================
// 4. test_baby_to_kid_blocked_by_low_attention
// Baby no pasa a Kid si attentionScore < 300 aunque se alcance kBabyToKidMin.
// (Sí pasará al 150% del umbral por la regla de forzado.)
// =============================================================================
void test_baby_to_kid_blocked_by_low_attention() {
    g_fsm->setStage(LifeStage::Baby);
    Stats s = healthyStats();
    // avanzar exactamente kBabyToKidMin con attentionScore < 300
    advanceMinutes(*g_fsm, LifeFSM::kBabyToKidMin, s, 200);
    // Debe seguir en Baby — todavía no alcanza el umbral forzado
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby),
                            static_cast<uint8_t>(g_fsm->stage()));
}

// =============================================================================
// 5. test_baby_to_kid_forced_after_extended_time
// Al llegar al 150% de kBabyToKidMin, se fuerza la transición a Kid
// aunque attentionScore < 300.
// =============================================================================
void test_baby_to_kid_forced_after_extended_time() {
    g_fsm->setStage(LifeStage::Baby);
    Stats s = healthyStats();
    // umbral forzado = kBabyToKidMin * 3 / 2
    uint32_t forcedMin = LifeFSM::kBabyToKidMin * 3u / 2u;
    advanceMinutes(*g_fsm, forcedMin, s, 100);   // attentionScore muy bajo
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Kid),
                            static_cast<uint8_t>(g_fsm->stage()));
}

// =============================================================================
// 6. test_kid_to_adult_at_threshold
// Al alcanzar kKidToAdultMin minutos en Kid, transiciona a Adult.
// =============================================================================
void test_kid_to_adult_at_threshold() {
    g_fsm->setStage(LifeStage::Kid);
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, LifeFSM::kKidToAdultMin, s, 500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Adult),
                            static_cast<uint8_t>(g_fsm->stage()));
}

// =============================================================================
// 7. test_kid_to_adult_picks_athletic_with_high_attention
// Con attentionScore >= 800, el adulto resultante es AdultAthletic.
// =============================================================================
void test_kid_to_adult_picks_athletic_with_high_attention() {
    g_fsm->setStage(LifeStage::Kid);
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, LifeFSM::kKidToAdultMin, s, 900);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Adult),
                            static_cast<uint8_t>(g_fsm->stage()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdultAthletic),
                            static_cast<uint8_t>(g_fsm->adultType()));
}

// =============================================================================
// 8. test_kid_to_adult_picks_obese_with_low_attention
// Con attentionScore < 200, el adulto resultante es AdultObese.
// =============================================================================
void test_kid_to_adult_picks_obese_with_low_attention() {
    g_fsm->setStage(LifeStage::Kid);
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, LifeFSM::kKidToAdultMin, s, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Adult),
                            static_cast<uint8_t>(g_fsm->stage()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdultObese),
                            static_cast<uint8_t>(g_fsm->adultType()));
}

// =============================================================================
// 9. test_adult_to_old_at_threshold
// Al alcanzar kAdultToOldMin minutos en Adult, transiciona a Old.
// =============================================================================
void test_adult_to_old_at_threshold() {
    g_fsm->setStage(LifeStage::Adult);
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, LifeFSM::kAdultToOldMin, s, 500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Old),
                            static_cast<uint8_t>(g_fsm->stage()));
}

// =============================================================================
// 10. test_old_to_dead_at_threshold_with_cause_old_age
// Al alcanzar kOldToDeadMin minutos en Old, transiciona a Dead con cause=3 (oldAge).
// =============================================================================
void test_old_to_dead_at_threshold_with_cause_old_age() {
    g_fsm->setStage(LifeStage::Old);
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, LifeFSM::kOldToDeadMin, s, 500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Dead),
                            static_cast<uint8_t>(g_fsm->stage()));
    TEST_ASSERT_EQUAL_UINT8(3, g_fsm->lastDeathCause());   // oldAge
}

// =============================================================================
// 11. test_any_to_dead_when_health_zero_sustained
// health==0 sostenido durante >= kDeathSustainMs provoca transición a Dead.
// Se usa Baby (no Egg — en Egg el sustain timer no opera).
// =============================================================================
void test_any_to_dead_when_health_zero_sustained() {
    g_fsm->setStage(LifeStage::Baby);
    Stats s   = healthyStats();
    s.health  = 0;
    s.hunger  = 95;   // causa inferida: hunger (1)

    // Acumular kDeathSustainMs en varios ticks de 10 000 ms
    uint32_t accMs = 0;
    while (accMs < LifeFSM::kDeathSustainMs) {
        g_fsm->tick(10000u, s, 500);
        accMs += 10000u;
    }
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Dead),
                            static_cast<uint8_t>(g_fsm->stage()));
    TEST_ASSERT_EQUAL_UINT8(1, g_fsm->lastDeathCause());   // hunger
}

// =============================================================================
// 12. test_health_zero_short_does_not_kill
// health==0 durante menos de kDeathSustainMs NO provoca muerte.
// =============================================================================
void test_health_zero_short_does_not_kill() {
    g_fsm->setStage(LifeStage::Baby);
    Stats s  = healthyStats();
    s.health = 0;

    // Acumular solo kDeathSustainMs - 1 ms (un tick de 59 999 ms, dentro del umbral)
    // Usamos ticks de 10 000 ms y paramos antes de superar el umbral.
    uint32_t accumulated = 0;
    while (accumulated + 10000u < LifeFSM::kDeathSustainMs) {
        g_fsm->tick(10000u, s, 500);
        accumulated += 10000u;
    }
    // El tamagochi sigue vivo
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(LifeStage::Dead),
                          static_cast<uint8_t>(g_fsm->stage()));
}

// =============================================================================
// 13. test_dead_to_egg_after_resurrection_delay
// Tras kResurrectionMs en Dead, transiciona automáticamente a Egg.
// =============================================================================
void test_dead_to_egg_after_resurrection_delay() {
    g_fsm->setStage(LifeStage::Dead);
    Stats s = healthyStats();

    // Avanzar kResurrectionMs en ticks de 10 000 ms
    uint32_t accMs = 0;
    while (accMs < LifeFSM::kResurrectionMs) {
        g_fsm->tick(10000u, s, 500);
        accMs += 10000u;
    }
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Egg),
                            static_cast<uint8_t>(g_fsm->stage()));
}

// =============================================================================
// 14. test_dead_state_pauses_age_counter
// Mientras está en Dead, ageMinutes_ no avanza (el tiempo de Dead no cuenta como edad).
// Se usan ticks cortos que no superan kResurrectionMs para no triggear la
// transición automática Dead → Egg (que resetea ageMinutes a 0).
// =============================================================================
void test_dead_state_pauses_age_counter() {
    g_fsm->setStage(LifeStage::Dead);
    g_fsm->setAgeMinutes(100);
    Stats s = healthyStats();

    // Avanzar solo 2 minutos (120 000 ms < kResurrectionMs=300 000 ms).
    // No se alcanza la resurrección, por lo que ageMinutes_ no debe cambiar.
    advanceMinutes(*g_fsm, 2u, s, 500);

    TEST_ASSERT_EQUAL_UINT32(100u, g_fsm->ageMinutes());
}

// =============================================================================
// 15. test_publishes_life_state_changed_event_on_transition
// Cada transición debe publicar exactamente 1 evento LifeStateChanged.
// El payload debe codificar la nueva etapa en el byte alto.
// =============================================================================
void test_publishes_life_state_changed_event_on_transition() {
    Stats s = healthyStats();

    // Verificar que Egg → Baby publica evento
    int prevCount = g_lifeStateChangedCount;
    advanceMinutes(*g_fsm, LifeFSM::kEggHatchMin, s, 500);
    TEST_ASSERT_EQUAL_INT(prevCount + 1, g_lifeStateChangedCount);

    // El byte alto del payload debe ser LifeStage::Baby (1)
    uint8_t payloadStage = static_cast<uint8_t>((g_lastLifeStatePayload >> 8) & 0xFF);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), payloadStage);
}

// =============================================================================
// 16. test_death_cause_inferred_from_stats
// inferDeathCause_ prioriza: isSick > hunger >= 90 > happiness <= 10 > unknown.
// Se verifica de forma indirecta: provocar cada causa y comprobar lastDeathCause_.
// =============================================================================
void test_death_cause_inferred_from_stats() {
    // Caso: muerte por sickness (isSick=true tiene máxima prioridad)
    {
        LifeFSM fsm(*g_bus);
        fsm.setStage(LifeStage::Baby);
        Stats s   = healthyStats();
        s.health  = 0;
        s.isSick  = true;
        s.hunger  = 50;   // hunger no es causa — isSick gana

        uint32_t accMs = 0;
        while (accMs < LifeFSM::kDeathSustainMs) {
            fsm.tick(10000u, s, 500);
            accMs += 10000u;
        }
        TEST_ASSERT_EQUAL_UINT8(2, fsm.lastDeathCause());   // sickness
    }

    // Caso: muerte por neglect (happiness <= 10, sin sick ni hunger extremo)
    {
        LifeFSM fsm(*g_bus);
        fsm.setStage(LifeStage::Baby);
        Stats s      = healthyStats();
        s.health     = 0;
        s.isSick     = false;
        s.hunger     = 50;
        s.happiness  = 5;   // neglect

        uint32_t accMs = 0;
        while (accMs < LifeFSM::kDeathSustainMs) {
            fsm.tick(10000u, s, 500);
            accMs += 10000u;
        }
        TEST_ASSERT_EQUAL_UINT8(4, fsm.lastDeathCause());   // neglect
    }
}

// =============================================================================
// Tope de crecimiento (growth cap) — pausa de crecimiento en Baby.
// =============================================================================

// Helper: acumula health==0 durante kDeathSustainMs en ticks de 10 s.
static void sustainZeroHealth(LifeFSM& fsm, const Stats& s) {
    uint32_t accMs = 0;
    while (accMs < LifeFSM::kDeathSustainMs) {
        fsm.tick(10000u, s, 500);
        accMs += 10000u;
    }
}

// Helper: avanza kResurrectionMs en ticks de 10 s (rama Dead).
static void waitForResurrection(LifeFSM& fsm, const Stats& s) {
    uint32_t accMs = 0;
    while (accMs < LifeFSM::kResurrectionMs) {
        fsm.tick(10000u, s, 500);
        accMs += 10000u;
    }
}

static uint8_t stageOf(const LifeFSM& fsm) {
    return static_cast<uint8_t>(fsm.stage());
}

static const uint32_t kBabyMaxAgeUnderCap = LifeFSM::kBabyToKidMin - 1u;

// 17. Por defecto no hay tope: el comportamiento original no cambia.
void test_growth_cap_is_disabled_by_default() {
    TEST_ASSERT_FALSE(g_fsm->hasGrowthCap());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeFSM::kNoGrowthCap),
                            static_cast<uint8_t>(g_fsm->growthCap()));
}

// 18. Un valor de tope fuera de rango se trata como "sin tope".
void test_growth_cap_out_of_range_value_means_no_cap() {
    g_fsm->setGrowthCap(static_cast<LifeStage>(9));
    TEST_ASSERT_FALSE(g_fsm->hasGrowthCap());
}

// 19. Con tope Baby, el Egg eclosiona a Baby con normalidad (y publica el evento).
void test_growth_cap_baby_should_still_hatch_egg_into_baby() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, LifeFSM::kEggHatchMin, s, 500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_INT(1, g_lifeStateChangedCount);
}

// 20. Con tope Baby, no pasa a Kid aunque attentionScore >= 300 y pasen 60 min.
void test_growth_cap_baby_should_block_kid_with_high_attention_after_60_min() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    g_fsm->setStage(LifeStage::Baby);
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, LifeFSM::kBabyToKidMin, s, 1000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_INT(0, g_lifeStateChangedCount);
}

// 21. Con tope Baby, tampoco pasa a Kid por la transición forzada de los 90 min.
void test_growth_cap_baby_should_block_forced_kid_after_90_min() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    g_fsm->setStage(LifeStage::Baby);
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, LifeFSM::kBabyToKidMin * 3u / 2u + 10u, s, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_INT(0, g_lifeStateChangedCount);
}

// 22. La edad se congela justo antes de kBabyToKidMin y no sigue creciendo.
void test_growth_cap_baby_should_clamp_age_below_baby_to_kid_threshold() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, 200u, s, 500);
    TEST_ASSERT_EQUAL_UINT32(kBabyMaxAgeUnderCap, g_fsm->ageMinutes());

    advanceMinutes(*g_fsm, 500u, s, 500);
    TEST_ASSERT_EQUAL_UINT32(kBabyMaxAgeUnderCap, g_fsm->ageMinutes());
}

// 23. Con tope Baby no hay muerte por vejez: nunca llega a Old.
void test_growth_cap_baby_should_prevent_old_age_death() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, LifeFSM::kOldToDeadMin + 100u, s, 900);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT8(0, g_fsm->lastDeathCause());
    TEST_ASSERT_EQUAL_INT(1, g_lifeStateChangedCount);   // solo la eclosión
}

// 24. Con tope Baby, la muerte por salud sigue funcionando igual.
void test_growth_cap_baby_should_still_die_from_sustained_zero_health() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    g_fsm->setStage(LifeStage::Baby);
    Stats s  = healthyStats();
    s.health = 0;
    s.isSick = true;
    sustainZeroHealth(*g_fsm, s);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Dead), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT8(2, g_fsm->lastDeathCause());   // sickness
}

// 25. Con tope Baby, el ciclo muerte → Egg → Baby funciona y vuelve a quedarse en Baby.
void test_growth_cap_baby_should_resurrect_as_egg_and_stop_again_at_baby() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    g_fsm->setStage(LifeStage::Baby);
    Stats dying  = healthyStats();
    dying.health = 0;
    sustainZeroHealth(*g_fsm, dying);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Dead), stageOf(*g_fsm));

    Stats s = healthyStats();
    waitForResurrection(*g_fsm, s);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Egg), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT32(0u, g_fsm->ageMinutes());

    advanceMinutes(*g_fsm, 300u, s, 900);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT32(kBabyMaxAgeUnderCap, g_fsm->ageMinutes());
}

// 26. Al quitar el tope, sigue creciendo desde Baby sin saltarse etapas.
void test_growth_cap_removed_should_resume_from_baby_one_stage_at_a_time() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, 1000u, s, 500);
    TEST_ASSERT_EQUAL_UINT32(kBabyMaxAgeUnderCap, g_fsm->ageMinutes());

    g_fsm->setGrowthCap(LifeFSM::kNoGrowthCap);
    advanceMinutes(*g_fsm, 1u, s, 500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Kid), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT32(LifeFSM::kBabyToKidMin, g_fsm->ageMinutes());
}

// 27. Cargar un Adult con tope Baby lo degrada a Baby con age=kEggHatchMin.
void test_restore_adult_with_growth_cap_baby_should_degrade_to_baby_at_hatch_age() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    bool degraded = g_fsm->restoreLifeState(LifeStage::Adult, 300u);
    TEST_ASSERT_TRUE(degraded);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT32(LifeFSM::kEggHatchMin, g_fsm->ageMinutes());
    // Igual que los setters de boot: no publica LifeStateChanged.
    TEST_ASSERT_EQUAL_INT(0, g_lifeStateChangedCount);
}

// 28. Cargar un Kid con tope Baby lo degrada a Baby con age=kEggHatchMin.
void test_restore_kid_with_growth_cap_baby_should_degrade_to_baby_at_hatch_age() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    TEST_ASSERT_TRUE(g_fsm->restoreLifeState(LifeStage::Kid, 120u));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT32(LifeFSM::kEggHatchMin, g_fsm->ageMinutes());
}

// 29. Cargar un Old con tope Baby lo degrada a Baby con age=kEggHatchMin.
void test_restore_old_with_growth_cap_baby_should_degrade_to_baby_at_hatch_age() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    TEST_ASSERT_TRUE(g_fsm->restoreLifeState(LifeStage::Old, 1000u));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT32(LifeFSM::kEggHatchMin, g_fsm->ageMinutes());
}

// 30. El Baby degradado se comporta como un Baby normal bajo el tope.
void test_restore_degraded_baby_should_stay_baby_after_ticking() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    g_fsm->restoreLifeState(LifeStage::Adult, 300u);
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, 200u, s, 900);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT32(kBabyMaxAgeUnderCap, g_fsm->ageMinutes());
    TEST_ASSERT_EQUAL_INT(0, g_lifeStateChangedCount);
}

// 31. Degradar desde Adult resetea adultType (solo tiene sentido en Adult).
void test_restore_adult_with_growth_cap_baby_should_reset_adult_type() {
    g_fsm->setStage(LifeStage::Kid);
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, LifeFSM::kKidToAdultMin, s, 900);   // → AdultAthletic
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdultAthletic),
                            static_cast<uint8_t>(g_fsm->adultType()));

    g_fsm->setGrowthCap(LifeStage::Baby);
    g_fsm->restoreLifeState(LifeStage::Adult, 300u);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdultNormal),
                            static_cast<uint8_t>(g_fsm->adultType()));
}

// 32. Cargar un Dead con tope no lo resucita ni lo cambia.
void test_restore_dead_with_growth_cap_baby_should_keep_dead_unchanged() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    TEST_ASSERT_FALSE(g_fsm->restoreLifeState(LifeStage::Dead, 800u));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Dead), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT32(800u, g_fsm->ageMinutes());
}

// 33. Cargar un Baby con edad ya por encima del máximo bajo el tope: no es
// degradación, pero la edad se clampa para que no pueda pasar a Kid.
void test_restore_baby_past_max_age_with_growth_cap_should_clamp_age_without_degrading() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    TEST_ASSERT_FALSE(g_fsm->restoreLifeState(LifeStage::Baby, 75u));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT32(kBabyMaxAgeUnderCap, g_fsm->ageMinutes());

    Stats s = healthyStats();
    advanceMinutes(*g_fsm, 1u, s, 1000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));
}

// 34. Cargar una etapa por debajo del tope no la cambia.
void test_restore_egg_with_growth_cap_baby_should_keep_egg() {
    g_fsm->setGrowthCap(LifeStage::Baby);
    TEST_ASSERT_FALSE(g_fsm->restoreLifeState(LifeStage::Egg, 2u));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Egg), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT32(2u, g_fsm->ageMinutes());
}

// 35. Regresión: sin tope, restoreLifeState carga etapa y edad tal cual.
void test_restore_without_growth_cap_should_keep_stage_and_age() {
    TEST_ASSERT_FALSE(g_fsm->restoreLifeState(LifeStage::Adult, 300u));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Adult), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT32(300u, g_fsm->ageMinutes());
}

// 36. Regresión: sin tope, el ciclo completo Egg → Dead mantiene los umbrales.
void test_without_growth_cap_full_lifecycle_should_match_original_thresholds() {
    Stats s = healthyStats();
    advanceMinutes(*g_fsm, LifeFSM::kEggHatchMin, s, 500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));

    advanceMinutes(*g_fsm, LifeFSM::kBabyToKidMin - LifeFSM::kEggHatchMin, s, 500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Kid), stageOf(*g_fsm));

    advanceMinutes(*g_fsm, LifeFSM::kKidToAdultMin - LifeFSM::kBabyToKidMin, s, 500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Adult), stageOf(*g_fsm));

    advanceMinutes(*g_fsm, LifeFSM::kAdultToOldMin - LifeFSM::kKidToAdultMin, s, 500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Old), stageOf(*g_fsm));

    advanceMinutes(*g_fsm, LifeFSM::kOldToDeadMin - LifeFSM::kAdultToOldMin, s, 500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Dead), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT8(3, g_fsm->lastDeathCause());   // oldAge
    TEST_ASSERT_EQUAL_INT(5, g_lifeStateChangedCount);
}

// 37. TURBO_FACTOR=60 con tope Baby: ticks turbo de 1 s real (= 1 min
// subjetivo) durante 2 h reales eclosionan el huevo pero no pasan de Baby.
void test_growth_cap_baby_should_hold_with_turbo_factor_60() {
    g_fsm->setTimeScale(60u);
    g_fsm->setGrowthCap(LifeStage::Baby);
    Stats s = healthyStats();
    for (uint32_t i = 0; i < 2u * 3600u; i++) {
        g_fsm->tick(1000u, s, 900);
    }
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT32(kBabyMaxAgeUnderCap, g_fsm->ageMinutes());
    TEST_ASSERT_EQUAL_INT(1, g_lifeStateChangedCount);   // solo la eclosión
}

// 38. TURBO_FACTOR=60 con tope Baby: un único tick largo (10 min reales = 600
// min subjetivos, p.ej. un loop bloqueado) tampoco salta etapas.
void test_growth_cap_baby_should_hold_with_single_long_turbo_tick() {
    g_fsm->setTimeScale(60u);
    g_fsm->setGrowthCap(LifeStage::Baby);
    Stats s = healthyStats();
    g_fsm->tick(10u * 60000u, s, 900);   // Egg → Baby, edad clampada
    g_fsm->tick(10u * 60000u, s, 900);   // Baby se queda en Baby
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LifeStage::Baby), stageOf(*g_fsm));
    TEST_ASSERT_EQUAL_UINT32(kBabyMaxAgeUnderCap, g_fsm->ageMinutes());
}

// =============================================================================
// main — punto de entrada Unity
// =============================================================================
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_initial_stage_is_egg);
    RUN_TEST(test_egg_hatches_to_baby_after_threshold);
    RUN_TEST(test_egg_does_not_hatch_below_threshold);
    RUN_TEST(test_baby_to_kid_blocked_by_low_attention);
    RUN_TEST(test_baby_to_kid_forced_after_extended_time);
    RUN_TEST(test_kid_to_adult_at_threshold);
    RUN_TEST(test_kid_to_adult_picks_athletic_with_high_attention);
    RUN_TEST(test_kid_to_adult_picks_obese_with_low_attention);
    RUN_TEST(test_adult_to_old_at_threshold);
    RUN_TEST(test_old_to_dead_at_threshold_with_cause_old_age);
    RUN_TEST(test_any_to_dead_when_health_zero_sustained);
    RUN_TEST(test_health_zero_short_does_not_kill);
    RUN_TEST(test_dead_to_egg_after_resurrection_delay);
    RUN_TEST(test_dead_state_pauses_age_counter);
    RUN_TEST(test_publishes_life_state_changed_event_on_transition);
    RUN_TEST(test_death_cause_inferred_from_stats);

    // Tope de crecimiento (growth cap)
    RUN_TEST(test_growth_cap_is_disabled_by_default);
    RUN_TEST(test_growth_cap_out_of_range_value_means_no_cap);
    RUN_TEST(test_growth_cap_baby_should_still_hatch_egg_into_baby);
    RUN_TEST(test_growth_cap_baby_should_block_kid_with_high_attention_after_60_min);
    RUN_TEST(test_growth_cap_baby_should_block_forced_kid_after_90_min);
    RUN_TEST(test_growth_cap_baby_should_clamp_age_below_baby_to_kid_threshold);
    RUN_TEST(test_growth_cap_baby_should_prevent_old_age_death);
    RUN_TEST(test_growth_cap_baby_should_still_die_from_sustained_zero_health);
    RUN_TEST(test_growth_cap_baby_should_resurrect_as_egg_and_stop_again_at_baby);
    RUN_TEST(test_growth_cap_removed_should_resume_from_baby_one_stage_at_a_time);
    RUN_TEST(test_restore_adult_with_growth_cap_baby_should_degrade_to_baby_at_hatch_age);
    RUN_TEST(test_restore_kid_with_growth_cap_baby_should_degrade_to_baby_at_hatch_age);
    RUN_TEST(test_restore_old_with_growth_cap_baby_should_degrade_to_baby_at_hatch_age);
    RUN_TEST(test_restore_degraded_baby_should_stay_baby_after_ticking);
    RUN_TEST(test_restore_adult_with_growth_cap_baby_should_reset_adult_type);
    RUN_TEST(test_restore_dead_with_growth_cap_baby_should_keep_dead_unchanged);
    RUN_TEST(test_restore_baby_past_max_age_with_growth_cap_should_clamp_age_without_degrading);
    RUN_TEST(test_restore_egg_with_growth_cap_baby_should_keep_egg);
    RUN_TEST(test_restore_without_growth_cap_should_keep_stage_and_age);
    RUN_TEST(test_without_growth_cap_full_lifecycle_should_match_original_thresholds);
    RUN_TEST(test_growth_cap_baby_should_hold_with_turbo_factor_60);
    RUN_TEST(test_growth_cap_baby_should_hold_with_single_long_turbo_tick);

    return UNITY_END();
}
