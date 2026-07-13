// =============================================================================
// test_battery_gauge.cpp — tests para BatteryGauge.
//
// Framework: Unity (pio test -e native).
// BatteryGauge es lógica pura (sin Arduino.h): se ejercita directamente,
// controlando el tiempo mediante el parámetro nowMs de addSample().
//
// Convenciones:
//   - g_gauge se recrea en cada test (setUp/tearDown) con Config por defecto,
//     salvo que el propio test necesite un Config a medida (se crea local).
//   - Los tests de heurística de carga que no quieren interferencia de la
//     pendiente positiva usan un Config con riseDeltaMv muy alto — aísla el
//     comportamiento bajo prueba (rules/testing.md: tests independientes).
// =============================================================================

#include <unity.h>
#include "BatteryGauge.h"

static BatteryGauge* g_gauge = nullptr;

void setUp() {
    g_gauge = new BatteryGauge();
}

void tearDown() {
    delete g_gauge;
    g_gauge = nullptr;
}

// Helper: alimenta `count` muestras idénticas de `mv`, separadas `stepMs` en
// el reloj sintético, empezando en `startMs`. Devuelve el nowMs tras la última.
static uint32_t feedStable(BatteryGauge& gauge, uint16_t mv, uint8_t count,
                            uint32_t startMs, uint32_t stepMs) {
    uint32_t t = startMs;
    for (uint8_t i = 0; i < count; ++i) {
        gauge.addSample(mv, t);
        t += stepMs;
    }
    return t;
}

// =============================================================================
// 1. test_percent_from_mv_breakpoints_exact
// La curva debe devolver exactamente el valor de tabla en cada breakpoint.
// =============================================================================
void test_percent_from_mv_breakpoints_exact() {
    TEST_ASSERT_EQUAL_UINT8(0,   BatteryGauge::percentFromMv(3300));
    TEST_ASSERT_EQUAL_UINT8(2,   BatteryGauge::percentFromMv(3400));
    TEST_ASSERT_EQUAL_UINT8(5,   BatteryGauge::percentFromMv(3500));
    TEST_ASSERT_EQUAL_UINT8(12,  BatteryGauge::percentFromMv(3600));
    TEST_ASSERT_EQUAL_UINT8(28,  BatteryGauge::percentFromMv(3700));
    TEST_ASSERT_EQUAL_UINT8(45,  BatteryGauge::percentFromMv(3800));
    TEST_ASSERT_EQUAL_UINT8(62,  BatteryGauge::percentFromMv(3900));
    TEST_ASSERT_EQUAL_UINT8(78,  BatteryGauge::percentFromMv(4000));
    TEST_ASSERT_EQUAL_UINT8(90,  BatteryGauge::percentFromMv(4100));
    TEST_ASSERT_EQUAL_UINT8(100, BatteryGauge::percentFromMv(4200));
}

// =============================================================================
// 2. test_percent_from_mv_interpolates_linearly
// A mitad de tramo (3750, entre 3700=28 y 3800=45) el resultado interpola
// con redondeo al entero más cercano: 28 + round((50*17)/100) = 28 + 9 = 37.
// =============================================================================
void test_percent_from_mv_interpolates_linearly() {
    TEST_ASSERT_EQUAL_UINT8(37, BatteryGauge::percentFromMv(3750));
}

// =============================================================================
// 3. test_percent_from_mv_clamps_out_of_range
// Por debajo del primer breakpoint y por encima del último, satura a 0/100.
// =============================================================================
void test_percent_from_mv_clamps_out_of_range() {
    TEST_ASSERT_EQUAL_UINT8(0,   BatteryGauge::percentFromMv(0));
    TEST_ASSERT_EQUAL_UINT8(0,   BatteryGauge::percentFromMv(2000));
    TEST_ASSERT_EQUAL_UINT8(100, BatteryGauge::percentFromMv(4500));
    TEST_ASSERT_EQUAL_UINT8(100, BatteryGauge::percentFromMv(65000));
}

// =============================================================================
// 4. test_percent_from_mv_is_monotonic
// La curva nunca puede bajar al subir mV — recorre todo el rango plausible.
// =============================================================================
void test_percent_from_mv_is_monotonic() {
    uint8_t prev = BatteryGauge::percentFromMv(2800);
    for (uint16_t mv = 2800; mv <= 4350; mv += 10) {
        uint8_t cur = BatteryGauge::percentFromMv(mv);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(prev, cur);
        prev = cur;
    }
}

// =============================================================================
// 5. test_boot_state_is_unknown
// Sin ninguna muestra, el gauge arranca en Unknown (valid=false, todo a cero).
// =============================================================================
void test_boot_state_is_unknown() {
    BatterySnapshot snap = g_gauge->snapshot();
    TEST_ASSERT_FALSE(snap.valid);
    TEST_ASSERT_EQUAL_UINT8(0, snap.percent);
    TEST_ASSERT_FALSE(snap.charging);
    TEST_ASSERT_EQUAL_UINT16(0, snap.millivolts);
}

// =============================================================================
// 6. test_eight_stable_samples_become_valid
// 8 muestras consecutivas plausibles y estables (misma mv) sacan de Unknown
// exactamente en la 8ª, y addSample devuelve true solo en ese instante.
// =============================================================================
void test_eight_stable_samples_become_valid() {
    uint32_t t = 0;
    for (uint8_t i = 0; i < 7; ++i) {
        bool changed = g_gauge->addSample(3700, t);
        TEST_ASSERT_FALSE(changed);
        TEST_ASSERT_FALSE(g_gauge->snapshot().valid);
        t += 1000;
    }
    bool changedOnEighth = g_gauge->addSample(3700, t);
    TEST_ASSERT_TRUE(changedOnEighth);
    TEST_ASSERT_TRUE(g_gauge->snapshot().valid);
    TEST_ASSERT_EQUAL_UINT8(28, g_gauge->snapshot().percent);
}

// =============================================================================
// 7. test_floating_out_of_range_never_becomes_valid
// Un pin flotante que siempre cae fuera del rango plausible jamás sale de
// Unknown, por muchas muestras que lleguen.
// =============================================================================
void test_floating_out_of_range_never_becomes_valid() {
    uint32_t t = 0;
    for (uint8_t i = 0; i < 20; ++i) {
        g_gauge->addSample(5000, t);  // fuera de [2800, 4350]
        t += 1000;
    }
    TEST_ASSERT_FALSE(g_gauge->snapshot().valid);
}

// =============================================================================
// 8. test_floating_in_range_with_jitter_never_becomes_valid
// El caso clave: un pin flotante puede caer DENTRO del rango plausible por
// azar, pero su ruido supera stableBandMv (120 mV) — nunca debe validarse.
// =============================================================================
void test_floating_in_range_with_jitter_never_becomes_valid() {
    uint32_t t = 0;
    for (uint8_t i = 0; i < 20; ++i) {
        // Alterna 3300 / 3450: ambos plausibles, dispersión 150 mV > 120 mV.
        uint16_t mv = (i % 2 == 0) ? 3300 : 3450;
        g_gauge->addSample(mv, t);
        t += 1000;
    }
    TEST_ASSERT_FALSE(g_gauge->snapshot().valid);
}

// =============================================================================
// 8b. test_floating_pin_alternating_in_and_out_of_range_never_valid
// CRÍTICO: el contrato del .h exige `samplesToValid` (8) muestras CONSECUTIVAS
// plausibles+estables. Un pin flotante que alterna una lectura fuera de rango
// (5000) con lecturas dentro de rango y estables entre sí (3000, 3010, 3020...
// dispersión 70 mV, dentro de stableBandMv=120) NUNCA debe validar, aunque
// acumule 8 lecturas plausibles en total — porque no son consecutivas (cada
// una está separada de la siguiente por una implausible).
//
// Antes del fix, una muestra implausible mientras !valid_ solo hacía `return`
// sin limpiar el ring — las 8 muestras plausibles (3000..3070) se seguían
// empujando al ring pese a estar intercaladas con 5000, y como su dispersión
// (70 mV) cumplía stableBandMv, el gauge validaba por error en la 16ª muestra.
// =============================================================================
void test_floating_pin_alternating_in_and_out_of_range_never_valid() {
    uint32_t t = 0;
    uint16_t plausibleMv = 3000;
    for (uint8_t round = 0; round < 5; ++round) {
        g_gauge->addSample(5000, t);        // fuera de rango — debe romper la racha
        t += 1000;
        g_gauge->addSample(plausibleMv, t); // dentro de rango y estable entre sí
        t += 1000;
        plausibleMv += 10;

        TEST_ASSERT_FALSE(g_gauge->snapshot().valid);
    }
    TEST_ASSERT_FALSE(g_gauge->snapshot().valid);
}

// =============================================================================
// 9. test_single_glitch_does_not_invalidate
// Estando valid_, una única muestra fuera de rango no debe volver a Unknown.
// =============================================================================
void test_single_glitch_does_not_invalidate() {
    uint32_t t = feedStable(*g_gauge, 3700, 8, 0, 1000);
    TEST_ASSERT_TRUE(g_gauge->snapshot().valid);

    bool changed = g_gauge->addSample(5000, t);  // glitch aislado, fuera de rango
    t += 1000;

    TEST_ASSERT_FALSE(changed);
    TEST_ASSERT_TRUE(g_gauge->snapshot().valid);

    // Recupera lectura normal: sigue valid, sin sobresaltos.
    changed = g_gauge->addSample(3700, t);
    TEST_ASSERT_FALSE(changed);
    TEST_ASSERT_TRUE(g_gauge->snapshot().valid);
}

// =============================================================================
// 10. test_four_consecutive_glitches_invalidate
// 4 muestras fuera de rango seguidas sí deben volver el estado a Unknown,
// y addSample debe devolver true exactamente en la 4ª.
// =============================================================================
void test_four_consecutive_glitches_invalidate() {
    uint32_t t = feedStable(*g_gauge, 3700, 8, 0, 1000);
    TEST_ASSERT_TRUE(g_gauge->snapshot().valid);

    for (uint8_t i = 0; i < 3; ++i) {
        bool changed = g_gauge->addSample(5000, t);
        TEST_ASSERT_FALSE(changed);
        TEST_ASSERT_TRUE(g_gauge->snapshot().valid);
        t += 1000;
    }

    bool changedOnFourth = g_gauge->addSample(5000, t);
    TEST_ASSERT_TRUE(changedOnFourth);
    TEST_ASSERT_FALSE(g_gauge->snapshot().valid);
}

// =============================================================================
// 10b. test_samples_to_valid_clamped_to_ring_capacity
// [MINOR] samplesToValid no puede superar kRingCapacity (8): ringCount_ satura
// ahí, así que un Config con samplesToValid=20 haría la validez inalcanzable
// EN SILENCIO (nunca se cumpliría ringCount_ >= 20). El ctor clampa el valor a
// 8, y por tanto 8 muestras estables siguen bastando para validar.
// =============================================================================
void test_samples_to_valid_clamped_to_ring_capacity() {
    BatteryGauge::Config cfg;
    cfg.samplesToValid = 20;  // por encima de kRingCapacity (8) — debe clamparse
    BatteryGauge gauge(cfg);

    uint32_t t = 0;
    for (uint8_t i = 0; i < 7; ++i) {
        gauge.addSample(3700, t);
        TEST_ASSERT_FALSE(gauge.snapshot().valid);
        t += 1000;
    }
    gauge.addSample(3700, t);  // 8ª muestra estable -> debe validar (clamp a 8)
    TEST_ASSERT_TRUE(gauge.snapshot().valid);
}

// =============================================================================
// 11. test_percent_hysteresis_prevents_flicker
// El % mostrado no debe moverse hasta que el % crudo (sobre la media
// filtrada) difiera en >= percentHysteresis (3) puntos del mostrado.
// riseDeltaMv se desactiva (umbral altísimo) para aislar este test de la
// heurística de carga por pendiente — la subida de mV usada aquí (72 mV en
// una sola muestra) la dispararía si no se aislara.
// =============================================================================
void test_percent_hysteresis_prevents_flicker() {
    BatteryGauge::Config cfg;
    cfg.riseDeltaMv = 60000;
    BatteryGauge gauge(cfg);

    uint32_t t = feedStable(gauge, 3700, 8, 0, 1000);
    TEST_ASSERT_TRUE(gauge.snapshot().valid);
    TEST_ASSERT_EQUAL_UINT8(28, gauge.snapshot().percent);

    // Media tras esta muestra: (3700*7 + 3772) / 8 = 3709 -> percent 30 (diff 2, <3)
    bool changed1 = gauge.addSample(3772, t);
    t += 1000;
    TEST_ASSERT_FALSE(changed1);
    TEST_ASSERT_EQUAL_UINT8(28, gauge.snapshot().percent);

    // Media tras esta muestra: (3700*6 + 3772*2) / 8 = 3718 -> percent 31 (diff 3, >=3)
    bool changed2 = gauge.addSample(3772, t);
    TEST_ASSERT_TRUE(changed2);
    TEST_ASSERT_EQUAL_UINT8(31, gauge.snapshot().percent);
}

// =============================================================================
// 11b. test_hysteresis_reaches_zero_percent_on_discharge
// [MINOR] Con percentHysteresis=3, sin forzar la actualización en los
// extremos, el paso final de la descarga (raw 1 -> 0, diff=2 < 3) queda
// bloqueado por la histéresis y el mostrado se queda pegado en 2% para
// siempre — nunca llega a 0% aunque la batería esté totalmente descargada.
// =============================================================================
void test_hysteresis_reaches_zero_percent_on_discharge() {
    uint32_t t = feedStable(*g_gauge, 3700, 8, 0, 1000);  // valid, percent=28
    TEST_ASSERT_TRUE(g_gauge->snapshot().valid);
    TEST_ASSERT_EQUAL_UINT8(28, g_gauge->snapshot().percent);

    // Reemplaza todo el ring con el mínimo plausible (3300 mV -> raw 0%).
    feedStable(*g_gauge, 3300, 8, t, 1000);
    TEST_ASSERT_EQUAL_UINT8(0, g_gauge->snapshot().percent);
}

// =============================================================================
// 11c. test_hysteresis_reaches_hundred_percent_on_charge
// [MINOR] Simétrico al anterior: el paso final de la carga (raw 99 -> 100,
// diff=1 < 3, o el previo 98->100 con diff=2 < 3 según el punto de partida)
// queda bloqueado sin el fix — el mostrado se queda pegado en 98-99% y nunca
// llega a 100% pese a que la batería está totalmente cargada.
// =============================================================================
void test_hysteresis_reaches_hundred_percent_on_charge() {
    uint32_t t = feedStable(*g_gauge, 3700, 8, 0, 1000);  // valid, percent=28

    // Estabiliza cerca del 98% (media=4175mV) para acercarse al caso límite.
    t = feedStable(*g_gauge, 4175, 8, t, 1000);
    TEST_ASSERT_EQUAL_UINT8(98, g_gauge->snapshot().percent);

    // Termina de cargar hasta el máximo plausible (4200 mV -> raw 100%).
    feedStable(*g_gauge, 4200, 8, t, 1000);
    TEST_ASSERT_EQUAL_UINT8(100, g_gauge->snapshot().percent);
}

// =============================================================================
// 12. test_charging_by_sustained_threshold
// mV >= fullThresholdMv sostenido >= fullSustainMs (60 s) activa charging
// por heurística de umbral, sin pin de sensado (ChargeSenseMode::None).
//
// El tracking de "sostenido" (fullSinceMs_) arranca en la muestra donde el
// gauge pasa a valid_ (la 8ª de feedStable, t=7000), no antes — updateCharging_
// solo se ejecuta con valid_==true (charging gateado por validez, ver
// BatteryGauge::addSample). Por eso el umbral de 60s se cumple en
// t=7000+60000=67000, no en t=60000 como si el conteo hubiera empezado en t=0.
// =============================================================================
void test_charging_by_sustained_threshold() {
    uint32_t t = feedStable(*g_gauge, 4200, 8, 0, 1000);  // t queda en 8000; valid_ desde t=7000
    TEST_ASSERT_TRUE(g_gauge->snapshot().valid);
    TEST_ASSERT_FALSE(g_gauge->snapshot().charging);  // aún no sostenido 60s desde que se volvió valid

    bool changed = g_gauge->addSample(4200, 67000);  // 67000 - 7000 (inicio del umbral) >= 60000
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_TRUE(g_gauge->snapshot().charging);
}

// =============================================================================
// 13. test_charging_by_positive_slope
// Una subida >= riseDeltaMv (25 mV) dentro de la ventana de pendiente activa
// charging por heurística de pendiente, sin necesidad de sostenerse 60 s.
// =============================================================================
void test_charging_by_positive_slope() {
    uint32_t t = feedStable(*g_gauge, 3700, 8, 0, 1000);  // valid, charging false
    TEST_ASSERT_FALSE(g_gauge->snapshot().charging);

    g_gauge->addSample(3730, t);  // subida de 30 mV >= riseDeltaMv(25) en la misma ventana
    TEST_ASSERT_TRUE(g_gauge->snapshot().charging);
}

// =============================================================================
// 14. test_charge_sense_mode_vbus_presence
// HIGH + no llena -> charging; HIGH + llena -> sin rayo; LOW -> descargando
// (anula la heurística incluso si hay indicios de carga).
// =============================================================================
void test_charge_sense_mode_vbus_presence() {
    uint32_t t = feedStable(*g_gauge, 3700, 8, 0, 1000);  // valid, no llena

    g_gauge->setChargeSense(ChargeSenseMode::VbusPresence, true);  // HIGH
    g_gauge->addSample(3700, t);
    t += 1000;
    TEST_ASSERT_TRUE(g_gauge->snapshot().charging);

    // Llena (>= fullThresholdMv) con VBUS HIGH: se oculta el rayo.
    g_gauge->addSample(4200, t);
    t += 1000;
    TEST_ASSERT_FALSE(g_gauge->snapshot().charging);

    // VBUS LOW: descargando pase lo que pase con la heurística.
    g_gauge->setChargeSense(ChargeSenseMode::VbusPresence, false);
    g_gauge->addSample(3700, t);
    TEST_ASSERT_FALSE(g_gauge->snapshot().charging);
}

// =============================================================================
// 15. test_charge_sense_mode_charge_led_active_low
// LOW (LED encendido) -> charging siempre. HIGH (LED apagado) -> decide la
// heurística (aquí false: mv estable, sin umbral ni pendiente).
// =============================================================================
void test_charge_sense_mode_charge_led_active_low() {
    uint32_t t = feedStable(*g_gauge, 3700, 8, 0, 1000);

    g_gauge->setChargeSense(ChargeSenseMode::ChargeLedActiveLow, false);  // LOW = activo
    g_gauge->addSample(3700, t);
    t += 1000;
    TEST_ASSERT_TRUE(g_gauge->snapshot().charging);

    g_gauge->setChargeSense(ChargeSenseMode::ChargeLedActiveLow, true);  // HIGH = inactivo
    g_gauge->addSample(3700, t);
    TEST_ASSERT_FALSE(g_gauge->snapshot().charging);
}

// =============================================================================
// 15b. test_charging_never_true_while_unknown
// [MAJOR] Mientras !valid_ (Unknown), charging debe ser SIEMPRE false, incluso
// si las lecturas (aunque implausibles/inestables) sugerirían "cargando" por
// pura casualidad de la heurística. Antes del fix, updateCharging_ se
// ejecutaba con cualquier muestra plausible aunque valid_==false, pudiendo
// dejar charging_=true expuesto en la API con batteryValid=false.
// =============================================================================
void test_charging_never_true_while_unknown() {
    uint32_t t = 0;
    // 7 muestras plausibles y estables — todavía Unknown (falta la 8ª).
    for (uint8_t i = 0; i < 7; ++i) {
        g_gauge->addSample(4200, t);  // >= fullThresholdMv, "sugeriría" carga
        TEST_ASSERT_FALSE(g_gauge->snapshot().valid);
        TEST_ASSERT_FALSE(g_gauge->snapshot().charging);
        t += 1000;
    }
}

// =============================================================================
// 15c. test_charging_resets_to_false_after_invalidation
// [MAJOR] Tras perder validez (4 muestras fuera de rango), charging debe
// volver a false inmediatamente, aunque justo antes de invalidarse estuviera
// cargando por heurística de umbral sostenido. Sin el fix, charging_ se
// quedaba "congelado" en true durante todo el tiempo en Unknown, porque
// updateCharging_ ya no se ejecutaba pero tampoco se reseteaba el estado.
// =============================================================================
void test_charging_resets_to_false_after_invalidation() {
    // Válido y cargando por umbral sostenido (mismo patrón que test 12).
    uint32_t t = feedStable(*g_gauge, 4200, 8, 0, 1000);  // valid desde t=7000
    g_gauge->addSample(4200, 67000);  // sostenido >=60s desde t=7000 -> charging=true
    TEST_ASSERT_TRUE(g_gauge->snapshot().valid);
    TEST_ASSERT_TRUE(g_gauge->snapshot().charging);

    // 4 muestras fuera de rango consecutivas -> vuelve a Unknown.
    t = 67000;
    for (uint8_t i = 0; i < 4; ++i) {
        t += 1000;
        g_gauge->addSample(5000, t);
    }
    TEST_ASSERT_FALSE(g_gauge->snapshot().valid);
    TEST_ASSERT_FALSE(g_gauge->snapshot().charging);
}

// =============================================================================
// 15d. test_slope_window_rotation_preserves_charging_during_slow_continuous_rise
// [MINOR] Una carga lenta y continua (+0.5 mV/s) que cruza la frontera de
// slopeWindowMs (10 min) NO debe hacer parpadear el rayo. Antes del fix, al
// rotar la ventana "tumbling" el mínimo se reseteaba a la muestra actual y
// chargingBySlope pasaba a false instantáneamente durante los primeros
// ~riseDeltaMv/0.5mV/s = 50s de cada ventana nueva — parpadeo cada 10 min.
// riseDeltaMv por defecto es 25 mV; a +0.5 mV/s tarda 50s en volver a
// detectarse tras cada rotación si no se conserva el estado.
//
// maxPlausibleMv se amplía (6000) SOLO para aislar este test de la banda de
// plausibilidad real de una LiPo (2800-4350): en 25 min a +0.5mV/s el mV
// sintético sube ~750 mV desde 3700, superando el máximo real de una LiPo 1S.
// Lo que se ejercita aquí es exclusivamente la rotación de la ventana de
// pendiente, no el rango físico de voltaje (mismo criterio de aislamiento que
// test_percent_hysteresis_prevents_flicker con riseDeltaMv).
// =============================================================================
void test_slope_window_rotation_preserves_charging_during_slow_continuous_rise() {
    BatteryGauge::Config cfg;
    cfg.maxPlausibleMv = 6000;
    BatteryGauge gauge(cfg);

    uint32_t t = feedStable(gauge, 3700, 8, 0, 1000);  // valid, charging false
    TEST_ASSERT_TRUE(gauge.snapshot().valid);
    TEST_ASSERT_FALSE(gauge.snapshot().charging);

    // +0.5 mV/s durante 25 minutos (1500 s), muestreando cada 5s (300 muestras)
    // — cruza dos fronteras de slopeWindowMs (10 min = 600000 ms).
    uint16_t mv = 3700;
    bool sawChargingOnce = false;
    for (uint16_t i = 0; i < 300; ++i) {
        t += 5000;               // paso de 5s
        mv = static_cast<uint16_t>(3700 + (static_cast<uint32_t>(i) * 5) / 2);  // ~+0.5 mV/s
        gauge.addSample(mv, t);

        if (t < 60000) continue;  // ignora el arranque, antes de alcanzar riseDeltaMv la 1a vez

        if (gauge.snapshot().charging) {
            sawChargingOnce = true;
        } else if (sawChargingOnce) {
            // Una vez visto charging=true por pendiente sostenida, NUNCA debe
            // volver a false mientras la subida continúa — ni siquiera en las
            // fronteras de ventana (t≈600000, t≈1200000 tras el inicio de la
            // primera ventana).
            TEST_FAIL_MESSAGE("charging parpadeo a false durante una subida lenta y continua");
        }
    }
    TEST_ASSERT_TRUE(sawChargingOnce);
    TEST_ASSERT_TRUE(gauge.snapshot().charging);
}

// =============================================================================
// 16. test_add_sample_returns_true_only_on_change
// Una vez estable, repetir la misma muestra no debe reportar cambio; solo
// una muestra que altere valid/percent/charging debe devolver true.
// =============================================================================
void test_add_sample_returns_true_only_on_change() {
    uint32_t t = feedStable(*g_gauge, 3700, 8, 0, 1000);
    TEST_ASSERT_TRUE(g_gauge->snapshot().valid);

    for (uint8_t i = 0; i < 5; ++i) {
        bool changed = g_gauge->addSample(3700, t);
        TEST_ASSERT_FALSE(changed);
        t += 1000;
    }

    // Salto grande de mV: cambia el % mostrado (fuera de la banda de histéresis).
    bool changed = g_gauge->addSample(4200, t);
    TEST_ASSERT_TRUE(changed);
}

// =============================================================================
// main — punto de entrada Unity para env:native
// =============================================================================
int main() {
    UNITY_BEGIN();

    RUN_TEST(test_percent_from_mv_breakpoints_exact);
    RUN_TEST(test_percent_from_mv_interpolates_linearly);
    RUN_TEST(test_percent_from_mv_clamps_out_of_range);
    RUN_TEST(test_percent_from_mv_is_monotonic);

    RUN_TEST(test_boot_state_is_unknown);
    RUN_TEST(test_eight_stable_samples_become_valid);
    RUN_TEST(test_floating_out_of_range_never_becomes_valid);
    RUN_TEST(test_floating_in_range_with_jitter_never_becomes_valid);
    RUN_TEST(test_floating_pin_alternating_in_and_out_of_range_never_valid);
    RUN_TEST(test_single_glitch_does_not_invalidate);
    RUN_TEST(test_four_consecutive_glitches_invalidate);
    RUN_TEST(test_samples_to_valid_clamped_to_ring_capacity);

    RUN_TEST(test_percent_hysteresis_prevents_flicker);
    RUN_TEST(test_hysteresis_reaches_zero_percent_on_discharge);
    RUN_TEST(test_hysteresis_reaches_hundred_percent_on_charge);

    RUN_TEST(test_charging_by_sustained_threshold);
    RUN_TEST(test_charging_by_positive_slope);
    RUN_TEST(test_charge_sense_mode_vbus_presence);
    RUN_TEST(test_charge_sense_mode_charge_led_active_low);
    RUN_TEST(test_charging_never_true_while_unknown);
    RUN_TEST(test_charging_resets_to_false_after_invalidation);
    RUN_TEST(test_slope_window_rotation_preserves_charging_during_slow_continuous_rise);

    RUN_TEST(test_add_sample_returns_true_only_on_change);

    return UNITY_END();
}
