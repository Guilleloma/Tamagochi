#pragma once
// =============================================================================
// BatteryMonitor — infra Arduino: ADC del divisor de batería + pin opcional
// de sensado de carga + publicación de eventos. Clona el patrón no bloqueante
// de WifiManager (ctor(EventBus&), begin(), loop() con guard de tiempo).
//
// Toda la lógica de decisión (curva mV→%, gate Unknown, histéresis, heurística
// de carga) vive en BatteryGauge (lib/core — puro, testeado en native). Este
// módulo solo se encarga de leer el ADC/pin de hardware y alimentar el gauge.
//
// Sin hardware soldado (divisor pendiente — ver docs/hardware-battery.md), el
// pin queda flotante: BatteryGauge nunca sale de Unknown y este módulo no
// publica eventos "falsos" — solo un log informativo único.
//
// Ciclo de vida:
//   1. begin() → configura resolución/atenuación del ADC. < 5 ms.
//   2. loop()  → llamar cada vuelta del loop principal. Muestrea cada
//                kSampleIntervalMs y publica BatteryStateChanged solo si el
//                snapshot {valid, percent, charging} cambió.
//
// Dependencias externas: Arduino.h (analogRead*). No testeable host-native
// (por eso vive en lib/infra, no en lib/core — ver rules del repo).
// =============================================================================

#include <stdint.h>
#include "BatteryGauge.h"

class EventBus;

class BatteryMonitor {
public:
    explicit BatteryMonitor(EventBus& bus);

    // Configura el ADC (resolución 12 bits, atenuación 11dB) y, si
    // BATTERY_CHARGE_PIN_ENABLED, el pin de sensado de carga. No bloqueante.
    void begin();

    // Muestrea el ADC y actualiza BatteryGauge. Guard interno: actúa solo
    // cada kSampleIntervalMs. Publica BatteryStateChanged únicamente cuando
    // el snapshot publicado cambia (patrón WifiManager::publishState_).
    void loop();

    // Snapshot actual del gauge — usado por WebUI (JsonCodec) y el renderer.
    // Sin hardware detectado devuelve {valid:false, percent:0, charging:false, mv:0}.
    BatterySnapshot snapshot() const { return gauge_.snapshot(); }

    // Referencia al BatteryGauge puro subyacente — permite a los consumidores
    // que solo necesitan el gauge (p.ej. WebUI) depender de BatteryGauge (lib/core,
    // sin Arduino.h, compila en cualquier plataforma) en lugar de BatteryMonitor
    // (infra, arrastra analogRead*/Arduino.h — ver comentario de desacople en
    // WebUI.h sobre por qué esto importa para el build ESP8266).
    const BatteryGauge& gauge() const { return gauge_; }

private:
    EventBus&    bus_;
    BatteryGauge gauge_;

    uint32_t lastSampleMs_ = 0;

    // Log único informativo si el gauge nunca llega a validarse (hardware
    // ausente/pin flotante) — evita spamear el log cada kSampleIntervalMs.
    bool     everBecameValid_    = false;
    bool     loggedNeverValid_   = false;
    uint16_t consecutiveInvalidLoops_ = 0;

    // Umbral de intentos (loops) antes de asumir "no hay hardware" y loguear
    // una vez. Da margen de sobra sobre los ~8 muestras (16s) que necesita
    // BatteryGauge para validar si el divisor SÍ está soldado.
    static constexpr uint16_t kNeverValidLogThresholdLoops = 10;

    // Intervalo entre muestras ADC (ms). 2s es más que suficiente para un
    // valor que varía lentamente (descarga de LiPo) y barato en heap/CPU.
    static constexpr uint32_t kSampleIntervalMs = 2000;

    // Ráfaga de 9 analogReadMilliVolts(), descarta la primera (asentamiento
    // del mux del ADC) y promedia las 8 restantes. Aplica compensación del
    // divisor (BATTERY_DIVIDER_NUM/DEN) antes de devolver mV de la batería.
    uint16_t readBatteryMv_();

    // Publica BatteryStateChanged si `changed` (viene de BatteryGauge::addSample)
    // y actualiza el estado del log único de "nunca validó".
    void publishIfChanged_(bool changed);
};
