#include "InputHandler.h"
#include "EventBus.h"
#include "Logger.h"
#include <Arduino.h>

// instance_ estático — permite a los callbacks C acceder al objeto vivo.
InputHandler* InputHandler::instance_ = nullptr;

InputHandler::InputHandler(EventBus& bus, uint8_t pin, bool activeLow, bool pullupActive)
    : bus_(&bus), button_(pin, activeLow, pullupActive)
{
    // Garantía de singleton: solo un InputHandler activo en Fase 1.
    if (instance_ != nullptr) {
        LOG_ERROR("InputHandler: ya existe una instancia activa");
    }
    instance_ = this;
}

void InputHandler::begin() {
    button_.attachClick(onShortClick_);
    button_.attachLongPressStart(onLongPress_);
    // attachDoubleClick deliberadamente NO registrado en Fase 1:
    // con doble click registrado, OneButton espera _clickMs tras cada pulsación
    // para distinguir single de double, lo que retrasa el click corto y en
    // botones con rebote llega a perderse. El plan reserva el evento doble
    // click para hitos futuros — cuando haga falta, se vuelve a registrar.
    button_.setClickMs(80);     // espera mínima tras click (default 400ms)
    button_.setDebounceMs(40);  // sensible para click rápido, sin doble click registrado evita rebotes contables
    button_.setPressMs(600);    // long press a 600ms (default 800ms)
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
