#pragma once
// =============================================================================
// AuthChecker.h — Verificación de autenticación Basic para la WebUI.
//
// Sin Arduino.h: testeable en host (env:native).
//
// checkBasicAuth:
//   - Valida header "Authorization: Basic <base64(user:pass)>"
//   - Comparación constant-time para mitigar timing attacks (security.md)
//
// decodeBase64:
//   - Helper expuesto para tests; implementación RFC 4648 con soporte de padding.
// =============================================================================

#include <stdint.h>
#include <stddef.h>

namespace AuthChecker {

// Verifica un header Authorization de tipo Basic.
//   authHeader  : cadena completa del header, p.ej. "Basic dGFtYTpwYXNzd29yZA=="
//                 Si es nullptr → false.
//   expectedUser: credencial válida (de secrets.h en runtime).
//   expectedPass: credencial válida (de secrets.h en runtime).
//
// Retorna true solo si el header es válido y las credenciales coinciden.
// La comparación se realiza de forma constant-time para mitigar timing attacks.
bool checkBasicAuth(const char* authHeader,
                    const char* expectedUser,
                    const char* expectedPass);

// Decodifica base64 estándar (RFC 4648) con padding '='.
//   in     : buffer base64 de entrada.
//   inLen  : longitud de in.
//   out    : buffer de salida; debe tener al menos (inLen * 3) / 4 + 1 bytes.
//   outSize: capacidad de out.
//
// Retorna la longitud del resultado decodificado, o 0 si hay error
// (char inválido, buffer insuficiente, etc.).
size_t decodeBase64(const char* in, size_t inLen, char* out, size_t outSize);

}  // namespace AuthChecker
