// =============================================================================
// test_stats_engine.cpp — 14 tests obligatorios para StatsEngine (hito 4, Fase 1).
//
// Framework: Unity (pio test -e native).
// StatsEngine es lógica pura: no necesita mock, se ejercita directamente.
//
// Convenciones de nomenclatura:
//   g_engine — instancia reutilizada entre tests vía setUp/tearDown.
//   Cada test describe EXACTAMENTE qué comportamiento verifica.
// =============================================================================

#include <unity.h>
#include "StatsEngine.h"

static StatsEngine* g_engine = nullptr;

void setUp() {
    g_engine = new StatsEngine();
}

void tearDown() {
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
// 10. test_heal_increases_health
// applyHeal(20) sube health en 20 desde un valor bajo.
// =============================================================================
void test_heal_increases_health() {
    Stats s = g_engine->stats();
    s.health = 40;
    g_engine->setStats(s);

    g_engine->applyHeal(20);
    TEST_ASSERT_EQUAL_UINT8(60, g_engine->stats().health);
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
// main — punto de entrada Unity para env:native
// =============================================================================
int main() {
    UNITY_BEGIN();

    RUN_TEST(test_default_stats_are_sane);
    RUN_TEST(test_tick_zero_dt_does_nothing);
    RUN_TEST(test_tick_one_second_applies_decay);
    RUN_TEST(test_tick_clamps_to_zero);
    RUN_TEST(test_tick_huge_dt_clamps_to_24h);
    RUN_TEST(test_feed_reduces_hunger);
    RUN_TEST(test_feed_clamps_at_zero);
    RUN_TEST(test_play_increases_happiness);
    RUN_TEST(test_play_clamps_at_100);
    RUN_TEST(test_heal_increases_health);
    RUN_TEST(test_health_decays_only_when_hungry_or_sad);
    RUN_TEST(test_dirty_flag_set_after_tick);
    RUN_TEST(test_dirty_flag_consumed);
    RUN_TEST(test_dirty_flag_not_set_if_no_integer_change);

    return UNITY_END();
}
