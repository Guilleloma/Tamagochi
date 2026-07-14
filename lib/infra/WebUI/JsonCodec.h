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
#include "BatteryGauge.h"   // BatterySnapshot — puro, compatible con env:native

namespace JsonCodec {

// =============================================================================
// Serialización
// =============================================================================

// Serializa el estado del Tamagotchi como JSON.
// Devuelve bytes escritos (sin contar el null terminator).
// out debe tener al menos 512 bytes; el JSON real cabe en ~320.
// Formato: { "data": {...}, "meta": { "apiVersion": "v1", "timestamp": <uptimeMs> } }
// Campos de batería siempre presentes en data: batteryPercent, batteryCharging,
// batteryValid (batteryPercent/batteryCharging valen 0/false si !battery.valid).
size_t serializeState(const Stats& stats, LifeStage stage, uint32_t ageMinutes,
                      uint16_t deathCount, bool turbo, uint32_t uptimeMs,
                      const BatterySnapshot& battery,
                      char* out, size_t outSize);

// Serializa info de telemetría del dispositivo.
// Formato: { "data": { "firmwareVersion", "uptimeMs", "freeHeap", "wifiRssi",
//                      "ipAddress", "hostname", "macAddress", ...,
//                      "resetReason", "bootCount", "minFreeHeap" } }
// Además de batteryPercent/batteryCharging/batteryValid, incluye batteryMillivolts
// (mV filtrados del gauge, 0 si !valid) — /info es el endpoint canónico de
// telemetría detallada.
//
// ipAddress / hostname / macAddress: identidad de red, SIEMPRE presentes
// (cadena vacía "" si WiFi no está conectado — nunca null, para no romper
// consumidores que asuman string). ipAddress/macAddress se recalculan en cada
// llamada (WiFi.localIP()/macAddress() en el caller); hostname es el
// configurado (OTA_HOSTNAME ".local"), no depende de la conexión.
//
// batteryRawMillivolts / batteryGateProgress: diagnóstico de soldadura, SIEMPRE
// presentes (a diferencia de batteryMillivolts, nunca se gatean a 0/valid).
//   - batteryRawMillivolts: última lectura del ADC sin filtrar ni gatear (ver
//     BatteryGauge::lastRawMillivolts) — permite ver qué lee el pin ADC en
//     tiempo real aunque el gate nunca llegue a validar.
//   - batteryGateProgress: muestras consecutivas plausibles+estables hacia la
//     validez, 0-8 (ver BatteryGauge::consecutivePlausible) — permite saber si
//     el gate está "a punto" de validar o si cada muestra rompe la racha.
// Exclusivos de /info (no de /state): son datos de diagnóstico de hardware,
// no de estado de juego.
//
// resetReason / bootCount / minFreeHeap — telemetría de reinicios (Fase 5,
// hito 9). Permiten saber POR QUÉ se reinició el dispositivo (¿watchdog?
// ¿brownout de batería? ¿reinicio manual?) sin cable serie. El caller
// (WebUI::handleGetInfo_) resuelve estos valores con la API nativa de cada
// plataforma (esp_reset_reason() en ESP32, ESP.getResetReason() en ESP8266
// -- formatos de string distintos por plataforma, mismo propósito).
//   - resetReason: string legible (p.ej. "POWERON", "SW", "TASK_WDT",
//     "BROWNOUT", "PANIC" en ESP32). SIEMPRE presente.
//   - bootCount: reinicios "por WiFi caído" acumulados desde el último
//     power-on-reset (ver lib/infra/BootLoopGuard, solo ESP32 — 0 en ESP8266,
//     mismo criterio "0 == no soportado en esta plataforma" que los campos
//     de batería).
//   - minFreeHeap: mínimo histórico de heap libre desde el boot
//     (esp_get_minimum_free_heap_size(), solo ESP32 — 0 en ESP8266, que no
//     expone un equivalente nativo).
size_t serializeInfo(uint16_t firmwareVersion, uint32_t uptimeMs,
                     uint32_t freeHeap, int8_t wifiRssi,
                     const char* ipAddress, const char* hostname, const char* macAddress,
                     const BatterySnapshot& battery,
                     uint16_t batteryRawMillivolts, uint8_t batteryGateProgress,
                     const char* resetReason, uint32_t bootCount, uint32_t minFreeHeap,
                     char* out, size_t outSize);

// Serializa respuesta de acción: state completo + actionApplied.
// actionValue se usa para "amount" (feed/play/discipline) o ignorado (-1) para clean/medicate.
// Para sleep, actionValue: 1=true, 0=false.
size_t serializeActionResponse(const Stats& stats, LifeStage stage, uint32_t ageMinutes,
                               uint16_t deathCount, bool turbo, uint32_t uptimeMs,
                               const char* actionType, int32_t actionValue,
                               const BatterySnapshot& battery,
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
