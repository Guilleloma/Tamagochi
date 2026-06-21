// =============================================================================
// BootSplashLogic.cpp — Implementación pura de bootSplashLogoY().
//
// Este archivo NO incluye Arduino_GFX_Library.h ni ninguna cabecera de hardware.
// Se compila en env:native (tests unitarios) y en env:esp32s3.
// =============================================================================

#include "BootSplashLogic.h"

// bootSplashLogoY — calcula la posición Y del logo para el frame dado.
//
// Fase 0 – CAÍDA (frames 0..kFallFrames):
//   Interpolación lineal de kYStart a kYFinal.
//   En frame 0  → kYStart (logo fuera de pantalla por arriba).
//   En frame kFallFrames → kYFinal (logo en el centro exacto).
//
// Fase 1 – REBOTE (frames kFallFrames+1..kFallFrames+kBounceFrames):
//   Parábola que arranca en kYFinal, baja hasta kYFinal+kBounceAmp en el
//   punto medio y vuelve a kYFinal al final de la fase.
//   t normalizado ∈ [0..1] dentro de la fase.
//   y(t) = kYFinal + 4 * kBounceAmp * t * (1 - t)
//   (parábola cuadrática simétrica, máximo en t=0.5)
//
// Fase 2 – HOLD (frames kFallFrames+kBounceFrames+1..total):
//   Logo quieto en kYFinal.
//
// Frames >= total: clamp a kYFinal.
int16_t bootSplashLogoY(int frame) {
    // Fase de caída: frames 0..kFallFrames (inclusive)
    if (frame <= kFallFrames) {
        // Interpolación lineal. kFallFrames pasos desde kYStart hasta kYFinal.
        // frame=0 → kYStart, frame=kFallFrames → kYFinal.
        int32_t y = static_cast<int32_t>(kYStart)
                    + (static_cast<int32_t>(kYFinal - kYStart) * frame) / kFallFrames;
        return static_cast<int16_t>(y);
    }

    // Fase de rebote: frames kFallFrames+1..kFallFrames+kBounceFrames
    int bounceFrame = frame - kFallFrames;   // 1..kBounceFrames
    if (bounceFrame <= kBounceFrames) {
        // t ∈ (0..1]: dividir bounceFrame entre kBounceFrames
        // Usamos aritmética entera: 4 * kBounceAmp * t * (1-t)
        // = 4 * kBounceAmp * bounceFrame * (kBounceFrames - bounceFrame) / kBounceFrames^2
        int32_t num = 4L * kBounceAmp * bounceFrame * (kBounceFrames - bounceFrame);
        int32_t den = static_cast<int32_t>(kBounceFrames) * kBounceFrames;
        int32_t offset = num / den;
        return static_cast<int16_t>(kYFinal + offset);
    }

    // Fase de hold + clamp
    return kYFinal;
}
