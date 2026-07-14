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
// LifeStage — etapa de vida del Tamagochi.
// Valor uint8_t para ser POD y poder persistirse en PersistedState.lifeStage.
// Compatibilidad con Fase 1: Baby=1 se mantiene.
// -----------------------------------------------------------------------------
enum LifeStage : uint8_t {
    Egg   = 0,
    Baby  = 1,   // valor heredado de Fase 1 — no cambiar
    Kid   = 2,
    Adult = 3,
    Old   = 4,
    Dead  = 5
};

// -----------------------------------------------------------------------------
// PersistedState — 40 bytes exactos, layout documentado en fase-2-plan.md §3.1
// (v3 extiende el layout v2 SIN cambiar tamaño ni offsets — reinterpreta los
// 2 bytes [34..36) que en v2 eran `reservedV2b` como `energy`+`reservedV3`).
// -----------------------------------------------------------------------------
struct PersistedState {
    // === Bloque heredado de v1 (idéntico layout, no tocar) ===
    uint16_t magic;               // [0..2]   0xCAFE
    uint16_t version;             // [2..4]   3 en Fase 2 (energía/luces)
    uint32_t bornAtEpoch;         // [4..8]   0 hasta NTP (Fase 1.5)
    uint32_t lastSavedAtEpoch;    // [8..12]
    uint32_t ageMinutes;          // [12..16] contador local, independiente de NTP
    uint8_t  lifeStage;           // [16]     LifeStage enum (Baby=1 heredado)
    uint8_t  hunger;              // [17]     0-100
    uint8_t  happiness;           // [18]     0-100
    uint8_t  health;              // [19]     0-100
    uint16_t deathCount;          // [20..22] veces que ha muerto desde fábrica
    uint16_t reserved;            // [22..24] padding v1 — en v2/v3 sigue siendo 0

    // === Bloque nuevo v2 (12 bytes) ===
    uint8_t  discipline;          // [24]     0-100, 50 default (0=indisciplinado)
    uint8_t  weight;              // [25]     0-100, 50 default (50=sano, 0=anoréxico, 100=obeso)
    uint8_t  poopCount;           // [26]     número de cacas visibles, 0-9
    uint8_t  flags;               // [27]     bit0=isSick, bit1=isSleeping, bit2=lightsOn (v3),
                                   //          bit3..7 reservados
    uint16_t attentionScore;      // [28..30] 0-1000, 500 default — métrica acumulada que dirige evolución
    uint16_t deathSustainMs;      // [30..32] cuenta atrás de health=0 — persistido para no perderlo en power-loss
    uint8_t  lastDeathCauseCode;  // [32]     0=none, 1=hunger, 2=sickness, 3=oldAge, 4=neglect, 5=unknown
    uint8_t  adultType;           // [33]     0=normal, 1=athletic, 2=obese, 3=grumpy (antes reservedV2)

    // === Bloque nuevo v3 (2 bytes — antes `reservedV2b` v2) ===
    uint8_t  energy;              // [34]     0-100, 100 default (0=agotado, 100=descansado)
    uint8_t  reservedV3;          // [35]     padding v3, debe ser 0

    // === CRC ===
    uint32_t crc32;               // [36..40] CRC32 sobre bytes [0..36]
};
static_assert(sizeof(PersistedState) == 40, "PersistedState v3 debe ser exactamente 40 bytes");

// -----------------------------------------------------------------------------
// IStorage — interfaz de almacenamiento inyectable por constructor.
// LittleFsStorage implementa esta interfaz para el target esp12e.
// InMemoryStorage la implementa para tests host-native.
//
// Semántica de read(): si el archivo existe pero es más corto que size,
// se rellenan con 0 los bytes faltantes y se devuelve true.
// Si el archivo no existe, devuelve false.
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
    static constexpr uint16_t kCurrentVersion = 3;

    explicit Persistence(IStorage& storage);

    // Carga el slot más reciente válido.
    // Si ninguno es válido, rellena out con defaults y devuelve NoneFound o AllCorrupt.
    // Aplica memset(&out, 0, sizeof(out)) al inicio — crítico para migración segura.
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

    // Rellena state con el estado inicial de Fase 2: Egg recién nacido.
    static void fillDefaults(PersistedState& state);

private:
    IStorage& storage_;
    uint8_t   nextSlot_ = 0;  // se recalcula al final de load()

    // Migración específica v1 → v2. Rellena los 12 bytes nuevos con defaults.
    // Premisa: los bytes [24..40] ya están a 0 por el memset en load().
    // Deja version=2 (NO kCurrentVersion) — migrate() encadena a v2_to_v3_ después.
    static bool migrate_v1_to_v2_(PersistedState& state);

    // Migración específica v2 → v3. energy=100, flags|=0x04 (lightsOn=true).
    // Imprescindible: un blob v2 tiene esos bytes en 0 (reservedV2b) — sin esto
    // arrancaría con energy=0 (dormido) y lightsOn=false (a oscuras).
    static bool migrate_v2_to_v3_(PersistedState& state);

    // Devuelve el path de un slot dado. Buffer estático — no reentrant.
    static const char* slotPath_(uint8_t slot);
};
