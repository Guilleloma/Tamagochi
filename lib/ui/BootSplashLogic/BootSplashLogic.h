#pragma once
// =============================================================================
// BootSplashLogic.h — Constantes y lógica pura del boot splash.
//
// SIN dependencias de Arduino_GFX ni de ningún header de hardware.
// Compilable en env:native para tests unitarios.
//
// Para la función de render (playBootSplash) que sí usa GFX, ver BootSplash.h.
//
// TODO(guillermo): disparar ding con tone() si se añade buzzer pasivo a un GPIO
// =============================================================================

#include <stdint.h>

// ── Colores RGB565 ────────────────────────────────────────────────────────────
// COL_SPLASH_BG  : verde pálido DMG  (~#9CB890 → RGB565 0x9E8C)
// COL_SPLASH_LOGO: gris-azul oscuro  (~#306090 → RGB565 0x3186)
static constexpr uint16_t COL_SPLASH_BG   = 0x9E8C;
static constexpr uint16_t COL_SPLASH_LOGO = 0x3186;

// ── Geometría ─────────────────────────────────────────────────────────────────
// Logo fuente: 65 px ancho × 7 px alto.
// A escala kLogoScale=3: 195 × 21 px en pantalla.
static constexpr int16_t kLogoScale  = 3;
static constexpr int16_t kLogoSrcW  = 65;
static constexpr int16_t kLogoSrcH  = 7;

// ── Fases de animación ────────────────────────────────────────────────────────
// kFallFrames  : frames de caída lineal desde arriba hasta centro.
// kBounceFrames: frames de rebote parabólico tras llegar al centro.
// kHoldFrames  : frames de espera con el logo en reposo.
// kFrameDelayMs: ~30 fps.
static constexpr int16_t kFallFrames   = 20;
static constexpr int16_t kBounceFrames = 6;
static constexpr int16_t kHoldFrames   = 40;
static constexpr int16_t kFrameDelayMs = 33;

// Amplitud del overshoot de rebote (px, medida en coordenadas de pantalla).
static constexpr int16_t kBounceAmp = 7;

// ── Posición Y calculada por frame ───────────────────────────────────────────
// Pantalla 296 px de alto.
// kYStart  : logo completamente fuera de pantalla por arriba.
// kYFinal  : centro vertical de pantalla para el logo escalado.
//   kYFinal = (296 - 7*3) / 2 = 137
static constexpr int16_t kScreenH = 296;
static constexpr int16_t kYStart  = -(kLogoSrcH * kLogoScale);            // -21
static constexpr int16_t kYFinal  = (kScreenH - kLogoSrcH * kLogoScale) / 2; // 137

// bootSplashLogoY — posición Y del logo para el frame dado.
//
// frame 0..kFallFrames               : caída lineal kYStart → kYFinal
// frame kFallFrames+1..+kBounceFrames: rebote parabólico (overshoot y vuelta)
// frame > kFallFrames+kBounceFrames  : hold / clamp en kYFinal
int16_t bootSplashLogoY(int frame);
