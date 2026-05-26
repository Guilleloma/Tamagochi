#pragma once

// =============================================================================
// Logger — macros de logging con niveles configurables en compile time.
//
// Niveles:  0 = NONE, 1 = ERROR, 2 = WARN, 3 = INFO, 4 = DEBUG
//
// Configurar vía build flag: -D LOG_LEVEL=N en platformio.ini.
// Default = 3 (INFO) si no se define.
//
// SEGURIDAD: nunca pasar strings con credenciales, passwords o tokens a estas
// macros. Cumple rules/security.md — los logs no deben filtrar secrets.
//
// USO:
//   LOG_INFO("tick dt=%lu heap=%u", dtMs, ESP.getFreeHeap());
//   LOG_ERROR("LittleFS begin failed");
//
// Las macros usan do { ... } while(0) para que funcionen correctamente
// dentro de sentencias if/else sin llaves. Ejemplo:
//   if (err) LOG_ERROR("failed"); else LOG_INFO("ok");  // OK
// =============================================================================

#ifndef LOG_LEVEL
  #define LOG_LEVEL 3
#endif

#ifdef ARDUINO
  // Entorno Arduino: loguear por Serial.
  // Serial.printf() acepta fmt estándar de printf.
  #include <Arduino.h>

  #define LOG_ERROR(fmt, ...) \
    do { if (LOG_LEVEL >= 1) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__); } while (0)

  #define LOG_WARN(fmt, ...) \
    do { if (LOG_LEVEL >= 2) Serial.printf("[WARN ] " fmt "\n", ##__VA_ARGS__); } while (0)

  #define LOG_INFO(fmt, ...) \
    do { if (LOG_LEVEL >= 3) Serial.printf("[INFO ] " fmt "\n", ##__VA_ARGS__); } while (0)

  #define LOG_DEBUG(fmt, ...) \
    do { if (LOG_LEVEL >= 4) Serial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); } while (0)

#else
  // Entorno host-native (pio test -e native): mapear a printf stdout.
  // Permite usar LOG_* en código de dominio compilado en host sin Arduino.h.
  #include <stdio.h>

  #define LOG_ERROR(fmt, ...) \
    do { if (LOG_LEVEL >= 1) printf("[ERROR] " fmt "\n", ##__VA_ARGS__); } while (0)

  #define LOG_WARN(fmt, ...) \
    do { if (LOG_LEVEL >= 2) printf("[WARN ] " fmt "\n", ##__VA_ARGS__); } while (0)

  #define LOG_INFO(fmt, ...) \
    do { if (LOG_LEVEL >= 3) printf("[INFO ] " fmt "\n", ##__VA_ARGS__); } while (0)

  #define LOG_DEBUG(fmt, ...) \
    do { if (LOG_LEVEL >= 4) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); } while (0)

#endif
