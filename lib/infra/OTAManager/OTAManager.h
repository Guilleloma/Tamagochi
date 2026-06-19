#pragma once
// =============================================================================
// OTAManager — Actualización Over-The-Air vía ArduinoOTA (Puerto 8266).
//
// Activar SOLO cuando WiFi esté Connected. Si OTA_PASSWORD está vacío en
// secrets.h, begin() no arranca el servidor OTA y emite LOG_WARN.
//
// Callbacks internos traducen eventos OTA a EventBus:
//   OtaStarted   → renderer cambia a Scene::OtaProgress
//   OtaProgress  → payload & 0xFF = porcentaje 0-100
//   OtaEnded     → ESP reinicia automáticamente
//   OtaError     → payload = ota_error_t; renderer vuelve a IdleMain
//
// PATRÓN SINGLETON via instance_: ArduinoOTA solo acepta function pointers
// planos. instance_ permite a los callbacks estáticos acceder al bus_.
// Solo un OTAManager vivo a la vez.
//
// Coste de loop(): < 5 µs sin actualización en curso (ArduinoOTA.handle()).
//
// Dependencias externas: ArduinoOTA.h, ESP8266mDNS.h (Arduino-only).
// No testeable en env:native.
// =============================================================================

#include <stdint.h>
// OTAManager es un módulo Arduino-only (excluido de env:native en platformio.ini).
// Incluir ArduinoOTA.h aquí es seguro — este header nunca llega a env:native.
// arduino-esp32 y Arduino-ESP8266 ambos proveen <ArduinoOTA.h>.
#include <ArduinoOTA.h>

class EventBus;

class OTAManager {
public:
    explicit OTAManager(EventBus& bus);

    // Configura ArduinoOTA con hostname, password y callbacks.
    // Llamar SOLO tras WiFi conectado. Idempotente: si ya está ready, no-op.
    // Si OTA_PASSWORD está vacío → LOG_WARN y retorna sin arrancar.
    void begin();

    // Llama a ArduinoOTA.handle(). Coste < 5 µs sin actualización en curso.
    void loop();

    // Detiene OTA cuando WiFi se cae (ready_ = false).
    void end();

    bool isUpdating() const { return updating_; }
    bool isReady()    const { return ready_; }

private:
    EventBus& bus_;
    bool      ready_    = false;
    bool      updating_ = false;

    // Singleton: permite a los callbacks estáticos acceder al estado del objeto.
    // Solo un OTAManager puede existir a la vez (afirmado en constructor).
    static OTAManager* instance_;

    static void onStart_();
    static void onEnd_();
    static void onProgress_(unsigned int progress, unsigned int total);
    static void onError_(ota_error_t error);
};
