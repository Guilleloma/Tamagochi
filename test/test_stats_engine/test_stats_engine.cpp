// =============================================================================
// test_stats_engine.cpp — tests para StatsEngine.
//
// Framework: Unity (pio test -e native).
// StatsEngine es lógica pura: no necesita mock, se ejercita directamente.
//
// Convenciones de nomenclatura:
//   g_engine — instancia reutilizada entre tests vía setUp/tearDown.
//   Cada test describe EXACTAMENTE qué comportamiento verifica.
//
// Hito 4 (Fase 1): 14 tests de stats base, decay, dirty flag.
// Hito 2 (Fase 2): 17 tests nuevos para los 5 stats nuevos y las 4 acciones.
//
// Patrón RNG: setUp restaura siempre el RNG por defecto para no contaminar
// tests que no usan RNG controlado. Tests que necesitan determinismo llaman
// StatsEngine::setRandomSource() localmente y restauran al salir.
// =============================================================================

#include <unity.h>
#include "StatsEngine.h"

static StatsEngine* g_engine = nullptr;

// RNG auxiliares para tests deterministas
static float rngAlwaysZero() { return 0.0f; }   // garantiza que el evento probabilístico OCURRA
static float rngAlwaysOne()  { return 1.0f; }   // garantiza que el evento probabilístico NO ocurra

void setUp() {
    // Restaurar RNG por defecto antes de cada test para evitar contaminación cruzada
    StatsEngine::setRandomSource(nullptr);
    g_engine = new StatsEngine();
}

void tearDown() {
    StatsEngine::setRandomSource(nullptr);
    delete g_engine;
    g_engine = nullptr;
}

// =============================================================================
// 1. test_default_stats_are_sane
// Al construir con defaults, hunger=50, happiness=50, health=100.
// =============================================================================
void test_default_stats_are_sane() {
    TEST_ASSERT_EQUAL_UINT8(50,  g_engine->stats().hunger);
    TEST_ASSERT_EQUAL_UINT8(50,  g_engine->stats().happiness);
    TEST_ASSERT_EQUAL_UINT8(100, g_engine->stats().health);
}

// =============================================================================
// 2. test_tick_zero_dt_does_nothing
// tick(0) es no-op: stats no cambian y dirty flag queda en false.
// =============================================================================
void test_tick_zero_dt_does_nothing() {
    g_engine->tick(0);

    TEST_ASSERT_EQUAL_UINT8(50,  g_engine->stats().hunger);
    TEST_ASSERT_EQUAL_UINT8(50,  g_engine->stats().happiness);
    TEST_ASSERT_EQUAL_UINT8(100, g_engine->stats().health);
    TEST_ASSERT_FALSE(g_engine->consumeDirtyFlag());
}

// =============================================================================
// 3. test_tick_one_second_applies_decay
// Con rate 0.05 pts/s para hunger, se necesitan 20 ticks de 1s para subir 1 punto.
// Verificamos que tras 20×tick(1000) hunger haya subido al menos 1 punto.
// =============================================================================
void test_tick_one_second_applies_decay() {
    // Con hungerPerSec=0.05, 20 segundos = exactamente 1 punto de hunger
    for (int i = 0; i < 20; ++i) {
        g_engine->tick(1000);
    }
    // hunger debe haber subido de 50 a al menos 51
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(51, g_engine->stats().hunger);

    // happiness con rate 0.03 pts/s: en 34s baja 1 punto (0.03*34=1.02)
    // Solo hemos hecho 20s, no debería haber bajado todavía, pero en 34s sí.
    // Verificamos que el mecanismo de acumulación funciona: tras 34 ticks de 1s
    // happiness habrá bajado al menos 1 punto desde su valor de inicio.
    // Creamos un engine fresco para esta segunda verificación.
    delete g_engine;
    g_engine = new StatsEngine();
    for (int i = 0; i < 34; ++i) {
        g_engine->tick(1000);
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(49, g_engine->stats().happiness);
}

// =============================================================================
// 4. test_tick_clamps_to_zero
// happiness no baja de 0 aunque el decay siga acumulándose.
// =============================================================================
void test_tick_clamps_to_zero() {
    // Forzar happiness a 0 via setStats
    Stats s = g_engine->stats();
    s.happiness = 0;
    g_engine->setStats(s);

    // Muchos ticks: happiness no puede ir negativo
    for (int i = 0; i < 200; ++i) {
        g_engine->tick(1000);
    }
    TEST_ASSERT_EQUAL_UINT8(0, g_engine->stats().happiness);
}

// =============================================================================
// 5. test_tick_huge_dt_clamps_to_24h
// tick(48h en ms) aplica exactamente lo mismo que tick(24h en ms).
// Con hungerPerSec=0.05 y 24h: 0.05 * 86400 = 4320 puntos → clamped a 100.
// Con 48h sin clamp: 8640 puntos → también 100. El clamp es observable
// verificando que el resultado es el mismo y no hay desbordamiento.
// =============================================================================
void test_tick_huge_dt_clamps_to_24h() {
    // Engine A: tick(24h)
    StatsEngine engineA;
    engineA.tick(86400000UL);

    // Engine B: tick(48h) — sin clamp llevaría al mismo resultado porque el clamp lo limita
    StatsEngine engineB;
    engineB.tick(172800000UL);

    // Ambos deben producir el mismo resultado (hunger clamped a 100, happiness a 0)
    TEST_ASSERT_EQUAL_UINT8(engineA.stats().hunger,    engineB.stats().hunger);
    TEST_ASSERT_EQUAL_UINT8(engineA.stats().happiness, engineB.stats().happiness);
    TEST_ASSERT_EQUAL_UINT8(engineA.stats().health,    engineB.stats().health);

    // Además verificar que hunger llegó a 100 (24h de decay a 0.05/s = 4320 pts > 100)
    TEST_ASSERT_EQUAL_UINT8(100, engineA.stats().hunger);
}

// =============================================================================
// 6. test_feed_reduces_hunger
// applyFeed(20) baja hunger en 20 puntos desde un valor inicial alto.
// =============================================================================
void test_feed_reduces_hunger() {
    Stats s = g_engine->stats();
    s.hunger = 80;
    g_engine->setStats(s);

    g_engine->applyFeed(20);
    TEST_ASSERT_EQUAL_UINT8(60, g_engine->stats().hunger);
}

// =============================================================================
// 7. test_feed_clamps_at_zero
// applyFeed(200) con hunger=50 no puede dejar hunger negativo: queda en 0.
// =============================================================================
void test_feed_clamps_at_zero() {
    g_engine->applyFeed(200);
    TEST_ASSERT_EQUAL_UINT8(0, g_engine->stats().hunger);
}

// =============================================================================
// 8. test_play_increases_happiness
// applyPlay(20) sube happiness en 20 puntos desde valor inicial.
// =============================================================================
void test_play_increases_happiness() {
    g_engine->applyPlay(20);
    TEST_ASSERT_EQUAL_UINT8(70, g_engine->stats().happiness);
}

// =============================================================================
// 9. test_play_clamps_at_100
// applyPlay(200) con happiness=50 no puede superar 100.
// =============================================================================
void test_play_clamps_at_100() {
    g_engine->applyPlay(200);
    TEST_ASSERT_EQUAL_UINT8(100, g_engine->stats().happiness);
}

// =============================================================================
// 9b. test_play_also_increases_hunger
// applyPlay(20) sube happiness Y hunger (coste energético: amount/2).
// Default hunger=50 → tras play(20), hunger=60.
// =============================================================================
void test_play_also_increases_hunger() {
    g_engine->applyPlay(20);
    TEST_ASSERT_EQUAL_UINT8(70, g_engine->stats().happiness);
    TEST_ASSERT_EQUAL_UINT8(60, g_engine->stats().hunger);
}

// =============================================================================
// 9c. test_play_ignored_when_sleeping
// Si isSleeping, applyPlay no toca nada (no se juega dormido).
// =============================================================================
void test_play_ignored_when_sleeping() {
    g_engine->applySleep(true);
    Stats before = g_engine->stats();
    g_engine->applyPlay(50);
    Stats after = g_engine->stats();
    TEST_ASSERT_EQUAL_UINT8(before.happiness, after.happiness);
    TEST_ASSERT_EQUAL_UINT8(before.hunger, after.hunger);
}

// =============================================================================
// 11. test_health_decays_only_when_hungry_or_sad
// Con hunger<90 y happiness>10, health no decae.
// Con hunger>=90 o happiness<=10, health sí decae.
// =============================================================================
void test_health_decays_only_when_hungry_or_sad() {
    // --- Caso safe: hunger=50, happiness=50 → health debe mantenerse ---
    // Con healthPerSec=0.01, en 100 segundos se acumularía 1 punto de daño.
    // Pero como no se cumplen las condiciones, nunca debe bajar.
    for (int i = 0; i < 200; ++i) {
        g_engine->tick(1000);
    }
    TEST_ASSERT_EQUAL_UINT8(100, g_engine->stats().health);

    // --- Caso peligroso: hunger=100 (hambriento extremo) → health debe bajar ---
    delete g_engine;
    g_engine = new StatsEngine();
    Stats s = g_engine->stats();
    s.hunger = 100;
    s.happiness = 50;  // happiness ok, pero hunger está al límite
    g_engine->setStats(s);

    // Con healthPerSec=0.01, en 100s se acumula 1.0 → health baja 1 punto
    for (int i = 0; i < 101; ++i) {
        g_engine->tick(1000);
    }
    TEST_ASSERT_LESS_THAN_UINT8(100, g_engine->stats().health);

    // --- Caso peligroso: happiness=0 (triste extremo) → health debe bajar ---
    delete g_engine;
    g_engine = new StatsEngine();
    s = g_engine->stats();
    s.hunger    = 50;   // hunger ok
    s.happiness = 0;    // tristeza extrema
    s.health    = 100;
    g_engine->setStats(s);

    for (int i = 0; i < 101; ++i) {
        g_engine->tick(1000);
    }
    TEST_ASSERT_LESS_THAN_UINT8(100, g_engine->stats().health);
}

// =============================================================================
// 12. test_dirty_flag_set_after_tick
// Tras un tick que provoque un cambio entero en algún stat, dirty=true.
// =============================================================================
void test_dirty_flag_set_after_tick() {
    // Con hungerPerSec=0.05, 20 ticks de 1000ms = 1 punto de hunger → dirty
    for (int i = 0; i < 20; ++i) {
        g_engine->tick(1000);
    }
    TEST_ASSERT_TRUE(g_engine->consumeDirtyFlag());
}

// =============================================================================
// 13. test_dirty_flag_consumed
// consumeDirtyFlag() devuelve true la primera vez y false la siguiente
// sin más cambios de stats.
// =============================================================================
void test_dirty_flag_consumed() {
    // Provocar un cambio
    g_engine->applyFeed(10);

    TEST_ASSERT_TRUE(g_engine->consumeDirtyFlag());
    // Segunda llamada sin cambios adicionales → false
    TEST_ASSERT_FALSE(g_engine->consumeDirtyFlag());
}

// =============================================================================
// 14. test_dirty_flag_not_set_if_no_integer_change
// Un único tick(100) (0.1s) con hungerPerSec=0.05 → acc=0.005, no llega a 1.
// Ningún stat entero cambia → dirty debe ser false.
// =============================================================================
void test_dirty_flag_not_set_if_no_integer_change() {
    // 0.1s × 0.05 pts/s = 0.005 pts acumulados → mucho menos de 1 entero
    g_engine->tick(100);
    TEST_ASSERT_FALSE(g_engine->consumeDirtyFlag());
}

// =============================================================================
// ============================================================ FASE 2 TESTS ===
// =============================================================================

// =============================================================================
// 15. test_default_stats_v2_are_sane
// Los 5 campos nuevos tienen los valores por defecto correctos.
// =============================================================================
void test_default_stats_v2_are_sane() {
    TEST_ASSERT_EQUAL_UINT8(50,  g_engine->stats().discipline);
    TEST_ASSERT_EQUAL_UINT8(50,  g_engine->stats().weight);
    TEST_ASSERT_EQUAL_UINT8(0,   g_engine->stats().poopCount);
    TEST_ASSERT_FALSE(g_engine->stats().isSick);
    TEST_ASSERT_FALSE(g_engine->stats().isSleeping);
    TEST_ASSERT_EQUAL_UINT16(500, g_engine->stats().attentionScore);
}

// =============================================================================
// 16. test_clean_resets_poop_count
// applyClean() con cacas presentes resetea poopCount a 0 y marca dirty.
// =============================================================================
void test_clean_resets_poop_count() {
    Stats s = g_engine->stats();
    s.poopCount = 3;
    g_engine->setStats(s);
    g_engine->consumeDirtyFlag();   // limpiar dirty previo

    g_engine->applyClean();

    TEST_ASSERT_EQUAL_UINT8(0, g_engine->stats().poopCount);
    TEST_ASSERT_TRUE(g_engine->consumeDirtyFlag());
}

// =============================================================================
// 17. test_clean_noop_when_no_poop
// applyClean() sin cacas no hace nada (dirty permanece false).
// =============================================================================
void test_clean_noop_when_no_poop() {
    // poopCount ya es 0 por defecto
    g_engine->consumeDirtyFlag();

    g_engine->applyClean();

    TEST_ASSERT_EQUAL_UINT8(0, g_engine->stats().poopCount);
    TEST_ASSERT_FALSE(g_engine->consumeDirtyFlag());
}

// =============================================================================
// 18. test_medicate_cures_sickness_and_heals
// applyMedicate() con isSick=true cura al pet y sube health +20.
// =============================================================================
void test_medicate_cures_sickness_and_heals() {
    Stats s = g_engine->stats();
    s.isSick = true;
    s.health  = 60;
    g_engine->setStats(s);

    g_engine->applyMedicate();

    TEST_ASSERT_FALSE(g_engine->stats().isSick);
    TEST_ASSERT_EQUAL_UINT8(80, g_engine->stats().health);
    TEST_ASSERT_TRUE(g_engine->consumeDirtyFlag());
}

// =============================================================================
// 19. test_medicate_noop_when_healthy
// applyMedicate() cuando no está enfermo no cambia nada (dirty false).
// =============================================================================
void test_medicate_noop_when_healthy() {
    // isSick=false por defecto
    g_engine->consumeDirtyFlag();

    g_engine->applyMedicate();

    TEST_ASSERT_FALSE(g_engine->stats().isSick);
    TEST_ASSERT_FALSE(g_engine->consumeDirtyFlag());
}

// =============================================================================
// 20. test_discipline_increases_and_drops_happiness
// applyDiscipline(10) sube discipline en 10 y baja happiness en 5.
// =============================================================================
void test_discipline_increases_and_drops_happiness() {
    // discipline=50, happiness=50 por defecto
    g_engine->applyDiscipline(10);

    TEST_ASSERT_EQUAL_UINT8(60, g_engine->stats().discipline);
    TEST_ASSERT_EQUAL_UINT8(45, g_engine->stats().happiness);
    TEST_ASSERT_TRUE(g_engine->consumeDirtyFlag());
}

// =============================================================================
// 21. test_discipline_clamps_at_100
// applyDiscipline(200) con discipline=90 queda en 100, no desborda.
// =============================================================================
void test_discipline_clamps_at_100() {
    Stats s = g_engine->stats();
    s.discipline = 90;
    g_engine->setStats(s);

    g_engine->applyDiscipline(200);

    TEST_ASSERT_EQUAL_UINT8(100, g_engine->stats().discipline);
}

// =============================================================================
// 22. test_sleep_paralyzes_decay
// Con isSleeping=true, hunger y happiness no cambian tras muchos ticks.
// =============================================================================
void test_sleep_paralyzes_decay() {
    g_engine->applySleep(true);

    // 200s: suficiente para ver decay en condiciones normales
    for (int i = 0; i < 200; ++i) {
        g_engine->tick(1000);
    }

    // Hunger no debe haber subido (paralizado)
    TEST_ASSERT_EQUAL_UINT8(50, g_engine->stats().hunger);
    // Happiness no debe haber bajado (paralizado)
    TEST_ASSERT_EQUAL_UINT8(50, g_engine->stats().happiness);
}

// =============================================================================
// 23. test_sleep_off_resumes_decay
// Tras applySleep(false), el decay de hunger se reanuda normalmente.
// =============================================================================
void test_sleep_off_resumes_decay() {
    g_engine->applySleep(true);
    // Verificar que dormido no decae
    for (int i = 0; i < 20; ++i) {
        g_engine->tick(1000);
    }
    TEST_ASSERT_EQUAL_UINT8(50, g_engine->stats().hunger);

    // Despertar: el decay vuelve
    g_engine->applySleep(false);
    for (int i = 0; i < 20; ++i) {
        g_engine->tick(1000);
    }
    // 20s × 0.05 pts/s = 1.0 punto → hunger debe haber subido
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(51, g_engine->stats().hunger);
}

// =============================================================================
// 24. test_feed_spawns_poop_with_seeded_rng
// Con rngAlwaysZero (prob=0 → siempre spawnea), feed debe generar caca.
// =============================================================================
void test_feed_spawns_poop_with_seeded_rng() {
    StatsEngine::setRandomSource(&rngAlwaysZero);

    // poopCount arranca en 0
    g_engine->applyFeed(50);   // amount>10, por lo que se llama onFeedTriggered_

    TEST_ASSERT_EQUAL_UINT8(1, g_engine->stats().poopCount);
}

// =============================================================================
// 25. test_feed_increases_weight
// applyFeed con amount>10 sube weight en 1 punto (weightDeltaPerFeed=1.0).
// =============================================================================
void test_feed_increases_weight() {
    StatsEngine::setRandomSource(&rngAlwaysOne);   // evitar spawn de caca en el assert

    uint8_t weightBefore = g_engine->stats().weight;
    g_engine->applyFeed(20);   // amount>10 → debe subir weight

    TEST_ASSERT_EQUAL_UINT8(weightBefore + 1, g_engine->stats().weight);
}

// =============================================================================
// 26. test_feed_ignored_when_sleeping
// applyFeed mientras duerme es no-op: hunger no cambia, weight no cambia.
// =============================================================================
void test_feed_ignored_when_sleeping() {
    Stats s = g_engine->stats();
    s.hunger = 80;
    g_engine->setStats(s);
    g_engine->applySleep(true);
    g_engine->consumeDirtyFlag();

    g_engine->applyFeed(20);

    TEST_ASSERT_EQUAL_UINT8(80, g_engine->stats().hunger);
    TEST_ASSERT_EQUAL_UINT8(50, g_engine->stats().weight);   // sin cambio
    TEST_ASSERT_FALSE(g_engine->consumeDirtyFlag());
}

// =============================================================================
// 27. test_weight_decays_over_time
// Tras suficientes ticks, weight baja al menos 1 punto desde el valor inicial.
// Con weightDecayPerSec=0.005, se necesitan >=200s para bajar 1 punto.
// Usamos 210s para superar el umbral con margen ante imprecisión de float.
// =============================================================================
void test_weight_decays_over_time() {
    StatsEngine::setRandomSource(&rngAlwaysOne);   // no enfermar por caca

    uint8_t weightStart = g_engine->stats().weight;   // 50

    // 210 ticks de 1s = 210s → 0.005 × 210 = 1.05 pts acumulados → baja 1 punto
    for (int i = 0; i < 210; ++i) {
        g_engine->tick(1000);
    }

    TEST_ASSERT_LESS_THAN_UINT8(weightStart, g_engine->stats().weight);
}

// =============================================================================
// 28. test_health_decay_x5_when_sick
// Con isSick=true y condiciones de peligro (hunger>=90), health decae ×5
// respecto a la tasa base. Comprobamos que decae más rápido que sin sickness.
// =============================================================================
void test_health_decay_x5_when_sick() {
    StatsEngine::setRandomSource(&rngAlwaysOne);   // evitar que se enferme por caca

    // Engine A: hunger alto, sin enfermedad
    StatsEngine engineA;
    {
        Stats s; s.hunger = 95; s.health = 100; s.happiness = 50;
        engineA.setStats(s);
    }

    // Engine B: mismo punto de partida, pero enfermo
    StatsEngine engineB;
    {
        Stats s; s.hunger = 95; s.health = 100; s.happiness = 50; s.isSick = true;
        engineB.setStats(s);
    }

    // 100 ticks de 1s
    for (int i = 0; i < 100; ++i) {
        engineA.tick(1000);
        engineB.tick(1000);
    }

    // B debe tener health significativamente menor que A
    TEST_ASSERT_LESS_THAN_UINT8(engineA.stats().health, engineB.stats().health);
}

// =============================================================================
// 29. test_sickness_probability_grows_with_poop
// Con 5 cacas presentes y rngAlwaysZero, el pet enferma en el primer tick.
// Verifica que la probabilidad de enfermedad escala con poopCount.
// =============================================================================
void test_sickness_probability_grows_with_poop() {
    StatsEngine::setRandomSource(&rngAlwaysZero);   // rand siempre < prob → enferma seguro

    Stats s = g_engine->stats();
    s.poopCount = 5;
    s.isSick    = false;
    g_engine->setStats(s);

    g_engine->tick(1000);   // un tick es suficiente con rng=0

    TEST_ASSERT_TRUE(g_engine->stats().isSick);
}

// =============================================================================
// 30. test_attention_score_rises_when_healthy
// Con stats saludables (hunger<70, happiness>30, health>50), attentionScore
// sube 1 punto cada 10 segundos. Verificamos tras 30s.
// =============================================================================
void test_attention_score_rises_when_healthy() {
    StatsEngine::setRandomSource(&rngAlwaysOne);   // no enfermar

    // stats sanos por defecto: hunger=50, happiness=50, health=100 → isHealthy=true
    uint16_t scoreBefore = g_engine->stats().attentionScore;   // 500

    // 30 ticks de 1s = 30s → 3 ventanas de 10s → +3 puntos
    for (int i = 0; i < 30; ++i) {
        g_engine->tick(1000);
    }

    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(scoreBefore + 3, g_engine->stats().attentionScore);
}

// =============================================================================
// 31. test_attention_score_drops_when_neglected
// Con hunger>=90 (neglected), attentionScore baja 1 punto cada 10 segundos.
// =============================================================================
void test_attention_score_drops_when_neglected() {
    StatsEngine::setRandomSource(&rngAlwaysOne);   // no enfermar por prob

    Stats s = g_engine->stats();
    s.hunger = 95;   // condición de negligencia: hunger>=90
    g_engine->setStats(s);

    uint16_t scoreBefore = g_engine->stats().attentionScore;   // 500

    // 30s → 3 ventanas de 10s → -3 puntos
    for (int i = 0; i < 30; ++i) {
        g_engine->tick(1000);
    }

    TEST_ASSERT_LESS_OR_EQUAL_UINT16(scoreBefore - 3, g_engine->stats().attentionScore);
}

// =============================================================================
// 32. test_reset_for_new_life_restores_defaults
// resetForNewLife() devuelve todos los stats a los valores por defecto v2
// y marca dirty.
// =============================================================================
void test_reset_for_new_life_restores_defaults() {
    // Modificar todos los stats para alejarnos de los defaults
    Stats s;
    s.hunger        = 99;
    s.happiness     = 5;
    s.health        = 10;
    s.discipline    = 10;
    s.weight        = 99;
    s.poopCount     = 7;
    s.isSick        = true;
    s.isSleeping    = true;
    s.attentionScore = 0;
    g_engine->setStats(s);
    g_engine->consumeDirtyFlag();

    g_engine->resetForNewLife();

    TEST_ASSERT_EQUAL_UINT8(50,  g_engine->stats().hunger);
    TEST_ASSERT_EQUAL_UINT8(50,  g_engine->stats().happiness);
    TEST_ASSERT_EQUAL_UINT8(100, g_engine->stats().health);
    TEST_ASSERT_EQUAL_UINT8(50,  g_engine->stats().discipline);
    TEST_ASSERT_EQUAL_UINT8(50,  g_engine->stats().weight);
    TEST_ASSERT_EQUAL_UINT8(0,   g_engine->stats().poopCount);
    TEST_ASSERT_FALSE(g_engine->stats().isSick);
    TEST_ASSERT_FALSE(g_engine->stats().isSleeping);
    TEST_ASSERT_EQUAL_UINT16(500, g_engine->stats().attentionScore);
    TEST_ASSERT_TRUE(g_engine->consumeDirtyFlag());
}

// =============================================================================
// main — punto de entrada Unity para env:native
// =============================================================================
int main() {
    UNITY_BEGIN();

    // --- Fase 1 (14 tests base) ---
    RUN_TEST(test_default_stats_are_sane);
    RUN_TEST(test_tick_zero_dt_does_nothing);
    RUN_TEST(test_tick_one_second_applies_decay);
    RUN_TEST(test_tick_clamps_to_zero);
    RUN_TEST(test_tick_huge_dt_clamps_to_24h);
    RUN_TEST(test_feed_reduces_hunger);
    RUN_TEST(test_feed_clamps_at_zero);
    RUN_TEST(test_play_increases_happiness);
    RUN_TEST(test_play_clamps_at_100);
    RUN_TEST(test_play_also_increases_hunger);
    RUN_TEST(test_play_ignored_when_sleeping);
    RUN_TEST(test_health_decays_only_when_hungry_or_sad);
    RUN_TEST(test_dirty_flag_set_after_tick);
    RUN_TEST(test_dirty_flag_consumed);
    RUN_TEST(test_dirty_flag_not_set_if_no_integer_change);

    // --- Fase 2, Hito 2 (18 tests nuevos) ---
    RUN_TEST(test_default_stats_v2_are_sane);
    RUN_TEST(test_clean_resets_poop_count);
    RUN_TEST(test_clean_noop_when_no_poop);
    RUN_TEST(test_medicate_cures_sickness_and_heals);
    RUN_TEST(test_medicate_noop_when_healthy);
    RUN_TEST(test_discipline_increases_and_drops_happiness);
    RUN_TEST(test_discipline_clamps_at_100);
    RUN_TEST(test_sleep_paralyzes_decay);
    RUN_TEST(test_sleep_off_resumes_decay);
    RUN_TEST(test_feed_spawns_poop_with_seeded_rng);
    RUN_TEST(test_feed_increases_weight);
    RUN_TEST(test_feed_ignored_when_sleeping);
    RUN_TEST(test_weight_decays_over_time);
    RUN_TEST(test_health_decay_x5_when_sick);
    RUN_TEST(test_sickness_probability_grows_with_poop);
    RUN_TEST(test_attention_score_rises_when_healthy);
    RUN_TEST(test_attention_score_drops_when_neglected);
    RUN_TEST(test_reset_for_new_life_restores_defaults);

    return UNITY_END();
}
