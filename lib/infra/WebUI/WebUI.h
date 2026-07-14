#pragma once
// =============================================================================
// WebUI — servidor HTTP AsyncWebServer para la REST API del Tamagochi.
//
// Endpoints (hito 5):
//   GET  /api/v1/state        → estado completo del Tamagochi (stats + stage + age)
//                                + batteryPercent/batteryCharging/batteryValid
//   GET  /api/v1/info         → telemetría del dispositivo (firmware, heap, RSSI)
//                                + battery* (igual que /state) + batteryMillivolts
//                                + resetReason/bootCount/minFreeHeap (hito 9 —
//                                  bootCount/minFreeHeap siempre 0 en ESP8266,
//                                  ver JsonCodec.h)
//
// Endpoints (hito 6 — acciones de juego):
//   POST /api/v1/feed         → body: {"amount":1-100}  publica ActionFeedRequested
//   POST /api/v1/play         → body: {"amount":1-100}  publica ActionPlayRequested
//   POST /api/v1/discipline   → body: {"amount":1-100}  publica ActionDisciplineRequested
//   POST /api/v1/sleep        → body: {"value":true|false} publica ActionSleepRequested
//   POST /api/v1/clean        → sin body                publica ActionCleanRequested
//   POST /api/v1/medicate     → sin body                publica ActionMedicateRequested
//
// Endpoint de sistema:
//   POST /api/v1/reboot       → sin body. Persiste el estado (síncrono, ANTES
//                               de responder) y reinicia el ESP ~750ms después
//                               de enviar la respuesta 200 (ver rebootAtMs_ y
//                               loop() — ESP.restart() nunca se llama dentro
//                               del handler porque cortaría la respuesta HTTP,
//                               que corre en la task async_tcp).
//
// Endpoint de mensaje del padre (marquee, hito 8):
//   POST /api/v1/message      → body: {"text":"..."} (UTF-8, máx
//                               MessageBox::kMaxRawLen bytes crudos). Sin
//                               acuse de recibo — 200 al aceptar, el
//                               Tamagotchi lo muestra async en su pantalla.
//                               Solo se monta si messageBox != nullptr (ver
//                               6º parámetro del ctor) — permite que ESP8266
//                               (src/main.cpp) siga sin este endpoint sin
//                               tocar su código, pasando el default nullptr.
//
// Patrón de acción: WebUI NO llama directo a StatsEngine. Publica evento al bus
// (síncrono); main.cpp suscribe handlers que aplican la acción y persisten.
// Como el bus es síncrono, cuando publish() retorna, las stats ya están actualizadas.
//
// Seguridad:
//   - HTTP Basic Auth en todos los endpoints /api/v1/* (AuthChecker)
//   - Rate limiting token-bucket por IP (RateLimiter, 10 tokens, 1/600ms)
//
// Ciclo de vida:
//   - begin() solo cuando WiFi.Connected. Idempotente (no-op si ya running).
//   - end() libera el servidor.
//   - loop() es un hook para el futuro; AsyncWebServer es event-driven.
//
// Arduino-only: depende de ESPAsyncWebServer. NO compilar en env:native.
// =============================================================================

#include <stdint.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include "RateLimiter.h"
// BatteryGauge (lib/core) es lógica pura — sin Arduino.h, compila igual en
// env:native, ESP8266 y ESP32. A propósito WebUI NO incluye BatteryMonitor.h
// (infra ESP32-only: analogSetPinAttenuation vía Arduino.h) — ver comentario
// del 5º parámetro del ctor más abajo sobre por qué este desacople importa.
#include "BatteryGauge.h"

class EventBus;
class StatsEngine;
class LifeFSM;
class WifiManager;
class MessageBox;

class WebUI {
public:
    // battery: puntero NULLABLE al BatteryGauge puro (no BatteryMonitor). Esto
    // desacopla WebUI de la infra específica de ESP32 — WebUI se compila igual
    // en ESP8266 (que nunca tendrá medidor de batería) pasando nullptr, y en
    // ESP32-S3 pasando &battery.gauge(). Con nullptr, los handlers usan un
    // BatterySnapshot{} vacío (valid:false) — mismo contrato de API que un
    // BatteryMonitor real sin hardware soldado (ver sendActionResponse_ y los
    // handlers GET en el .cpp).
    //
    // messageBox: puntero NULLABLE al MessageBox (lib/core, puro — igual
    // desacople que battery). Con nullptr (default), mountRoutes_() NO monta
    // /api/v1/message — así src/main.cpp (ESP8266) sigue compilando y
    // corriendo sin cambios, sin este endpoint.
    WebUI(EventBus& bus, const StatsEngine& engine, const LifeFSM& fsm,
          const WifiManager& wifi, const BatteryGauge* battery = nullptr,
          MessageBox* messageBox = nullptr);

    // Arranca el servidor HTTP en el puerto 80. Idempotente.
    // Llamar solo cuando WiFi.state() == Connected.
    void begin();

    // Para el servidor HTTP. Idempotente.
    void end();

    bool isRunning() const { return running_; }

    // Hook para el loop principal. AsyncWebServer es event-driven — se usa
    // para el reinicio diferido tras POST /api/v1/reboot (ver rebootAtMs_).
    void loop();

private:
    EventBus&             bus_;
    const StatsEngine&    engine_;
    const LifeFSM&        fsm_;
    const WifiManager&    wifi_;
    const BatteryGauge*   battery_;     // nullable — ver comentario del ctor
    MessageBox*           messageBox_;  // nullable — ver comentario del ctor
    AsyncWebServer        server_;
    RateLimiter        rateLimiter_;
    bool               running_ = false;

    // Instante (millis()) en el que loop() debe llamar a ESP.restart(), o 0 si
    // no hay reinicio programado. Se fija en handlePostReboot_ tras responder
    // 200 al cliente — NUNCA se llama ESP.restart() dentro del handler HTTP
    // (correría en la task async_tcp y cortaría la propia respuesta a mitad
    // de envío). 0 es seguro como centinela de "sin programar": millis()==0
    // solo ocurre en el primer instante de boot, muy antes de que WebUI exista.
    uint32_t rebootAtMs_ = 0;

    // Monta las rutas en el servidor.
    void mountRoutes_();

    // Handlers de endpoints GET.
    void handleGetState_(AsyncWebServerRequest* req);
    void handleGetInfo_(AsyncWebServerRequest* req);
    void handleNotFound_(AsyncWebServerRequest* req);

    // Handlers de endpoints POST — acciones de juego (hito 6).
    // Los de amount usan AsyncCallbackJsonWebHandler (body JSON).
    // Los de no-body (clean, medicate) usan server_.on() con HTTP_POST directo.
    void handlePostFeed_(AsyncWebServerRequest* req, JsonVariant& json);
    void handlePostPlay_(AsyncWebServerRequest* req, JsonVariant& json);
    void handlePostDiscipline_(AsyncWebServerRequest* req, JsonVariant& json);
    void handlePostSleep_(AsyncWebServerRequest* req, JsonVariant& json);
    void handlePostClean_(AsyncWebServerRequest* req);
    void handlePostMedicate_(AsyncWebServerRequest* req);

    // POST /api/v1/reboot — sin body. Persiste (síncrono) y programa el
    // reinicio para ~750ms después de responder (ver rebootAtMs_ y loop()).
    void handlePostReboot_(AsyncWebServerRequest* req);

    // POST /api/v1/message — body: {"text":"..."}. Sanitiza y encola en
    // messageBox_ (productor SPSC — ver MessageBox.h). Solo se monta si
    // messageBox_ != nullptr (ver mountRoutes_ y el ctor).
    void handlePostMessage_(AsyncWebServerRequest* req, JsonVariant& json);

    // Serializa y envía la respuesta estándar de acción:
    //   { "data": <state completo>, "meta": {...}, "actionApplied": {"type":..., "amount":...} }
    // actionValue < 0 → no se incluye "amount" (clean/medicate).
    void sendActionResponse_(AsyncWebServerRequest* req,
                             const char* actionType, int32_t actionValue);

    // Extrae y valida "amount" (int 1-100) de un JsonVariant.
    // Si falla, envía 422 y devuelve false. Si OK, escribe en outAmount y devuelve true.
    bool extractAmount_(AsyncWebServerRequest* req, JsonVariant& json, int& outAmount);

    // Aplica auth + rate limit. Si falla, envía 401/429 y devuelve false.
    // Si OK, devuelve true y el caller procesa la request.
    bool checkAuthAndRate_(AsyncWebServerRequest* req);

    // Serializa y envía una respuesta de error JSON estándar (api-conventions.md).
    void sendJsonError_(AsyncWebServerRequest* req, int code,
                        const char* errorCode, const char* message);

    // Snapshot de batería para los handlers — {valid:false,...} si battery_
    // es nullptr (ESP8266, o ESP32 con BATTERY_SENSE_ENABLED=0). Centraliza el
    // null-check en un solo sitio en vez de repetirlo en cada handler.
    BatterySnapshot batterySnapshot_() const;

    // Diagnóstico de batería sin gatear — 0 si battery_ es nullptr. Usados
    // solo en handleGetInfo_ (ver JsonCodec::serializeInfo). Mismo criterio de
    // "0 == sin sensor" que batterySnapshot_(), pero estos dos SIEMPRE reflejan
    // la lectura real (no se gatean por valid_).
    uint16_t batteryRawMillivolts_() const;
    uint8_t  batteryGateProgress_() const;
};
