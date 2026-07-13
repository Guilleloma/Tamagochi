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
    : config_(config) {
    // Clamp defensivo: samplesToValid no puede superar kRingCapacity (8) — el
    // ring físico jamás acumula más de kRingCapacity muestras (ringCount_ satura
    // ahí, ver pushRing_). Un Config con samplesToValid > 8 (p.ej. por error de
    // configuración) haría el gate de validez inalcanzable SIN error visible:
    // ringCount_ nunca llegaría a cumplir `ringCount_ >= samplesToValid` y el
    // gauge se quedaría en Unknown para siempre, silenciosamente. Clampar aquí
    // hace explícito el límite real en vez de fallar mudo en runtime.
    if (config_.samplesToValid > kRingCapacity) {
        config_.samplesToValid = kRingCapacity;
    }
}

// -----------------------------------------------------------------------------
// addSample — punto de entrada único. Orden de las etapas (importa):
//   1. Gate de validez (puede hacer Unknown -> valid o valid -> Unknown).
//   2. Filtro + curva + histéresis de % (solo tiene efecto si valid_).
//   3. Heurística de carga + fusión con pin (solo con muestra plausible Y
//      valid_ — un glitch fuera de rango no debe ensuciar el tracking de
//      "sostenido" ni de "pendiente", y la API nunca debe reportar charging
//      mientras el propio voltaje aún no es fiable (valid==false). Ver
//      resetChargingState_ para el reset al perder validez).
//   4. Comparar contra la última tupla publicada y devolver si cambió.
// -----------------------------------------------------------------------------
bool BatteryGauge::addSample(uint16_t batteryMv, uint32_t nowMs) {
    const bool plausible = (batteryMv >= config_.minPlausibleMv)
                         && (batteryMv <= config_.maxPlausibleMv);

    updateValidityGate_(batteryMv, plausible);
    updateDisplayedPercent_();

    if (valid_ && plausible) {
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
// Y su dispersión (máx-mín) es ≤ stableBandMv, se declara valid_=true.
//
// CRÍTICO: una muestra implausible mientras !valid_ debe limpiar el ring
// (clearRing_), no solo "no acumular". El contrato del .h exige `samplesToValid`
// muestras CONSECUTIVAS — sin este reset, un pin flotante que alterna dentro y
// fuera de rango (p.ej. 5000, 3000, 5000, 3010, 5000, 3020...) nunca ve una
// implausible "contar" contra él: cada muestra plausible se sigue empujando al
// ring aunque estén separadas por implausibles, y con suficientes rondas el
// ring acumula 8 muestras plausibles NO consecutivas que además caen dentro de
// stableBandMv → valida por error. clearRing_() fuerza a que la racha de 8 sea
// realmente ininterrumpida.
//
// Mientras valid_: se cuenta una racha de muestras fuera de rango. Un glitch
// aislado (racha < samplesToInvalid) no invalida nada — ni siquiera se resta
// del ring. Al alcanzar samplesToInvalid muestras seguidas fuera de rango se
// vuelve a Unknown, se limpia el ring (exige una racha nueva de 8 para
// recuperar validez, no se reutiliza el histórico anterior) y se resetea toda
// la heurística de carga (resetChargingState_) — ver comentario ahí sobre por
// qué `charging` no puede sobrevivir a la pérdida de validez.
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
        } else {
            // Muestra implausible mientras Unknown: rompe la racha consecutiva.
            // Sin esto, un pin flotante que oscila dentro/fuera de rango podría
            // acumular 8 muestras plausibles dispersas en el tiempo y validar
            // por error (ver comentario CRÍTICO arriba).
            clearRing_();
        }
        return;
    }

    // valid_ == true
    if (!plausible) {
        outOfRangeStreak_++;
        if (outOfRangeStreak_ >= config_.samplesToInvalid) {
            valid_ = false;
            clearRing_();
            outOfRangeStreak_ = 0;
            resetChargingState_();
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
// Excepción a la histéresis: si el % crudo es exactamente 0 o 100 (extremos
// físicos reales — batería a 3300mV o menos, o a 4200mV o más), se fuerza la
// actualización aunque la diferencia sea menor que percentHysteresis. Sin esto,
// con percentHysteresis=3 el valor mostrado puede quedarse "pegado" en 1-2% al
// descargarse (nunca llega a mostrar 0%) o en 98-99% al cargar (nunca llega a
// 100%) — el usuario nunca ve los extremos aunque la batería sí los alcance.
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

    const bool isExtreme = (rawPercent == 0) || (rawPercent == 100);
    const int diff = static_cast<int>(rawPercent) - static_cast<int>(displayedPercent_);
    if (isExtreme || (diff >= 0 ? diff : -diff) >= static_cast<int>(config_.percentHysteresis)) {
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
// Pendiente: ventana "tumbling" de slopeWindowMs. Dentro de la ventana se hace
// seguimiento del mínimo visto; si la muestra actual supera ese mínimo en
// >= riseDeltaMv, se marca slopeWindowSawRise_=true y chargingBySlope_=true
// (subida sostenida propia de un cargador).
//
// chargingBySlope_ es PERSISTENTE a través de las rotaciones de ventana — al
// expirar una ventana, solo se apaga si esa ventana que se cierra completa
// (slopeWindowSawRise_) nunca vio una subida. Si la vio, se mantiene true
// mientras arranca la ventana nueva, que la reconfirmará o la desmentirá a lo
// largo de su propio ciclo. Sin esto (recalcular desde cero en cada rotación,
// con mínimo = muestra actual), una carga lenta y continua "apagaba" el rayo
// durante los primeros ~riseDeltaMv/tasa segundos de cada ventana nueva —
// parpadeo visible cada slopeWindowMs (10 min por defecto).
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

    const bool windowExpired = slopeWindowStarted_
        && (nowMs - slopeWindowStartMs_ > config_.slopeWindowMs);

    if (!slopeWindowStarted_) {
        slopeWindowStarted_  = true;
        slopeWindowStartMs_  = nowMs;
        slopeWindowMinMv_    = mv;
        slopeWindowSawRise_  = false;
    } else if (windowExpired) {
        // La ventana que se cierra decide si chargingBySlope_ se apaga: solo si
        // NINGUNA muestra de esa ventana llegó a ver la subida (ver comentario
        // arriba). Si sí la vio, se conserva true — la ventana nueva la
        // reconfirmará por su cuenta, sin parpadeo intermedio.
        if (!slopeWindowSawRise_) {
            chargingBySlope_ = false;
        }
        slopeWindowStartMs_ = nowMs;
        slopeWindowMinMv_   = mv;
        slopeWindowSawRise_ = false;
    } else if (mv < slopeWindowMinMv_) {
        slopeWindowMinMv_ = mv;
    }

    if ((mv >= slopeWindowMinMv_)
        && (static_cast<uint16_t>(mv - slopeWindowMinMv_) >= config_.riseDeltaMv)) {
        slopeWindowSawRise_ = true;
        chargingBySlope_    = true;
    }

    const bool heuristic = chargingByThreshold || chargingBySlope_;
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
// resetChargingState_ — limpia toda la heurística de carga (umbral sostenido +
// pendiente) y fuerza charging_=false. Se invoca exclusivamente al perder
// validez (ver updateValidityGate_).
//
// Por qué es necesario: addSample() solo llama a updateCharging_() cuando
// valid_==true, así que charging_ nunca se recalcula mientras !valid_. Sin
// este reset explícito, charging_ se quedaría "congelado" en su último valor
// (potencialmente true) durante todo el tiempo en Unknown — violando el
// contrato del .h de que BatterySnapshot::charging es false si !valid.
// -----------------------------------------------------------------------------
void BatteryGauge::resetChargingState_() {
    fullSinceValid_     = false;
    fullSinceMs_        = 0;
    slopeWindowStarted_ = false;
    slopeWindowStartMs_ = 0;
    slopeWindowMinMv_   = 0;
    slopeWindowSawRise_ = false;
    chargingBySlope_    = false;
    charging_           = false;
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
