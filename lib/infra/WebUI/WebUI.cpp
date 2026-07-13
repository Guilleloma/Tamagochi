// =============================================================================
// WebUI.cpp — Implementación del servidor HTTP REST para el Tamagochi.
//
// Hito 5, Fase 3. Endpoints GET /api/v1/state y GET /api/v1/info.
// Hito 6, Fase 3. Endpoints POST /api/v1/{feed,play,discipline,sleep,clean,medicate}.
//
// Patrón de acciones: WebUI publica eventos al EventBus (síncrono). Los handlers
// en main.cpp aplican la acción sobre StatsEngine y persisten. Como el bus es
// síncrono, las stats están actualizadas cuando publish() retorna, por lo que
// sendActionResponse_() ya ve el estado post-acción.
//
// Seguridad: HTTP Basic Auth + Rate limit token-bucket por IP.
// =============================================================================

#ifndef UNIT_TEST  // Guard: este archivo no se compila en env:native

#include "WebUI.h"

#include <Arduino.h>
// Platform-conditional WiFi header (same API surface: WiFi.RSSI, remoteIP, etc.)
#if defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
#else
  #include <ESP8266WiFi.h>
#endif
#include <LittleFS.h>

#include "config.h"
#include "secrets.h"
#include "Logger.h"
#include "EventBus.h"
#include "StatsEngine.h"
#include "LifeFSM.h"
#include "WifiManager.h"
// NOTA: NO incluir BatteryMonitor.h aquí — WebUI depende solo de BatteryGauge
// (vía WebUI.h), que ya trae el tipo BatterySnapshot. Incluir BatteryMonitor.h
// arrastraría infra ESP32-only (analogSetPinAttenuation) al LDF de builds que
// compilan WebUI sin necesitarlo. Ver comentario del ctor en WebUI.h.
#include "JsonCodec.h"
#include "AuthChecker.h"

// =============================================================================
// Constructor
// =============================================================================

WebUI::WebUI(EventBus& bus, const StatsEngine& engine, const LifeFSM& fsm,
             const WifiManager& wifi, const BatteryGauge* battery)
    : bus_(bus)
    , engine_(engine)
    , fsm_(fsm)
    , wifi_(wifi)
    , battery_(battery)
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

    // Reinicio diferido tras POST /api/v1/reboot (ver handlePostReboot_ y el
    // comentario de rebootAtMs_ en el .h). Se comprueba aquí, en el loop
    // principal, en vez de dentro del handler HTTP, porque ESP.restart() ahí
    // cortaría la respuesta 200 a mitad de envío (el handler corre en la task
    // async_tcp, no en el loop principal).
    if (rebootAtMs_ != 0 && millis() >= rebootAtMs_) {
        LOG_WARN("[webui] reiniciando dispositivo (solicitado via /api/v1/reboot)");
        ESP.restart();
    }
}

// =============================================================================
// Mount routes
// =============================================================================

void WebUI::mountRoutes_() {
    // --- GET endpoints ---
    server_.on("/api/v1/state", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetState_(req);
    });

    server_.on("/api/v1/info", HTTP_GET, [this](AsyncWebServerRequest* req) {
        handleGetInfo_(req);
    });

    // --- POST endpoints con body JSON (amount o value) ---
    // AsyncCallbackJsonWebHandler exige Content-Type: application/json.
    // Parsea el body y llama al handler con un JsonVariant ya deserializado.
    auto* feedHandler = new AsyncCallbackJsonWebHandler("/api/v1/feed",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            handlePostFeed_(req, json);
        }
    );
    feedHandler->setMethod(HTTP_POST);
    server_.addHandler(feedHandler);

    auto* playHandler = new AsyncCallbackJsonWebHandler("/api/v1/play",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            handlePostPlay_(req, json);
        }
    );
    playHandler->setMethod(HTTP_POST);
    server_.addHandler(playHandler);

    auto* disciplineHandler = new AsyncCallbackJsonWebHandler("/api/v1/discipline",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            handlePostDiscipline_(req, json);
        }
    );
    disciplineHandler->setMethod(HTTP_POST);
    server_.addHandler(disciplineHandler);

    auto* sleepHandler = new AsyncCallbackJsonWebHandler("/api/v1/sleep",
        [this](AsyncWebServerRequest* req, JsonVariant& json) {
            handlePostSleep_(req, json);
        }
    );
    sleepHandler->setMethod(HTTP_POST);
    server_.addHandler(sleepHandler);

    // --- POST endpoints sin body (clean, medicate) ---
    // Usan server_.on() directo: no requieren Content-Type ni body.
    server_.on("/api/v1/clean", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handlePostClean_(req);
    });

    server_.on("/api/v1/medicate", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handlePostMedicate_(req);
    });

    // --- POST endpoint de sistema (reboot) — sin body, mismo patrón que clean/medicate ---
    server_.on("/api/v1/reboot", HTTP_POST, [this](AsyncWebServerRequest* req) {
        handlePostReboot_(req);
    });

    // --- Archivos estáticos desde LittleFS (hito 7) ---
    // Sirve /, /app.css, /app.js, /favicon.ico desde la carpeta raíz de LittleFS.
    // index.html sin caché; CSS/JS/favicon con caché de 1 hora.
    server_.serveStatic("/", LittleFS, "/")
        .setDefaultFile("index.html")
        .setCacheControl("no-cache");

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
        batterySnapshot_(),
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

    char buf[448];  // 320 se quedaba corto al añadir ipAddress/hostname/macAddress

    // WiFi.localIP().toString() y WiFi.macAddress() devuelven Arduino String
    // TEMPORALES — hay que guardarlos en variables locales antes de pasar sus
    // .c_str() a serializeInfo(). JsonCodec (ver su .cpp) NO copia los
    // const char* que recibe (zero-copy, guarda el puntero), así que el
    // buffer subyacente debe seguir vivo durante toda la llamada; si se
    // encadenaran los .toString().c_str() como temporales inline, el String
    // temporal se destruiría antes de que serializeJson() los usara —
    // use-after-free silencioso.
    //
    // ipAddress/macAddress caen a "" si WiFi no está Connected (defensivo:
    // en la práctica WebUI solo corre con WiFi Connected — ver
    // onWifiStateChanged en main_esp32.cpp — pero así el contrato de "SIEMPRE
    // presentes, vacío si WiFi caído" se cumple incluso en una carrera justo
    // antes de que WifiManager detecte la caída).
    bool    wifiConnected = (WiFi.status() == WL_CONNECTED);
    String  ipStr         = wifiConnected ? WiFi.localIP().toString() : String("");
    String  macStr        = wifiConnected ? WiFi.macAddress()         : String("");

    size_t len = JsonCodec::serializeInfo(
        FIRMWARE_VERSION,
        millis(),
        ESP.getFreeHeap(),
        WiFi.RSSI(),
        ipStr.c_str(),
        OTA_HOSTNAME ".local",  // concatenación de literales en compile-time
        macStr.c_str(),
        batterySnapshot_(),
        batteryRawMillivolts_(),
        batteryGateProgress_(),
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

// =============================================================================
// POST handlers — acciones de juego (hito 6)
// =============================================================================

// Extrae y valida "amount" (int 1-100) de un JsonVariant ya deserializado.
// ArduinoJson v7: json["amount"].is<int>() es false si el campo no existe o no es int.
bool WebUI::extractAmount_(AsyncWebServerRequest* req, JsonVariant& json, int& outAmount) {
    JsonVariantConst amountVar = json["amount"];
    if (amountVar.isUnbound() || amountVar.isNull()) {
        sendJsonError_(req, 422, "MISSING_FIELD", "amount field required (int 1-100)");
        return false;
    }
    if (!amountVar.is<int>()) {
        sendJsonError_(req, 422, "INVALID_AMOUNT", "amount must be an integer");
        return false;
    }
    outAmount = amountVar.as<int>();
    if (outAmount < 1 || outAmount > 100) {
        sendJsonError_(req, 422, "INVALID_AMOUNT", "amount must be 1..100");
        return false;
    }
    return true;
}

void WebUI::handlePostFeed_(AsyncWebServerRequest* req, JsonVariant& json) {
    if (!checkAuthAndRate_(req)) return;
    int amount = 0;
    if (!extractAmount_(req, json, amount)) return;
    bus_.publish({EventType::ActionFeedRequested, static_cast<int32_t>(amount)});
    LOG_INFO("[webui] POST /api/v1/feed amount=%d", amount);
    sendActionResponse_(req, "feed", static_cast<int32_t>(amount));
}

void WebUI::handlePostPlay_(AsyncWebServerRequest* req, JsonVariant& json) {
    if (!checkAuthAndRate_(req)) return;
    int amount = 0;
    if (!extractAmount_(req, json, amount)) return;
    bus_.publish({EventType::ActionPlayRequested, static_cast<int32_t>(amount)});
    LOG_INFO("[webui] POST /api/v1/play amount=%d", amount);
    sendActionResponse_(req, "play", static_cast<int32_t>(amount));
}

void WebUI::handlePostDiscipline_(AsyncWebServerRequest* req, JsonVariant& json) {
    if (!checkAuthAndRate_(req)) return;
    int amount = 0;
    if (!extractAmount_(req, json, amount)) return;
    bus_.publish({EventType::ActionDisciplineRequested, static_cast<int32_t>(amount)});
    LOG_INFO("[webui] POST /api/v1/discipline amount=%d", amount);
    sendActionResponse_(req, "discipline", static_cast<int32_t>(amount));
}

void WebUI::handlePostSleep_(AsyncWebServerRequest* req, JsonVariant& json) {
    if (!checkAuthAndRate_(req)) return;

    JsonVariantConst valueVar = json["value"];
    if (valueVar.isUnbound() || valueVar.isNull()) {
        sendJsonError_(req, 422, "MISSING_FIELD", "value field required (bool)");
        return;
    }
    if (!valueVar.is<bool>()) {
        sendJsonError_(req, 422, "INVALID_VALUE", "value must be a boolean (true|false)");
        return;
    }

    bool on = valueVar.as<bool>();
    bus_.publish({EventType::ActionSleepRequested, on ? 1 : 0});
    LOG_INFO("[webui] POST /api/v1/sleep value=%s", on ? "true" : "false");
    // Para sleep, actionValue encoda true/false como 1/0
    sendActionResponse_(req, "sleep", on ? 1 : 0);
}

void WebUI::handlePostClean_(AsyncWebServerRequest* req) {
    if (!checkAuthAndRate_(req)) return;
    bus_.publish({EventType::ActionCleanRequested, 0});
    LOG_INFO("[webui] POST /api/v1/clean");
    sendActionResponse_(req, "clean", -1);  // -1 = sin amount en la respuesta
}

void WebUI::handlePostMedicate_(AsyncWebServerRequest* req) {
    if (!checkAuthAndRate_(req)) return;
    bus_.publish({EventType::ActionMedicateRequested, 0});
    LOG_INFO("[webui] POST /api/v1/medicate");
    sendActionResponse_(req, "medicate", -1);  // -1 = sin amount en la respuesta
}

// -----------------------------------------------------------------------------
// handlePostReboot_ — endpoint de sistema. Sin body.
//
// Motivación (ver commit): ArduinoOTA puede colgarse tras un apagón por
// batería sin dejar forma remota de reiniciar el dispositivo. Este endpoint
// da esa vía de escape.
//
// Orden crítico:
//   1. checkAuthAndRate_ — mismo gate que el resto de /api/v1/*.
//   2. PersistRequested SÍNCRONO antes de responder — el bus es síncrono
//      (mismo patrón que el resto de acciones), así que al llegar al paso 3
//      el estado ya está a salvo en LittleFS, pase lo que pase después.
//   3. Responder 200 con { data: { rebooting: true }, meta: {...} }.
//   4. Programar rebootAtMs_ = millis() + 750 — el reinicio real ocurre en
//      loop() (ver WebUI::loop), NUNCA aquí: ESP.restart() en este handler
//      cortaría la propia respuesta HTTP a mitad de envío, porque este
//      handler corre en la task async_tcp, no en el loop principal. El
//      margen de 750ms da tiempo a que AsyncTCP termine de enviar el buffer
//      de respuesta al cliente antes del reinicio.
// -----------------------------------------------------------------------------
void WebUI::handlePostReboot_(AsyncWebServerRequest* req) {
    if (!checkAuthAndRate_(req)) return;

    bus_.publish({EventType::PersistRequested, 0});
    LOG_WARN("[webui] POST /api/v1/reboot — estado persistido, reinicio programado en 750ms");

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"data\":{\"rebooting\":true},\"meta\":{\"apiVersion\":\"v1\",\"timestamp\":%lu}}",
             static_cast<unsigned long>(millis()));

    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", buf);
    resp->addHeader("Cache-Control", "no-store");
    req->send(resp);

    rebootAtMs_ = millis() + 750;
}

void WebUI::sendActionResponse_(AsyncWebServerRequest* req,
                                const char* actionType, int32_t actionValue) {
    char buf[600];  // serializeActionResponse ocupa ~420 bytes + overhead de actionApplied

    bool turbo = false;
#ifdef TURBO_MODE
    turbo = true;
#endif

    const uint16_t deathCount = 0;  // TODO mismo que en handleGetState_: sin acceso a Persistence

    size_t len = JsonCodec::serializeActionResponse(
        engine_.stats(),
        fsm_.stage(),
        fsm_.ageMinutes(),
        deathCount,
        turbo,
        millis(),
        actionType,
        actionValue,
        batterySnapshot_(),
        buf,
        sizeof(buf)
    );

    if (len == 0) {
        LOG_ERROR("[webui] sendActionResponse_: serialization failed action=%s", actionType);
        sendJsonError_(req, 500, "SERIALIZATION_FAILED", "Failed to encode action response");
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

// -----------------------------------------------------------------------------
// batterySnapshot_ — {valid:false, percent:0, charging:false, millivolts:0} si
// battery_ es nullptr. Mismo contrato de API que un BatteryMonitor real sin
// hardware detectado (ver BatteryGauge.h) — el cliente de la API nunca ve la
// diferencia entre "sin sensor de batería compilado" y "sensor presente pero
// aún sin validar".
// -----------------------------------------------------------------------------
BatterySnapshot WebUI::batterySnapshot_() const {
    return battery_ ? battery_->snapshot() : BatterySnapshot{};
}

// -----------------------------------------------------------------------------
// batteryRawMillivolts_ / batteryGateProgress_ — diagnóstico de soldadura
// (ver JsonCodec::serializeInfo). 0 si battery_ es nullptr, mismo criterio de
// "sin sensor" que batterySnapshot_() — pero a diferencia de esa, estos dos
// NUNCA se gatean por valid_: reflejan siempre la lectura real del ADC y el
// progreso real del gate, incluso mientras el gauge está en Unknown.
// -----------------------------------------------------------------------------
uint16_t WebUI::batteryRawMillivolts_() const {
    return battery_ ? battery_->lastRawMillivolts() : 0;
}

uint8_t WebUI::batteryGateProgress_() const {
    return battery_ ? battery_->consecutivePlausible() : 0;
}

#endif  // UNIT_TEST
