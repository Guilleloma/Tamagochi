#pragma once

// =============================================================================
// Pines de hardware — ideaspark ESP8266 0.96" OLED Module V3.2
// =============================================================================

// I2C del SSD1306 — valores según esquemático de ideaspark V3.2.
// VERIFICAR EMPÍRICAMENTE en Fase 0: algunas revisiones usan GPIO12/14 en su lugar.
// Si el OLED no responde con estos valores, probar: OLED_SDA=12, OLED_SCL=14.
#define OLED_SDA 4   // GPIO4  — D2 en numeración NodeMCU
#define OLED_SCL 5   // GPIO5  — D1 en numeración NodeMCU

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
