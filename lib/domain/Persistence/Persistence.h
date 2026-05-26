#pragma once
#include <stdint.h>
#include <stddef.h>

// =============================================================================
// Persistence — serialización/deserialización de estado con rotación LittleFS.
//
// Patrón: IStorage abstrae el sistema de ficheros real (LittleFS en target,
// InMemoryStorage en tests host). Persistence opera solo contra IStorage.
//
// Layout en flash: 4 slots rotativos (/state_0.bin ... /state_3.bin).
// Se escribe el slot N, luego se borra el slot N+1 (el "más antiguo").
// En caso de pérdida de energía siempre queda al menos un slot válido previo.
//
// Sin Arduino.h. Compilable en host-native para tests.
// =============================================================================

// -----------------------------------------------------------------------------
// PersistedState — 28 bytes exactos, layout documentado en fase-1-plan.md §4.1
// -----------------------------------------------------------------------------
struct PersistedState {
    uint16_t magic;               // [0..2]  0xCAFE
    uint16_t version;             // [2..4]  1 en Fase 1
    uint32_t bornAtEpoch;         // [4..8]  0 hasta NTP (Fase 1.5)
    uint32_t lastSavedAtEpoch;    // [8..12]
    uint32_t ageMinutes;          // [12..16] contador local, independiente de NTP
    uint8_t  lifeStage;           // [16]    Baby=1
    uint8_t  hunger;              // [17]
    uint8_t  happiness;           // [18]
    uint8_t  health;              // [19]
    uint16_t deathCount;          // [20..22] veces que ha muerto desde fábrica
    uint16_t reserved;            // [22..24] padding, siempre 0
    uint32_t crc32;               // [24..28] CRC32 sobre bytes [0..24]
};
static_assert(sizeof(PersistedState) == 28, "PersistedState debe ser exactamente 28 bytes");

// -----------------------------------------------------------------------------
// IStorage — interfaz de almacenamiento inyectable por constructor.
// LittleFsStorage implementa esta interfaz para el target esp12e.
// InMemoryStorage la implementa para tests host-native.
// -----------------------------------------------------------------------------
class IStorage {
public:
    virtual ~IStorage() = default;
    virtual bool exists(const char* path) = 0;
    virtual bool read(const char* path, void* buffer, size_t size) = 0;
    virtual bool write(const char* path, const void* buffer, size_t size) = 0;
    virtual bool remove(const char* path) = 0;
};

// -----------------------------------------------------------------------------
// LoadResult — resultado de load(), usado por el llamante para decidir qué hacer
// -----------------------------------------------------------------------------
enum class LoadResult : uint8_t {
    Ok,              // slot válido cargado
    NoneFound,       // sin archivos → estado inicial (primer boot)
    AllCorrupt,      // archivos presentes pero todos con CRC/magic inválido
    Migrated,        // versión antigua migrada correctamente
    MigrationFailed  // versión incompatible → estado inicial ("muerte digna")
};

// -----------------------------------------------------------------------------
// Persistence — lógica de persistencia rotativa con CRC32
// -----------------------------------------------------------------------------
class Persistence {
public:
    static constexpr uint8_t  kSlotCount      = 4;
    static constexpr uint16_t kPersistMagic   = 0xCAFE;
    static constexpr uint16_t kCurrentVersion = 1;

    explicit Persistence(IStorage& storage);

    // Carga el slot más reciente válido.
    // Si ninguno es válido, rellena out con defaults y devuelve NoneFound o AllCorrupt.
    LoadResult load(PersistedState& out);

    // Escribe en el siguiente slot rotativo (nextSlot_).
    // Calcula CRC antes de escribir. Borra el slot (nextSlot_+1)%4 tras fsync.
    // Devuelve false si el write falla (se loguea internamente).
    bool save(const PersistedState& state);

    // Aplica migración si version != kCurrentVersion.
    // Devuelve false si la versión es incompatible (> current o desconocida).
    // Es static para que los tests puedan llamarla sin instancia.
    static bool migrate(PersistedState& state);

    // CRC32 IEEE polynomial 0xEDB88320, implementación table-less (sin Flash extra).
    // Public static para que los tests lo ejerzan directamente.
    static uint32_t computeCrc32(const void* data, size_t size);

    // Rellena state con el estado inicial de Fase 1: Baby recién nacido.
    static void fillDefaults(PersistedState& state);

private:
    IStorage& storage_;
    uint8_t   nextSlot_ = 0;  // se recalcula al final de load()

    // Devuelve el path de un slot dado. Buffer estático — no reentrant.
    static const char* slotPath_(uint8_t slot);
};
