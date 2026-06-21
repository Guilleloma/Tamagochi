// =============================================================================
// test_boot_splash.cpp — Tests unitarios para bootSplashLogoY().
//
// Framework: Unity (pio test -e native).
// bootSplashLogoY() es lógica pura sin dependencias de hardware.
//
// Constantes verificadas:
//   kYStart      = -(kLogoSrcH * kLogoScale) = -(9 * 2) = -18
//   kYFinal      = (296 - 9 * 2) / 2 = 139
//   kFallFrames  = 20
//   kBounceFrames= 6
//   kHoldFrames  = 40
//   kBounceAmp   = 7
//   total frames = kFallFrames + kBounceFrames + kHoldFrames = 66
// =============================================================================

#include <unity.h>
#include "BootSplashLogic.h"

void setUp()    {}
void tearDown() {}

// =============================================================================
// 1. should start above screen
// En frame 0 el logo debe estar completamente fuera de pantalla por arriba.
// kYStart = -(kLogoSrcH * kLogoScale) = -21.
// =============================================================================
void should_start_above_screen() {
    int16_t y = bootSplashLogoY(0);
    TEST_ASSERT_EQUAL_INT16(kYStart, y);
    TEST_ASSERT_LESS_THAN_INT16(0, y);   // confirmación: fuera de pantalla
}

// =============================================================================
// 2. should reach vertical center after fall
// En frame kFallFrames el logo debe estar exactamente en kYFinal.
// =============================================================================
void should_reach_vertical_center_after_fall() {
    int16_t y = bootSplashLogoY(kFallFrames);
    TEST_ASSERT_EQUAL_INT16(kYFinal, y);
}

// =============================================================================
// 3. should overshoot below center during bounce
// Durante la fase de rebote (frames kFallFrames+1 .. kFallFrames+kBounceFrames)
// debe existir al menos un frame con y > kYFinal (overshoot hacia abajo).
// =============================================================================
void should_overshoot_below_center_during_bounce() {
    bool foundOvershoot = false;
    for (int f = kFallFrames + 1; f <= kFallFrames + kBounceFrames; f++) {
        if (bootSplashLogoY(f) > kYFinal) {
            foundOvershoot = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(foundOvershoot,
        "Ningún frame de rebote supera kYFinal (overshoot esperado)");
}

// =============================================================================
// 4. should hold at center at end
// Al llegar al último frame (total = kFallFrames+kBounceFrames+kHoldFrames)
// y más allá (clamp), el logo debe estar en kYFinal.
// =============================================================================
void should_hold_at_center_at_end() {
    const int totalFrames = kFallFrames + kBounceFrames + kHoldFrames;
    // Último frame del hold
    TEST_ASSERT_EQUAL_INT16(kYFinal, bootSplashLogoY(totalFrames));
    // Clamp: frame fuera de rango
    TEST_ASSERT_EQUAL_INT16(kYFinal, bootSplashLogoY(totalFrames + 100));
}

// =============================================================================
// 5. should be monotonic non-decreasing during fall
// Durante la fase de caída (frames 0..kFallFrames), la posición Y debe ser
// no decreciente (el logo solo cae, nunca sube).
// =============================================================================
void should_be_monotonic_non_decreasing_during_fall() {
    for (int f = 1; f <= kFallFrames; f++) {
        int16_t prev = bootSplashLogoY(f - 1);
        int16_t curr = bootSplashLogoY(f);
        TEST_ASSERT_GREATER_OR_EQUAL_INT16_MESSAGE(
            prev, curr,
            "La posición Y retrocedió durante la fase de caída");
    }
}

// =============================================================================
// main — punto de entrada Unity
// =============================================================================
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(should_start_above_screen);
    RUN_TEST(should_reach_vertical_center_after_fall);
    RUN_TEST(should_overshoot_below_center_during_bounce);
    RUN_TEST(should_hold_at_center_at_end);
    RUN_TEST(should_be_monotonic_non_decreasing_during_fall);

    return UNITY_END();
}
