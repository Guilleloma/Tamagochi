// =============================================================================
// test_connectivity_watchdog_logic.cpp — Tests unitarios para
// ConnectivityWatchdogLogic (decisión pura del watchdog de conectividad).
//
// Framework: Unity (pio test -e native).
//
// Contexto (bug de campo, ver ConnectivityWatchdogLogic.h): antes de esta
// clase, connectivityWatchdog_() (main_esp32.cpp) reutilizaba
// WifiManager::lastConnectedMs() — "instante de la ÚLTIMA conexión" — para
// medir "tiempo caído". Con uptime > WIFI_DEAD_RESTART_MS, cualquier
// microcorte de WiFi disparaba un reinicio inmediato en vez de esperar los
// 5 min de caída continua. El test 1 (`should_not_restart_when_healthy_...`)
// reproduce exactamente ese escenario y falla contra la lógica vieja.
//
// El tiempo SIEMPRE se inyecta por parámetro (`now`) — la clase bajo test no
// depende de millis() real ni de WiFi.h.
// =============================================================================

#include <unity.h>
#include "ConnectivityWatchdogLogic.h"

static const uint32_t kThresholdMs = 5UL * 60UL * 1000UL;  // 5 min, == WIFI_DEAD_RESTART_MS

void setUp()    {}
void tearDown() {}

// =============================================================================
// 1. should_not_restart_when_wifi_healthy_even_with_long_uptime
// Caso que fallaba en campo: WiFi conectado desde el boot, nunca cayó. Con
// uptime muy superior al umbral, NO debe reiniciar -- downSinceMs_ debe
// seguir en 0 mientras se reportan observaciones "conectado".
// =============================================================================
void should_not_restart_when_wifi_healthy_even_with_long_uptime() {
    ConnectivityWatchdogLogic watchdog;

    watchdog.onWifiStatus(/*connected=*/true, /*now=*/0);
    // Uptime largo, muchas confirmaciones de "sigue conectado" en el camino.
    watchdog.onWifiStatus(true, 60UL * 1000UL);
    watchdog.onWifiStatus(true, 6UL * 60UL * 1000UL);

    const uint32_t now = 10UL * 60UL * 1000UL;  // uptime 10 min >> umbral 5 min
    TEST_ASSERT_FALSE(watchdog.isDown());
    TEST_ASSERT_EQUAL_UINT32(0, watchdog.downSinceMs());
    TEST_ASSERT_FALSE_MESSAGE(
        watchdog.shouldRestart(/*hasEverConnected=*/true, now, kThresholdMs),
        "WiFi sano (nunca caido) no debe reiniciar aunque el uptime supere el umbral");
}

// =============================================================================
// 2. should_not_restart_when_down_for_less_than_threshold
// WiFi cae y el tiempo transcurrido desde la caida es MENOR que el umbral.
// =============================================================================
void should_not_restart_when_down_for_less_than_threshold() {
    ConnectivityWatchdogLogic watchdog;

    watchdog.onWifiStatus(true, 0);
    watchdog.onWifiStatus(false, 1000);  // cae en t=1s

    const uint32_t now = 1000 + (2UL * 60UL * 1000UL);  // 2 min caido
    TEST_ASSERT_TRUE(watchdog.isDown());
    TEST_ASSERT_FALSE(watchdog.shouldRestart(true, now, kThresholdMs));
}

// =============================================================================
// 3. should_restart_when_down_continuously_beyond_threshold
// WiFi cae y permanece caido (sin observaciones "conectado" intermedias) más
// del umbral -> debe reiniciar.
// =============================================================================
void should_restart_when_down_continuously_beyond_threshold() {
    ConnectivityWatchdogLogic watchdog;

    watchdog.onWifiStatus(true, 0);
    watchdog.onWifiStatus(false, 1000);  // cae en t=1s

    const uint32_t now = 1000 + kThresholdMs + 1;  // 1 ms mas alla del umbral
    TEST_ASSERT_TRUE_MESSAGE(
        watchdog.shouldRestart(true, now, kThresholdMs),
        "WiFi caido continuamente mas del umbral debe reiniciar");
}

// =============================================================================
// 4. should_not_restart_on_boundary_exactly_at_threshold
// Edge case: exactamente en el umbral (no lo supera) NO debe reiniciar --
// la comparación es estrictamente ">" (mismo criterio que el codigo original
// `now - lastConnectedMs() <= WIFI_DEAD_RESTART_MS -> return`).
// =============================================================================
void should_not_restart_on_boundary_exactly_at_threshold() {
    ConnectivityWatchdogLogic watchdog;

    watchdog.onWifiStatus(true, 0);
    watchdog.onWifiStatus(false, 1000);

    const uint32_t now = 1000 + kThresholdMs;  // exactamente el umbral
    TEST_ASSERT_FALSE(watchdog.shouldRestart(true, now, kThresholdMs));
}

// =============================================================================
// 5. should_clear_and_not_accumulate_after_a_short_blip
// Blip: cae y se recupera ANTES del umbral -> no reinicia, y downSinceMs_ se
// limpia (vuelve a 0). Una segunda caida posterior, tambien corta, se mide
// desde SU PROPIO inicio -- no acumula con la caida anterior.
// =============================================================================
void should_clear_and_not_accumulate_after_a_short_blip() {
    ConnectivityWatchdogLogic watchdog;

    watchdog.onWifiStatus(true, 0);
    watchdog.onWifiStatus(false, 1000);            // primera caida, t=1s
    TEST_ASSERT_TRUE(watchdog.isDown());
    TEST_ASSERT_FALSE(watchdog.shouldRestart(true, 1500, kThresholdMs));

    watchdog.onWifiStatus(true, 1500);             // recupera antes del umbral
    TEST_ASSERT_FALSE_MESSAGE(watchdog.isDown(),
        "tras reconectar, isDown() debe volver a false");
    TEST_ASSERT_EQUAL_UINT32(0, watchdog.downSinceMs());

    // Segunda caida, mucho mas tarde -- debe sellarse en SU propio instante
    // (t=5 min), no arrastrar el t=1s de la primera caida.
    const uint32_t secondDropAt = 5UL * 60UL * 1000UL;
    watchdog.onWifiStatus(false, secondDropAt);
    TEST_ASSERT_EQUAL_UINT32(secondDropAt, watchdog.downSinceMs());

    // Si hubiera acumulado desde la primera caida (t=1s), a los +2s de esta
    // segunda caida ya llevariamos "caido" ~5 min y dispararia reinicio
    // incorrectamente. Con el sello correcto, solo lleva 2s caido -> no.
    const uint32_t now = secondDropAt + 2000;
    TEST_ASSERT_FALSE_MESSAGE(
        watchdog.shouldRestart(true, now, kThresholdMs),
        "la segunda caida corta debe medirse desde su propio inicio, no acumular");
}

// =============================================================================
// 6. should_not_reseal_on_repeated_failed_reconnect_attempts
// Reintentos de reconexion fallidos (varias observaciones "no conectado"
// seguidas, p.ej. Connecting->Disconnected->Connecting->Disconnected del
// backoff) NO deben re-sellar downSinceMs_ -- debe quedarse en el instante de
// la PRIMERA perdida de la racha.
// =============================================================================
void should_not_reseal_on_repeated_failed_reconnect_attempts() {
    ConnectivityWatchdogLogic watchdog;

    watchdog.onWifiStatus(true, 0);
    watchdog.onWifiStatus(false, 1000);   // primera perdida -- sella en 1000

    // Reintentos fallidos sucesivos (todos "no conectado"): NO deben resellar.
    watchdog.onWifiStatus(false, 16000);  // timeout de un intento de conexion
    watchdog.onWifiStatus(false, 76000);  // otro intento tras backoff
    watchdog.onWifiStatus(false, 91000);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1000, watchdog.downSinceMs(),
        "reintentos fallidos repetidos no deben resellar downSinceMs_");
}

// =============================================================================
// 7. should_never_restart_without_previous_connection
// Sin conexion previa (hasEverConnected==false), nunca reinicia -- ni aunque
// haya una caida sellada y el tiempo transcurrido supere el umbral. Un
// dispositivo que arranco sin WiFi disponible debe poder seguir offline.
// =============================================================================
void should_never_restart_without_previous_connection() {
    ConnectivityWatchdogLogic watchdog;

    // Nunca se reporto connected=true -- ni siquiera hay caida "real" sellada.
    watchdog.onWifiStatus(false, 0);
    watchdog.onWifiStatus(false, 1000);

    const uint32_t now = kThresholdMs * 10;  // tiempo enorme
    TEST_ASSERT_FALSE_MESSAGE(
        watchdog.shouldRestart(/*hasEverConnected=*/false, now, kThresholdMs),
        "sin conexion previa, nunca debe reiniciar aunque el tiempo supere el umbral");
}

// =============================================================================
// 8. should_not_seal_down_since_when_never_connected
// Complemento del anterior: si nunca hubo una observacion "conectado", el
// flanco Connected->no-Connected nunca ocurre -- downSinceMs_ se queda en 0
// (isDown()==false) aunque se reporten muchas observaciones "no conectado".
// =============================================================================
void should_not_seal_down_since_when_never_connected() {
    ConnectivityWatchdogLogic watchdog;

    watchdog.onWifiStatus(false, 0);
    watchdog.onWifiStatus(false, 5000);
    watchdog.onWifiStatus(false, 20000);

    TEST_ASSERT_FALSE(watchdog.isDown());
    TEST_ASSERT_EQUAL_UINT32(0, watchdog.downSinceMs());
}

// =============================================================================
// 9. should_restart_one_ms_past_threshold_after_previous_stable_connection
// Sanity check simetrico al test 4: un 1 ms mas alla del umbral SI dispara
// -- confirma el limite exacto ">" en ambos sentidos.
// =============================================================================
void should_restart_one_ms_past_threshold_after_previous_stable_connection() {
    ConnectivityWatchdogLogic watchdog;

    // downSinceMs_ usa 0 como centinela de "no caido" (mismo convenio que
    // lastConnectedMs_ en WifiManager) -- se sella la caida en t=1, no en
    // t=0, para no chocar con ese centinela en el test.
    watchdog.onWifiStatus(true, 0);
    watchdog.onWifiStatus(false, 1);

    TEST_ASSERT_TRUE(watchdog.shouldRestart(true, 1 + kThresholdMs + 1, kThresholdMs));
    TEST_ASSERT_FALSE(watchdog.shouldRestart(true, 1 + kThresholdMs, kThresholdMs));
}

// =============================================================================
// main — punto de entrada Unity
// =============================================================================
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(should_not_restart_when_wifi_healthy_even_with_long_uptime);
    RUN_TEST(should_not_restart_when_down_for_less_than_threshold);
    RUN_TEST(should_restart_when_down_continuously_beyond_threshold);
    RUN_TEST(should_not_restart_on_boundary_exactly_at_threshold);
    RUN_TEST(should_clear_and_not_accumulate_after_a_short_blip);
    RUN_TEST(should_not_reseal_on_repeated_failed_reconnect_attempts);
    RUN_TEST(should_never_restart_without_previous_connection);
    RUN_TEST(should_not_seal_down_since_when_never_connected);
    RUN_TEST(should_restart_one_ms_past_threshold_after_previous_stable_connection);

    return UNITY_END();
}
