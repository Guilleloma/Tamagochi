// =============================================================================
// BootLoopGuard.cpp — implementación (ver BootLoopGuard.h para el diseño).
//
// Cumple rules/error-handling.md: cada decisión (reset limpio, incremento,
// modo degradado) se loguea con contexto.
//
// La decisión (¿se agotó el tope de reinicios?) se delega a BootLoopGuardLogic
// para no duplicarla — ver BootLoopGuardLogic.h. Este archivo solo aporta la
// persistencia RTC_NOINIT_ATTR y la detección cold-boot vs soft-reset.
// =============================================================================

#ifndef UNIT_TEST  // Guard: este archivo no se compila en env:native (Arduino-only)

#include "BootLoopGuard.h"

#include <Arduino.h>
#include <esp_system.h>
#include "config.h"
#include "Logger.h"
#include "BootLoopGuardLogic.h"

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

// Lógica pura de decisión. Vive en RAM normal (se reinicializa en cada boot,
// a diferencia de rtcCount_) — se resincroniza con rtcCount_ en begin() y se
// escribe de vuelta a rtcCount_ tras cada mutación, para que el contador siga
// sobreviviendo al soft-reset exactamente igual que antes de este refactor.
BootLoopGuardLogic logic_;

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

    logic_.reset(BOOT_LOOP_MAX, rtcCount_);
}

bool allowRestart() {
    bool allowed = logic_.allowRestart();
    rtcCount_ = logic_.count();  // persistir el nuevo contador en RTC_NOINIT_ATTR
    return allowed;
}

void reportStableConnection() {
    logic_.reportStableConnection();
    rtcCount_ = logic_.count();
}

uint32_t count() {
    return logic_.count();
}

}  // namespace BootLoopGuard

#endif  // UNIT_TEST
