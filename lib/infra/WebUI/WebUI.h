#pragma once
// =============================================================================
// WebUI — servidor HTTP AsyncWebServer para la REST API del Tamagochi.
//
// Endpoints (hito 5):
//   GET  /api/v1/state        → estado completo del Tamagochi (stats + stage + age)
//                                + batteryPercent/batteryCharging/batteryValid
//   GET  /api/v1/info         → telemetría del dispositivo (firmware, heap, RSSI)
//                                + battery* (igual que /state) + batteryMillivolts
//
// Endpoints (hito 6 — acciones de juego):
//   POST /api/v1/feed         → body: {"amount":1-100}  publica ActionFeedRequested
//   POST /api/v1/play         → body: {"amount":1-100}  publica ActionPlayRequested
//   POST /api/v1/heal         → body: {"amount":1-100}  publica ActionHealRequested
//   POST /api/v1/discipline   → body: {"amount":1-100}  publica ActionDisciplineRequested
//   POST /api/v1/sleep        → body: {"value":true|false} publica ActionSleepRequested
//   POST /api/v1/clean        → sin body                publica ActionCleanRequested
//   POST /api/v1/medicate     → sin body                publica ActionMedicateRequested
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

class EventBus;
class StatsEngine;
class LifeFSM;
class WifiManager;
class BatteryMonitor;

class WebUI {
public:
    WebUI(EventBus& bus, const StatsEngine& engine, const LifeFSM& fsm,
          const WifiManager& wifi, const BatteryMonitor& battery);

    // Arranca el servidor HTTP en el puerto 80. Idempotente.
    // Llamar solo cuando WiFi.state() == Connected.
    void begin();

    // Para el servidor HTTP. Idempotente.
    void end();

    bool isRunning() const { return running_; }

    // Hook para el loop principal. AsyncWebServer es event-driven — por ahora no-op.
    void loop();

private:
    EventBus&             bus_;
    const StatsEngine&    engine_;
    const LifeFSM&        fsm_;
    const WifiManager&    wifi_;
    const BatteryMonitor& battery_;
    AsyncWebServer        server_;
    RateLimiter        rateLimiter_;
    bool               running_ = false;

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
    void handlePostHeal_(AsyncWebServerRequest* req, JsonVariant& json);
    void handlePostDiscipline_(AsyncWebServerRequest* req, JsonVariant& json);
    void handlePostSleep_(AsyncWebServerRequest* req, JsonVariant& json);
    void handlePostClean_(AsyncWebServerRequest* req);
    void handlePostMedicate_(AsyncWebServerRequest* req);

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
};
