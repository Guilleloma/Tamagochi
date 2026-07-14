// =============================================================================
// ConnectivityWatchdogLogic.cpp — implementación de la lógica pura de
// decisión del watchdog de conectividad. Ver ConnectivityWatchdogLogic.h para
// el diseño y el porqué de esta marca de tiempo separada de lastConnectedMs_.
// Sin dependencias de hardware: compila en env:native (tests) y en
// env:esp32s3/env:esp12e.
// =============================================================================

#include "ConnectivityWatchdogLogic.h"

void ConnectivityWatchdogLogic::onWifiStatus(bool connected, uint32_t now) {
    if (connected) {
        downSinceMs_  = 0;
        wasConnected_ = true;
        return;
    }

    // Solo sella en el flanco Connected -> no-Connected (primera pérdida de
    // la racha). Si downSinceMs_ ya está sellado, o si la observación previa
    // ya era "no conectado" (reintento fallido / nunca hubo conexión previa),
    // esto es un no-op: no re-sellar, no acumular.
    if (wasConnected_ && downSinceMs_ == 0) {
        downSinceMs_ = now;
    }
    wasConnected_ = false;
}

bool ConnectivityWatchdogLogic::shouldRestart(bool hasEverConnected, uint32_t now,
                                               uint32_t thresholdMs) const {
    if (!hasEverConnected) return false;
    if (downSinceMs_ == 0) return false;  // conectado, o nunca ha caído
    return (now - downSinceMs_) > thresholdMs;
}
