#pragma once

// =============================================================================
// Pines de hardware — ideaspark ESP8266 0.96" OLED Module V3.2
// =============================================================================

// I2C del SSD1306 — esta revisión de ideaspark V3.2 usa SOFTWARE I2C
// en GPIO14 (clock) y GPIO12 (data), NO el hardware I2C estándar GPIO4/5.
// Confirmado empíricamente en Fase 0 + guía oficial del fabricante.
// El constructor U8g2 debe ser SW_I2C (no HW_I2C). Wire.h no se usa.
#define OLED_SDA 12  // GPIO12 — D6 en numeración NodeMCU
#define OLED_SCL 14  // GPIO14 — D5 en numeración NodeMCU

// ── Configuración de botón — condicional por plataforma ─────────────────────
//
// ESP32-S3 Zero: TTP223 (sensor touch capacitivo) en GPIO4.
//   - Salida push-pull active-HIGH: en reposo LOW, al tocar HIGH.
//   - No requiere INPUT_PULLUP (el módulo TTP223 drivea la salida OUT).
//   - GPIO4 es pin seguro (no es pin de strapping).
//   - activeLow=false, pullupActive=false.
//
// ESP8266 (TTP223 en GPIO4): sensor touch capacitivo.
//   - Salida push-pull active-HIGH: en reposo LOW, al tocar HIGH.
//   - No requiere INPUT_PULLUP (el chip impone el nivel).
//   - InputHandler debe instanciarse con activeLow=false, pullupActive=false.

#if defined(ARDUINO_ARCH_ESP32)
  // TTP223 en GPIO4 del ESP32-S3 Zero — active-HIGH, sin pull-up interno.
  #define PIN_BUTTON           4
  #define BUTTON_ACTIVE_LOW    false
  #define BUTTON_PULLUP_ACTIVE false

  // ── Medidor de batería — ESP32-S3 Zero ──────────────────────────────────
  // El divisor de tensión (B+ → R1 100k → GPIO1 → R2 100k → GND) se soldará
  // en otra sesión — ver guía completa en docs/hardware-battery.md. Hasta
  // entonces el pin queda flotante y BatteryGauge lo detecta en runtime vía
  // su gate de plausibilidad+estabilidad (nunca sale de Unknown sin hardware).
  //
  // GPIO libres verificados en esta placa: 1, 2, 5, 6.
  // Ocupados: 4 botón, 7-12 TFT, 19/20 USB, 21 WS2812.
  #define BATTERY_SENSE_ENABLED       1
  #define PIN_BATTERY_ADC             1   // GPIO1 = ADC1_CH0 (ADC1: compatible con WiFi activo)
  #define BATTERY_DIVIDER_NUM         2   // divisor 2×100k: Vbat = Vpin × 2/1
  #define BATTERY_DIVIDER_DEN         1
  // Sensado de carga: OFF por defecto — activar SOLO tras soldar y verificar
  // con multímetro (docs/hardware-battery.md). MODE=1 VbusPresence, MODE=2 LED activo-LOW.
  #define BATTERY_CHARGE_PIN_ENABLED  0
  #define PIN_CHARGE_SENSE            2   // GPIO2 (GPIO3 es strapping — evitar)
  #define BATTERY_CHARGE_SENSE_MODE   1   // 1=VbusPresence, 2=ChargeLedActiveLow
#else
  // TTP223 en GPIO4 del ESP8266 — active-HIGH, sin pull-up externo.
  #define PIN_BUTTON           4
  #define BUTTON_ACTIVE_LOW    false
  #define BUTTON_PULLUP_ACTIVE false
#endif

// =============================================================================
// Timings del loop principal (Fase 0 — bring-up)
// =============================================================================

// Período entre renders.
// ESP32-S3: 66 ms → ~15 FPS objetivo (el flush 240×296 tarda ~40 ms vía SPI 27 MHz,
//   dejando ~26 ms de margen. Si el panel no lo aguanta, bajar a 100 ms / 10 FPS).
// ESP8266:  200 ms → 5 FPS (sin cambios — pantalla mono más lenta).
// El FPS real se mide en el reporte serie cada FPS_REPORT_MS.
#if defined(ARDUINO_ARCH_ESP32)
  #define RENDER_FRAME_MS  66
#else
  #define RENDER_FRAME_MS  200
#endif

// El sprite BABY alterna entre frame 0 y frame 1 cada ANIM_FRAME_MS.
// 400 ms da una animación suave visible sin ser frenética.
#define ANIM_FRAME_MS    400

// =============================================================================
// Ralentización global de animación de sprites
// =============================================================================
// Feedback de usuario: los sprites se perciben "demasiado rápido" a los fps
// declarados en el manifest (8 fps personaje, 2 fps egg) y en ANIM_FRAME_MS
// (mono). En vez de tocar fps por sprite/manifest (muchos puntos, difícil de
// afinar), este factor multiplica la CADENCIA de avance de animFrame_/
// animTickCount_ en ambos renderers (color ESP32-S3 y mono ESP8266) — un
// único sitio para ajustar la sensación global, iterativo por diseño.
// 1 = velocidad original del manifest/ANIM_FRAME_MS. 2 = mitad de velocidad
// (valor inicial elegido tras el reporte de usuario). Sube/baja este entero
// para volver a tunear sin tocar lógica de renderer.
#define SPRITE_ANIM_SLOWDOWN 2

// Log de FPS medido cada 5 segundos por monitor serie.
#define FPS_REPORT_MS    5000

// =============================================================================
// Timings Fase 1
// =============================================================================

// Frecuencia del tick de decay de StatsEngine.
// TURBO_MODE reduce el intervalo ~60× para validar el ciclo completo de decay
// en minutos (útil en hardware sin esperar horas).
// Nunca activar TURBO_MODE en firmware de producción.
#ifdef TURBO_MODE
  #define STATS_TICK_MS  16    // ~60× turbo (~16 min de decay en ~16 s)
#else
  #define STATS_TICK_MS  1000  // decay a 1 Hz — 1 tick por segundo
#endif

// Versión de firmware codificada como 0xMMmm (mayor × 256 + menor).
//
// ESP8266 (Fase 2):
//   0x0200 = versión 2.0 prod
//   0x02FE = turbo (solo validación en hardware; no producción)
//
// ESP32-S3 (Fase 4):
//   0x0300 = versión 3.0 prod
//   0x03FE = turbo (solo validación en hardware; no producción)
//
// Los static_assert en main.cpp y main_esp32.cpp verifican el valor correcto
// para cada plataforma en compile-time, previniendo mezcla de binarios.
#if defined(ARDUINO_ARCH_ESP32)
  #ifdef TURBO_MODE
    #define FIRMWARE_VERSION 0x03FE
  #else
    #define FIRMWARE_VERSION 0x0300   // Fase 4 ESP32-S3 release
  #endif
#else
  // ESP8266 — valores heredados: no cambiar (compatibilidad con Persistence v2).
  #ifdef TURBO_MODE
    #define FIRMWARE_VERSION 0x02FE
  #else
    #define FIRMWARE_VERSION 0x0200  // Fase 2 release
  #endif
#endif

// Escala de tiempo para la FSM de vida.
// TURBO_MODE acelera el envejecimiento × TURBO_FACTOR para validar el ciclo
// completo de estados (Egg→Baby→Kid→Adult→Old→Dead) en minutos en lugar de días.
// LIFE_FSM_TIME_SCALE=1 en producción → sin efecto sobre el avance de edad.
#ifdef TURBO_MODE
  #ifndef TURBO_FACTOR
    #define TURBO_FACTOR 60
  #endif
  #define LIFE_FSM_TIME_SCALE   TURBO_FACTOR
#else
  #define LIFE_FSM_TIME_SCALE   1
#endif

// === Pausa de crecimiento (growth cap) ===
// GROWTH_CAP_STAGE: etapa máxima a la que la mascota puede llegar por edad,
// como valor de LifeStage (Persistence.h): 0=Egg 1=Baby 2=Kid 3=Adult 4=Old.
// Por qué: de momento solo el bebé tiene las animaciones nuevas (Kling), así
// que el crecimiento se pausa en Baby hasta que el resto de etapas tengan las
// suyas. Con tope (ver LifeFSM::setGrowthCap / restoreLifeState):
//   - no evoluciona más allá del tope y la edad se congela justo antes del
//     umbral de salida (al quitar el tope no se salta etapas);
//   - un pet guardado en una etapa superior se degrada al tope al arrancar,
//     conservando sus stats, y el estado degradado se persiste en el acto;
//   - muerte por salud y resurrección como Egg no cambian.
// Se define en platformio.ini ([env:esp32s3], lo heredan _turbo y _ota):
// quitar esa línea = vuelve a evolucionar. Sin definir = sin tope.
// Solo lo aplica main_esp32.cpp (ESP32-S3); el firmware ESP8266 no lo usa.
// El rango se valida con static_assert en main_esp32.cpp (allí LifeStage es
// visible; un #if aquí no podría comprobar un -D con nombre de enum).
#ifndef GROWTH_CAP_STAGE
  #define GROWTH_CAP_ENABLED 0
#else
  #define GROWTH_CAP_ENABLED 1
#endif

// === Persistence (Hito 7) ===
#define PERSIST_AUTOSAVE_MS    (5UL * 60UL * 1000UL)   // 5 min wall-clock
// Política: NO se escala con TURBO_MODE — evita escribir flash en tests

// === Apagado de pantalla por inactividad (ahorro de batería) ===
// Wall-clock real: NO escala con TURBO_MODE (mismo criterio que
// PERSIST_AUTOSAVE_MS — evitar que el ahorro de batería se dispare en
// segundos durante una validación turbo en hardware). Ver ScreenPowerLogic.h
// para la lógica de decisión (pura, sin hardware) y main*.cpp para el efecto
// físico (backlight/panel OFF vía sleepScreen()/wakeScreen()).
#define SCREEN_TIMEOUT_MS      (3UL * 60UL * 1000UL)   // 3 min

// =============================================================================
// Robustez de red y auto-recuperación (Fase 5, hito 9 — solo ESP32-S3)
// =============================================================================
//
// Contexto del incidente que motiva estas constantes: WiFi.setAutoReconnect(true)
// reconecta a nivel bajo sin pasar siempre por el polling de 500ms de
// WifiManager::loop() (ver WifiManager.cpp) — un blip de reconexión más corto
// que ese intervalo puede dejar WebUI/OTA con el socket muerto pero con sus
// flags running_/ready_ todavía en true (nunca se llamó a end()). El supervisor
// de red (NET_SUPERVISOR_INTERVAL_MS) es la red de seguridad idempotente;
// WifiManager::onEvent (WiFi.onEvent) es la fuente rápida que corrige esos
// flags en cuanto ocurre el blip, sin esperar al poll.

// Cada cuánto el loop principal revisa si WebUI/OTA siguen realmente arriba
// con WiFi conectado, y los relevanta si no (ver loop() en main_esp32.cpp).
// Idempotente y barato — begin()/isRunning()/isReady() son simples getters.
#define NET_SUPERVISOR_INTERVAL_MS   5000UL

// Refresco de la página de red de Stats (NetworkView) mientras está visible:
// relee WifiManager::info() (RSSI/IP en vivo, unos µs) para que las barras
// de señal se muevan. Los cambios de estado (conexión/caída) no esperan a
// esto: llegan al instante por WifiStateChanged. Wall-clock, no escala con turbo.
#define NET_INFO_REFRESH_MS          2000UL

// Timeout del Task Watchdog Timer sobre la loopTask (segundos). Generoso:
// canvas->flush() por SPI (~138 KB framebuffer) + accesos LittleFS pueden
// tardar bastante en el peor caso; no queremos falsos positivos.
#define WDT_TIMEOUT_S                 10

// Si WiFi lleva caído SEGUIDO más de esto (WifiManager::wifiDownSinceMs(),
// NO el uptime total ni lastConnectedMs() — ver ConnectivityWatchdogLogic.h)
// habiendo estado conectado antes (WifiManager::hasEverConnected()), se asume
// un estado irrecuperable del stack de red y se reinicia (con guarda
// anti-boot-loop, ver BOOT_LOOP_MAX).
#define WIFI_DEAD_RESTART_MS          (5UL * 60UL * 1000UL)   // 5 min

// Anti-boot-loop: máximo de reinicios "por WiFi caído" tolerados antes de
// entrar en modo degradado (seguir jugando offline sin reintentar reinicio —
// evita ciclo infinito si el router está apagado de verdad). El contador vive
// en RTC_NOINIT_ATTR (sobrevive a ESP.restart(), se pierde al desconectar la
// alimentación — ver BootLoopGuard.h) y se resetea a 0 tras
// BOOT_LOOP_RESET_STABLE_MS de conexión estable. Sin ventana de tiempo
// persistida entre reinicios (millis() no sirve para eso sin NTP) — el reset
// por conexión estable ya cubre el caso "el router volvió" de forma más
// simple y igual de efectiva.
#define BOOT_LOOP_MAX                  5
#define BOOT_LOOP_RESET_STABLE_MS     (60UL * 1000UL)         // 1 min
