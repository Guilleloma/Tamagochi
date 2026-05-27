#pragma once

// =============================================================================
// EventBus — pub/sub mínimo sin allocación dinámica.
//
// Patrón: observer síncrono con function pointers planos.
// Razones:
//   - Sin heap: todo en arrays estáticos dentro de EventBus. ESP8266 no tolera
//     fragmentación en sesiones largas (endurance 24h).
//   - Sin queue intermedia: publish() entrega síncronamente en el mismo tick.
//     Determinista y sin latencia.
//   - Sin std::function: añade ~200 bytes RAM por instancia por capturas.
//
// LIMITACIÓN DOCUMENTADA: los handlers no deben tardar más de 1-2 ms.
// Un handler lento bloquea todo el publish() y puede romper el budget de 5 FPS.
//
// HANDLERS y LAMBDAS: solo function pointers planos o lambdas SIN captura.
//   OK:  bus.subscribe(EventType::ButtonShortClick, onShort);
//   OK:  bus.subscribe(EventType::ButtonShortClick, [](const Event&){ ... });
//   MAL: bus.subscribe(EventType::ButtonShortClick, [&bus](const Event& e){ ... });
//   // lambdas con captura NO son convertibles a function pointer.
//
// Coste RAM: EventTypeCount * kMaxSubscribersPerEvent * 4 bytes (punteros)
//            + EventTypeCount * 1 byte (contadores) ≈ 55 bytes por EventBus.
//
// No incluye Arduino.h: compilable en entorno host-native para tests.
// =============================================================================

#include <stdint.h>

enum class EventType : uint8_t {
    ButtonShortClick,
    ButtonLongPress,
    ButtonDoubleClick,
    StatsTick,          // emitido 1 Hz desde el main loop
    StatsChanged,       // emitido tras feed/decay cuando las stats cambian
    LifeStateChanged,   // transición de etapa de vida (Fase 2 lo usará más)
    PersistRequested,   // alguien solicita guardar estado inmediatamente
    OtaStarted,
    OtaProgress,
    OtaEnded,           // actualización completada; ESP reinicia justo después
    OtaError,
    WifiStateChanged,   // payload & 0xFF = WifiState enum value
    ActionFeedRequested,        // payload = amount (1-100)
    ActionPlayRequested,        // payload = amount (1-100)
    ActionHealRequested,        // payload = amount (1-100)
    ActionCleanRequested,       // payload = 0 (sin amount)
    ActionMedicateRequested,    // payload = 0
    ActionDisciplineRequested,  // payload = amount (1-100)
    ActionSleepRequested,       // payload = 0 (off) / 1 (on)
    EventTypeCount      // centinela — mantener al final siempre
};

struct Event {
    EventType type;
    int32_t   payload;  // semántica depende del tipo; suficiente para Fase 1
                        // Ejemplo: OtaProgress usa payload como porcentaje 0-100
};

using EventHandler = void(*)(const Event&);

class EventBus {
public:
    static constexpr uint8_t kMaxSubscribersPerEvent = 4;

    // Registra handler para el tipo de evento dado.
    // Devuelve false si ya hay kMaxSubscribersPerEvent registrados para ese tipo.
    // Llamar en setup(), no en loop() — no es thread-safe (ESP8266 es single-core).
    bool subscribe(EventType type, EventHandler handler);

    // Invoca síncronamente a todos los handlers suscritos al tipo del evento.
    // No falla aunque no haya suscriptores. No encola — entrega inmediata.
    void publish(const Event& event);

private:
    // Arrays fijos: [tipo][suscriptor]. Inicializados a nullptr / 0 in-class.
    EventHandler handlers_[static_cast<uint8_t>(EventType::EventTypeCount)][kMaxSubscribersPerEvent] = {};
    uint8_t      counts_[static_cast<uint8_t>(EventType::EventTypeCount)] = {};
};
