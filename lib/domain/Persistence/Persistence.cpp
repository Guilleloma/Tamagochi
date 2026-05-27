// =============================================================================
// Persistence.cpp — lógica pura: CRC32, rotación de slots, load/save/migrate.
//
// Sin Arduino.h. Compilable en host-native (env:native) para tests.
// Solo dependencias: <string.h>, <stdint.h>, <stddef.h>, Logger.h.
//
// Algoritmo de rotación documentado en fase-1-plan.md §5.1 y §5.2.
// Schema v2 documentado en fase-2-plan.md §3.
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
// ~250 ciclos para 40 bytes → <50 µs a 80 MHz. Aceptable para save() a 5 min.
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
// fillDefaults — estado inicial v2: Egg recién nacido.
// Cambia respecto a Fase 1: lifeStage=Egg (0) en vez de Baby (1).
// La FSM hará eclosionar el huevo tras kEggHatchMin minutos.
// -----------------------------------------------------------------------------
void Persistence::fillDefaults(PersistedState& state) {
    memset(&state, 0, sizeof(state));
    state.magic          = kPersistMagic;
    state.version        = kCurrentVersion;
    state.bornAtEpoch    = 0;
    state.lastSavedAtEpoch = 0;
    state.ageMinutes     = 0;
    state.lifeStage      = static_cast<uint8_t>(LifeStage::Egg);  // Fase 2: arranca como Egg
    state.hunger         = 0;     // huevos no tienen hambre
    state.happiness      = 100;   // huevos son felices
    state.health         = 100;
    state.deathCount     = 0;
    state.reserved       = 0;
    // Campos v2
    state.discipline     = 50;
    state.weight         = 50;
    state.poopCount      = 0;
    state.flags          = 0;     // no sick, no sleeping
    state.attentionScore = 500;   // neutro
    state.deathSustainMs = 0;
    state.lastDeathCauseCode = 0;
    state.adultType      = 0;
    state.reservedV2b    = 0;
    state.crc32          = computeCrc32(&state, offsetof(PersistedState, crc32));
}

// -----------------------------------------------------------------------------
// migrate_v1_to_v2_ — rellena los 12 bytes nuevos con defaults coherentes.
//
// Premisa: load() hace memset(&out, 0, sizeof(out)) antes de leer, por lo que
// los bytes [24..40] ya están a 0 cuando llega aquí. Aun así los asignamos
// explícitamente para que la función sea correcta aunque se llame directamente
// desde tests sin pasar por load().
// -----------------------------------------------------------------------------
bool Persistence::migrate_v1_to_v2_(PersistedState& state) {
    state.discipline         = 50;
    state.weight             = 50;
    state.poopCount          = 0;
    state.flags              = 0;    // no sick, no sleeping
    state.attentionScore     = 500;  // neutro
    state.deathSustainMs     = 0;
    state.lastDeathCauseCode = 0;
    state.adultType          = 0;
    state.reservedV2b        = 0;
    state.version            = kCurrentVersion;
    // Nota: el CRC se recalcula en save() antes de la próxima escritura.
    LOG_INFO("Persist: migrado v1->v2 (defaults para campos nuevos)");
    return true;
}

// -----------------------------------------------------------------------------
// migrate — dispatch de versión. Encadena v1→v2→v3... cuando sea necesario.
// -----------------------------------------------------------------------------
bool Persistence::migrate(PersistedState& state) {
    if (state.version == kCurrentVersion) return true;

    if (state.version > kCurrentVersion) {
        LOG_ERROR("Persist: version %u > current %u — descartando estado",
                  (unsigned)state.version, (unsigned)kCurrentVersion);
        return false;
    }

    if (state.version == 1) {
        return migrate_v1_to_v2_(state);
    }

    LOG_WARN("Persist: version desconocida %u — descartando", (unsigned)state.version);
    return false;
}

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
Persistence::Persistence(IStorage& storage) : storage_(storage), nextSlot_(0) {}

// -----------------------------------------------------------------------------
// Tamaño del blob v1 en bytes, para CRC check y zero-pad.
// -----------------------------------------------------------------------------
static constexpr size_t kV1BlobSize    = 28u;
static constexpr size_t kV1CrcPayload  = 24u;  // CRC v1 cubre bytes [0..24]
static constexpr size_t kV1CrcOffset   = 24u;  // byte offset del crc32 en un blob v1

// -----------------------------------------------------------------------------
// load — escanea 4 slots, filtra por magic + CRC, elige el más reciente.
//
// Criterio de "más reciente":
//   1. lastSavedAtEpoch mayor (solo fiable si NTP sincronizó alguna vez)
//   2. Desempate por ageMinutes mayor (contador local, siempre válido)
//   3. Desempate final por índice de slot mayor
//
// Estrategia de lectura:
//   - Se lee siempre sizeof(PersistedState) bytes. IStorage::read() hace zero-pad
//     si el archivo es más corto (blobs v1 = 28 bytes → bytes [28..40] quedan a 0).
//   - Para v1, el crc32 del archivo está en bytes [24..28] (campo `discipline` +
//     los 3 bytes siguientes en el layout v2). Se extrae del buffer raw antes de
//     interpretar el struct, y se valida con CRC sobre los primeros 24 bytes.
//   - Para v2, el crc32 está en offsetof(PersistedState, crc32) = 36 y cubre [0..36].
//
// nextSlot_ se fija al final para que el próximo save() use el slot correcto.
// -----------------------------------------------------------------------------
LoadResult Persistence::load(PersistedState& out) {
    // CRÍTICO: garantiza base limpia — bytes extra de versiones antiguas quedan a 0.
    memset(&out, 0, sizeof(out));

    // Candidatos válidos y sus índices de slot
    PersistedState candidates[kSlotCount];
    uint8_t        candidateSlots[kSlotCount];
    uint8_t        candidateCount = 0;
    bool           anyFileFound   = false;

    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        const char* path = slotPath_(slot);
        if (!storage_.exists(path)) continue;

        anyFileFound = true;

        // Buffer de 40 bytes (máximo tamaño). memset garantiza que los bytes
        // sobrantes de un blob v1 (28 bytes) empiecen a 0.
        uint8_t rawBuf[sizeof(PersistedState)];
        memset(rawBuf, 0, sizeof(rawBuf));

        if (!storage_.read(path, rawBuf, sizeof(rawBuf))) {
            LOG_WARN("Persist: no se pudo leer slot %u", (unsigned)slot);
            continue;
        }

        // Leer magic y version del buffer raw (mismo offset en v1 y v2)
        uint16_t fileMagic;
        uint16_t fileVersion;
        memcpy(&fileMagic,   rawBuf + 0, sizeof(fileMagic));
        memcpy(&fileVersion, rawBuf + 2, sizeof(fileVersion));

        if (fileMagic != kPersistMagic) {
            LOG_WARN("Persist: slot %u magic inválido (0x%04X)", (unsigned)slot, (unsigned)fileMagic);
            continue;
        }

        // Validar CRC según versión:
        //   v1 → CRC almacenado en bytes [24..28], cubre payload [0..24].
        //   v2 → CRC almacenado en bytes [36..40], cubre payload [0..36].
        //   Versión desconocida → rechazar sin intentar validar.
        bool crcOk = false;
        if (fileVersion == 1) {
            uint32_t storedCrc;
            memcpy(&storedCrc, rawBuf + kV1CrcOffset, sizeof(storedCrc));
            uint32_t expected = computeCrc32(rawBuf, kV1CrcPayload);
            crcOk = (storedCrc == expected);
            if (!crcOk) {
                LOG_WARN("Persist: slot %u v1 CRC fallo (got 0x%08X expected 0x%08X)",
                         (unsigned)slot, (unsigned)storedCrc, (unsigned)expected);
            }
        } else if (fileVersion == kCurrentVersion) {
            // Para v2, copiar al struct y usar offsetof normalente
            PersistedState tmpV2;
            memcpy(&tmpV2, rawBuf, sizeof(tmpV2));
            uint32_t expected = computeCrc32(&tmpV2, offsetof(PersistedState, crc32));
            crcOk = (tmpV2.crc32 == expected);
            if (!crcOk) {
                LOG_WARN("Persist: slot %u v2 CRC fallo (got 0x%08X expected 0x%08X)",
                         (unsigned)slot, (unsigned)tmpV2.crc32, (unsigned)expected);
            }
        } else {
            // Versión futura o desconocida: intentar validar CRC como si fuera v2
            // (mismo layout de cabecera). Si pasa, migrate() la rechazará por versión.
            PersistedState tmpUnknown;
            memcpy(&tmpUnknown, rawBuf, sizeof(tmpUnknown));
            uint32_t expected = computeCrc32(&tmpUnknown, offsetof(PersistedState, crc32));
            crcOk = (tmpUnknown.crc32 == expected);
            if (!crcOk) {
                LOG_WARN("Persist: slot %u version desconocida %u CRC fallo",
                         (unsigned)slot, (unsigned)fileVersion);
            }
        }

        if (!crcOk) continue;

        // Construir el PersistedState desde el buffer raw (con zero-pad incluido)
        PersistedState tmp;
        memset(&tmp, 0, sizeof(tmp));
        memcpy(&tmp, rawBuf, sizeof(tmp));

        // Para v1: el crc32 del struct está en offset 36 (que en rawBuf era 0 por zero-pad).
        // Lo restauramos desde donde realmente está en el blob (offset 24) para que
        // migrate() y posteriores usos de `tmp` sean coherentes.
        // (Solo relevante para preservar el campo en candidatos — tras migrar, save()
        //  recalculará el CRC correctamente sobre el struct v2 completo.)
        if (fileVersion == 1) {
            memcpy(&tmp.crc32, rawBuf + kV1CrcOffset, sizeof(tmp.crc32));
            // Los bytes v2 [24..36] incluyen el CRC v1 sobreimpreso en `discipline` etc.
            // Los limpiamos a 0 para que migrate_v1_to_v2_() los sobrescriba con defaults.
            tmp.discipline         = 0;
            tmp.weight             = 0;
            tmp.poopCount          = 0;
            tmp.flags              = 0;
            tmp.attentionScore     = 0;
            tmp.deathSustainMs     = 0;
            tmp.lastDeathCauseCode = 0;
            tmp.adultType          = 0;
            tmp.reservedV2b        = 0;
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

    PersistedState chosen = candidates[bestIdx];

    // nextSlot_ apunta al slot siguiente al elegido para que save() rote correctamente
    nextSlot_ = (bestSlot + 1) % kSlotCount;

    bool wasOlderVersion = (chosen.version != kCurrentVersion);
    if (!migrate(chosen)) {
        fillDefaults(out);
        return LoadResult::MigrationFailed;
    }

    out = chosen;

    if (wasOlderVersion) {
        LOG_INFO("Persist: migrado desde version anterior → %u", (unsigned)kCurrentVersion);
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
// Siempre escribe sizeof(PersistedState) = 40 bytes (v2 completo).
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
