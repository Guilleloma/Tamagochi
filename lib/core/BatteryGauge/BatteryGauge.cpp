// =============================================================================
// BatteryGauge.cpp — implementación del gate de plausibilidad, filtro,
// histéresis de % y heurística de carga. Ver semántica completa en el .h.
// =============================================================================

#include "BatteryGauge.h"

// Fuera de línea: aquí BatteryGauge ya es una clase completa, así que
// construir un Config{} con sus default member initializers es válido
// (ver nota en el .h sobre por qué no se usa un default argument inline).
BatteryGauge::BatteryGauge()
    : BatteryGauge(Config{}) {}

BatteryGauge::BatteryGauge(const Config& config)
    : config_(config) {}

// -----------------------------------------------------------------------------
// addSample — punto de entrada único. Orden de las etapas (importa):
//   1. Gate de validez (puede hacer Unknown -> valid o valid -> Unknown).
//   2. Filtro + curva + histéresis de % (solo tiene efecto si valid_).
//   3. Heurística de carga + fusión con pin (solo con muestra plausible —
//      un glitch fuera de rango no debe ensuciar el tracking de "sostenido"
//      ni de "pendiente").
//   4. Comparar contra la última tupla publicada y devolver si cambió.
// -----------------------------------------------------------------------------
bool BatteryGauge::addSample(uint16_t batteryMv, uint32_t nowMs) {
    const bool plausible = (batteryMv >= config_.minPlausibleMv)
                         && (batteryMv <= config_.maxPlausibleMv);

    updateValidityGate_(batteryMv, plausible);
    updateDisplayedPercent_();

    if (plausible) {
        updateCharging_(batteryMv, nowMs);
    }

    const bool changed = (valid_    != lastPublishedValid_)
                       || (displayedPercent_ != lastPublishedPercent_)
                       || (charging_ != lastPublishedCharging_);

    lastPublishedValid_    = valid_;
    lastPublishedPercent_  = displayedPercent_;
    lastPublishedCharging_ = charging_;

    return changed;
}

// -----------------------------------------------------------------------------
// updateValidityGate_ — implementa el gate de plausibilidad + estabilidad.
//
// Mientras !valid_: cada muestra plausible entra al ring (media móvil de las
// últimas kRingCapacity). En cuanto el ring acumula `samplesToValid` muestras
// Y su dispersión (máx-mín) es ≤ stableBandMv, se declara valid_=true. Las
// muestras fuera de rango simplemente no entran al ring — un pin flotante que
// nunca cae en rango jamás llena el ring y por tanto nunca sale de Unknown.
//
// Mientras valid_: se cuenta una racha de muestras fuera de rango. Un glitch
// aislado (racha < samplesToInvalid) no invalida nada — ni siquiera se resta
// del ring. Al alcanzar samplesToInvalid muestras seguidas fuera de rango se
// vuelve a Unknown y se limpia el ring (exige una racha nueva de 8 para
// recuperar validez, no se reutiliza el histórico anterior).
// -----------------------------------------------------------------------------
void BatteryGauge::updateValidityGate_(uint16_t mv, bool plausible) {
    if (!valid_) {
        if (plausible) {
            pushRing_(mv);
            if (ringCount_ >= config_.samplesToValid) {
                uint16_t mn, mx;
                ringMinMax_(mn, mx);
                if (static_cast<uint16_t>(mx - mn) <= config_.stableBandMv) {
                    valid_ = true;
                    outOfRangeStreak_ = 0;
                }
            }
        }
        // Muestra implausible mientras Unknown: no se acumula rumbo a valid,
        // pero tampoco hace falta "castigar" nada — ya estamos en el peor caso.
        return;
    }

    // valid_ == true
    if (!plausible) {
        outOfRangeStreak_++;
        if (outOfRangeStreak_ >= config_.samplesToInvalid) {
            valid_ = false;
            clearRing_();
            outOfRangeStreak_ = 0;
        }
        return;
    }

    outOfRangeStreak_ = 0;
    pushRing_(mv);
}

// -----------------------------------------------------------------------------
// updateDisplayedPercent_ — aplica curva mV->% sobre la media filtrada y
// solo actualiza el valor mostrado si difiere en >= percentHysteresis del
// mostrado anteriormente. Evita parpadeo de +-1% por ruido de ADC.
//
// Al perder validez se resetea a 0 y se limpia el flag de inicialización:
// al recuperar validez, la primera lectura fija el valor mostrado sin demora
// (no tiene sentido aplicar histéresis contra un valor obsoleto de "antes de
// Unknown").
// -----------------------------------------------------------------------------
void BatteryGauge::updateDisplayedPercent_() {
    if (!valid_) {
        displayedPercent_      = 0;
        hysteresisInitialized_ = false;
        return;
    }

    const uint8_t rawPercent = percentFromMv(ringAverage_());

    if (!hysteresisInitialized_) {
        displayedPercent_      = rawPercent;
        hysteresisInitialized_ = true;
        return;
    }

    const int diff = static_cast<int>(rawPercent) - static_cast<int>(displayedPercent_);
    if ((diff >= 0 ? diff : -diff) >= static_cast<int>(config_.percentHysteresis)) {
        displayedPercent_ = rawPercent;
    }
}

// -----------------------------------------------------------------------------
// updateCharging_ — heurística por umbral sostenido + pendiente, fusionada
// con el pin externo si hay uno configurado (setChargeSense).
//
// Umbral sostenido: se recuerda desde cuándo mv >= fullThresholdMv de forma
// ininterrumpida; si ese tiempo supera fullSustainMs, la heurística dice
// "cargando". Cualquier lectura por debajo del umbral reinicia el contador.
//
// Pendiente: ventana "tumbling" de slopeWindowMs — al expirar se reinicia
// con el mínimo visto = la muestra actual. Dentro de la ventana se seguimiento
// del mínimo; si la muestra actual supera ese mínimo en >= riseDeltaMv, la
// heurística dice "cargando" (subida sostenida propia de un cargador).
//
// Fusión con pin (ver plan):
//   VbusPresence      : HIGH y no llena -> charging; HIGH y llena -> sin rayo;
//                        LOW -> descargando, anula la heurística.
//   ChargeLedActiveLow: LOW (activo) -> charging; HIGH (inactivo) -> heurística.
//   None               : heurística pura.
// -----------------------------------------------------------------------------
void BatteryGauge::updateCharging_(uint16_t mv, uint32_t nowMs) {
    if (mv >= config_.fullThresholdMv) {
        if (!fullSinceValid_) {
            fullSinceValid_ = true;
            fullSinceMs_    = nowMs;
        }
    } else {
        fullSinceValid_ = false;
    }
    const bool chargingByThreshold = fullSinceValid_
        && (nowMs - fullSinceMs_ >= config_.fullSustainMs);

    if (!slopeWindowStarted_ || (nowMs - slopeWindowStartMs_ > config_.slopeWindowMs)) {
        slopeWindowStarted_ = true;
        slopeWindowStartMs_ = nowMs;
        slopeWindowMinMv_   = mv;
    } else if (mv < slopeWindowMinMv_) {
        slopeWindowMinMv_ = mv;
    }
    const bool chargingBySlope = (mv >= slopeWindowMinMv_)
        && (static_cast<uint16_t>(mv - slopeWindowMinMv_) >= config_.riseDeltaMv);

    const bool heuristic = chargingByThreshold || chargingBySlope;
    const bool isFull    = (mv >= config_.fullThresholdMv);

    switch (chargeMode_) {
        case ChargeSenseMode::VbusPresence:
            // LOW: nada enchufado -> descargando, la heurística no puede contradecirlo.
            // HIGH + llena: alimentado pero sin necesidad real de mostrar el rayo.
            charging_ = chargeRawLevel_ ? !isFull : false;
            break;
        case ChargeSenseMode::ChargeLedActiveLow:
            // LOW = LED encendido = cargando. HIGH = LED apagado -> decide la heurística.
            charging_ = chargeRawLevel_ ? heuristic : true;
            break;
        case ChargeSenseMode::None:
        default:
            charging_ = heuristic;
            break;
    }
}

// -----------------------------------------------------------------------------
// setChargeSense — guarda modo + nivel crudo; se aplica en el próximo addSample.
// -----------------------------------------------------------------------------
void BatteryGauge::setChargeSense(ChargeSenseMode mode, bool rawLevel) {
    chargeMode_     = mode;
    chargeRawLevel_ = rawLevel;
}

// -----------------------------------------------------------------------------
// snapshot — estado publicable actual, sin efectos secundarios.
// -----------------------------------------------------------------------------
BatterySnapshot BatteryGauge::snapshot() const {
    BatterySnapshot snap;
    snap.valid      = valid_;
    snap.percent    = displayedPercent_;
    snap.charging   = charging_;
    snap.millivolts = valid_ ? ringAverage_() : 0;
    return snap;
}

// -----------------------------------------------------------------------------
// percentFromMv — curva LiPo 1S por tramos, interpolación lineal entre
// breakpoints, clamp fuera de rango. Redondeo al entero más cercano
// (+span/2 antes de dividir) para evitar sesgo sistemático hacia abajo.
// -----------------------------------------------------------------------------
uint8_t BatteryGauge::percentFromMv(uint16_t mv) {
    static constexpr uint16_t kBreakMv[]      = {3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
    static constexpr uint8_t  kBreakPercent[] = {0,    2,    5,    12,   28,   45,   62,   78,   90,   100};
    static constexpr uint8_t  kBreakCount     = sizeof(kBreakMv) / sizeof(kBreakMv[0]);

    if (mv <= kBreakMv[0])               return kBreakPercent[0];
    if (mv >= kBreakMv[kBreakCount - 1]) return kBreakPercent[kBreakCount - 1];

    for (uint8_t i = 0; i < kBreakCount - 1; ++i) {
        const uint16_t loMv = kBreakMv[i];
        const uint16_t hiMv = kBreakMv[i + 1];
        if (mv < loMv || mv > hiMv) continue;

        const uint8_t  loPct = kBreakPercent[i];
        const uint8_t  hiPct = kBreakPercent[i + 1];
        const uint32_t span     = static_cast<uint32_t>(hiMv - loMv);
        const uint32_t deltaMv  = static_cast<uint32_t>(mv - loMv);
        const uint32_t deltaPct = static_cast<uint32_t>(hiPct - loPct);
        const uint32_t interpolated = (deltaMv * deltaPct + span / 2) / span;
        return static_cast<uint8_t>(loPct + interpolated);
    }
    return 0;  // inalcanzable: los breakpoints cubren [3300, 4200] sin huecos
}

// -----------------------------------------------------------------------------
// Ring buffer — helpers privados. `ring_[0..ringCount_)` siempre contiene las
// muestras actualmente "en ventana" en orden de inserción (válido tanto antes
// de llenarse como en régimen circular, porque solo se itera hasta ringCount_).
// -----------------------------------------------------------------------------
void BatteryGauge::pushRing_(uint16_t mv) {
    ring_[ringHead_] = mv;
    ringHead_ = static_cast<uint8_t>((ringHead_ + 1) % kRingCapacity);
    if (ringCount_ < kRingCapacity) ringCount_++;
}

void BatteryGauge::clearRing_() {
    ringCount_ = 0;
    ringHead_  = 0;
}

uint16_t BatteryGauge::ringAverage_() const {
    if (ringCount_ == 0) return 0;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ringCount_; ++i) sum += ring_[i];
    return static_cast<uint16_t>(sum / ringCount_);
}

void BatteryGauge::ringMinMax_(uint16_t& outMin, uint16_t& outMax) const {
    outMin = ring_[0];
    outMax = ring_[0];
    for (uint8_t i = 1; i < ringCount_; ++i) {
        if (ring_[i] < outMin) outMin = ring_[i];
        if (ring_[i] > outMax) outMax = ring_[i];
    }
}
