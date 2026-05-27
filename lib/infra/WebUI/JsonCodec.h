#pragma once
// =============================================================================
// JsonCodec — serialización/deserialización JSON para la REST API.
//
// Sin Arduino.h: compilable en host-native para tests Unity.
// Usa ArduinoJson v7 (header-only, compatible con host via lib_deps native).
//
// Todas las funciones son libres en el namespace JsonCodec.
// Sin allocación dinámica propia — ArduinoJson usa stack-based JsonDocument en v7.
//
// Formato de serialización sigue api-conventions.md:
//   - camelCase en todos los campos JSON
//   - Formato de error: { "error": { "code": "...", "message": "...", "details": [] } }
//   - Fechas: ISO 8601 cuando aplique
// =============================================================================

#include <stdint.h>
#include <stddef.h>
#include "Persistence.h"    // LifeStage enum
#include "StatsEngine.h"    // Stats struct

namespace JsonCodec {

// =============================================================================
// Serialización
// =============================================================================

// Serializa el estado del Tamagotchi como JSON.
// Devuelve bytes escritos (sin contar el null terminator).
// out debe tener al menos 512 bytes; el JSON real cabe en ~320.
// Formato: { "data": {...}, "meta": { "apiVersion": "v1", "timestamp": <uptimeMs> } }
size_t serializeState(const Stats& stats, LifeStage stage, uint32_t ageMinutes,
                      uint16_t deathCount, bool turbo, uint32_t uptimeMs,
                      char* out, size_t outSize);

// Serializa info de telemetría del dispositivo.
// Formato: { "data": { "firmwareVersion", "uptimeMs", "freeHeap", "wifiRssi", ... } }
size_t serializeInfo(uint16_t firmwareVersion, uint32_t uptimeMs,
                     uint32_t freeHeap, int8_t wifiRssi,
                     char* out, size_t outSize);

// Serializa respuesta de acción: state completo + actionApplied.
// actionValue se usa para "amount" (feed/play/heal/discipline) o ignorado (-1) para clean/medicate.
// Para sleep, actionValue: 1=true, 0=false.
size_t serializeActionResponse(const Stats& stats, LifeStage stage, uint32_t ageMinutes,
                               uint16_t deathCount, bool turbo, uint32_t uptimeMs,
                               const char* actionType, int32_t actionValue,
                               char* out, size_t outSize);

// Serializa formato de error estándar (cumple api-conventions.md).
// Formato: { "error": { "code": "...", "message": "...", "details": [] } }
size_t serializeError(const char* code, const char* message,
                      char* out, size_t outSize);

// Convierte LifeStage enum a string literal (sin alloc).
// Devuelve "Unknown" si el valor no es reconocido.
const char* lifeStageToString(LifeStage stage);

// =============================================================================
// Parseo de payloads
// =============================================================================

// Código de resultado del parseo (0 = Ok para comparaciones con 0).
enum class ParseError : uint8_t {
    Ok = 0,
    BadJson,        // JSON malformado o no parseable
    MissingField,   // campo requerido ausente
    InvalidAmount,  // amount fuera de rango 1-100 o tipo incorrecto
    InvalidValue,   // value no es bool en sleep (u otro campo inválido)
    UnknownAction
};

// Parsea { "amount": N } donde 1 <= N <= 100.
// body nulo o vacío → MissingField.
// amount con tipo string → InvalidAmount.
ParseError parseAmountPayload(const char* body, size_t bodyLen, uint8_t& outAmount);

// Parsea { "value": true|false }.
// value con tipo numérico → InvalidValue.
ParseError parseSleepPayload(const char* body, size_t bodyLen, bool& outValue);

// Para endpoints sin payload (clean, medicate):
// acepta body nulo, vacío, "{}" o "null" → Ok.
// JSON malformado → BadJson.
ParseError parseEmptyPayload(const char* body, size_t bodyLen);

}  // namespace JsonCodec
