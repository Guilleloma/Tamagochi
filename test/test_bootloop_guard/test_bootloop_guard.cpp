// =============================================================================
// test_bootloop_guard.cpp — Tests unitarios para BootLoopGuardLogic.
//
// Framework: Unity (pio test -e native).
// BootLoopGuardLogic es lógica pura (sin RTC_NOINIT_ATTR, sin esp_reset_reason,
// sin Arduino.h) — se ejercita instanciando la clase directamente y pasando el
// estado (maxRestarts/initialCount) por constructor o reset(), igual que hace
// el wrapper ESP32 (ver BootLoopGuard.cpp) con el contador leído de RTC.
//
// kMaxRestarts=3 se usa como tope de prueba en la mayoría de los tests para
// mantenerlos legibles; un test dedicado replica el valor real de producción
// (BOOT_LOOP_MAX=5 en config.h) para no perder cobertura de ese número exacto.
// =============================================================================

#include <unity.h>
#include "BootLoopGuardLogic.h"

void setUp()    {}
void tearDown() {}

// =============================================================================
// 1. should_allow_restart_up_to_max_then_block
// allowRestart() debe permitir exactamente kMaxRestarts llamadas (true) y
// bloquear (false, modo degradado) a partir de la siguiente.
// =============================================================================
void should_allow_restart_up_to_max_then_block() {
    const uint32_t kMaxRestarts = 3;
    BootLoopGuardLogic guard(kMaxRestarts);

    TEST_ASSERT_TRUE(guard.allowRestart());  // 1/3
    TEST_ASSERT_TRUE(guard.allowRestart());  // 2/3
    TEST_ASSERT_TRUE(guard.allowRestart());  // 3/3 -- alcanza el tope

    TEST_ASSERT_FALSE_MESSAGE(guard.allowRestart(),
        "allowRestart() debería bloquear tras agotar kMaxRestarts");
    TEST_ASSERT_FALSE_MESSAGE(guard.allowRestart(),
        "llamadas repetidas en modo degradado deben seguir devolviendo false");
}

// =============================================================================
// 2. should_block_at_exact_boundary_value
// Edge case: con count_ ya exactamente en maxRestarts_ (vía reset()),
// allowRestart() debe bloquear sin incrementar el contador más allá del tope.
// =============================================================================
void should_block_at_exact_boundary_value() {
    const uint32_t kMaxRestarts = 5;  // == BOOT_LOOP_MAX de producción (config.h)
    BootLoopGuardLogic guard;
    guard.reset(kMaxRestarts, /*initialCount=*/kMaxRestarts);

    TEST_ASSERT_EQUAL_UINT32(kMaxRestarts, guard.count());
    TEST_ASSERT_FALSE(guard.allowRestart());
    TEST_ASSERT_EQUAL_UINT32(kMaxRestarts, guard.count());  // no incrementó
}

// =============================================================================
// 3. should_allow_restart_when_one_below_boundary
// Edge case complementario: con count_ == maxRestarts_ - 1, la siguiente
// llamada SÍ debe permitirse (es la última antes del tope).
// =============================================================================
void should_allow_restart_when_one_below_boundary() {
    const uint32_t kMaxRestarts = 5;
    BootLoopGuardLogic guard;
    guard.reset(kMaxRestarts, /*initialCount=*/kMaxRestarts - 1);

    TEST_ASSERT_TRUE(guard.allowRestart());
    TEST_ASSERT_EQUAL_UINT32(kMaxRestarts, guard.count());
    TEST_ASSERT_FALSE(guard.allowRestart());  // ahora sí, en el tope
}

// =============================================================================
// 4. should_reset_count_to_zero_on_stable_connection
// reportStableConnection() debe resetear el contador a 0, restaurando la
// confianza para permitir de nuevo BOOT_LOOP_MAX reinicios.
// =============================================================================
void should_reset_count_to_zero_on_stable_connection() {
    const uint32_t kMaxRestarts = 3;
    BootLoopGuardLogic guard(kMaxRestarts);

    guard.allowRestart();
    guard.allowRestart();
    guard.allowRestart();
    TEST_ASSERT_FALSE(guard.allowRestart());  // agotado

    guard.reportStableConnection();
    TEST_ASSERT_EQUAL_UINT32(0, guard.count());
    TEST_ASSERT_TRUE_MESSAGE(guard.allowRestart(),
        "tras reportStableConnection() debe volver a permitir reinicios");
}

// =============================================================================
// 5. should_be_idempotent_on_repeated_stable_connection_reports
// Llamar reportStableConnection() repetidamente mientras la conexión sigue
// estable debe ser un no-op seguro (el contador se queda en 0).
// =============================================================================
void should_be_idempotent_on_repeated_stable_connection_reports() {
    BootLoopGuardLogic guard(3);

    guard.reportStableConnection();
    guard.reportStableConnection();
    guard.reportStableConnection();

    TEST_ASSERT_EQUAL_UINT32(0, guard.count());
}

// =============================================================================
// 6. should_reflect_correct_count_after_increments_and_reset
// count() debe reflejar el número exacto de allowRestart() exitosos, y
// volver a 0 tras reportStableConnection().
// =============================================================================
void should_reflect_correct_count_after_increments_and_reset() {
    BootLoopGuardLogic guard(5);

    TEST_ASSERT_EQUAL_UINT32(0, guard.count());

    guard.allowRestart();
    TEST_ASSERT_EQUAL_UINT32(1, guard.count());

    guard.allowRestart();
    guard.allowRestart();
    TEST_ASSERT_EQUAL_UINT32(3, guard.count());

    guard.reportStableConnection();
    TEST_ASSERT_EQUAL_UINT32(0, guard.count());
}

// =============================================================================
// 7. should_seed_initial_count_via_constructor
// El constructor con initialCount simula al wrapper ESP32 restaurando el
// contador preservado en RTC_NOINIT_ATTR tras un soft-reset.
// =============================================================================
void should_seed_initial_count_via_constructor() {
    BootLoopGuardLogic guard(/*maxRestarts=*/5, /*initialCount=*/2);

    TEST_ASSERT_EQUAL_UINT32(2, guard.count());
    TEST_ASSERT_TRUE(guard.allowRestart());
    TEST_ASSERT_EQUAL_UINT32(3, guard.count());
}

// =============================================================================
// 8. should_reconfigure_via_reset_like_wrapper_begin
// reset() simula BootLoopGuard::begin(): resincroniza maxRestarts_/count_
// con el valor recién resuelto de RTC_NOINIT_ATTR (cold-boot -> 0).
// =============================================================================
void should_reconfigure_via_reset_like_wrapper_begin() {
    BootLoopGuardLogic guard(5, 4);  // estado preservado de un boot anterior
    TEST_ASSERT_EQUAL_UINT32(4, guard.count());

    guard.reset(5, 0);  // cold-boot detectado -> begin() resetea a 0
    TEST_ASSERT_EQUAL_UINT32(0, guard.count());
    TEST_ASSERT_EQUAL_UINT32(5, guard.maxRestarts());
}

// =============================================================================
// 9. should_block_immediately_when_max_restarts_is_zero
// Edge case: maxRestarts_=0 (geometría degenerada) no debe permitir ningún
// reinicio ni incrementar el contador -- 0 >= 0 bloquea desde la primera llamada.
// =============================================================================
void should_block_immediately_when_max_restarts_is_zero() {
    BootLoopGuardLogic guard(/*maxRestarts=*/0);

    TEST_ASSERT_FALSE(guard.allowRestart());
    TEST_ASSERT_EQUAL_UINT32(0, guard.count());
}

// =============================================================================
// main — punto de entrada Unity
// =============================================================================
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(should_allow_restart_up_to_max_then_block);
    RUN_TEST(should_block_at_exact_boundary_value);
    RUN_TEST(should_allow_restart_when_one_below_boundary);
    RUN_TEST(should_reset_count_to_zero_on_stable_connection);
    RUN_TEST(should_be_idempotent_on_repeated_stable_connection_reports);
    RUN_TEST(should_reflect_correct_count_after_increments_and_reset);
    RUN_TEST(should_seed_initial_count_via_constructor);
    RUN_TEST(should_reconfigure_via_reset_like_wrapper_begin);
    RUN_TEST(should_block_immediately_when_max_restarts_is_zero);

    return UNITY_END();
}
