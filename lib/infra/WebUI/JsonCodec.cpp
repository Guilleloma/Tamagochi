// =============================================================================
// JsonCodec.cpp — implementación de serialización/parseo JSON.
//
// Sin Arduino.h. Usa ArduinoJson v7 (JsonDocument en lugar de
// StaticJsonDocument/DynamicJsonDocument, que están deprecados en v7).
//
// Decisiones de implementación:
// - JsonDocument en stack local (no heap): sin fragmentación.
// - serializeJson() escribe directamente al buffer char* del caller.
// - deserializeJson() recibe el body como puntero const char* — ArduinoJson
//   v7 acepta DeserializationInput de tipo const char* sin copia si se
//   proporciona la longitud. Usamos DeserializationInput desde buffer.
// - No se loguean los contenidos de los payloads para cumplir security.md
//   (podrían contener datos del usuario).
// =============================================================================

#include "JsonCodec.h"
#include <ArduinoJson.h>
#include <string.h>

namespace JsonCodec {

// =============================================================================
// lifeStageToString
// =============================================================================

const char* lifeStageToString(LifeStage stage) {
    switch (stage) {
        case LifeStage::Egg:   return "Egg";
        case LifeStage::Baby:  return "Baby";
        case LifeStage::Kid:   return "Kid";
        case LifeStage::Adult: return "Adult";
        case LifeStage::Old:   return "Old";
        case LifeStage::Dead:  return "Dead";
        default:               return "Unknown";
    }
}

// =============================================================================
// Serialización de estado
// =============================================================================

size_t serializeState(const Stats& stats, LifeStage stage, uint32_t ageMinutes,
                      uint16_t deathCount, bool turbo, uint32_t uptimeMs,
                      const BatterySnapshot& battery,
                      char* out, size_t outSize) {
    JsonDocument doc;

    JsonObject data = doc["data"].to<JsonObject>();
    data["lifeStage"]      = lifeStageToString(stage);
    data["ageMinutes"]     = ageMinutes;
    data["hunger"]         = stats.hunger;
    data["happiness"]      = stats.happiness;
    data["health"]         = stats.health;
    data["discipline"]     = stats.discipline;
    data["weight"]         = stats.weight;
    data["poopCount"]      = stats.poopCount;
    data["isSick"]         = stats.isSick;
    data["isSleeping"]     = stats.isSleeping;
    data["attentionScore"] = stats.attentionScore;
    data["deathCount"]     = deathCount;
    data["turbo"]          = turbo;
    // Campos siempre presentes (backward compatible) — batteryValid indica si
    // el resto de campos de batería son fiables (hardware no soldado aún → false).
    data["batteryPercent"]  = battery.percent;
    data["batteryCharging"] = battery.charging;
    data["batteryValid"]    = battery.valid;

    JsonObject meta = doc["meta"].to<JsonObject>();
    meta["apiVersion"] = "v1";
    meta["timestamp"]  = uptimeMs;

    return serializeJson(doc, out, outSize);
}

// =============================================================================
// Serialización de info de telemetría
// =============================================================================

size_t serializeInfo(uint16_t firmwareVersion, uint32_t uptimeMs,
                     uint32_t freeHeap, int8_t wifiRssi,
                     const char* ipAddress, const char* hostname, const char* macAddress,
                     const BatterySnapshot& battery,
                     uint16_t batteryRawMillivolts, uint8_t batteryGateProgress,
                     char* out, size_t outSize) {
    JsonDocument doc;

    JsonObject data = doc["data"].to<JsonObject>();
    data["firmwareVersion"] = firmwareVersion;
    data["uptimeMs"]        = uptimeMs;
    data["freeHeap"]        = freeHeap;
    data["wifiRssi"]        = wifiRssi;
    // Identidad de red — SIEMPRE presentes (cadena vacía si WiFi caído, ver
    // comentario del .h). ArduinoJson v7 NO copia const char* (guarda el
    // puntero, zero-copy) — es responsabilidad del caller mantener vivos los
    // buffers hasta que serializeJson() (más abajo, misma llamada síncrona)
    // termine de escribir. Ver WebUI::handleGetInfo_: los String de Arduino
    // se guardan en variables locales del caller para cumplir esto.
    data["ipAddress"]  = ipAddress;
    data["hostname"]   = hostname;
    data["macAddress"] = macAddress;
    // /info es el endpoint canónico de telemetría — además del trío común
    // (percent/charging/valid) expone batteryMillivolts para diagnóstico fino.
    data["batteryPercent"]    = battery.percent;
    data["batteryCharging"]   = battery.charging;
    data["batteryValid"]      = battery.valid;
    data["batteryMillivolts"] = battery.millivolts;
    // Diagnóstico de soldadura — SIEMPRE presentes, nunca gateados por valid
    // (a diferencia de batteryMillivolts). Ver comentario del .h.
    data["batteryRawMillivolts"] = batteryRawMillivolts;
    data["batteryGateProgress"]  = batteryGateProgress;

    return serializeJson(doc, out, outSize);
}

// =============================================================================
// Serialización de respuesta de acción
// =============================================================================

size_t serializeActionResponse(const Stats& stats, LifeStage stage, uint32_t ageMinutes,
                               uint16_t deathCount, bool turbo, uint32_t uptimeMs,
                               const char* actionType, int32_t actionValue,
                               const BatterySnapshot& battery,
                               char* out, size_t outSize) {
    JsonDocument doc;

    JsonObject data = doc["data"].to<JsonObject>();
    data["lifeStage"]      = lifeStageToString(stage);
    data["ageMinutes"]     = ageMinutes;
    data["hunger"]         = stats.hunger;
    data["happiness"]      = stats.happiness;
    data["health"]         = stats.health;
    data["discipline"]     = stats.discipline;
    data["weight"]         = stats.weight;
    data["poopCount"]      = stats.poopCount;
    data["isSick"]         = stats.isSick;
    data["isSleeping"]     = stats.isSleeping;
    data["attentionScore"] = stats.attentionScore;
    data["deathCount"]     = deathCount;
    data["turbo"]          = turbo;
    data["batteryPercent"]  = battery.percent;
    data["batteryCharging"] = battery.charging;
    data["batteryValid"]    = battery.valid;

    JsonObject meta = doc["meta"].to<JsonObject>();
    meta["apiVersion"] = "v1";
    meta["timestamp"]  = uptimeMs;

    JsonObject action = doc["actionApplied"].to<JsonObject>();
    action["type"] = actionType;
    if (actionValue >= 0) {
        action["amount"] = (uint32_t)actionValue;
    }

    return serializeJson(doc, out, outSize);
}

// =============================================================================
// Serialización de error estándar
// =============================================================================

size_t serializeError(const char* code, const char* message,
                      char* out, size_t outSize) {
    JsonDocument doc;

    JsonObject error = doc["error"].to<JsonObject>();
    error["code"]    = code;
    error["message"] = message;
    error["details"] = doc["error"]["details"].to<JsonArray>();

    return serializeJson(doc, out, outSize);
}

// =============================================================================
// Parseo de payloads
// =============================================================================

ParseError parseAmountPayload(const char* body, size_t bodyLen, uint8_t& outAmount) {
    if (body == nullptr || bodyLen == 0) {
        return ParseError::MissingField;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, bodyLen);
    if (err) {
        return ParseError::BadJson;
    }

    // Verificar que "amount" existe
    if (!doc["amount"].is<JsonVariant>() || doc["amount"].isNull()) {
        return ParseError::MissingField;
    }

    // Verificar que "amount" es numérico (no string)
    if (!doc["amount"].is<int>()) {
        return ParseError::InvalidAmount;
    }

    int value = doc["amount"].as<int>();
    if (value < 1 || value > 100) {
        return ParseError::InvalidAmount;
    }

    outAmount = (uint8_t)value;
    return ParseError::Ok;
}

ParseError parseSleepPayload(const char* body, size_t bodyLen, bool& outValue) {
    if (body == nullptr || bodyLen == 0) {
        return ParseError::MissingField;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, bodyLen);
    if (err) {
        return ParseError::BadJson;
    }

    if (!doc["value"].is<JsonVariant>() || doc["value"].isNull()) {
        return ParseError::MissingField;
    }

    // Verificar que "value" es estrictamente booleano (no número)
    if (!doc["value"].is<bool>()) {
        return ParseError::InvalidValue;
    }

    outValue = doc["value"].as<bool>();
    return ParseError::Ok;
}

ParseError parseEmptyPayload(const char* body, size_t bodyLen) {
    // Aceptar body nulo, vacío o exactamente "{}" o "null"
    if (body == nullptr || bodyLen == 0) {
        return ParseError::Ok;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, bodyLen);
    if (err) {
        return ParseError::BadJson;
    }

    // Aceptar null o objeto vacío (o con campos ignorados)
    return ParseError::Ok;
}

}  // namespace JsonCodec
