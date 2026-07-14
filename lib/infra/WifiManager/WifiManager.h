#pragma once
// =============================================================================
// WifiManager — conexión WiFi STA no bloqueante.
//
// Ciclo de vida:
//   1. begin()  → configura modo STA y lanza WiFi.begin(). Devuelve inmediatamente.
//                 Si WIFI_SSID está vacío en secrets.h, marca Disabled y no conecta.
//                 En ESP32 además registra WiFi.onEvent() (ver más abajo).
//   2. loop()   → llamar cada vuelta del loop principal. Publica WifiStateChanged
//                 cuando hay cambio de estado.
//
// Fuente de verdad de conectividad (solo ESP32, Fase 5 hito 9):
//   WiFi.setAutoReconnect(true) reconecta a nivel bajo del driver, y un blip
//   de reconexión más corto que kWifiPollIntervalMs (500 ms) puede no ser
//   visible para el polling de WiFi.status() — el estado nunca transiciona a
//   Disconnected, así que la máquina de estados de alto nivel nunca vuelve a
//   pasar por Connected (que es lo que reactiva WebUI/OTA en
//   onWifiStateChanged, main_esp32.cpp). Root cause de un incidente real: ver
//   plan de robustez de red — ping/mDNS seguían vivos pero los puertos 80 y
//   3232 devolvían RST porque nunca se re-invocó begin() en ellos.
//
//   Para cerrar ese hueco, begin() registra WiFi.onEvent() para
//   ARDUINO_EVENT_WIFI_STA_GOT_IP / ARDUINO_EVENT_WIFI_STA_DISCONNECTED. Esos
//   callbacks corren en la task "arduino_events" del core (NO loopTask, NO
//   async_tcp) — así que NUNCA llaman a bus_.publish() directamente (EventBus
//   no es thread-safe, ver EventBus.h). Solo marcan flags `volatile` baratos
//   de escribir/leer entre tasks. loop() los consume y hace el publish() real
//   desde loopTask, en cada llamada (sin gatear por kWifiPollIntervalMs) —
//   así un blip más corto que el poll igualmente dispara la transición.
//   El polling se mantiene como respaldo (cubre el caso Connecting→Connected
//   inicial y cualquier evento que se pierda).
//
// Garantías de timing:
//   - begin()  : < 5 ms (sin espera de red).
//   - loop()   : < 1 µs en el 99% de los casos (guard de tiempo del polling;
//                el chequeo de flags de evento es aún más barato, 2 bools).
//                En la comprobación real: WiFi.status() ≈ 5-50 µs.
//
// WiFi degradado: si la conexión falla o se cae en runtime, el firmware sigue
// funcionando offline. Ver plan Fase 3 §1.3.
//
// Dependencias externas: ESP8266WiFi.h / WiFi.h (Arduino-only). No testeable host-native.
// =============================================================================

#include <stdint.h>
// WifiState enum extraído a WifiState.h para que módulos que solo necesitan
// el enum (DisplayRendererColor) puedan incluirlo sin arrastrar este header
// completo, que en esp12e viene acompañado de <ESP8266WiFi.h>.
#include "WifiState.h"  // reside en lib/core — siempre accesible

#if defined(ARDUINO_ARCH_ESP32)
  // Solo para el tipo arduino_event_id_t usado en la firma de onWifiEvent_()
  // (declaración de clase, no solo en el .cpp). ESP8266 y env:native no
  // definen ARDUINO_ARCH_ESP32 -> no arrastran este include.
  #include <WiFi.h>
#endif

class EventBus;

class WifiManager {
public:
    explicit WifiManager(EventBus& bus);

    // Inicia la conexión WiFi. No bloqueante — devuelve en < 5 ms.
    // Si WIFI_SSID está vacío, marca Disabled y retorna sin conectar.
    void begin();

    // Polea el estado de la conexión. Llamar en cada iteración del loop principal.
    // Guard interno: actúa solo cada kWifiPollIntervalMs.
    void loop();

    WifiState state() const { return state_; }

    // RSSI en dBm. Devuelve 0 si no está conectado.
    int8_t rssi() const;

    // true si alguna vez se llegó a Connected desde el boot (aunque ahora
    // esté caído). Usado por el watchdog de conectividad (commit 3) para
    // decidir si vale la pena reiniciar: sin conexión previa, el router
    // puede estar simplemente apagado — reiniciar no ayudaría y arriesgaría
    // un boot-loop.
    bool hasEverConnected() const { return hasEverConnected_; }

    // millis() del último sello de conexión exitosa (0 si nunca se conectó).
    // Se sella cada vez que se transiciona A Connected (evento o polling).
    uint32_t lastConnectedMs() const { return lastConnectedMs_; }

private:
    EventBus& bus_;
    WifiState state_              = WifiState::Disabled;
    uint32_t  connectStartedAtMs_ = 0;
    uint32_t  disconnectAtMs_     = 0;
    uint32_t  lastPollMs_         = 0;

    // Ver hasEverConnected()/lastConnectedMs() arriba. Sellados en publishState_().
    bool     hasEverConnected_ = false;
    uint32_t lastConnectedMs_  = 0;

    // Flags escritos por los callbacks de WiFi.onEvent() (task "arduino_events",
    // ver comentario de begin() en el .cpp) y consumidos por loop() (loopTask).
    // `volatile`: se escriben en una task y se leen en otra sin lock — cada uno
    // es un bool independiente de 1 byte, la escritura es atómica en Xtensa/RISC-V
    // (no hay estado compuesto que pueda verse a medias). No sustituyen al
    // polling, solo lo adelantan.
    volatile bool eventGotIpPending_       = false;
    volatile bool eventDisconnectedPending_ = false;

    // Intervalo entre comprobaciones de estado WiFi (ms).
    static constexpr uint32_t kWifiPollIntervalMs   = 500;
    // Tiempo máximo esperando conexión antes de marcar Disconnected (ms).
    static constexpr uint32_t kWifiConnectTimeoutMs = 15000;
    // Backoff entre reintentos de reconexión (ms).
    static constexpr uint32_t kWifiRetryBackoffMs   = 60000;

    void publishState_(WifiState newState);

    // Consume eventGotIpPending_/eventDisconnectedPending_ y dispara la
    // transición de estado correspondiente si aplica. Llamado desde loop(),
    // en cada invocación (no gateado por kWifiPollIntervalMs). Solo ESP32
    // (ver begin(), que solo registra WiFi.onEvent() en esa plataforma).
    void consumePendingEvents_();

#if defined(ARDUINO_ARCH_ESP32)
    // Callback de WiFi.onEvent() — corre en la task "arduino_events", nunca en
    // loopTask. Solo escribe los flags `volatile` de arriba; jamás toca bus_
    // ni cualquier otro estado no thread-safe (ver comentario de la clase).
    void onWifiEvent_(arduino_event_id_t event);
#endif
};
