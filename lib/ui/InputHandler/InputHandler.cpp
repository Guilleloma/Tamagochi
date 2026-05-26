#include "InputHandler.h"
#include "EventBus.h"
#include "Logger.h"
#include <Arduino.h>

// instance_ estático — permite a los callbacks C acceder al objeto vivo.
InputHandler* InputHandler::instance_ = nullptr;

InputHandler::InputHandler(EventBus& bus, uint8_t pin, bool activeLow)
    : bus_(&bus), button_(pin, activeLow)
{
    // Garantía de singleton: solo un InputHandler activo en Fase 1.
    if (instance_ != nullptr) {
        LOG_ERROR("InputHandler: ya existe una instancia activa");
    }
    instance_ = this;
}

void InputHandler::begin() {
    button_.attachClick(onShortClick_);
    button_.attachDoubleClick(onDoubleClick_);
    button_.attachLongPressStart(onLongPress_);
    // setClickMs reduce el retardo del click simple a 150ms (default 400ms).
    // OneButton espera _clickMs tras un click para descartar doble click.
    // 150ms basta para distinguir click vs doble click y se siente inmediato.
    button_.setClickMs(150);
    // setPressMs reduce el umbral de long press a 600ms (default 800ms).
    // Permite que el usuario perciba la pulsación larga más rápido sin
    // confundirla con click corto.
    button_.setPressMs(600);
}

void InputHandler::loop() {
    button_.tick();
}

// -----------------------------------------------------------------------------
// Callbacks — hito 6: publican eventos al EventBus vía instance_->bus_.
// LOG_INFO para debugging por monitor serie (formato consistente con Logger).
// instance_ es accesible porque los callbacks estáticos forman parte de la
// propia clase, por lo que tienen acceso a sus miembros privados.
// -----------------------------------------------------------------------------

void InputHandler::onShortClick_() {
    if (instance_) {
        LOG_INFO("[BTN] short");
        instance_->bus_->publish({EventType::ButtonShortClick, 0});
    }
}

void InputHandler::onDoubleClick_() {
    if (instance_) {
        LOG_INFO("[BTN] double");
        instance_->bus_->publish({EventType::ButtonDoubleClick, 0});
    }
}

void InputHandler::onLongPress_() {
    if (instance_) {
        LOG_INFO("[BTN] long");
        instance_->bus_->publish({EventType::ButtonLongPress, 0});
    }
}
