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

// GPIO0 = botón FLASH onboard. La placa ya tiene pull-up a 3.3V integrada
// en la red de arranque (necesaria para modo normal vs. modo programación).
// OneButton debe configurarse con INPUT_PULLUP redundante por seguridad.
#define PIN_BUTTON 0

// =============================================================================
// Timings del loop principal (Fase 0 — bring-up)
// =============================================================================

// 200 ms entre renders → 5 FPS objetivo.
// El budget real se mide en el FPS report por serie cada FPS_REPORT_MS.
#define RENDER_FRAME_MS  200

// El sprite dummy alterna entre frame 0 y frame 1 cada ANIM_FRAME_MS.
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
// 0x0100 = versión 1.0 (Fase 1 core).
#define FIRMWARE_VERSION 0x0100
