// =============================================================================
// Persistence.cpp — lógica pura: CRC32, rotación de slots, load/save/migrate.
//
// Sin Arduino.h. Compilable en host-native (env:native) para tests.
// Solo dependencias: <string.h>, <stdint.h>, <stddef.h>, Logger.h.
//
// Algoritmo de rotación documentado en fase-1-plan.md §5.1 y §5.2.
// =============================================================================

#include "Persistence.h"
#include "Logger.h"
#include <string.h>

// Paths de slots en LittleFS raíz. Buffer estático — no reentrant, solo 1 hilo.
static char s_slotPathBuf[16];

// -----------------------------------------------------------------------------
// slotPath_ — devuelve "/state_N.bin" para slot 0..3
// -----------------------------------------------------------------------------
const char* Persistence::slotPath_(uint8_t slot) {
    // Construir manualmente para evitar dependencia de sprintf/snprintf de Arduino.
    // Formato fijo: "/state_X.bin" (12 chars + null)
    s_slotPathBuf[0]  = '/';
    s_slotPathBuf[1]  = 's';
    s_slotPathBuf[2]  = 't';
    s_slotPathBuf[3]  = 'a';
    s_slotPathBuf[4]  = 't';
    s_slotPathBuf[5]  = 'e';
    s_slotPathBuf[6]  = '_';
    s_slotPathBuf[7]  = (char)('0' + (slot % kSlotCount));
    s_slotPathBuf[8]  = '.';
    s_slotPathBuf[9]  = 'b';
    s_slotPathBuf[10] = 'i';
    s_slotPathBuf[11] = 'n';
    s_slotPathBuf[12] = '\0';
    return s_slotPathBuf;
}

// -----------------------------------------------------------------------------
// computeCrc32 — CRC32 IEEE poly 0xEDB88320, sin tabla precomputada.
// ~250 ciclos para 28 bytes → <50 µs a 80 MHz. Aceptable para save() a 5 min.
// Implementación portable: también corre en env:native sin modificación.
// -----------------------------------------------------------------------------
uint32_t Persistence::computeCrc32(const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
        }
    }
    return ~crc;
}

// -----------------------------------------------------------------------------
// fillDefaults — estado inicial Baby recién nacido (Fase 1)
// -----------------------------------------------------------------------------
void Persistence::fillDefaults(PersistedState& state) {
    memset(&state, 0, sizeof(state));
    state.magic          = kPersistMagic;
    state.version        = kCurrentVersion;
    state.bornAtEpoch    = 0;
    state.lastSavedAtEpoch = 0;
    state.ageMinutes     = 0;
    state.lifeStage      = 1;   // Baby
    state.hunger         = 50;
    state.happiness      = 50;
    state.health         = 100;
    state.deathCount     = 0;
    state.reserved       = 0;
    state.crc32          = computeCrc32(&state, offsetof(PersistedState, crc32));
}

// -----------------------------------------------------------------------------
// migrate — aplica migraciones de versión cuando la versión leída != kCurrentVersion.
// Fase 1 solo tiene v1; cualquier versión > 1 es de firmware futuro y se rechaza.
// Las migraciones futuras se encadenan aquí: v1→v2→v3...
// -----------------------------------------------------------------------------
bool Persistence::migrate(PersistedState& state) {
    if (state.version == kCurrentVersion) return true;

    if (state.version > kCurrentVersion) {
        LOG_ERROR("Persist: version %u > current %u — descartando estado",
                  (unsigned)state.version, (unsigned)kCurrentVersion);
        return false;
    }

    // version < kCurrentVersion: en Fase 1 nunca pasa (solo existe v1),
    // pero se deja la estructura preparada para Fase 2+.
    LOG_WARN("Persist: version desconocida %u — usando defaults", (unsigned)state.version);
    return false;
}

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
Persistence::Persistence(IStorage& storage) : storage_(storage), nextSlot_(0) {}

// -----------------------------------------------------------------------------
// load — escanea 4 slots, filtra por magic + CRC, elige el más reciente.
//
// Criterio de "más reciente":
//   1. lastSavedAtEpoch mayor (solo fiable si NTP sincronizó alguna vez)
//   2. Desempate por ageMinutes mayor (contador local, siempre válido)
//   3. Desempate final por índice de slot mayor
//
// nextSlot_ se fija al final para que el próximo save() use el slot correcto.
// -----------------------------------------------------------------------------
LoadResult Persistence::load(PersistedState& out) {
    // Candidatos válidos y sus índices de slot
    PersistedState candidates[kSlotCount];
    uint8_t        candidateSlots[kSlotCount];
    uint8_t        candidateCount = 0;
    bool           anyFileFound   = false;

    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        const char* path = slotPath_(slot);
        if (!storage_.exists(path)) continue;

        anyFileFound = true;

        PersistedState tmp;
        if (!storage_.read(path, &tmp, sizeof(tmp))) {
            LOG_WARN("Persist: no se pudo leer slot %u", (unsigned)slot);
            continue;
        }

        if (tmp.magic != kPersistMagic) {
            LOG_WARN("Persist: slot %u magic inválido (0x%04X)", (unsigned)slot, (unsigned)tmp.magic);
            continue;
        }

        uint32_t expected = computeCrc32(&tmp, offsetof(PersistedState, crc32));
        if (tmp.crc32 != expected) {
            LOG_WARN("Persist: slot %u CRC fallo (got 0x%08X expected 0x%08X)",
                     (unsigned)slot, (unsigned)tmp.crc32, (unsigned)expected);
            continue;
        }

        candidates[candidateCount]     = tmp;
        candidateSlots[candidateCount] = slot;
        ++candidateCount;
    }

    if (candidateCount == 0) {
        fillDefaults(out);
        if (anyFileFound) {
            LOG_ERROR("Persist: todos los slots corruptos — reiniciando estado");
            return LoadResult::AllCorrupt;
        }
        LOG_INFO("Persist: primer boot, estado inicial");
        return LoadResult::NoneFound;
    }

    // Seleccionar el candidato más reciente
    uint8_t bestIdx  = 0;
    uint8_t bestSlot = candidateSlots[0];

    for (uint8_t i = 1; i < candidateCount; ++i) {
        const PersistedState& best = candidates[bestIdx];
        const PersistedState& cur  = candidates[i];

        bool newer = false;
        if (cur.lastSavedAtEpoch != best.lastSavedAtEpoch) {
            newer = cur.lastSavedAtEpoch > best.lastSavedAtEpoch;
        } else if (cur.ageMinutes != best.ageMinutes) {
            newer = cur.ageMinutes > best.ageMinutes;
        } else {
            // Desempate por índice de slot mayor
            newer = candidateSlots[i] > bestSlot;
        }

        if (newer) {
            bestIdx  = i;
            bestSlot = candidateSlots[i];
        }
    }

    PersistedState& chosen = candidates[bestIdx];

    // nextSlot_ apunta al slot siguiente al elegido para que save() rote correctamente
    nextSlot_ = (bestSlot + 1) % kSlotCount;

    bool wasOlderVersion = (chosen.version != kCurrentVersion);
    if (!migrate(chosen)) {
        fillDefaults(out);
        return LoadResult::MigrationFailed;
    }

    out = chosen;

    if (wasOlderVersion) {
        LOG_INFO("Persist: migrado desde version %u → %u", (unsigned)chosen.version, (unsigned)kCurrentVersion);
        return LoadResult::Migrated;
    }

    LOG_INFO("Persist: cargado slot %u, ageMinutes=%lu", (unsigned)bestSlot, (unsigned long)out.ageMinutes);
    return LoadResult::Ok;
}

// -----------------------------------------------------------------------------
// save — escritura rotativa en nextSlot_, borra el slot "más antiguo" tras fsync.
//
// Orden: escribe nuevo → (LittleFS hace COW) → borra antiguo.
// Si la escritura falla, no se borra nada: el estado anterior queda intacto.
// -----------------------------------------------------------------------------
bool Persistence::save(const PersistedState& state) {
    // Hacer copia local para poder fijar el CRC sobre los datos actuales
    PersistedState toWrite = state;
    toWrite.crc32 = computeCrc32(&toWrite, offsetof(PersistedState, crc32));

    uint8_t slotToWrite = nextSlot_;
    const char* writePath = slotPath_(slotToWrite);

    // Copiar el path antes de que slotPath_() sobreescriba el buffer estático
    char writePathCopy[16];
    memcpy(writePathCopy, writePath, sizeof(writePathCopy));

    if (!storage_.write(writePathCopy, &toWrite, sizeof(toWrite))) {
        LOG_ERROR("Persist: fallo al escribir slot %u", (unsigned)slotToWrite);
        return false;
    }

    // Borrar el slot que se sobrescribirá en la siguiente vuelta (el "más antiguo")
    // Si no existe, remove() devuelve false pero eso es OK — lo silenciamos.
    uint8_t oldestSlot = (slotToWrite + 1) % kSlotCount;
    const char* oldestPath = slotPath_(oldestSlot);
    storage_.remove(oldestPath);  // fallo ignorado intencionalmente (puede no existir)

    nextSlot_ = (slotToWrite + 1) % kSlotCount;

    LOG_DEBUG("Persist: guardado en slot %u, siguiente=%u", (unsigned)slotToWrite, (unsigned)nextSlot_);
    return true;
}
