// =============================================================================
// test_message_box.cpp — tests para MessageBox y TextSanitizer::utf8ToAscii.
//
// Framework: Unity (pio test -e native).
// MessageBox y TextSanitizer no dependen de Arduino.h — se ejercitan
// directamente en host.
//
// Convención: g_box se recrea en cada test (setUp/tearDown), igual que
// test_battery_gauge.cpp — evita estado cruzado entre tests (rules/testing.md).
// =============================================================================

#include <unity.h>
#include <string.h>
#include "MessageBox.h"

static MessageBox* g_box = nullptr;

void setUp() {
    g_box = new MessageBox();
}

void tearDown() {
    delete g_box;
    g_box = nullptr;
}

// =============================================================================
// 1. test_post_consume_roundtrip
// Un post válido debe poder consumirse íntegro.
// =============================================================================
void test_post_consume_roundtrip() {
    TEST_ASSERT_EQUAL(MessageBox::PostResult::Ok, g_box->post("Hola mundo"));

    char out[MessageBox::kMaxLen + 1];
    TEST_ASSERT_TRUE(g_box->consume(out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("Hola mundo", out);
}

// =============================================================================
// 2. test_has_pending_false_after_consume
// =============================================================================
void test_has_pending_false_after_consume() {
    g_box->post("Es hora de comer");
    TEST_ASSERT_TRUE(g_box->hasPending());

    char out[MessageBox::kMaxLen + 1];
    TEST_ASSERT_TRUE(g_box->consume(out, sizeof(out)));
    TEST_ASSERT_FALSE(g_box->hasPending());
}

// =============================================================================
// 3. test_consume_without_pending_returns_false
// =============================================================================
void test_consume_without_pending_returns_false() {
    char out[MessageBox::kMaxLen + 1];
    TEST_ASSERT_FALSE(g_box->consume(out, sizeof(out)));
}

// =============================================================================
// 4. test_last_wins_when_two_posts_before_consume
// Dos posts sin consumir entre medias: consume() debe devolver el segundo.
// =============================================================================
void test_last_wins_when_two_posts_before_consume() {
    TEST_ASSERT_EQUAL(MessageBox::PostResult::Ok, g_box->post("Primero"));
    TEST_ASSERT_EQUAL(MessageBox::PostResult::Ok, g_box->post("Segundo"));

    char out[MessageBox::kMaxLen + 1];
    TEST_ASSERT_TRUE(g_box->consume(out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("Segundo", out);
}

// =============================================================================
// 5. test_post_empty_for_blank_inputs
// "", "   " y un string solo de emoji sanitizan a vacío -> PostResult::Empty.
// =============================================================================
void test_post_empty_for_blank_inputs() {
    TEST_ASSERT_EQUAL(MessageBox::PostResult::Empty, g_box->post(""));
    TEST_ASSERT_EQUAL(MessageBox::PostResult::Empty, g_box->post("   "));
    // U+1F600 (cara sonriente), 4 bytes UTF-8: F0 9F 98 80 — repetido dos veces.
    TEST_ASSERT_EQUAL(MessageBox::PostResult::Empty,
                      g_box->post("\xF0\x9F\x98\x80\xF0\x9F\x98\x80"));
}

// =============================================================================
// 6. test_post_too_long_for_96_sanitized_chars
// 100 chars ASCII (sanitiza 1:1) -> supera kMaxLen (96) -> TooLong.
// =============================================================================
void test_post_too_long_for_96_sanitized_chars() {
    char longText[101];
    memset(longText, 'a', 100);
    longText[100] = '\0';
    TEST_ASSERT_EQUAL(MessageBox::PostResult::TooLong, g_box->post(longText));
}

// =============================================================================
// 7. test_post_too_long_for_192_raw_bytes
// 200 bytes crudos superan kMaxRawLen (192) -> TooLong, sin llegar a sanitizar.
// =============================================================================
void test_post_too_long_for_192_raw_bytes() {
    char longText[201];
    memset(longText, 'b', 200);
    longText[200] = '\0';
    TEST_ASSERT_EQUAL(MessageBox::PostResult::TooLong, g_box->post(longText));
}

// =============================================================================
// 8. test_sanitizer_transliterates_latin1_accents
// =============================================================================
void test_sanitizer_transliterates_latin1_accents() {
    char out[64];
    size_t n = TextSanitizer::utf8ToAscii("áéíóúñÑüÜçÇ", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("aeiounNuUcC", out);
    TEST_ASSERT_EQUAL(11, n);
}

// =============================================================================
// 9. test_sanitizer_discards_inverted_punctuation
// ¿ y ¡ se descartan sin dejar rastro (no se sustituyen por espacio ni nada).
// =============================================================================
void test_sanitizer_discards_inverted_punctuation() {
    char out[64];
    size_t n = TextSanitizer::utf8ToAscii("¿Vamos?", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Vamos?", out);
    TEST_ASSERT_EQUAL(6, n);
}

// =============================================================================
// 10. test_sanitizer_leaves_plain_ascii_intact
// =============================================================================
void test_sanitizer_leaves_plain_ascii_intact() {
    char out[64];
    TextSanitizer::utf8ToAscii("Hello, World! 123", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Hello, World! 123", out);
}

// =============================================================================
// 11. test_sanitizer_truncated_or_invalid_utf8_does_not_crash
// Bytes de cabecera UTF-8 sin sus continuaciones (string cortado a mitad de
// secuencia) o bytes de continuación sueltos no deben crashear ni colgarse.
// =============================================================================
void test_sanitizer_truncated_or_invalid_utf8_does_not_crash() {
    char out[64];

    // 0xC3 (cabecera de 2 bytes) sin byte de continuación -> string cortado.
    size_t n1 = TextSanitizer::utf8ToAscii("Hola\xC3", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Hola", out);
    TEST_ASSERT_EQUAL(4, n1);

    // 0x80 (continuation byte suelto, sin cabecera previa) -> se descarta.
    size_t n2 = TextSanitizer::utf8ToAscii("A\x80" "B", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("AB", out);
    TEST_ASSERT_EQUAL(2, n2);

    // 0xE0 (cabecera de 3 bytes) truncada a 1 byte de continuación real.
    size_t n3 = TextSanitizer::utf8ToAscii("X\xE0\x80" "Y", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("XY", out);
    TEST_ASSERT_EQUAL(2, n3);
}

// =============================================================================
// 12. test_sanitizer_controls_become_space
// =============================================================================
void test_sanitizer_controls_become_space() {
    char out[64];
    // \t (0x09) y \n (0x0A) entre palabras -> espacio.
    size_t n = TextSanitizer::utf8ToAscii("Hola\tmundo\ncruel", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Hola mundo cruel", out);
    TEST_ASSERT_EQUAL(16, n);
}

// =============================================================================
// 13. test_sanitizer_trims_and_collapses_spaces
// =============================================================================
void test_sanitizer_trims_and_collapses_spaces() {
    char out[64];
    size_t n = TextSanitizer::utf8ToAscii("   Hola    mundo   ", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Hola mundo", out);
    TEST_ASSERT_EQUAL(10, n);
}

// =============================================================================
// main — punto de entrada Unity para env:native
// =============================================================================
int main() {
    UNITY_BEGIN();

    RUN_TEST(test_post_consume_roundtrip);
    RUN_TEST(test_has_pending_false_after_consume);
    RUN_TEST(test_consume_without_pending_returns_false);
    RUN_TEST(test_last_wins_when_two_posts_before_consume);
    RUN_TEST(test_post_empty_for_blank_inputs);
    RUN_TEST(test_post_too_long_for_96_sanitized_chars);
    RUN_TEST(test_post_too_long_for_192_raw_bytes);
    RUN_TEST(test_sanitizer_transliterates_latin1_accents);
    RUN_TEST(test_sanitizer_discards_inverted_punctuation);
    RUN_TEST(test_sanitizer_leaves_plain_ascii_intact);
    RUN_TEST(test_sanitizer_truncated_or_invalid_utf8_does_not_crash);
    RUN_TEST(test_sanitizer_controls_become_space);
    RUN_TEST(test_sanitizer_trims_and_collapses_spaces);

    return UNITY_END();
}
