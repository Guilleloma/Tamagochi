#include "InputHandler.h"
#include <Arduino.h>

// instance_ estático — permite a los callbacks C acceder al objeto vivo.
InputHandler* InputHandler::instance_ = nullptr;

InputHandler::InputHandler(EventBus& bus, uint8_t pin, bool activeLow)
    : bus_(&bus), button_(pin, activeLow)
{
    // Garantía de singleton: solo un InputHandler activo en Fase 1.
    if (instance_ != nullptr) {
        // En producción no hay pánico disponible; log de error y continuar.
        Serial.println("[ERROR] InputHandler: ya existe una instancia activa");
    }
    instance_ = this;
}

void InputHandler::begin() {
    button_.attachClick(onShortClick_);
    button_.attachDoubleClick(onDoubleClick_);
    button_.attachLongPressStart(onLongPress_);
}

void InputHandler::loop() {
    button_.tick();
}

// -----------------------------------------------------------------------------
// Callbacks — hito 5: emiten Serial.println idéntico a Fase 0.
// TODO(hito-6): reemplazar Serial.println por bus_->publish(...)
// -----------------------------------------------------------------------------

void InputHandler::onShortClick_() {
    Serial.println("[BOTON] Click corto");
}

void InputHandler::onDoubleClick_() {
    Serial.println("[BOTON] Doble click");
}

void InputHandler::onLongPress_() {
    Serial.println("[BOTON] Pulsacion larga");
}
