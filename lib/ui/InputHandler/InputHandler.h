#pragma once
#include <OneButton.h>

// =============================================================================
// InputHandler — wrapper de OneButton que traduce gestos de botón a logs serie.
//
// En hito 5 (Fase 1) los callbacks emiten Serial.println como en Fase 0.
// En hito 6 se sustituirán por bus_->publish(...) para integrar el EventBus.
//
// RAZÓN DE instance_: OneButton solo acepta function pointers C (sin capturas).
// instance_ permite que los callbacks estáticos accedan al contexto del objeto.
// Solo un InputHandler vivo a la vez — afirmado en el constructor.
// =============================================================================

class EventBus;  // fwd declaration — no se usa en hito 5, pero la API es vinculante

class InputHandler {
public:
    InputHandler(EventBus& bus, uint8_t pin, bool activeLow = true);

    void begin();
    void loop();   // delega a OneButton::tick()

private:
    EventBus*  bus_;
    OneButton  button_;

    static InputHandler* instance_;

    static void onShortClick_();
    static void onDoubleClick_();
    static void onLongPress_();
};
