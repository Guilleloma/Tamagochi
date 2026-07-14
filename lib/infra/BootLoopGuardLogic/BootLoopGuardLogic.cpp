// =============================================================================
// BootLoopGuardLogic.cpp — implementación de la lógica pura de decisión.
// Ver BootLoopGuardLogic.h para el diseño. Sin dependencias de hardware:
// compila en env:native (tests unitarios) y en env:esp32s3.
// =============================================================================

#include "BootLoopGuardLogic.h"

BootLoopGuardLogic::BootLoopGuardLogic(uint32_t maxRestarts, uint32_t initialCount)
    : maxRestarts_(maxRestarts), count_(initialCount) {}

void BootLoopGuardLogic::reset(uint32_t maxRestarts, uint32_t initialCount) {
    maxRestarts_ = maxRestarts;
    count_ = initialCount;
}

bool BootLoopGuardLogic::allowRestart() {
    // count_ ya alcanzó el tope -- modo degradado, no incrementar más.
    if (count_ >= maxRestarts_) {
        return false;
    }
    count_++;
    return true;
}

void BootLoopGuardLogic::reportStableConnection() {
    count_ = 0;
}
