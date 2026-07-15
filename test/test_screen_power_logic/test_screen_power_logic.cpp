// =============================================================================
// test_screen_power_logic.cpp — Tests unitarios para ScreenPowerLogic
// (decisión pura de apagado de pantalla por inactividad).
//
// Framework: Unity (pio test -e native).
// ScreenPowerLogic es lógica pura: no necesita mock, se ejercita directamente.
// El tiempo SIEMPRE se inyecta por parámetro (`now`) -- la clase bajo test no
// depende de millis() real.
// =============================================================================

#include <unity.h>
#include "ScreenPowerLogic.h"

static const uint32_t kTimeoutMs = 3UL * 60UL * 1000UL;  // 3 min, == SCREEN_TIMEOUT_MS

void setUp()    {}
void tearDown() {}

// =============================================================================
// 1. should_stay_on_before_timeout
// Con idle < timeout, update() no hace nada -- la pantalla sigue encendida.
// =============================================================================
void should_stay_on_before_timeout() {
    ScreenPowerLogic screenPower(kTimeoutMs);
    screenPower.noteInteraction(0);

    const uint32_t now = kTimeoutMs - 1;  // 1 ms antes del timeout
    TEST_ASSERT_TRUE(screenPower.isScreenOn());
    TEST_ASSERT_TRUE(screenPower.update(now, /*inhibitSleep=*/false) == ScreenPowerLogic::Action::None);
    TEST_ASSERT_TRUE(screenPower.isScreenOn());
}

// =============================================================================
// 2. should_sleep_after_timeout
// A los `timeout` ms sin interacción -> Sleep, isScreenOn() pasa a false.
// Boundary inclusivo: exactamente en el timeout ya duerme (idle >= timeoutMs_).
// =============================================================================
void should_sleep_after_timeout() {
    ScreenPowerLogic screenPower(kTimeoutMs);
    screenPower.noteInteraction(0);

    const uint32_t now = kTimeoutMs;  // exactamente el timeout
    TEST_ASSERT_TRUE(screenPower.update(now, /*inhibitSleep=*/false) == ScreenPowerLogic::Action::Sleep);
    TEST_ASSERT_FALSE(screenPower.isScreenOn());
}

// =============================================================================
// 3. should_not_sleep_when_inhibited
// inhibitSleep=true (p.ej. OTA en curso) -> None aunque el timeout ya expiró;
// la pantalla se queda encendida.
// =============================================================================
void should_not_sleep_when_inhibited() {
    ScreenPowerLogic screenPower(kTimeoutMs);
    screenPower.noteInteraction(0);

    const uint32_t now = kTimeoutMs + 60000;  // muy pasado el timeout
    TEST_ASSERT_TRUE_MESSAGE(
        screenPower.update(now, /*inhibitSleep=*/true) == ScreenPowerLogic::Action::None,
        "con inhibitSleep=true no debe dormir aunque haya expirado el timeout");
    TEST_ASSERT_TRUE(screenPower.isScreenOn());
}

// =============================================================================
// 4. should_wake_on_interaction_when_off
// Con la pantalla apagada, una interacción (noteInteraction) hace que la
// siguiente llamada a update() devuelva Wake e isScreenOn() vuelva a true.
// =============================================================================
void should_wake_on_interaction_when_off() {
    ScreenPowerLogic screenPower(kTimeoutMs);
    screenPower.noteInteraction(0);

    // Dejar que se duerma primero.
    uint32_t now = kTimeoutMs;
    TEST_ASSERT_TRUE(screenPower.update(now, false) == ScreenPowerLogic::Action::Sleep);
    TEST_ASSERT_FALSE(screenPower.isScreenOn());

    // Interacción (botón, acción HTTP, mensaje del padre) -- resetea el reloj.
    now += 1000;
    screenPower.noteInteraction(now);

    TEST_ASSERT_TRUE_MESSAGE(
        screenPower.update(now, false) == ScreenPowerLogic::Action::Wake,
        "tras noteInteraction() con la pantalla apagada, la siguiente update() debe despertar");
    TEST_ASSERT_TRUE(screenPower.isScreenOn());
}

// =============================================================================
// 5. should_reset_timer_on_interaction
// Una interacción ANTES de que expire el timeout pospone el sleep -- el
// instante en que se habría dormido con el reloj viejo ya no dispara Sleep.
// =============================================================================
void should_reset_timer_on_interaction() {
    ScreenPowerLogic screenPower(kTimeoutMs);
    screenPower.noteInteraction(0);

    // Interacción a mitad de camino del timeout original.
    const uint32_t interactionAt = kTimeoutMs / 2;
    screenPower.noteInteraction(interactionAt);

    // En el instante en que el timeout ORIGINAL (desde t=0) habría expirado,
    // ya no debe dormir -- el reloj se reinició en interactionAt.
    TEST_ASSERT_TRUE_MESSAGE(
        screenPower.update(kTimeoutMs, false) == ScreenPowerLogic::Action::None,
        "una interaccion antes del timeout debe posponer el sleep");
    TEST_ASSERT_TRUE(screenPower.isScreenOn());

    // Pero al llegar al timeout medido desde la interacción SÍ duerme.
    const uint32_t now = interactionAt + kTimeoutMs;
    TEST_ASSERT_TRUE(screenPower.update(now, false) == ScreenPowerLogic::Action::Sleep);
    TEST_ASSERT_FALSE(screenPower.isScreenOn());
}

// =============================================================================
// 6. should_handle_millis_wraparound
// La resta uint32_t debe dar el idle correcto cuando millis() da la vuelta
// (overflow ~49.7 días) entre la última interacción y `now`.
// =============================================================================
void should_handle_millis_wraparound() {
    ScreenPowerLogic screenPower(kTimeoutMs);

    // Última interacción justo antes del overflow de uint32_t.
    const uint32_t lastInteraction = 0xFFFFFFFFu - 50u;  // 50 ms antes del wrap
    screenPower.noteInteraction(lastInteraction);

    // now envuelve: 100 ms después del wrap -> idle real = 50 + 100 = 150 ms.
    const uint32_t now = 100u;  // wrapped
    TEST_ASSERT_TRUE_MESSAGE(
        screenPower.update(now, false) == ScreenPowerLogic::Action::None,
        "150 ms de idle (post-wraparound) no debe alcanzar el timeout de 3 min");
    TEST_ASSERT_TRUE(screenPower.isScreenOn());

    // Avanzar hasta que el idle real (calculado con wraparound) alcance el
    // timeout: now = lastInteraction + kTimeoutMs, calculado en aritmética
    // uint32_t (envuelve igual que millis() real).
    const uint32_t sleepAt = static_cast<uint32_t>(lastInteraction + kTimeoutMs);
    TEST_ASSERT_TRUE(screenPower.update(sleepAt, false) == ScreenPowerLogic::Action::Sleep);
    TEST_ASSERT_FALSE(screenPower.isScreenOn());
}

// =============================================================================
// main — punto de entrada Unity
// =============================================================================
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(should_stay_on_before_timeout);
    RUN_TEST(should_sleep_after_timeout);
    RUN_TEST(should_not_sleep_when_inhibited);
    RUN_TEST(should_wake_on_interaction_when_off);
    RUN_TEST(should_reset_timer_on_interaction);
    RUN_TEST(should_handle_millis_wraparound);

    return UNITY_END();
}
