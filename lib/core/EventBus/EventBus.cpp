// =============================================================================
// EventBus — implementación
// Los arrays se inicializan in-class en el header (`= {}`), así que no se
// necesita constructor explícito.
// =============================================================================

#include "EventBus.h"

bool EventBus::subscribe(EventType type, EventHandler handler) {
    uint8_t idx = static_cast<uint8_t>(type);
    if (counts_[idx] >= kMaxSubscribersPerEvent) {
        // Límite alcanzado — el llamante debe revisar kMaxSubscribersPerEvent
        // o reducir el número de suscriptores para este tipo de evento.
        return false;
    }
    handlers_[idx][counts_[idx]++] = handler;
    return true;
}

void EventBus::publish(const Event& event) {
    uint8_t idx   = static_cast<uint8_t>(event.type);
    uint8_t count = counts_[idx];
    for (uint8_t s = 0; s < count; ++s) {
        handlers_[idx][s](event);
    }
}
