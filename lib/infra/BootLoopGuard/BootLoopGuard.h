#pragma once
// =============================================================================
// BootLoopGuard — anti-boot-loop para el watchdog de conectividad (Fase 5,
// hito 9). Ver plan de robustez de red, commit 3.
//
// Problema que resuelve: si el WiFi lleva caído demasiado tiempo habiendo
// estado conectado antes (ver WifiManager::hasEverConnected()/lastConnectedMs()),
// main_esp32.cpp reinicia el dispositivo para recuperar estados irrecuperables
// del stack de red. Sin guarda, un router realmente apagado (no un fallo de
// firmware) provocaría un ciclo infinito de reinicios cada WIFI_DEAD_RESTART_MS.
//
// Mecanismo: contador en RTC_NOINIT_ATTR — memoria RTC que SOBREVIVE a
// ESP.restart() (soft reset) pero se pierde al desconectar la alimentación
// (power-on reset) o brownout. Eso es exactamente lo que se necesita: un
// corte de corriente real es una señal fuerte de "el usuario intervino", así
// que el contador se resetea limpio en ese caso.
//
// No hay ventana de tiempo persistida entre reinicios (millis() se reinicia
// a 0 en cada boot, no sirve para medir una ventana wall-clock cruzando
// reinicios sin NTP). En su lugar, el contador se resetea a 0 en cuanto la
// conexión se mantiene estable BOOT_LOOP_RESET_STABLE_MS (ver
// reportStableConnection()) — más simple y igual de efectivo: si el router
// vuelve a funcionar, el contador se limpia solo; si no, se agota en
// BOOT_LOOP_MAX intentos y el dispositivo entra en modo degradado (sigue
// jugando offline, deja de intentar reiniciar) hasta el próximo power-on.
//
// bootLoopCount() también se expone para telemetría (GET /api/v1/info,
// ver WebUI::handleGetInfo_ / commit 4) como bootCount.
//
// Arduino-ESP32 only (RTC_NOINIT_ATTR, esp_reset_reason()). No testeable en
// env:native — ver guard #ifndef UNIT_TEST en el .cpp.
//
// La decisión de negocio pura (contador + comparación contra BOOT_LOOP_MAX)
// vive en BootLoopGuardLogic (lib/infra/BootLoopGuardLogic), SIN RTC_NOINIT_ATTR
// ni esp_reset_reason() — por eso sí es testeable en env:native. Este wrapper
// solo añade la persistencia RTC y la detección cold-boot vs soft-reset,
// delegando la decisión para no duplicarla. Ver test/test_bootloop_guard/.
// =============================================================================

#include <stdint.h>

namespace BootLoopGuard {

// Debe llamarse UNA VEZ al inicio de setup(), antes de cualquier lógica que
// dependa del contador. Detecta arranque en frío (power-on / brownout, o
// memoria RTC con basura tras el primer flasheo) y resetea el contador a 0;
// en un soft-reset (ESP.restart()) lo preserva.
void begin();

// true si aún no se alcanzó BOOT_LOOP_MAX -- el llamador puede proceder con
// ESP.restart() con seguridad (incrementa el contador internamente).
// false = modo degradado: NO reiniciar (el router puede estar realmente
// apagado; reiniciar no ayudaría y arriesgaría un ciclo infinito).
bool allowRestart();

// Llamar cuando la conexión WiFi lleva BOOT_LOOP_RESET_STABLE_MS estable —
// restaura la confianza y resetea el contador a 0. Idempotente (llamarlo
// repetidamente mientras la conexión sigue estable es un no-op seguro).
void reportStableConnection();

// Reinicios "por WiFi caído" acumulados desde el último power-on-reset.
// Expuesto para telemetría (ver JsonCodec::serializeInfo -> bootCount).
uint32_t count();

}  // namespace BootLoopGuard
