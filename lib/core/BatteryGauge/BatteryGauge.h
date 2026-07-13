#pragma once
#include <stdint.h>

// =============================================================================
// BatteryGauge — lógica pura de medición de batería. Sin Arduino.h.
// Compilable en host-native para tests (pio test -e native).
//
// Responsabilidad: convertir una serie de lecturas de voltaje (mV) en un
// estado publicable {valid, percent, charging} estable y sin ruido, sin
// asumir NADA sobre si hay hardware (divisor) soldado o no.
//
// Problema que resuelve: el ESP32-S3 no tiene forma fiable de detectar en
// hardware si un pin ADC está flotante (sin divisor soldado) o conectado a
// un divisor real. La solución es 100% software — un "gate de plausibilidad
// + estabilidad" (ver detalle en cada sección):
//
//   1. Boot → estado Unknown (valid=false). Nunca se asume batería presente.
//   2. Salir de Unknown exige `samplesToValid` (8) muestras CONSECUTIVAS
//      plausibles (dentro de [minPlausibleMv, maxPlausibleMv]) Y estables
//      (dispersión máx-mín de esas 8 muestras ≤ stableBandMv). Un pin
//      flotante produce ruido aleatorio: o sale de rango, o dispersa más de
//      stableBandMv — nunca cumple ambas condiciones 8 veces seguidas.
//   3. Volver a Unknown exige `samplesToInvalid` (4) muestras CONSECUTIVAS
//      fuera de rango. Un glitch aislado (1 lectura mala) NO invalida —
//      evita parpadeo del icono por ruido eléctrico puntual.
//
// Filtro: media móvil de las últimas 8 muestras plausibles (mismo ring que
// alimenta el gate de estabilidad) — suaviza el ADC antes de aplicar la
// curva mV→%.
//
// Histéresis de %: el valor mostrado (`percent` en BatterySnapshot) solo se
// actualiza cuando el % crudo (curva sobre la media filtrada) difiere del
// mostrado en ≥ percentHysteresis puntos. Evita que el dígito "parpadee"
// ±1% por ruido de conversión ADC.
//
// Heurística de carga (sin pin de sensado, ChargeSenseMode::None):
//   charging = (mV ≥ fullThresholdMv sostenido fullSustainMs)
//           || (mV sube ≥ riseDeltaMv dentro de una ventana de slopeWindowMs)
// Con pin de sensado (ver ChargeSenseMode), el nivel del pin prevalece sobre
// la heurística (fusión, ver .cpp).
//
// El tiempo entra siempre por parámetro (`nowMs`) — nunca se llama a millis()
// aquí. Esto hace el módulo determinista y 100% testeable en host-native.
// =============================================================================

// -----------------------------------------------------------------------------
// ChargeSenseMode — cómo se interpreta (si existe) el pin externo de carga.
//   None              : sin pin soldado — solo heurística por voltaje.
//   VbusPresence      : pin refleja presencia de 5V del cargador (divisor).
//   ChargeLedActiveLow: pin refleja el LED de carga del módulo (activo-LOW).
// -----------------------------------------------------------------------------
enum class ChargeSenseMode : uint8_t {
    None = 0,
    VbusPresence,
    ChargeLedActiveLow
};

// -----------------------------------------------------------------------------
// BatterySnapshot — estado publicable, listo para UI/API.
//   valid       : false mientras no se confirme hardware presente (Unknown).
//   percent     : 0-100, con histéresis aplicada. 0 si !valid.
//   charging    : heurística/pin fusionados. false si !valid.
//   millivolts  : media filtrada de la batería. 0 si !valid.
// -----------------------------------------------------------------------------
struct BatterySnapshot {
    bool     valid      = false;
    uint8_t  percent    = 0;
    bool     charging   = false;
    uint16_t millivolts = 0;
};

class BatteryGauge {
public:
    // Umbrales — ver comentarios de cada campo. Todos con valor por defecto
    // acordado en el plan; ajustables por test o por variante de hardware.
    struct Config {
        // --- Gate de plausibilidad + estabilidad (detección Unknown) ---
        uint16_t minPlausibleMv   = 2800;   // por debajo: LiPo descargada más allá de lo razonable / pin no conectado a batería real
        uint16_t maxPlausibleMv   = 4350;   // por encima: sobretensión imposible en LiPo 1S / pin flotante alto
        uint16_t stableBandMv     = 120;    // dispersión máx-mín tolerada en las últimas `samplesToValid` muestras
        uint8_t  samplesToValid   = 8;      // muestras consecutivas plausibles+estables para salir de Unknown
        uint8_t  samplesToInvalid = 4;      // muestras consecutivas fuera de rango para volver a Unknown

        // --- Heurística de carga sin pin ---
        uint16_t fullThresholdMv  = 4180;   // mV a partir de los cuales se considera "cerca de llena"
        uint32_t fullSustainMs    = 60000;  // sostenido este tiempo ⇒ heurística de carga por umbral
        uint16_t riseDeltaMv      = 25;     // subida mínima dentro de la ventana ⇒ heurística de carga por pendiente
        uint32_t slopeWindowMs    = 600000; // ventana de 10 min para medir la pendiente

        // --- Histéresis del % mostrado ---
        uint8_t  percentHysteresis = 3;     // diferencia mínima (puntos %) para actualizar el valor publicado
    };

    // NOTA: se exponen dos constructores en lugar de un único constructor con
    // argumento por defecto `= Config{}` — Clang rechaza evaluar los default
    // member initializers de un struct anidado (Config) dentro de un default
    // argument de la propia clase contenedora todavía incompleta (BatteryGauge).
    // Ambos se definen fuera de línea en el .cpp, donde la clase ya es completa.
    BatteryGauge();
    explicit BatteryGauge(const Config& config);

    // Registra una lectura de batería en mV, ya compensada por el divisor
    // (ver BatteryMonitor). `nowMs` es el reloj del llamador (millis() en
    // producción, valor sintético en tests) — nunca se lee tiempo internamente.
    // Devuelve true si el snapshot publicable {valid, percent, charging}
    // cambió respecto a la llamada anterior (para que el caller decida si
    // publicar un evento).
    bool addSample(uint16_t batteryMv, uint32_t nowMs);

    // Informa el nivel crudo del pin de sensado de carga (si existe) y su
    // modo de interpretación. Se aplica en la siguiente llamada a addSample().
    // rawLevel = true equivale a HIGH; false a LOW (mismo criterio que digitalRead()).
    void setChargeSense(ChargeSenseMode mode, bool rawLevel);

    // Estado publicable actual (sin efectos secundarios).
    BatterySnapshot snapshot() const;

    // Curva LiPo 1S por tramos, interpolación lineal entre breakpoints,
    // clamp fuera de rango. Pura función de mV → %, sin estado.
    static uint8_t percentFromMv(uint16_t mv);

public:
    // Capacidad física del ring de muestras — expuesta para que el ctor pueda
    // clampar samplesToValid contra ella (ver BatteryGauge(const Config&) en el
    // .cpp): un samplesToValid > kRingCapacity haría la validez inalcanzable en
    // silencio, porque ringCount_ nunca puede superar kRingCapacity.
    static constexpr uint8_t kRingCapacity = 8;

private:

    Config config_;

    // --- Gate de validez + filtro (ring de últimas muestras plausibles) ---
    uint16_t ring_[kRingCapacity] = {};
    uint8_t  ringCount_ = 0;
    uint8_t  ringHead_  = 0;
    uint8_t  outOfRangeStreak_ = 0;   // muestras consecutivas fuera de rango mientras valid_
    bool     valid_ = false;

    // --- % mostrado con histéresis ---
    uint8_t displayedPercent_    = 0;
    bool    hysteresisInitialized_ = false;

    // --- Heurística de carga ---
    bool     fullSinceValid_   = false;
    uint32_t fullSinceMs_      = 0;
    bool     slopeWindowStarted_ = false;
    uint32_t slopeWindowStartMs_ = 0;
    uint16_t slopeWindowMinMv_   = 0;
    // slopeWindowSawRise_: true si la ventana ACTUAL (aún abierta) ya detectó
    // en alguna muestra una subida >= riseDeltaMv. Se usa solo para decidir,
    // al expirar la ventana, si chargingBySlope_ debe apagarse (ver .cpp).
    bool     slopeWindowSawRise_ = false;
    // chargingBySlope_ es el estado PERSISTENTE de la heurística de pendiente —
    // a diferencia de una ventana "tumbling" que se recalcula desde cero en cada
    // rotación, este flag sobrevive a la rotación de ventana y solo se apaga si
    // una ventana completa transcurre sin ver ninguna subida (ver updateCharging_
    // en el .cpp para el porqué: evita que el rayo parpadee ~50s en cada frontera
    // de los 10 min de slopeWindowMs durante una carga lenta y continua).
    bool     chargingBySlope_ = false;
    bool     charging_ = false;

    // --- Fusión con pin externo ---
    ChargeSenseMode chargeMode_     = ChargeSenseMode::None;
    bool             chargeRawLevel_ = false;

    // --- Última tupla publicada (para el "solo si cambió" de addSample) ---
    bool    lastPublishedValid_    = false;
    uint8_t lastPublishedPercent_  = 0;
    bool    lastPublishedCharging_ = false;

    void pushRing_(uint16_t mv);
    void clearRing_();
    uint16_t ringAverage_() const;
    void ringMinMax_(uint16_t& outMin, uint16_t& outMax) const;

    void updateValidityGate_(uint16_t mv, bool plausible);
    void updateDisplayedPercent_();
    void updateCharging_(uint16_t mv, uint32_t nowMs);

    // Resetea todo el estado de la heurística de carga (umbral sostenido +
    // pendiente) y el `charging_` publicado. Se llama al PERDER validez —
    // ver comentario en updateValidityGate_ (.cpp) sobre por qué es necesario:
    // sin esto, `charging` podía quedar en true (obsoleto) mientras
    // valid==false, violando el contrato "charging siempre false si !valid".
    void resetChargingState_();
};
