#pragma once
// =============================================================================
// WebUI — servidor HTTP AsyncWebServer para la REST API del Tamagochi.
//
// Endpoints (hito 5):
//   GET /api/v1/state  → estado completo del Tamagochi (stats + stage + age)
//   GET /api/v1/info   → telemetría del dispositivo (firmware, heap, RSSI)
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
#include "RateLimiter.h"

class EventBus;
class StatsEngine;
class LifeFSM;
class WifiManager;

class WebUI {
public:
    WebUI(EventBus& bus, const StatsEngine& engine, const LifeFSM& fsm,
          const WifiManager& wifi);

    // Arranca el servidor HTTP en el puerto 80. Idempotente.
    // Llamar solo cuando WiFi.state() == Connected.
    void begin();

    // Para el servidor HTTP. Idempotente.
    void end();

    bool isRunning() const { return running_; }

    // Hook para el loop principal. AsyncWebServer es event-driven — por ahora no-op.
    void loop();

private:
    EventBus&          bus_;
    const StatsEngine& engine_;
    const LifeFSM&     fsm_;
    const WifiManager& wifi_;
    AsyncWebServer     server_;
    RateLimiter        rateLimiter_;
    bool               running_ = false;

    // Monta las rutas en el servidor.
    void mountRoutes_();

    // Handlers de endpoints.
    void handleGetState_(AsyncWebServerRequest* req);
    void handleGetInfo_(AsyncWebServerRequest* req);
    void handleNotFound_(AsyncWebServerRequest* req);

    // Aplica auth + rate limit. Si falla, envía 401/429 y devuelve false.
    // Si OK, devuelve true y el caller procesa la request.
    bool checkAuthAndRate_(AsyncWebServerRequest* req);

    // Serializa y envía una respuesta de error JSON estándar (api-conventions.md).
    void sendJsonError_(AsyncWebServerRequest* req, int code,
                        const char* errorCode, const char* message);
};
