#pragma once
// =============================================================================
// ConnectivityWatchdogLogic.h — lógica pura de decisión del watchdog de
// conectividad (¿debe reiniciar el chip por WiFi caído?).
//
// SIN dependencias de WiFi.h/Arduino.h ni millis() real — mismo espíritu que
// BootLoopGuardLogic.h: compilable en env:native para tests unitarios y en
// env:esp32s3/env:esp12e. El tiempo SIEMPRE se inyecta (parámetro `now`),
// nunca se lee del reloj real.
//
// -----------------------------------------------------------------------------
// POR QUÉ esta clase existe (bug de campo, ver plan de robustez de red):
//
// WifiManager ya exponía lastConnectedMs_ — el instante de la ÚLTIMA
// transición A Connected. Esa marca es correcta para lo que la usa hoy: la
// métrica de estabilidad del anti-boot-loop (BootLoopGuard::reportStableConnection(),
// ver main_esp32.cpp), que necesita saber "¿cuánto llevamos conectados AHORA
// MISMO sin cortes?".
//
// El bug: connectivityWatchdog_() reutilizaba esa MISMA marca para una
// pregunta completamente distinta — "¿cuánto lleva el WiFi CAÍDO?" — asumiendo
// que `now - lastConnectedMs_` medía tiempo sin conexión. Pero lastConnectedMs_
// NO se actualiza mientras el WiFi sigue conectado, así que en cuanto el
// uptime superaba WIFI_DEAD_RESTART_MS (5 min), esa resta ya daba un valor
// grande de forma permanente — el instante exacto en que el WiFi se caía
// (aunque fuera un microcorte de segundos), el watchdog veía "llevo minutos
// caído" y reiniciaba el chip de inmediato, saltándose por completo la
// recuperación en caliente de netSupervisor_().
//
// El fix: una marca de tiempo DISTINTA — "instante de la PRIMERA pérdida de
// conexión desde la última vez que estuvo arriba" — que solo existe/avanza
// mientras el WiFi está efectivamente caído, y se limpia en cuanto vuelve a
// Connected. Eso es exactamente lo que trackea esta clase (downSinceMs_).
// lastConnectedMs_ NO se toca — sigue viviendo en WifiManager con su
// semántica original, intacta.
// -----------------------------------------------------------------------------
//
// Uso (ver WifiManager::publishState_ y main_esp32.cpp::connectivityWatchdog_):
//   - onWifiStatus(connected, now) se llama en el único punto de transición de
//     estado de WifiManager, en CADA transición (evento o polling).
//   - shouldRestart(...) es la decisión pura consumida por el watchdog.
// =============================================================================

#include <stdint.h>

class ConnectivityWatchdogLogic {
public:
    ConnectivityWatchdogLogic() = default;

    // Reporta el estado de conexión observado en el instante `now`.
    //   connected == true  -> limpia el sello de caída (downSinceMs_ = 0) y
    //                         recuerda que la última observación fue "arriba".
    //   connected == false -> sella downSinceMs_ = now SOLO si la observación
    //                         INMEDIATAMENTE ANTERIOR fue "arriba" (flanco
    //                         Connected -> no-Connected) Y no había ya un
    //                         sello pendiente. Llamadas repetidas con
    //                         connected == false (reintentos de reconexión
    //                         fallidos: Connecting -> Disconnected, backoff,
    //                         Disconnected -> Connecting, ...) NO re-sellan —
    //                         downSinceMs_ se queda en el instante de la
    //                         PRIMERA pérdida de esta racha.
    //
    // Si nunca hubo una observación "arriba" (p.ej. el dispositivo arrancó
    // sin WiFi disponible desde el boot), downSinceMs_ nunca se sella —
    // coherente con que hasEverConnected() sea el gate real que usa
    // shouldRestart() para no reiniciar en ese escenario.
    //
    // Nota: 0 es el centinela de "no caído" (mismo convenio que
    // WifiManager::lastConnectedMs()) — si la caída ocurriera exactamente en
    // millis()==0 quedaría indistinguible de "no caído". Riesgo aceptado: solo
    // posible en el primer milisegundo tras el boot, ventana en la que
    // WifiManager todavía no ha llegado a Connected.
    void onWifiStatus(bool connected, uint32_t now);

    // true si hay una caída sellada pendiente de recuperación.
    bool isDown() const { return downSinceMs_ != 0; }

    // millis() de la primera pérdida de la racha de caída actual.
    // 0 si está conectado, o si nunca ha llegado a caer desde una conexión.
    uint32_t downSinceMs() const { return downSinceMs_; }

    // Decisión pura: ¿debe el watchdog forzar un reinicio?
    //   hasEverConnected: WifiManager::hasEverConnected() — sin conexión
    //                     previa, nunca reinicia (el router puede estar
    //                     simplemente apagado desde el boot).
    //   now             : millis() inyectado por el llamador.
    //   thresholdMs     : WIFI_DEAD_RESTART_MS en producción.
    bool shouldRestart(bool hasEverConnected, uint32_t now, uint32_t thresholdMs) const;

private:
    // 0 == no caído (conectado, o nunca cayó desde la última conexión).
    uint32_t downSinceMs_  = 0;
    // Última observación reportada a onWifiStatus() — permite detectar el
    // flanco Connected -> no-Connected sin que el llamador tenga que pasar
    // el estado previo explícitamente.
    bool     wasConnected_ = false;
};
