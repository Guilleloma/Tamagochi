// =============================================================================
// BootLoopGuard.cpp — implementación (ver BootLoopGuard.h para el diseño).
//
// Cumple rules/error-handling.md: cada decisión (reset limpio, incremento,
// modo degradado) se loguea con contexto.
// =============================================================================

#ifndef UNIT_TEST  // Guard: este archivo no se compila en env:native (Arduino-only)

#include "BootLoopGuard.h"

#include <Arduino.h>
#include <esp_system.h>
#include "config.h"
#include "Logger.h"

namespace {

// Sentinel para distinguir "memoria RTC con estado válido de un soft-reset
// previo" de "memoria RTC con basura" (primer flasheo, o el bloque
// .rtc_noinit nunca se inicializó todavía en esta sesión de alimentación).
constexpr uint32_t kBootLoopMagic = 0x544D4731UL;  // "TMG1" en ASCII, arbitrario

// RTC_NOINIT_ATTR: sección .rtc_noinit — sobrevive a ESP.restart() y deep
// sleep, se pierde en power-on reset / brownout (justo el comportamiento
// que se busca, ver comentario del .h). Variables de traducción interna:
// no exponer estado mutable directo fuera de este archivo.
RTC_NOINIT_ATTR uint32_t rtcMagic_;
RTC_NOINIT_ATTR uint32_t rtcCount_;

}  // namespace

namespace BootLoopGuard {

void begin() {
    esp_reset_reason_t reason = esp_reset_reason();

    // rtcMagic_ no coincide -> memoria RTC con basura (primer flasheo, o el
    // linker reusó la sección desde un binario anterior sin este módulo).
    // ESP_RST_POWERON / ESP_RST_BROWNOUT -> corte de corriente real, aunque
    // el magic coincidiera por casualidad, es una señal fuerte de reset limpio.
    if (rtcMagic_ != kBootLoopMagic ||
        reason == ESP_RST_POWERON ||
        reason == ESP_RST_BROWNOUT) {
        rtcMagic_ = kBootLoopMagic;
        rtcCount_ = 0;
        LOG_INFO("[boot-loop-guard] contador reseteado (reset_reason=%d)", (int)reason);
    } else {
        LOG_INFO("[boot-loop-guard] contador preservado tras soft-reset: %u/%u (reset_reason=%d)",
                  rtcCount_, (unsigned)BOOT_LOOP_MAX, (int)reason);
    }
}

bool allowRestart() {
    if (rtcCount_ >= BOOT_LOOP_MAX) {
        return false;
    }
    rtcCount_++;
    return true;
}

void reportStableConnection() {
    rtcCount_ = 0;
}

uint32_t count() {
    return rtcCount_;
}

}  // namespace BootLoopGuard

#endif  // UNIT_TEST
