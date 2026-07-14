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

// === Persistence (Hito 7) ===
#define PERSIST_AUTOSAVE_MS    (5UL * 60UL * 1000UL)   // 5 min wall-clock
// Política: NO se escala con TURBO_MODE — evita escribir flash en tests

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

// Timeout del Task Watchdog Timer sobre la loopTask (segundos). Generoso:
// canvas->flush() por SPI (~138 KB framebuffer) + accesos LittleFS pueden
// tardar bastante en el peor caso; no queremos falsos positivos.
#define WDT_TIMEOUT_S                 10

// Si WiFi lleva caído más de esto habiendo estado conectado antes
// (WifiManager::hasEverConnected()), se asume un estado irrecuperable del
// stack de red y se reinicia (con guarda anti-boot-loop, ver BOOT_LOOP_MAX).
#define WIFI_DEAD_RESTART_MS          (5UL * 60UL * 1000UL)   // 5 min

// Anti-boot-loop: máximo de reinicios "por WiFi caído" tolerados dentro de
// BOOT_LOOP_WINDOW_MS antes de entrar en modo degradado (seguir jugando sin
// reintentar reinicio — evita ciclo infinito si el router está apagado de
// verdad). El contador vive en RTC_NOINIT_ATTR (sobrevive a ESP.restart(),
// se pierde al desconectar la alimentación) y se resetea tras
// BOOT_LOOP_RESET_STABLE_MS de conexión estable.
#define BOOT_LOOP_MAX                  5
#define BOOT_LOOP_WINDOW_MS           (30UL * 60UL * 1000UL)  // 30 min
#define BOOT_LOOP_RESET_STABLE_MS     (60UL * 1000UL)         // 1 min
