// =============================================================================
// BatteryMonitor.cpp — implementación ADC + fusión con BatteryGauge.
//
// Cumple rules/error-handling.md: nunca swallow errors — el caso "sin
// hardware" no es un error, es un estado válido (Unknown) que se loguea una
// única vez para no ensuciar el log en cada muestreo.
//
// Guard #ifndef UNIT_TEST: este archivo depende de Arduino.h (analogRead*)
// y no se compila en env:native (mismo patrón que WifiManager.cpp:13).
// =============================================================================

#ifndef UNIT_TEST

#include "BatteryMonitor.h"
#include "EventBus.h"
#include "Logger.h"
#include "config.h"
#include <Arduino.h>

BatteryMonitor::BatteryMonitor(EventBus& bus)
    : bus_(bus)
{}

// -----------------------------------------------------------------------------
// begin — configura el ADC. No bloqueante.
//
// Con BATTERY_SENSE_ENABLED=0 (feature compilada-fuera) es un no-op: el
// gauge se queda en Unknown para siempre y snapshot() siempre devuelve
// {valid:false}, sin tocar ningún pin.
// -----------------------------------------------------------------------------
void BatteryMonitor::begin() {
#if BATTERY_SENSE_ENABLED
    // 12 bits (0-4095) + atenuación 11dB (fondo de escala ~3100 mV en el pin;
    // con el divisor 2×100k el pin ve como máximo la mitad de Vbat, ~2175 mV
    // para una LiPo llena a 4.35V — dentro del fondo de escala).
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
    LOG_INFO("[battery] ADC configurado en GPIO%d (12 bits, 11dB, divisor %d/%d)",
             PIN_BATTERY_ADC, BATTERY_DIVIDER_NUM, BATTERY_DIVIDER_DEN);

#if BATTERY_CHARGE_PIN_ENABLED
    // Sin pull interno: el nivel lo definen las resistencias externas del
    // divisor de sensado de carga (ver docs/hardware-battery.md).
    pinMode(PIN_CHARGE_SENSE, INPUT);
    LOG_INFO("[battery] pin de carga en GPIO%d (modo=%d)", PIN_CHARGE_SENSE, BATTERY_CHARGE_SENSE_MODE);
#endif

#else
    LOG_INFO("[battery] BATTERY_SENSE_ENABLED=0 — medidor de bateria desactivado en compilacion");
#endif
}

// -----------------------------------------------------------------------------
// loop — muestrea el ADC cada kSampleIntervalMs y alimenta BatteryGauge.
// -----------------------------------------------------------------------------
void BatteryMonitor::loop() {
#if BATTERY_SENSE_ENABLED
    uint32_t now = millis();
    if (now - lastSampleMs_ < kSampleIntervalMs) return;
    lastSampleMs_ = now;

#if BATTERY_CHARGE_PIN_ENABLED
    bool rawLevelHigh = (digitalRead(PIN_CHARGE_SENSE) == HIGH);
    gauge_.setChargeSense(static_cast<ChargeSenseMode>(BATTERY_CHARGE_SENSE_MODE), rawLevelHigh);
#endif

    uint16_t batteryMv = readBatteryMv_();
    bool changed = gauge_.addSample(batteryMv, now);
    publishIfChanged_(changed);
#endif
}

// -----------------------------------------------------------------------------
// readBatteryMv_ — ráfaga de 9 analogReadMilliVolts() (calibración eFuse del
// ESP32-S3 — más precisa que analogRead crudo). Se descarta la primera
// lectura (asentamiento del multiplexor ADC tras seleccionar canal) y se
// promedian las 8 siguientes. Compensa el divisor de tensión externo antes
// de devolver el mV equivalente en bornes de la batería.
// -----------------------------------------------------------------------------
uint16_t BatteryMonitor::readBatteryMv_() {
#if BATTERY_SENSE_ENABLED
    (void)analogReadMilliVolts(PIN_BATTERY_ADC);  // descartada — asentamiento del mux

    uint32_t sum = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        sum += analogReadMilliVolts(PIN_BATTERY_ADC);
    }
    uint16_t pinMv = static_cast<uint16_t>(sum / 8);

    return static_cast<uint16_t>(
        (static_cast<uint32_t>(pinMv) * BATTERY_DIVIDER_NUM) / BATTERY_DIVIDER_DEN);
#else
    return 0;
#endif
}

// -----------------------------------------------------------------------------
// publishIfChanged_ — publica BatteryStateChanged solo si `changed` (viene de
// BatteryGauge::addSample, que ya compara contra el último snapshot publicado
// — mismo patrón que WifiManager::publishState_ comparando `desired != state_`).
//
// También gestiona el log único de "nunca sale de Unknown": tras
// kNeverValidLogThresholdLoops intentos sin validar (y sin haber validado
// nunca), informa una vez que no se detecta hardware — no es un error, es el
// comportamiento esperado antes de soldar el divisor.
// -----------------------------------------------------------------------------
void BatteryMonitor::publishIfChanged_(bool changed) {
    BatterySnapshot snap = gauge_.snapshot();

    if (snap.valid) {
        everBecameValid_ = true;
    } else if (!everBecameValid_ && !loggedNeverValid_) {
        if (consecutiveInvalidLoops_ < UINT16_MAX) consecutiveInvalidLoops_++;
        if (consecutiveInvalidLoops_ >= kNeverValidLogThresholdLoops) {
            LOG_INFO("[battery] sin hardware detectado (pin flotante o divisor sin soldar) — permanece en Unknown");
            loggedNeverValid_ = true;
        }
    }

    if (!changed) return;

    LOG_INFO("[battery] valid=%d percent=%u%% charging=%d mv=%u",
             snap.valid ? 1 : 0, snap.percent, snap.charging ? 1 : 0, snap.millivolts);

    // Payload empaquetado — ver comentario de BatteryStateChanged en EventBus.h.
    int32_t payload = static_cast<int32_t>(snap.percent & 0x7F)
                     | (snap.charging ? (1 << 7) : 0)
                     | (snap.valid    ? (1 << 8) : 0);

    bus_.publish({EventType::BatteryStateChanged, payload});
}

#endif  // UNIT_TEST
