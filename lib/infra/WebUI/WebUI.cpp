// =============================================================================
// WebUI.cpp — Implementación del servidor HTTP REST para el Tamagochi.
//
// Hito 5, Fase 3. Endpoints GET /api/v1/state y GET /api/v1/info.
// Seguridad: HTTP Basic Auth + Rate limit token-bucket por IP.
// =============================================================================

#ifndef UNIT_TEST  // Guard: este archivo no se compila en env:native

#include "WebUI.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>

#include "config.h"
#include "secrets.h"
#include "Logger.h"
#include "EventBus.h"
#include "StatsEngine.h"
#include "LifeFSM.h"
#include "WifiManager.h"
#include "JsonCodec.h"
#include "AuthChecker.h"

// =============================================================================
// Constructor
// =============================================================================

WebUI::WebUI(EventBus& bus, const StatsEngine& engine, const LifeFSM& fsm,
             const WifiManager& wifi)
    : bus_(bus)
    , engine_(engine)
    , fsm_(fsm)
    , wifi_(wifi)
    , server_(80)
    , rateLimiter_()
    , running_(false)
{}

// =============================================================================
// begin / end / loop
// =============================================================================

void WebUI::begin() {
    if (running_) return;  // idempotente

    mountRoutes_();
    server_.begin();
    running_ = true;

    LOG_INFO("[webui] running on port 80");
}

void WebUI::end() {
    if (!running_) return;  // idempotente
    server_.end();
    running_ = false;
    LOG_INFO("[webui] stopped");
}

void WebUI::loop() {
    // AsyncWebServer es event-driven — no requiere polling.
    // Hook reservado para futuras extensiones (p.ej. WebSockets).
}

// =============================================================================
// Mount routes
// =============================================================================

void WebUI::mountRoutes_() {
    server_.on("/api/v1/state", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetState_(req);
    });

    server_.on("/api/v1/info", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetInfo_(req);
    });

    server_.onNotFound([this](AsyncWebServerRequest* req) {
        handleNotFound_(req);
    });
}

// =============================================================================
// Handlers
// =============================================================================

void WebUI::handleGetState_(AsyncWebServerRequest* req) {
    if (!checkAuthAndRate_(req)) return;

    char buf[512];

    bool turbo = false;
#ifdef TURBO_MODE
    turbo = true;
#endif

    // TODO(guillermo): usar deathCount persistido cuando WebUI reciba Persistence&.
    // PersistedState.deathCount existe pero no está expuesto por LifeFSM ni StatsEngine.
    // Por ahora se pasa 0 para no bloquear el hito. Hito 6 puede resolverlo al
    // añadir Persistence como dependencia de WebUI.
    const uint16_t deathCount = 0;

    size_t len = JsonCodec::serializeState(
        engine_.stats(),
        fsm_.stage(),
        fsm_.ageMinutes(),
        deathCount,
        turbo,
        millis(),
        buf,
        sizeof(buf)
    );

    if (len == 0) {
        LOG_ERROR("[webui] GET /api/v1/state: serialization failed");
        sendJsonError_(req, 500, "SERIALIZATION_FAILED", "Failed to encode state");
        return;
    }

    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", buf);
    resp->addHeader("Cache-Control", "no-store");
    req->send(resp);
}

void WebUI::handleGetInfo_(AsyncWebServerRequest* req) {
    if (!checkAuthAndRate_(req)) return;

    char buf[256];

    size_t len = JsonCodec::serializeInfo(
        FIRMWARE_VERSION,
        millis(),
        ESP.getFreeHeap(),
        WiFi.RSSI(),
        buf,
        sizeof(buf)
    );

    if (len == 0) {
        LOG_ERROR("[webui] GET /api/v1/info: serialization failed");
        sendJsonError_(req, 500, "SERIALIZATION_FAILED", "Failed to encode info");
        return;
    }

    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", buf);
    resp->addHeader("Cache-Control", "no-store");
    req->send(resp);
}

void WebUI::handleNotFound_(AsyncWebServerRequest* req) {
    const String& path = req->url();
    if (path.startsWith("/api/")) {
        // Endpoints /api/* desconocidos → 404 JSON estándar
        sendJsonError_(req, 404, "NOT_FOUND", "Endpoint not found");
    } else {
        // Rutas de frontend (hito 7) — por ahora texto plano
        req->send(404, "text/plain", "Not Found");
    }
}

// =============================================================================
// Helpers privados
// =============================================================================

bool WebUI::checkAuthAndRate_(AsyncWebServerRequest* req) {
    // 1. Basic Auth — header "Authorization: Basic <base64(user:pass)>"
    const char* authHeader = nullptr;
    String authHeaderStr;  // necesario para que el puntero sea válido tras el if
    if (req->hasHeader("Authorization")) {
        authHeaderStr = req->header("Authorization");
        authHeader = authHeaderStr.c_str();
    }

    if (!AuthChecker::checkBasicAuth(authHeader, WEB_AUTH_USER, WEB_AUTH_PASSWORD)) {
        AsyncWebServerResponse* resp = req->beginResponse(
            401,
            "application/json",
            "{\"error\":{\"code\":\"UNAUTHORIZED\",\"message\":\"Invalid credentials\"}}"
        );
        resp->addHeader("WWW-Authenticate", "Basic realm=\"Tamagochi\"");
        req->send(resp);
        LOG_WARN("[webui] 401 Unauthorized from %s", req->client()->remoteIP().toString().c_str());
        return false;
    }

    // 2. Rate limit — token bucket por IP (uint32_t de los 4 bytes de la IP)
    uint32_t ipKey = (uint32_t)req->client()->remoteIP();
    if (!rateLimiter_.allow(ipKey, millis())) {
        AsyncWebServerResponse* resp = req->beginResponse(
            429,
            "application/json",
            "{\"error\":{\"code\":\"RATE_LIMITED\",\"message\":\"Too many requests\"}}"
        );
        resp->addHeader("Retry-After", "1");
        req->send(resp);
        LOG_WARN("[webui] 429 Rate-Limited ip=%u", ipKey);
        return false;
    }

    return true;
}

void WebUI::sendJsonError_(AsyncWebServerRequest* req, int code,
                           const char* errorCode, const char* message) {
    char buf[200];
    size_t len = JsonCodec::serializeError(errorCode, message, buf, sizeof(buf));
    if (len == 0) {
        // Fallback hardcoded — si serializeError falla también, usamos literal mínimo
        req->send(code, "application/json",
                  "{\"error\":{\"code\":\"INTERNAL_ERROR\",\"message\":\"Error encoding error\"}}");
        return;
    }
    req->send(code, "application/json", buf);
}

#endif  // UNIT_TEST
