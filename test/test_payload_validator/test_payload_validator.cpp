// =============================================================================
// test_payload_validator.cpp — tests para PayloadValidator (hito 3, Fase 3).
//
// Framework: Unity (pio test -e native).
// PayloadValidator es puro: funciones libres sin estado, sin dependencias.
//
// 6 tests según plan fase-3-plan.md §9.2.
// =============================================================================

#include <unity.h>
#include "PayloadValidator.h"

void setUp() {}
void tearDown() {}

// =============================================================================
// 1. validate_amount_in_range_accepts_1_50_100
// Valores 1, 50 y 100 son todos válidos (rango inclusivo [1, 100]).
// =============================================================================
void test_validate_amount_in_range_accepts_1_50_100() {
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::Ok,
                      PayloadValidator::validateAmount(1));
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::Ok,
                      PayloadValidator::validateAmount(50));
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::Ok,
                      PayloadValidator::validateAmount(100));
}

// =============================================================================
// 2. validate_amount_rejects_0
// 0 está fuera del rango [1, 100] → InvalidAmount.
// =============================================================================
void test_validate_amount_rejects_0() {
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::InvalidAmount,
                      PayloadValidator::validateAmount(0));
}

// =============================================================================
// 3. validate_amount_rejects_101
// 101 está fuera del rango [1, 100] → InvalidAmount.
// =============================================================================
void test_validate_amount_rejects_101() {
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::InvalidAmount,
                      PayloadValidator::validateAmount(101));
}

// =============================================================================
// 4. validate_amount_rejects_negative
// Valores negativos → InvalidAmount.
// =============================================================================
void test_validate_amount_rejects_negative() {
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::InvalidAmount,
                      PayloadValidator::validateAmount(-1));
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::InvalidAmount,
                      PayloadValidator::validateAmount(-100));
}

// =============================================================================
// 5. validate_action_name_known
// Los 6 nombres de acción conocidos deben retornar Ok.
// =============================================================================
void test_validate_action_name_known() {
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::Ok,
                      PayloadValidator::validateActionName("feed"));
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::Ok,
                      PayloadValidator::validateActionName("play"));
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::Ok,
                      PayloadValidator::validateActionName("clean"));
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::Ok,
                      PayloadValidator::validateActionName("medicate"));
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::Ok,
                      PayloadValidator::validateActionName("discipline"));
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::Ok,
                      PayloadValidator::validateActionName("lights"));
}

// =============================================================================
// 6. validate_action_name_unknown
// Nombres no reconocidos → UnknownAction. Incluye string vacío y nullptr.
// =============================================================================
void test_validate_action_name_unknown() {
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::UnknownAction,
                      PayloadValidator::validateActionName("foo"));
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::UnknownAction,
                      PayloadValidator::validateActionName("heal"));
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::UnknownAction,
                      PayloadValidator::validateActionName("reset"));
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::UnknownAction,
                      PayloadValidator::validateActionName(""));
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::UnknownAction,
                      PayloadValidator::validateActionName(nullptr));
    // Mayúsculas no coinciden (case-sensitive)
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::UnknownAction,
                      PayloadValidator::validateActionName("Feed"));
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::UnknownAction,
                      PayloadValidator::validateActionName("LIGHTS"));
    // "sleep" ya no es una acción conocida — el sueño ya no es controlable
    // por el usuario (ver StatsEngine, sueño automático por energía).
    TEST_ASSERT_EQUAL(PayloadValidator::ValidationError::UnknownAction,
                      PayloadValidator::validateActionName("sleep"));
}

// =============================================================================
// main — punto de entrada Unity para env:native
// =============================================================================
int main() {
    UNITY_BEGIN();

    RUN_TEST(test_validate_amount_in_range_accepts_1_50_100);
    RUN_TEST(test_validate_amount_rejects_0);
    RUN_TEST(test_validate_amount_rejects_101);
    RUN_TEST(test_validate_amount_rejects_negative);
    RUN_TEST(test_validate_action_name_known);
    RUN_TEST(test_validate_action_name_unknown);

    return UNITY_END();
}
