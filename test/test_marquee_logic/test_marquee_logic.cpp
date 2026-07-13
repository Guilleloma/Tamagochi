// =============================================================================
// test_marquee_logic.cpp — tests para MarqueeLogic (scroll horizontal puro).
//
// Framework: Unity (pio test -e native).
// MarqueeLogic es header-only y puro (sin Arduino.h, sin millis()) — se
// ejercita directamente pasando el tiempo por parámetro.
// =============================================================================

#include <unity.h>
#include "MarqueeLogic.h"

void setUp()    {}
void tearDown() {}

// =============================================================================
// 1. test_offset_px_zero_at_start
// =============================================================================
void test_offset_px_zero_at_start() {
    TEST_ASSERT_EQUAL_UINT32(0, MarqueeLogic::offsetPx(0, 60));
}

// =============================================================================
// 2. test_offset_px_linear_scaling
// =============================================================================
void test_offset_px_linear_scaling() {
    TEST_ASSERT_EQUAL_UINT32(60, MarqueeLogic::offsetPx(1000, 60));
    TEST_ASSERT_EQUAL_UINT32(30, MarqueeLogic::offsetPx(500, 60));
}

// =============================================================================
// 3. test_offset_px_no_overflow_at_60000ms_high_speed
// elapsedMs=60000 (kMessageDurationMs) con una velocidad deliberadamente alta
// (100000 px/s): el producto intermedio (6e9) desborda un uint32_t
// (max ~4.29e9) si se multiplicara en 32 bits — el uint64_t intermedio de
// offsetPx() debe devolver el resultado exacto sin truncar.
// =============================================================================
void test_offset_px_no_overflow_at_60000ms_high_speed() {
    TEST_ASSERT_EQUAL_UINT32(6000000, MarqueeLogic::offsetPx(60000, 100000));
}

// =============================================================================
// 4. test_cycle_px_is_text_width_plus_screen_width
// =============================================================================
void test_cycle_px_is_text_width_plus_screen_width() {
    TEST_ASSERT_EQUAL_UINT32(540,   MarqueeLogic::cyclePx(300, 240));
    // textPxW en su máximo uint16_t — geometría de un mensaje muy largo.
    TEST_ASSERT_EQUAL_UINT32(65775, MarqueeLogic::cyclePx(65535, 240));
}

// =============================================================================
// 5. test_text_x_at_offset_zero_equals_screen_width
// En offset=0 el texto está completamente fuera de pantalla por la derecha.
// =============================================================================
void test_text_x_at_offset_zero_equals_screen_width() {
    TEST_ASSERT_EQUAL_INT32(240, MarqueeLogic::textX(0, 240, 540));
}

// =============================================================================
// 6. test_text_x_decreases_as_offset_increases_and_wraps_on_full_cycle
// =============================================================================
void test_text_x_decreases_as_offset_increases_and_wraps_on_full_cycle() {
    TEST_ASSERT_EQUAL_INT32(140, MarqueeLogic::textX(100, 240, 540));
    // Tras un ciclo completo, el módulo reinicia: vuelve a screenW.
    TEST_ASSERT_EQUAL_INT32(240, MarqueeLogic::textX(540, 240, 540));
}

// =============================================================================
// 7. test_pass_count_zero_before_first_cycle_completes
// =============================================================================
void test_pass_count_zero_before_first_cycle_completes() {
    TEST_ASSERT_EQUAL_UINT32(0, MarqueeLogic::passCount(0, 540));
    TEST_ASSERT_EQUAL_UINT32(0, MarqueeLogic::passCount(539, 540));
}

// =============================================================================
// 8. test_pass_count_increments_after_one_full_cycle
// =============================================================================
void test_pass_count_increments_after_one_full_cycle() {
    TEST_ASSERT_EQUAL_UINT32(1, MarqueeLogic::passCount(540, 540));
    TEST_ASSERT_EQUAL_UINT32(2, MarqueeLogic::passCount(1080, 540));
}

// =============================================================================
// 9. test_first_visible_char_zero_before_text_reaches_left_edge
// =============================================================================
void test_first_visible_char_zero_before_text_reaches_left_edge() {
    TEST_ASSERT_EQUAL_UINT32(0, MarqueeLogic::firstVisibleChar(0,   240, 540, 18));
    TEST_ASSERT_EQUAL_UINT32(0, MarqueeLogic::firstVisibleChar(240, 240, 540, 18));
}

// =============================================================================
// 10. test_first_visible_char_zero_at_exact_boundary
// phase == screenW+charW: el carácter 0 tiene su borde derecho exactamente
// en x=0 -> todavía "potencialmente visible" (borde inclusivo, ver .h).
// =============================================================================
void test_first_visible_char_zero_at_exact_boundary() {
    TEST_ASSERT_EQUAL_UINT32(0, MarqueeLogic::firstVisibleChar(258, 240, 540, 18));
}

// =============================================================================
// 11. test_first_visible_char_increments_just_past_boundary
// =============================================================================
void test_first_visible_char_increments_just_past_boundary() {
    TEST_ASSERT_EQUAL_UINT32(1, MarqueeLogic::firstVisibleChar(259, 240, 540, 18));
    TEST_ASSERT_EQUAL_UINT32(1, MarqueeLogic::firstVisibleChar(276, 240, 540, 18));
    TEST_ASSERT_EQUAL_UINT32(2, MarqueeLogic::firstVisibleChar(277, 240, 540, 18));
}

// =============================================================================
// 12. test_first_visible_char_guards_zero_cycle_and_zero_charw
// Geometría inválida (cycle=0 o charW=0) no debe dividir por cero.
// =============================================================================
void test_first_visible_char_guards_zero_cycle_and_zero_charw() {
    TEST_ASSERT_EQUAL_UINT32(0, MarqueeLogic::firstVisibleChar(100, 240, 0,   18));
    TEST_ASSERT_EQUAL_UINT32(0, MarqueeLogic::firstVisibleChar(100, 240, 540, 0));
}

// =============================================================================
// 13. test_no_overflow_with_large_text_width_and_large_offset
// textPxW en su máximo (65535) y un offset muy por delante de varios ciclos
// (500000, muy por encima de lo que se alcanzaría en 60s reales, pero la
// función pura no debe asumir ese límite): passCount/textX/firstVisibleChar
// deben seguir siendo exactos, sin overflow ni wraparound incorrecto.
// =============================================================================
void test_no_overflow_with_large_text_width_and_large_offset() {
    const uint32_t cycle = MarqueeLogic::cyclePx(65535, 240);  // 65775
    TEST_ASSERT_EQUAL_UINT32(65775, cycle);

    TEST_ASSERT_EQUAL_UINT32(7,      MarqueeLogic::passCount(500000, cycle));
    TEST_ASSERT_EQUAL_INT32(-39335,  MarqueeLogic::textX(500000, 240, cycle));
    TEST_ASSERT_EQUAL_UINT32(2185,   MarqueeLogic::firstVisibleChar(500000, 240, cycle, 18));
}

// =============================================================================
// main — punto de entrada Unity para env:native
// =============================================================================
int main() {
    UNITY_BEGIN();

    RUN_TEST(test_offset_px_zero_at_start);
    RUN_TEST(test_offset_px_linear_scaling);
    RUN_TEST(test_offset_px_no_overflow_at_60000ms_high_speed);
    RUN_TEST(test_cycle_px_is_text_width_plus_screen_width);
    RUN_TEST(test_text_x_at_offset_zero_equals_screen_width);
    RUN_TEST(test_text_x_decreases_as_offset_increases_and_wraps_on_full_cycle);
    RUN_TEST(test_pass_count_zero_before_first_cycle_completes);
    RUN_TEST(test_pass_count_increments_after_one_full_cycle);
    RUN_TEST(test_first_visible_char_zero_before_text_reaches_left_edge);
    RUN_TEST(test_first_visible_char_zero_at_exact_boundary);
    RUN_TEST(test_first_visible_char_increments_just_past_boundary);
    RUN_TEST(test_first_visible_char_guards_zero_cycle_and_zero_charw);
    RUN_TEST(test_no_overflow_with_large_text_width_and_large_offset);

    return UNITY_END();
}
