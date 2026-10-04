// =============================================================================
// test_json_codec.cpp — tests para JsonCodec (hito 3, Fase 3).
//
// Framework: Unity (pio test -e native).
// JsonCodec es puro: sin Arduino.h, sin efectos secundarios.
//
// 14 tests según plan fase-3-plan.md §9.1.
//
// Para tests de serialización: el JSON resultante se parsea de vuelta con
// ArduinoJson y se verifican campos específicos. Esto valida tanto el formato
// como los valores serializados.
// =============================================================================

#include <unity.h>
#include "JsonCodec.h"
#include <ArduinoJson.h>
#include <string.h>

// Buffer compartido para salida JSON — 512 bytes, suficiente según plan
static char g_buf[512];

// Stats por defecto para tests de serialización
static Stats makeDefaultStats() {
    Stats s;
    s.hunger        = 38;
    s.happiness     = 65;
    s.health        = 90;
    s.discipline    = 50;
    s.weight        = 12;
    s.poopCount     = 1;
    s.isSick        = false;
    s.isSleeping    = false;
    s.attentionScore = 420;
    s.energy        = 73;
    s.lightsOn      = true;
    return s;
}

// Snapshots de batería reutilizados en varios tests: uno "válido" con valores
// realistas y otro "Unknown" (hardware sin soldar, patrón que domina el repo).
static BatterySnapshot makeValidBattery() {
    BatterySnapshot b;
    b.valid      = true;
    b.percent    = 87;
    b.charging   = true;
    b.millivolts = 4120;
    return b;
}

static BatterySnapshot makeUnknownBattery() {
    return BatterySnapshot{};  // valid=false, percent=0, charging=false, millivolts=0
}

void setUp() {
    memset(g_buf, 0, sizeof(g_buf));
}

void tearDown() {}

// =============================================================================
// 1. serialize_state_produces_valid_json
// serializeState() produce un JSON parseable sin errores.
// =============================================================================
void test_serialize_state_produces_valid_json() {
    Stats s = makeDefaultStats();
    size_t written = JsonCodec::serializeState(
        s, LifeStage::Baby, 142, 0, false, 1741234567u, makeValidBattery(), nullptr, g_buf, sizeof(g_buf));

    TEST_ASSERT_GREATER_THAN(0u, written);

    // Parsear el resultado para verificar que es JSON válido
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, g_buf, written);
    TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, err.code(),
                              "El JSON resultante debe ser parseable");

    // Verificar que tiene los dos objetos de primer nivel
    TEST_ASSERT_TRUE(doc["data"].is<JsonObject>());
    TEST_ASSERT_TRUE(doc["meta"].is<JsonObject>());
}

// =============================================================================
// 2. serialize_state_camel_case_fields
// Los campos del JSON están en camelCase (cumple api-conventions.md).
// =============================================================================
void test_serialize_state_camel_case_fields() {
    Stats s = makeDefaultStats();
    JsonCodec::serializeState(s, LifeStage::Baby, 142, 0, false, 0u, makeValidBattery(), nullptr, g_buf, sizeof(g_buf));

    JsonDocument doc;
    deserializeJson(doc, g_buf);
    JsonObject data = doc["data"];

    // Verificar naming camelCase — si hubiera snake_case estos serían null
    TEST_ASSERT_FALSE(data["ageMinutes"].isNull());
    TEST_ASSERT_FALSE(data["poopCount"].isNull());
    TEST_ASSERT_FALSE(data["attentionScore"].isNull());
    TEST_ASSERT_FALSE(data["deathCount"].isNull());
    TEST_ASSERT_FALSE(data["isSick"].isNull());
    TEST_ASSERT_FALSE(data["isSleeping"].isNull());
    TEST_ASSERT_FALSE(data["energy"].isNull());
    TEST_ASSERT_FALSE(data["lightsOn"].isNull());
    TEST_ASSERT_FALSE(data["lifeStage"].isNull());
    TEST_ASSERT_FALSE(data["batteryPercent"].isNull());
    TEST_ASSERT_FALSE(data["batteryCharging"].isNull());
    TEST_ASSERT_FALSE(data["batteryValid"].isNull());

    // energy/lightsOn con los valores esperados de makeDefaultStats()
    TEST_ASSERT_EQUAL_UINT8(73, data["energy"].as<uint8_t>());
    TEST_ASSERT_TRUE(data["lightsOn"].as<bool>());

    // Asegurarse que NO hay snake_case
    TEST_ASSERT_TRUE(data["age_minutes"].isNull());
    TEST_ASSERT_TRUE(data["poop_count"].isNull());
    TEST_ASSERT_TRUE(data["battery_percent"].isNull());
    TEST_ASSERT_TRUE(data["lights_on"].isNull());
}

// =============================================================================
// 3. serialize_state_lifestage_as_string
// lifeStage se serializa como string, no como entero.
// =============================================================================
void test_serialize_state_lifestage_as_string() {
    Stats s = makeDefaultStats();
    JsonCodec::serializeState(s, LifeStage::Baby, 142, 0, false, 0u, makeValidBattery(), nullptr, g_buf, sizeof(g_buf));

    JsonDocument doc;
    deserializeJson(doc, g_buf);

    // Debe ser un string, no un entero
    TEST_ASSERT_TRUE(doc["data"]["lifeStage"].is<const char*>());
    TEST_ASSERT_EQUAL_STRING("Baby", doc["data"]["lifeStage"].as<const char*>());
}

// =============================================================================
// 4. serialize_state_below_buffer_limit
// El JSON cabe en 512 bytes incluso con todos los stats al máximo.
// =============================================================================
void test_serialize_state_below_buffer_limit() {
    // Stats al máximo para el peor caso de tamaño
    Stats s;
    s.hunger         = 100;
    s.happiness      = 100;
    s.health         = 100;
    s.discipline     = 100;
    s.weight         = 100;
    s.poopCount      = 9;
    s.isSick         = true;
    s.isSleeping     = true;
    s.attentionScore = 1000;
    s.energy         = 100;
    s.lightsOn       = true;

    size_t written = JsonCodec::serializeState(
        s, LifeStage::Old, 99999u, 9999u, true, 0xFFFFFFFFu, makeValidBattery(), "Adult", g_buf, sizeof(g_buf));

    // 0 indica truncado (serializeJson devuelve 0 si el buffer es insuficiente)
    TEST_ASSERT_GREATER_THAN(0u, written);
    // Debe caber holgadamente en 512 bytes
    TEST_ASSERT_LESS_THAN(512u, written);
}

// =============================================================================
// 5. serialize_info_includes_required_fields
// serializeInfo() produce JSON con firmwareVersion, uptimeMs, freeHeap, wifiRssi.
// =============================================================================
void test_serialize_info_includes_required_fields() {
    size_t written = JsonCodec::serializeInfo(0x0300u, 142567u, 38240u, -54,
                                              "", "", "",
                                              makeValidBattery(), 4120u, 8u,
                                              "SW", 0u, 0u, g_buf, sizeof(g_buf));

    TEST_ASSERT_GREATER_THAN(0u, written);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, g_buf, written);
    TEST_ASSERT_EQUAL(DeserializationError::Ok, err.code());

    JsonObject data = doc["data"];
    TEST_ASSERT_FALSE(data["firmwareVersion"].isNull());
    TEST_ASSERT_FALSE(data["uptimeMs"].isNull());
    TEST_ASSERT_FALSE(data["freeHeap"].isNull());
    TEST_ASSERT_FALSE(data["wifiRssi"].isNull());

    TEST_ASSERT_EQUAL_UINT32(0x0300u, data["firmwareVersion"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(142567u, data["uptimeMs"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(38240u,  data["freeHeap"].as<uint32_t>());
    TEST_ASSERT_EQUAL_INT8(-54,       data["wifiRssi"].as<int8_t>());
}

// =============================================================================
// 5b. serialize_info_battery_fields_valid
// serializeInfo() con snapshot válido expone percent/charging/valid/millivolts
// + batteryRawMillivolts/batteryGateProgress (diagnóstico, ver JsonCodec.h).
// =============================================================================
void test_serialize_info_battery_fields_valid() {
    size_t written = JsonCodec::serializeInfo(0x0300u, 0u, 0u, 0,
                                              "", "", "",
                                              makeValidBattery(), 4120u, 8u,
                                              "SW", 0u, 0u, g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0u, written);

    JsonDocument doc;
    deserializeJson(doc, g_buf, written);
    JsonObject data = doc["data"];

    TEST_ASSERT_FALSE(data["batteryMillivolts"].isNull());
    TEST_ASSERT_TRUE(data["batteryValid"].as<bool>());
    TEST_ASSERT_EQUAL_UINT8(87,   data["batteryPercent"].as<uint8_t>());
    TEST_ASSERT_TRUE(data["batteryCharging"].as<bool>());
    TEST_ASSERT_EQUAL_UINT16(4120, data["batteryMillivolts"].as<uint16_t>());

    TEST_ASSERT_FALSE(data["batteryRawMillivolts"].isNull());
    TEST_ASSERT_FALSE(data["batteryGateProgress"].isNull());
    TEST_ASSERT_EQUAL_UINT16(4120, data["batteryRawMillivolts"].as<uint16_t>());
    TEST_ASSERT_EQUAL_UINT8(8,     data["batteryGateProgress"].as<uint8_t>());
}

// =============================================================================
// 5c. serialize_info_battery_fields_unknown
// serializeInfo() con snapshot Unknown (hardware sin soldar) → batteryValid
// false y el resto de campos en su valor neutro, pero SIEMPRE presentes
// (backward compatible, api-conventions.md). Caso "nunca hubo lectura
// plausible" (raw=0, gateProgress=0 — p.ej. pin flotante, sin divisor).
// =============================================================================
void test_serialize_info_battery_fields_unknown() {
    size_t written = JsonCodec::serializeInfo(0x0300u, 0u, 0u, 0,
                                              "", "", "",
                                              makeUnknownBattery(), 0u, 0u,
                                              "SW", 0u, 0u, g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0u, written);

    JsonDocument doc;
    deserializeJson(doc, g_buf, written);
    JsonObject data = doc["data"];

    TEST_ASSERT_FALSE(data["batteryValid"].isNull());
    TEST_ASSERT_FALSE(data["batteryValid"].as<bool>());
    TEST_ASSERT_EQUAL_UINT8(0, data["batteryPercent"].as<uint8_t>());
    TEST_ASSERT_FALSE(data["batteryCharging"].as<bool>());
    TEST_ASSERT_EQUAL_UINT16(0, data["batteryMillivolts"].as<uint16_t>());

    TEST_ASSERT_FALSE(data["batteryRawMillivolts"].isNull());
    TEST_ASSERT_FALSE(data["batteryGateProgress"].isNull());
    TEST_ASSERT_EQUAL_UINT16(0, data["batteryRawMillivolts"].as<uint16_t>());
    TEST_ASSERT_EQUAL_UINT8(0,  data["batteryGateProgress"].as<uint8_t>());
}

// =============================================================================
// 5c2. serialize_info_battery_raw_fields_present_while_gate_in_progress
// El caso que motiva estos campos (ver bug report): el divisor está soldado
// y el pin ADC lee un valor plausible (3550mV), pero el gate de validez aún
// no acumuló las 8 muestras consecutivas requeridas → batteryValid sigue
// false. Sin batteryRawMillivolts/batteryGateProgress esto es indistinguible
// de "no hay hardware conectado" (raw=0) desde fuera del BatteryGauge — con
// ellos, se ve claramente "5/8, leyendo 3550mV" (a punto de validar).
// =============================================================================
void test_serialize_info_battery_raw_fields_present_while_gate_in_progress() {
    size_t written = JsonCodec::serializeInfo(0x0300u, 0u, 0u, 0,
                                              "", "", "",
                                              makeUnknownBattery(), 3550u, 5u,
                                              "SW", 0u, 0u, g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0u, written);

    JsonDocument doc;
    deserializeJson(doc, g_buf, written);
    JsonObject data = doc["data"];

    TEST_ASSERT_FALSE(data["batteryValid"].as<bool>());
    TEST_ASSERT_EQUAL_UINT16(3550, data["batteryRawMillivolts"].as<uint16_t>());
    TEST_ASSERT_EQUAL_UINT8(5,     data["batteryGateProgress"].as<uint8_t>());
}

// =============================================================================
// 5f. serialize_info_network_identity_fields_present
// serializeInfo() expone ipAddress/hostname/macAddress tal cual los pasa el
// caller — WebUI los recalcula desde WiFi.localIP()/OTA_HOSTNAME ".local"/
// WiFi.macAddress() (ver WebUI::handleGetInfo_); aquí solo se verifica que
// JsonCodec los serializa en camelCase sin alterarlos.
// =============================================================================
void test_serialize_info_network_identity_fields_present() {
    size_t written = JsonCodec::serializeInfo(0x0300u, 0u, 0u, -54,
                                              "192.168.1.42", "tamagotchi.local", "AA:BB:CC:DD:EE:FF",
                                              makeValidBattery(), 4120u, 8u,
                                              "SW", 0u, 0u, g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0u, written);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, g_buf, written);
    TEST_ASSERT_EQUAL(DeserializationError::Ok, err.code());

    JsonObject data = doc["data"];
    TEST_ASSERT_FALSE(data["ipAddress"].isNull());
    TEST_ASSERT_FALSE(data["hostname"].isNull());
    TEST_ASSERT_FALSE(data["macAddress"].isNull());
    TEST_ASSERT_EQUAL_STRING("192.168.1.42",      data["ipAddress"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("tamagotchi.local",  data["hostname"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", data["macAddress"].as<const char*>());
}

// =============================================================================
// 5g. serialize_info_network_identity_fields_empty_when_wifi_down
// Con WiFi caído, WebUI pasa cadenas vacías para ipAddress/macAddress (ver
// handleGetInfo_) en vez de omitir los campos — deben seguir SIEMPRE
// presentes (backward compatible, api-conventions.md) pero vacíos, nunca
// null. hostname NO depende de la conexión (es el configurado vía
// OTA_HOSTNAME, no el asignado por DHCP) — sigue presente con su valor normal
// incluso sin WiFi.
// =============================================================================
void test_serialize_info_network_identity_fields_empty_when_wifi_down() {
    size_t written = JsonCodec::serializeInfo(0x0300u, 0u, 0u, 0,
                                              "", "tamagotchi.local", "",
                                              makeUnknownBattery(), 0u, 0u,
                                              "SW", 0u, 0u, g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0u, written);

    JsonDocument doc;
    deserializeJson(doc, g_buf, written);
    JsonObject data = doc["data"];

    TEST_ASSERT_FALSE(data["ipAddress"].isNull());
    TEST_ASSERT_FALSE(data["macAddress"].isNull());
    TEST_ASSERT_EQUAL_STRING("", data["ipAddress"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("", data["macAddress"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("tamagotchi.local", data["hostname"].as<const char*>());
}

// =============================================================================
// 5h. serialize_info_reset_telemetry_fields_present
// serializeInfo() expone resetReason/bootCount/minFreeHeap tal cual los pasa
// el caller (hito 9 — WebUI::handleGetInfo_ los resuelve con la API nativa
// de cada plataforma, ver JsonCodec.h). Aquí solo se verifica que JsonCodec
// los serializa en camelCase con los tipos correctos, sin alterarlos.
// =============================================================================
void test_serialize_info_reset_telemetry_fields_present() {
    size_t written = JsonCodec::serializeInfo(0x0300u, 0u, 0u, 0,
                                              "", "", "",
                                              makeUnknownBattery(), 0u, 0u,
                                              "TASK_WDT", 3u, 21400u, g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0u, written);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, g_buf, written);
    TEST_ASSERT_EQUAL(DeserializationError::Ok, err.code());

    JsonObject data = doc["data"];
    TEST_ASSERT_FALSE(data["resetReason"].isNull());
    TEST_ASSERT_FALSE(data["bootCount"].isNull());
    TEST_ASSERT_FALSE(data["minFreeHeap"].isNull());

    TEST_ASSERT_TRUE(data["resetReason"].is<const char*>());
    TEST_ASSERT_EQUAL_STRING("TASK_WDT", data["resetReason"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT32(3u,     data["bootCount"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(21400u, data["minFreeHeap"].as<uint32_t>());
}

// =============================================================================
// 5d. serialize_state_battery_fields_unknown
// serializeState() con snapshot Unknown — mismo contrato que /info salvo que
// no expone batteryMillivolts (campo exclusivo de /info).
// =============================================================================
void test_serialize_state_battery_fields_unknown() {
    Stats s = makeDefaultStats();
    size_t written = JsonCodec::serializeState(
        s, LifeStage::Baby, 142, 0, false, 0u, makeUnknownBattery(), nullptr, g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0u, written);

    JsonDocument doc;
    deserializeJson(doc, g_buf, written);
    JsonObject data = doc["data"];

    TEST_ASSERT_FALSE(data["batteryValid"].isNull());
    TEST_ASSERT_FALSE(data["batteryValid"].as<bool>());
    TEST_ASSERT_EQUAL_UINT8(0, data["batteryPercent"].as<uint8_t>());
    TEST_ASSERT_FALSE(data["batteryCharging"].as<bool>());
    TEST_ASSERT_TRUE(data["batteryMillivolts"].isNull());  // no es campo de /state
}

// =============================================================================
// 5e. serialize_action_response_battery_fields_valid
// serializeActionResponse() no tenía cobertura de test para los campos de
// batería pese a incluirlos desde ac0464e (mismo trío que /state: percent,
// charging, valid). Verifica snapshot válido + que actionApplied no interfiere.
// =============================================================================
void test_serialize_action_response_battery_fields_valid() {
    Stats s = makeDefaultStats();
    size_t written = JsonCodec::serializeActionResponse(
        s, LifeStage::Baby, 142, 0, false, 0u,
        "feed", 20, makeValidBattery(), nullptr, g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0u, written);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, g_buf, written);
    TEST_ASSERT_EQUAL(DeserializationError::Ok, err.code());

    JsonObject data = doc["data"];
    TEST_ASSERT_FALSE(data["batteryValid"].isNull());
    TEST_ASSERT_TRUE(data["batteryValid"].as<bool>());
    TEST_ASSERT_EQUAL_UINT8(87, data["batteryPercent"].as<uint8_t>());
    TEST_ASSERT_TRUE(data["batteryCharging"].as<bool>());

    // actionApplied sigue presente y sin interferencia con los campos de batería.
    TEST_ASSERT_EQUAL_STRING("feed", doc["actionApplied"]["type"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT32(20, doc["actionApplied"]["amount"].as<uint32_t>());
}

// =============================================================================
// 5f. serialize_action_response_battery_fields_unknown
// serializeActionResponse() con snapshot Unknown (hardware sin soldar) —
// batteryValid false y el resto de campos de batería en su valor neutro,
// pero SIEMPRE presentes (backward compatible, api-conventions.md). Usa
// actionValue=-1 (patrón clean/medicate) para cubrir también ese caso.
// =============================================================================
void test_serialize_action_response_battery_fields_unknown() {
    Stats s = makeDefaultStats();
    size_t written = JsonCodec::serializeActionResponse(
        s, LifeStage::Baby, 142, 0, false, 0u,
        "clean", -1, makeUnknownBattery(), nullptr, g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0u, written);

    JsonDocument doc;
    deserializeJson(doc, g_buf, written);
    JsonObject data = doc["data"];

    TEST_ASSERT_FALSE(data["batteryValid"].isNull());
    TEST_ASSERT_FALSE(data["batteryValid"].as<bool>());
    TEST_ASSERT_EQUAL_UINT8(0, data["batteryPercent"].as<uint8_t>());
    TEST_ASSERT_FALSE(data["batteryCharging"].as<bool>());

    // clean/medicate: actionValue<0 -> "amount" no se incluye en la respuesta.
    TEST_ASSERT_EQUAL_STRING("clean", doc["actionApplied"]["type"].as<const char*>());
    TEST_ASSERT_TRUE(doc["actionApplied"]["amount"].isNull());
}

// =============================================================================
// 5g. serialize_state_growth_cap_null_without_cap
// Sin pausa de crecimiento, growthCap está PRESENTE y vale null (campo
// siempre presente — los clientes no tienen que distinguir ausente/null).
// isNull() de ArduinoJson no distingue ausente de null → se mira el texto.
// =============================================================================
void test_serialize_state_growth_cap_should_be_null_without_cap() {
    Stats s = makeDefaultStats();
    size_t written = JsonCodec::serializeState(
        s, LifeStage::Adult, 300, 0, false, 0u, makeValidBattery(), nullptr, g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0u, written);
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"growthCap\":null"));
}

// =============================================================================
// 5h. serialize_state_growth_cap_as_stage_string
// Con tope Baby, growthCap usa el mismo formato que lifeStage ("Baby").
// =============================================================================
void test_serialize_state_growth_cap_should_use_life_stage_format() {
    Stats s = makeDefaultStats();
    size_t written = JsonCodec::serializeState(
        s, LifeStage::Baby, 5, 0, false, 0u, makeValidBattery(),
        JsonCodec::lifeStageToString(LifeStage::Baby), g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0u, written);

    JsonDocument doc;
    deserializeJson(doc, g_buf, written);
    TEST_ASSERT_TRUE(doc["data"]["growthCap"].is<const char*>());
    TEST_ASSERT_EQUAL_STRING("Baby", doc["data"]["growthCap"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(doc["data"]["lifeStage"].as<const char*>(),
                             doc["data"]["growthCap"].as<const char*>());
}

// =============================================================================
// 5i. serialize_action_response_growth_cap
// Las respuestas de acción replican el state completo, incluido growthCap.
// =============================================================================
void test_serialize_action_response_should_include_growth_cap() {
    Stats s = makeDefaultStats();
    size_t written = JsonCodec::serializeActionResponse(
        s, LifeStage::Baby, 5, 0, false, 0u,
        "feed", 20, makeValidBattery(), "Baby", g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0u, written);

    JsonDocument doc;
    deserializeJson(doc, g_buf, written);
    TEST_ASSERT_EQUAL_STRING("Baby", doc["data"]["growthCap"].as<const char*>());

    written = JsonCodec::serializeActionResponse(
        s, LifeStage::Baby, 5, 0, false, 0u,
        "feed", 20, makeValidBattery(), nullptr, g_buf, sizeof(g_buf));
    TEST_ASSERT_GREATER_THAN(0u, written);
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"growthCap\":null"));
}

// =============================================================================
// 6. parse_feed_payload_valid
// { "amount": 20 } → ParseError::Ok, outAmount = 20.
// =============================================================================
void test_parse_feed_payload_valid() {
    const char* body = "{\"amount\":20}";
    uint8_t amount = 0;
    JsonCodec::ParseError result = JsonCodec::parseAmountPayload(body, strlen(body), amount);

    TEST_ASSERT_EQUAL(JsonCodec::ParseError::Ok, result);
    TEST_ASSERT_EQUAL_UINT8(20, amount);
}

// =============================================================================
// 7. parse_feed_payload_missing_amount
// {} → ParseError::MissingField.
// =============================================================================
void test_parse_feed_payload_missing_amount() {
    const char* body = "{}";
    uint8_t amount = 0;
    JsonCodec::ParseError result = JsonCodec::parseAmountPayload(body, strlen(body), amount);

    TEST_ASSERT_EQUAL(JsonCodec::ParseError::MissingField, result);
}

// =============================================================================
// 8. parse_feed_payload_amount_out_of_range
// { "amount": 150 } → ParseError::InvalidAmount.
// =============================================================================
void test_parse_feed_payload_amount_out_of_range() {
    const char* body = "{\"amount\":150}";
    uint8_t amount = 0;
    JsonCodec::ParseError result = JsonCodec::parseAmountPayload(body, strlen(body), amount);

    TEST_ASSERT_EQUAL(JsonCodec::ParseError::InvalidAmount, result);
}

// =============================================================================
// 9. parse_feed_payload_amount_wrong_type
// { "amount": "twenty" } → ParseError::InvalidAmount (string en vez de número).
// =============================================================================
void test_parse_feed_payload_amount_wrong_type() {
    const char* body = "{\"amount\":\"twenty\"}";
    uint8_t amount = 0;
    JsonCodec::ParseError result = JsonCodec::parseAmountPayload(body, strlen(body), amount);

    TEST_ASSERT_EQUAL(JsonCodec::ParseError::InvalidAmount, result);
}

// =============================================================================
// 10. parse_lights_payload_valid_true
// { "value": true } → ParseError::Ok, outValue = true.
// =============================================================================
void test_parse_lights_payload_valid_true() {
    const char* body = "{\"value\":true}";
    bool value = false;
    JsonCodec::ParseError result = JsonCodec::parseLightsPayload(body, strlen(body), value);

    TEST_ASSERT_EQUAL(JsonCodec::ParseError::Ok, result);
    TEST_ASSERT_TRUE(value);
}

// =============================================================================
// 11. parse_lights_payload_valid_false
// { "value": false } → ParseError::Ok, outValue = false.
// =============================================================================
void test_parse_lights_payload_valid_false() {
    const char* body = "{\"value\":false}";
    bool value = true;
    JsonCodec::ParseError result = JsonCodec::parseLightsPayload(body, strlen(body), value);

    TEST_ASSERT_EQUAL(JsonCodec::ParseError::Ok, result);
    TEST_ASSERT_FALSE(value);
}

// =============================================================================
// 12. parse_lights_payload_invalid_value
// { "value": 42 } → ParseError::InvalidValue (número en vez de bool).
// =============================================================================
void test_parse_lights_payload_invalid_value() {
    const char* body = "{\"value\":42}";
    bool value = false;
    JsonCodec::ParseError result = JsonCodec::parseLightsPayload(body, strlen(body), value);

    TEST_ASSERT_EQUAL(JsonCodec::ParseError::InvalidValue, result);
}

// =============================================================================
// 13. parse_clean_accepts_empty_body
// Body vacío o "{}" → ParseError::Ok (clean/medicate no requieren payload).
// =============================================================================
void test_parse_clean_accepts_empty_body() {
    // Body nulo
    JsonCodec::ParseError r1 = JsonCodec::parseEmptyPayload(nullptr, 0);
    TEST_ASSERT_EQUAL(JsonCodec::ParseError::Ok, r1);

    // Body string vacío
    JsonCodec::ParseError r2 = JsonCodec::parseEmptyPayload("", 0);
    TEST_ASSERT_EQUAL(JsonCodec::ParseError::Ok, r2);

    // Body "{}"
    const char* body3 = "{}";
    JsonCodec::ParseError r3 = JsonCodec::parseEmptyPayload(body3, strlen(body3));
    TEST_ASSERT_EQUAL(JsonCodec::ParseError::Ok, r3);

    // Body "null"
    const char* body4 = "null";
    JsonCodec::ParseError r4 = JsonCodec::parseEmptyPayload(body4, strlen(body4));
    TEST_ASSERT_EQUAL(JsonCodec::ParseError::Ok, r4);
}

// =============================================================================
// 14. parse_malformed_json
// "not json" → ParseError::BadJson.
// =============================================================================
void test_parse_malformed_json() {
    const char* body = "not json";
    uint8_t amount = 0;
    JsonCodec::ParseError result = JsonCodec::parseAmountPayload(body, strlen(body), amount);

    TEST_ASSERT_EQUAL(JsonCodec::ParseError::BadJson, result);
}

// =============================================================================
// main — punto de entrada Unity para env:native
// =============================================================================
int main() {
    UNITY_BEGIN();

    RUN_TEST(test_serialize_state_produces_valid_json);
    RUN_TEST(test_serialize_state_camel_case_fields);
    RUN_TEST(test_serialize_state_lifestage_as_string);
    RUN_TEST(test_serialize_state_below_buffer_limit);
    RUN_TEST(test_serialize_info_includes_required_fields);
    RUN_TEST(test_serialize_info_battery_fields_valid);
    RUN_TEST(test_serialize_info_battery_fields_unknown);
    RUN_TEST(test_serialize_info_battery_raw_fields_present_while_gate_in_progress);
    RUN_TEST(test_serialize_info_network_identity_fields_present);
    RUN_TEST(test_serialize_info_network_identity_fields_empty_when_wifi_down);
    RUN_TEST(test_serialize_info_reset_telemetry_fields_present);
    RUN_TEST(test_serialize_state_battery_fields_unknown);
    RUN_TEST(test_serialize_action_response_battery_fields_valid);
    RUN_TEST(test_serialize_action_response_battery_fields_unknown);
    RUN_TEST(test_serialize_state_growth_cap_should_be_null_without_cap);
    RUN_TEST(test_serialize_state_growth_cap_should_use_life_stage_format);
    RUN_TEST(test_serialize_action_response_should_include_growth_cap);
    RUN_TEST(test_parse_feed_payload_valid);
    RUN_TEST(test_parse_feed_payload_missing_amount);
    RUN_TEST(test_parse_feed_payload_amount_out_of_range);
    RUN_TEST(test_parse_feed_payload_amount_wrong_type);
    RUN_TEST(test_parse_lights_payload_valid_true);
    RUN_TEST(test_parse_lights_payload_valid_false);
    RUN_TEST(test_parse_lights_payload_invalid_value);
    RUN_TEST(test_parse_clean_accepts_empty_body);
    RUN_TEST(test_parse_malformed_json);

    return UNITY_END();
}
