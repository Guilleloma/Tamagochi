#pragma once
// =============================================================================
// ScreenPowerLogic.h — lógica pura de apagado de pantalla por inactividad
// (ahorro de batería). Header-only, SIN dependencias de hardware (mismo
// espíritu que MarqueeLogic.h / ConnectivityWatchdogLogic.h) — compilable en
// env:native para tests unitarios y en env:esp32s3/env:esp12e.
//
// Contexto: el backlight del TFT color (ESP32-S3) y el panel OLED (ESP8266)
// permanecen encendidos permanentemente hoy — el mayor drenaje de batería.
// Esta clase decide CUÁNDO apagar/encender el panel; NUNCA toca hardware.
// Los main*.cpp aplican el efecto (sleepScreen()/wakeScreen()) según el
// Action devuelto por update(). El juego (decay/energía/LifeFSM) sigue
// corriendo con la pantalla apagada — eso vive fuera de esta clase.
//
// Uso (ver loop() en main_esp32.cpp / main.cpp):
//   switch (screenPower.update(now, inhibitSleep)) {
//       case ScreenPowerLogic::Action::Sleep: sleepScreen(); break;
//       case ScreenPowerLogic::Action::Wake:  wakeScreen();  break;
//       default: break;
//   }
//   if (screenPower.isScreenOn() && now - lastRenderMs >= RENDER_FRAME_MS) { ... }
//
// Thread-safety:
//   - lastInteractionMs_ es el ÚNICO campo tocado fuera de loopTask: el botón
//     (loopTask), las acciones HTTP (task async_tcp vía WebUI) y el mensaje
//     del padre llaman noteInteraction(). Es volatile uint32_t -- la escritura
//     de una palabra de 32 bits alineada es atómica tanto en ESP32 como en
//     ESP8266, así que no hace falta lock ni sección crítica.
//   - screenOn_ solo lo lee/escribe loopTask (update()/isScreenOn(), y los
//     handlers de botón que corren en loopTask) -- sin necesidad de
//     sincronización adicional.
// =============================================================================

#include <stdint.h>

class ScreenPowerLogic {
public:
    enum class Action { None, Sleep, Wake };

    // timeoutMs: milisegundos de inactividad tras los que la pantalla se
    // apaga (SCREEN_TIMEOUT_MS en config.h). La pantalla arranca encendida
    // (screenOn_ = true) -- coherente con el backlight/panel ya encendidos
    // al llegar aquí desde setup().
    explicit ScreenPowerLogic(uint32_t timeoutMs) : timeoutMs_(timeoutMs) {}

    // noteInteraction — registra el instante (millis()) de la última
    // interacción del usuario. Único punto de escritura de
    // lastInteractionMs_ -- seguro de llamar desde cualquier task (ver nota
    // de thread-safety arriba).
    void noteInteraction(uint32_t now) { lastInteractionMs_ = now; }

    // update — única función que decide Sleep/Wake/None y muta screenOn_.
    // Llamar SIEMPRE desde loopTask, una vez por vuelta de loop(), antes del
    // bloque de render.
    //
    //   idle = now - lastInteractionMs_   -- resta en uint32_t: tolera el
    //   wraparound de millis() (~49.7 días) sin lógica especial, porque la
    //   aritmética modular de unsigned ya da la diferencia correcta aunque
    //   `now` haya dado la vuelta y `lastInteractionMs_` no (o viceversa).
    //
    //   - screenOn_ == true  y idle >= timeoutMs_ y !inhibitSleep -> Sleep.
    //   - screenOn_ == false y idle <  timeoutMs_                -> Wake
    //     (hubo un noteInteraction() reciente, p.ej. el botón, que resetea
    //     lastInteractionMs_ justo antes de la siguiente vuelta de loop()).
    //   - cualquier otro caso -> None.
    Action update(uint32_t now, bool inhibitSleep) {
        const uint32_t idle = now - lastInteractionMs_;

        if (screenOn_) {
            if (idle >= timeoutMs_ && !inhibitSleep) {
                screenOn_ = false;
                return Action::Sleep;
            }
            return Action::None;
        }

        if (idle < timeoutMs_) {
            screenOn_ = true;
            return Action::Wake;
        }
        return Action::None;
    }

    bool isScreenOn() const { return screenOn_; }

private:
    // volatile: único campo compartido con tasks distintas de loopTask (ver
    // nota de thread-safety). Escritura atómica de 32 bits -- sin locks.
    volatile uint32_t lastInteractionMs_ = 0;
    // Solo tocado por update()/isScreenOn() -- siempre en loopTask.
    bool     screenOn_ = true;
    uint32_t timeoutMs_;
};
