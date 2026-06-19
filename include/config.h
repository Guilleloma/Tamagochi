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
// ESP32-S3 Zero: GPIO0 es el botón BOOT físico de la placa.
//   - Active-LOW con pull-up interno (INPUT_PULLUP).
//   - AVISO STRAPPING: GPIO0 es pin de strapping del ESP32-S3. Si está LOW
//     durante el ciclo de reset/arranque, el chip entra en bootloader USB/UART
//     en lugar de arrancar el firmware. En runtime (después del boot) se puede
//     leer y usar libremente. NO mantener pulsado el botón BOOT al alimentar
//     la placa ni al pulsar RESET — si lo haces, entrará en bootloader y el
//     firmware no arrancará hasta liberar el botón y resetear de nuevo.
//   - activeLow=true, pullupActive=true.
//
// ESP8266 (TTP223 en GPIO4): sensor touch capacitivo.
//   - Salida push-pull active-HIGH: en reposo LOW, al tocar HIGH.
//   - No requiere INPUT_PULLUP (el chip impone el nivel).
//   - InputHandler debe instanciarse con activeLow=false, pullupActive=false.

#if defined(ARDUINO_ARCH_ESP32)
  // BOOT / GPIO0 del ESP32-S3 Zero — active-LOW, pull-up interno.
  #define PIN_BUTTON           0
  #define BUTTON_ACTIVE_LOW    true
  #define BUTTON_PULLUP_ACTIVE true
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
