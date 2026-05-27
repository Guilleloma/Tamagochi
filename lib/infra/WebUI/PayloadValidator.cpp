// =============================================================================
// PayloadValidator.cpp — implementación de validaciones de payload.
//
// Sin Arduino.h. Solo usa stdlib (string.h).
// Funciones puras: sin estado, sin efectos secundarios, sin allocación.
// =============================================================================

#include "PayloadValidator.h"
#include <string.h>

namespace PayloadValidator {

ValidationError validateAmount(int amount) {
    if (amount < 1 || amount > 100) {
        return ValidationError::InvalidAmount;
    }
    return ValidationError::Ok;
}

ValidationError validateActionName(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return ValidationError::UnknownAction;
    }

    // Lista fija de acciones conocidas del dominio (Fase 2)
    static const char* const kKnownActions[] = {
        "feed", "play", "heal", "clean", "medicate", "discipline", "sleep"
    };
    static constexpr size_t kKnownActionsCount =
        sizeof(kKnownActions) / sizeof(kKnownActions[0]);

    for (size_t i = 0; i < kKnownActionsCount; ++i) {
        if (strcmp(name, kKnownActions[i]) == 0) {
            return ValidationError::Ok;
        }
    }

    return ValidationError::UnknownAction;
}

}  // namespace PayloadValidator
