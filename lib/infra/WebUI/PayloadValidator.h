#pragma once
// =============================================================================
// PayloadValidator — validaciones puras de valores ya parseados.
//
// Sin Arduino.h: compilable en host-native para tests Unity.
// Sin dependencias de JSON — opera sobre valores C++ primitivos.
//
// Separación de responsabilidades:
//   - JsonCodec: parsea JSON a valores C++
//   - PayloadValidator: valida que esos valores cumplen las reglas de negocio
//
// Ambos módulos son testables independientemente en host-native.
// =============================================================================

#include <stdint.h>

namespace PayloadValidator {

enum class ValidationError : uint8_t {
    Ok = 0,
    InvalidAmount,  // amount fuera del rango válido (1-100 inclusive)
    UnknownAction   // nombre de acción no reconocido
};

// Valida que amount está en el rango [1, 100] inclusive.
// Acepta int para poder detectar negativos sin ambigüedad de uint8_t.
ValidationError validateAmount(int amount);

// Valida que name corresponde a una acción conocida del dominio.
// Acciones conocidas: "feed", "play", "heal", "clean", "medicate", "discipline", "sleep"
// name nulo o vacío → UnknownAction.
ValidationError validateActionName(const char* name);

}  // namespace PayloadValidator
