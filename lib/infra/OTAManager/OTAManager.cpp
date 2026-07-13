// =============================================================================
// OTAManager — implementación Fase 3 hito 2.
//
// Password: se usa setPassword(plain). ArduinoOTA hashea internamente a MD5.
// El plain queda en el binario, igual que WEB_AUTH_PASSWORD — asumido
// comprometido si el atacante tiene el binario (secrets.h gitignored mitiga esto).
// Ver plan Fase 3 §8.2.
//
// LittleFS NO se toca durante OTA flash (solo segmento del firmware).
// El estado persistido (PersistedState v2) sobrevive a la actualización.
// =============================================================================

#ifndef UNIT_TEST  // Guard: este archivo no se compila en env:native (Arduino-only)

#include "OTAManager.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include "EventBus.h"
#include "Logger.h"
#include "secrets.h"

// Definición del puntero singleton — nullptr hasta que se construya el objeto.
OTAManager* OTAManager::instance_ = nullptr;

OTAManager::OTAManager(EventBus& bus)
    : bus_(bus)
{
    // Solo un OTAManager a la vez — asegurado en tiempo de ejecución.
    instance_ = this;
}

// -----------------------------------------------------------------------------
// begin — configura ArduinoOTA y arranca el servidor.
// Llamar solo cuando WiFi esté Connected.
// -----------------------------------------------------------------------------
void OTAManager::begin() {
    if (ready_) return;  // idempotente

    // Password vacío → OTA deshabilitada (por diseño, no error crítico).
    // Ver secrets.h.example — el comentario advierte de este comportamiento.
    if (strlen(OTA_PASSWORD) == 0) {
        LOG_WARN("[ota] OTA disabled (empty password in secrets.h)");
        return;
    }

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);  // hashea a MD5 internamente
    // NO fijar setPort() — bug de porting: este código viene de la Fase 2
    // ESP8266 con `setPort(8266)` hardcodeado. Al portar a ESP32-S3 (Fase 5)
    // ese valor quedó arrastrado, pero espota/ArduinoOTA en ESP32 sondea por
    // defecto el puerto 3232, no 8266 — el dispositivo escuchaba en el puerto
    // equivocado y el flasheo remoto nunca recibía respuesta. Dejar setPort()
    // sin llamar hace que cada plataforma use su propio default correcto:
    // ESP32→3232, ESP8266→8266 (así env:esp12e_ota sigue funcionando).

    // Callbacks: function pointers estáticos que delegan en instance_->bus_.
    ArduinoOTA.onStart(onStart_);
    ArduinoOTA.onEnd(onEnd_);
    ArduinoOTA.onProgress(onProgress_);
    ArduinoOTA.onError(onError_);

    ArduinoOTA.begin();
    ready_ = true;

    LOG_INFO("[ota] ready hostname=%s.local", OTA_HOSTNAME);
}

// -----------------------------------------------------------------------------
// loop — sondea el servidor OTA. Coste < 5 µs sin actualización en curso.
// -----------------------------------------------------------------------------
void OTAManager::loop() {
    if (!ready_) return;
    ArduinoOTA.handle();
}

// -----------------------------------------------------------------------------
// end — detiene OTA cuando WiFi se cae.
// ESP32: ArduinoOTA.end() existe y detiene el servidor mDNS-SD + UDP socket.
// ESP8266: ArduinoOTA no expone end() en la API pública; marcar ready_=false
//          es suficiente para que loop() deje de llamar a handle().
// -----------------------------------------------------------------------------
void OTAManager::end() {
    if (!ready_) return;
#if defined(ARDUINO_ARCH_ESP32)
    ArduinoOTA.end();
#endif
    ready_    = false;
    updating_ = false;
    LOG_INFO("[ota] stopped (WiFi down)");
}

// =============================================================================
// Callbacks estáticos — acceden al objeto a través de instance_.
// =============================================================================

void OTAManager::onStart_() {
    if (!instance_) return;
    instance_->updating_ = true;
    instance_->bus_.publish({EventType::OtaStarted, 0});
    LOG_INFO("[ota] update started");
}

void OTAManager::onEnd_() {
    if (!instance_) return;
    instance_->updating_ = false;
    instance_->bus_.publish({EventType::OtaEnded, 0});
    LOG_INFO("[ota] update ended — rebooting");
    // El ESP8266 reinicia automáticamente tras onEnd(). No restauramos scene.
}

void OTAManager::onProgress_(unsigned int progress, unsigned int total) {
    if (!instance_) return;
    // Evitar división por cero (no debería ocurrir, pero total puede ser 0 al inicio).
    if (total == 0) return;
    uint8_t pct = static_cast<uint8_t>((static_cast<uint32_t>(progress) * 100u) / total);
    instance_->bus_.publish({EventType::OtaProgress, static_cast<int32_t>(pct)});
}

void OTAManager::onError_(ota_error_t error) {
    if (!instance_) return;
    instance_->updating_ = false;
    instance_->bus_.publish({EventType::OtaError, static_cast<int32_t>(error)});
    LOG_ERROR("[ota] error code=%u", static_cast<unsigned>(error));
}

#endif  // UNIT_TEST
