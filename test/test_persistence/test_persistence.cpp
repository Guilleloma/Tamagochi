// =============================================================================
// test_persistence.cpp — tests para Persistence (hito 1, Fase 2).
//
// Framework: Unity (pio test -e native).
// Mock: InMemoryStorage (sin LittleFS, sin Arduino).
//
// Tests baseline (Fase 1, adaptados a v2):
//   1-13: lógica de CRC, save/load, rotación de slots — sin cambios funcionales.
//   14-16: tests de migrate adaptados a la nueva versión actual (v2).
//   17: tamaño struct adaptado a 40 bytes.
//
// Tests nuevos (Fase 2 — migración v1→v2):
//   18: test_load_v1_blob_migrates_to_v2
//   19: test_migrate_v1_to_v2_preserves_v1_fields
//   20: test_migrate_v1_to_v2_sets_new_field_defaults
//   21: test_load_blob_with_short_size_fills_remaining_with_zero
//   22: test_save_in_v2_format_writes_40_bytes
// =============================================================================

#include <unity.h>
#include <stddef.h>
#include <string.h>
#include "Persistence.h"
#include "InMemoryStorage.h"

static InMemoryStorage* g_storage    = nullptr;
static Persistence*     g_persistence = nullptr;

void setUp() {
    g_storage     = new InMemoryStorage();
    g_persistence = new Persistence(*g_storage);
}

void tearDown() {
    delete g_persistence; g_persistence = nullptr;
    delete g_storage;     g_storage     = nullptr;
}

// =============================================================================
// Helper — construye un blob v1 válido (28 bytes, CRC sobre primeros 24 bytes).
// Simula un save de firmware Fase 1 en el storage.
// Layout v1: magic(2) version(2) bornAt(4) lastSaved(4) age(4) lifeStage(1)
//            hunger(1) happiness(1) health(1) deathCount(2) reserved(2) crc32(4)
// Total: 28 bytes. CRC cubre bytes [0..24].
// =============================================================================
struct V1Blob {
    uint16_t magic;
    uint16_t version;
    uint32_t bornAtEpoch;
    uint32_t lastSavedAtEpoch;
    uint32_t ageMinutes;
    uint8_t  lifeStage;
    uint8_t  hunger;
    uint8_t  happiness;
    uint8_t  health;
    uint16_t deathCount;
    uint16_t reserved;
    uint32_t crc32;
};
static_assert(sizeof(V1Blob) == 28, "V1Blob debe ser exactamente 28 bytes para simular blobs de Fase 1");

static V1Blob makeV1Blob(uint8_t hunger = 30, uint8_t happiness = 60, uint8_t health = 80,
                          uint8_t lifeStage = 1, uint8_t deathCount = 0,
                          uint32_t ageMinutes = 45, uint32_t lastSavedAt = 100) {
    V1Blob b;
    memset(&b, 0, sizeof(b));
    b.magic            = 0xCAFE;
    b.version          = 1;
    b.bornAtEpoch      = 0;
    b.lastSavedAtEpoch = lastSavedAt;
    b.ageMinutes       = ageMinutes;
    b.lifeStage        = lifeStage;
    b.hunger           = hunger;
    b.happiness        = happiness;
    b.health           = health;
    b.deathCount       = deathCount;
    b.reserved         = 0;
    // CRC sobre los primeros 24 bytes (mismo layout que v1 Fase 1)
    b.crc32 = Persistence::computeCrc32(&b, 24);
    return b;
}

// =============================================================================
// 1. test_crc32_roundtrip
// El CRC calculado sobre un PersistedState de defaults coincide con el campo crc32.
// =============================================================================
void test_crc32_roundtrip() {
    PersistedState state;
    Persistence::fillDefaults(state);

    uint32_t computed = Persistence::computeCrc32(&state, offsetof(PersistedState, crc32));
    TEST_ASSERT_EQUAL_UINT32(computed, state.crc32);
}

// =============================================================================
// 2. test_crc32_changes_on_byte_flip
// Modificar cualquier byte antes del CRC debe cambiar el CRC resultante.
// =============================================================================
void test_crc32_changes_on_byte_flip() {
    PersistedState state;
    Persistence::fillDefaults(state);

    uint32_t original = state.crc32;

    // Flipar un byte en el payload (discipline, offset 24 — campo nuevo v2)
    state.discipline ^= 0xFF;
    uint32_t after = Persistence::computeCrc32(&state, offsetof(PersistedState, crc32));

    TEST_ASSERT_NOT_EQUAL(original, after);
}

// =============================================================================
// 3. test_fill_defaults_produces_valid_state
// fillDefaults produce un PersistedState v2 con magic correcto, version=2,
// Egg como etapa inicial, stats de huevo y CRC válido.
// =============================================================================
void test_fill_defaults_produces_valid_state() {
    PersistedState state;
    Persistence::fillDefaults(state);

    TEST_ASSERT_EQUAL_UINT16(0xCAFE, state.magic);
    TEST_ASSERT_EQUAL_UINT16(2,      state.version);
    TEST_ASSERT_EQUAL_UINT8(0,       state.lifeStage);  // Egg=0
    TEST_ASSERT_EQUAL_UINT8(0,       state.hunger);     // huevos no tienen hambre
    TEST_ASSERT_EQUAL_UINT8(100,     state.happiness);  // huevos son felices
    TEST_ASSERT_EQUAL_UINT8(100,     state.health);
    TEST_ASSERT_EQUAL_UINT16(0,      state.deathCount);
    TEST_ASSERT_EQUAL_UINT16(0,      state.reserved);
    // Campos v2
    TEST_ASSERT_EQUAL_UINT8(50,      state.discipline);
    TEST_ASSERT_EQUAL_UINT8(50,      state.weight);
    TEST_ASSERT_EQUAL_UINT8(0,       state.poopCount);
    TEST_ASSERT_EQUAL_UINT8(0,       state.flags);
    TEST_ASSERT_EQUAL_UINT16(500,    state.attentionScore);
    TEST_ASSERT_EQUAL_UINT16(0,      state.deathSustainMs);
    TEST_ASSERT_EQUAL_UINT8(0,       state.lastDeathCauseCode);

    // CRC coherente
    uint32_t expected = Persistence::computeCrc32(&state, offsetof(PersistedState, crc32));
    TEST_ASSERT_EQUAL_UINT32(expected, state.crc32);
}

// =============================================================================
// 4. test_save_then_load_roundtrip
// Guardar un estado v2 y cargarlo devuelve exactamente los mismos datos.
// =============================================================================
void test_save_then_load_roundtrip() {
    PersistedState original;
    Persistence::fillDefaults(original);
    original.hunger       = 75;
    original.happiness    = 30;
    original.health       = 88;
    original.ageMinutes   = 120;
    original.discipline   = 70;
    original.weight       = 60;
    original.attentionScore = 650;

    bool saved = g_persistence->save(original);
    TEST_ASSERT_TRUE(saved);

    PersistedState loaded;
    LoadResult result = g_persistence->load(loaded);

    TEST_ASSERT_EQUAL(LoadResult::Ok, result);
    TEST_ASSERT_EQUAL_UINT8(75,  loaded.hunger);
    TEST_ASSERT_EQUAL_UINT8(30,  loaded.happiness);
    TEST_ASSERT_EQUAL_UINT8(88,  loaded.health);
    TEST_ASSERT_EQUAL_UINT32(120, loaded.ageMinutes);
    TEST_ASSERT_EQUAL_UINT8(70,  loaded.discipline);
    TEST_ASSERT_EQUAL_UINT8(60,  loaded.weight);
    TEST_ASSERT_EQUAL_UINT16(650, loaded.attentionScore);
}

// =============================================================================
// 5. test_load_when_no_files_returns_none_found
// Sin ningún archivo en storage, load() devuelve NoneFound y rellena defaults.
// =============================================================================
void test_load_when_no_files_returns_none_found() {
    PersistedState out;
    LoadResult result = g_persistence->load(out);

    TEST_ASSERT_EQUAL(LoadResult::NoneFound, result);
    // Debe haber rellenado defaults v2
    TEST_ASSERT_EQUAL_UINT16(0xCAFE, out.magic);
    TEST_ASSERT_EQUAL_UINT16(2,      out.version);
    TEST_ASSERT_EQUAL_UINT8(0,       out.hunger);   // Egg: hunger=0
    TEST_ASSERT_EQUAL_UINT8(100,     out.health);
    TEST_ASSERT_EQUAL_UINT8(50,      out.discipline);
}

// =============================================================================
// 6. test_load_when_all_files_corrupted_returns_all_corrupt
// Si hay archivos pero todos tienen CRC inválido, load() devuelve AllCorrupt.
// =============================================================================
void test_load_when_all_files_corrupted_returns_all_corrupt() {
    PersistedState state;
    Persistence::fillDefaults(state);
    g_persistence->save(state);

    // Corromper el slot 0
    g_storage->corruptFile("/state_0.bin");

    PersistedState out;
    LoadResult result = g_persistence->load(out);

    TEST_ASSERT_EQUAL(LoadResult::AllCorrupt, result);
    TEST_ASSERT_EQUAL_UINT16(0xCAFE, out.magic);
}

// =============================================================================
// 7. test_load_with_wrong_magic_skips_file
// Un archivo con magic distinto de 0xCAFE se descarta; si no hay otros válidos
// se devuelve AllCorrupt.
// =============================================================================
void test_load_with_wrong_magic_skips_file() {
    PersistedState state;
    Persistence::fillDefaults(state);
    state.magic = 0xDEAD;  // magic inválido
    uint8_t buf[sizeof(state)];
    memcpy(buf, &state, sizeof(state));
    g_storage->write("/state_0.bin", buf, sizeof(buf));

    PersistedState out;
    LoadResult result = g_persistence->load(out);

    TEST_ASSERT_EQUAL(LoadResult::AllCorrupt, result);
}

// =============================================================================
// 8. test_load_with_wrong_crc_skips_file
// Un archivo con magic correcto pero CRC incorrecto se descarta.
// =============================================================================
void test_load_with_wrong_crc_skips_file() {
    PersistedState state;
    Persistence::fillDefaults(state);
    state.crc32 = 0xDEADBEEF;  // CRC manipulado

    uint8_t buf[sizeof(state)];
    memcpy(buf, &state, sizeof(buf));
    g_storage->write("/state_0.bin", buf, sizeof(buf));

    PersistedState out;
    LoadResult result = g_persistence->load(out);

    TEST_ASSERT_EQUAL(LoadResult::AllCorrupt, result);
}

// =============================================================================
// 9. test_load_picks_newest_by_lastSavedAt
// Con varios slots válidos, load() elige el que tiene lastSavedAtEpoch mayor.
// =============================================================================
void test_load_picks_newest_by_lastSavedAt() {
    // Slot 0: epoch 100, hunger=10
    PersistedState old_state;
    Persistence::fillDefaults(old_state);
    old_state.lastSavedAtEpoch = 100;
    old_state.hunger = 10;
    old_state.crc32 = Persistence::computeCrc32(&old_state, offsetof(PersistedState, crc32));
    g_storage->write("/state_0.bin", &old_state, sizeof(old_state));

    // Slot 1: epoch 200, hunger=90 (más nuevo)
    PersistedState new_state;
    Persistence::fillDefaults(new_state);
    new_state.lastSavedAtEpoch = 200;
    new_state.hunger = 90;
    new_state.crc32 = Persistence::computeCrc32(&new_state, offsetof(PersistedState, crc32));
    g_storage->write("/state_1.bin", &new_state, sizeof(new_state));

    PersistedState out;
    LoadResult result = g_persistence->load(out);

    TEST_ASSERT_EQUAL(LoadResult::Ok, result);
    TEST_ASSERT_EQUAL_UINT8(90, out.hunger);
    TEST_ASSERT_EQUAL_UINT32(200, out.lastSavedAtEpoch);
}

// =============================================================================
// 10. test_load_picks_newest_by_ageMinutes_when_epoch_zero
// Cuando lastSavedAtEpoch == 0 (sin NTP), desempate por ageMinutes mayor.
// =============================================================================
void test_load_picks_newest_by_ageMinutes_when_epoch_zero() {
    // Slot 0: ageMinutes=50, hunger=20
    PersistedState older;
    Persistence::fillDefaults(older);
    older.lastSavedAtEpoch = 0;
    older.ageMinutes = 50;
    older.hunger = 20;
    older.crc32 = Persistence::computeCrc32(&older, offsetof(PersistedState, crc32));
    g_storage->write("/state_0.bin", &older, sizeof(older));

    // Slot 1: ageMinutes=120, hunger=80 (más viejo = más ageMinutes)
    PersistedState newer;
    Persistence::fillDefaults(newer);
    newer.lastSavedAtEpoch = 0;
    newer.ageMinutes = 120;
    newer.hunger = 80;
    newer.crc32 = Persistence::computeCrc32(&newer, offsetof(PersistedState, crc32));
    g_storage->write("/state_1.bin", &newer, sizeof(newer));

    PersistedState out;
    LoadResult result = g_persistence->load(out);

    TEST_ASSERT_EQUAL(LoadResult::Ok, result);
    TEST_ASSERT_EQUAL_UINT8(80, out.hunger);
    TEST_ASSERT_EQUAL_UINT32(120, out.ageMinutes);
}

// =============================================================================
// 11. test_save_rotation_writes_to_next_slot
// El primer save escribe en slot 0, el segundo en slot 1, etc.
// =============================================================================
void test_save_rotation_writes_to_next_slot() {
    PersistedState state;
    Persistence::fillDefaults(state);

    // Primer save → debe ir a slot 0
    g_persistence->save(state);
    TEST_ASSERT_TRUE(g_storage->exists("/state_0.bin"));

    // Segundo save → debe ir a slot 1
    g_persistence->save(state);
    TEST_ASSERT_TRUE(g_storage->exists("/state_1.bin"));

    // Tercer save → slot 2
    g_persistence->save(state);
    TEST_ASSERT_TRUE(g_storage->exists("/state_2.bin"));
}

// =============================================================================
// 12. test_save_rotation_removes_oldest_after_four_writes
// Tras 4 saves, el slot más antiguo ha sido borrado.
// =============================================================================
void test_save_rotation_removes_oldest_after_four_writes() {
    PersistedState state;
    Persistence::fillDefaults(state);

    for (int i = 0; i < 4; ++i) {
        g_persistence->save(state);
    }

    // El slot 0 debe haber sido borrado tras el 4º save
    TEST_ASSERT_FALSE(g_storage->exists("/state_0.bin"));

    // Los slots 1, 2, 3 deben existir
    TEST_ASSERT_TRUE(g_storage->exists("/state_1.bin"));
    TEST_ASSERT_TRUE(g_storage->exists("/state_2.bin"));
    TEST_ASSERT_TRUE(g_storage->exists("/state_3.bin"));
}

// =============================================================================
// 13. test_save_does_not_lose_old_state_on_simulated_power_loss
// Si write() falla (simula corte de energía), el estado anterior queda intacto.
// =============================================================================
void test_save_does_not_lose_old_state_on_simulated_power_loss() {
    PersistedState good;
    Persistence::fillDefaults(good);
    good.hunger    = 42;
    good.ageMinutes = 99;

    bool ok = g_persistence->save(good);
    TEST_ASSERT_TRUE(ok);

    g_storage->simulateWriteFailure = true;
    PersistedState corrupted;
    Persistence::fillDefaults(corrupted);
    corrupted.hunger = 1;
    g_persistence->save(corrupted);  // falla silenciosamente

    g_storage->simulateWriteFailure = false;

    PersistedState loaded;
    LoadResult result = g_persistence->load(loaded);

    TEST_ASSERT_EQUAL(LoadResult::Ok, result);
    TEST_ASSERT_EQUAL_UINT8(42, loaded.hunger);
    TEST_ASSERT_EQUAL_UINT32(99, loaded.ageMinutes);
}

// =============================================================================
// 14. test_migrate_same_version_returns_true
// migrate() con version == kCurrentVersion (2) devuelve true sin modificar estado.
// =============================================================================
void test_migrate_same_version_returns_true() {
    PersistedState state;
    Persistence::fillDefaults(state);

    TEST_ASSERT_EQUAL_UINT16(2, state.version);  // kCurrentVersion = 2
    bool result = Persistence::migrate(state);
    TEST_ASSERT_TRUE(result);
    // No debe haber modificado nada
    TEST_ASSERT_EQUAL_UINT16(2, state.version);
    TEST_ASSERT_EQUAL_UINT8(50, state.discipline);
}

// =============================================================================
// 15. test_migrate_higher_version_returns_false
// Un estado con version > kCurrentVersion (firmware futuro) no es migrable.
// =============================================================================
void test_migrate_higher_version_returns_false() {
    PersistedState state;
    Persistence::fillDefaults(state);
    state.version = 99;  // versión de firmware futuro

    bool result = Persistence::migrate(state);
    TEST_ASSERT_FALSE(result);
}

// =============================================================================
// 16. test_load_unknown_version_returns_migration_failed
// Si el archivo tiene versión incompatible, load() devuelve MigrationFailed
// y rellena defaults para que el firmware arranque igualmente.
// =============================================================================
void test_load_unknown_version_returns_migration_failed() {
    PersistedState future;
    Persistence::fillDefaults(future);
    future.version = 42;
    future.hunger  = 77;
    // Recalcular CRC con la versión modificada para que pase el filtro de CRC
    future.crc32 = Persistence::computeCrc32(&future, offsetof(PersistedState, crc32));
    g_storage->write("/state_0.bin", &future, sizeof(future));

    PersistedState out;
    LoadResult result = g_persistence->load(out);

    TEST_ASSERT_EQUAL(LoadResult::MigrationFailed, result);
    // Debe haber rellenado defaults
    TEST_ASSERT_EQUAL_UINT16(0xCAFE, out.magic);
    TEST_ASSERT_EQUAL_UINT8(0, out.hunger);  // default Egg: hunger=0, no 77
}

// =============================================================================
// 17. test_persistedstate_is_exactly_40_bytes
// Verificación en runtime del tamaño del struct v2 (complementa static_assert).
// =============================================================================
void test_persistedstate_is_exactly_40_bytes() {
    TEST_ASSERT_EQUAL_UINT32(40, sizeof(PersistedState));
}

// =============================================================================
// 18. test_load_v1_blob_migrates_to_v2
// Simula un slot con un blob v1 (28 bytes, CRC válido sobre primeros 24 bytes).
// load() debe devolver LoadResult::Migrated, version debe ser 2,
// campos v1 (hunger/happiness/health/lifeStage) preservados,
// campos v2 nuevos en defaults.
// =============================================================================
void test_load_v1_blob_migrates_to_v2() {
    V1Blob v1 = makeV1Blob(/*hunger=*/30, /*happiness=*/60, /*health=*/80,
                            /*lifeStage=*/1, /*deathCount=*/0,
                            /*ageMinutes=*/45, /*lastSavedAt=*/100);

    // Escribir 28 bytes (tamaño v1) directamente en el storage
    g_storage->write("/state_0.bin", &v1, sizeof(v1));

    PersistedState out;
    LoadResult result = g_persistence->load(out);

    TEST_ASSERT_EQUAL(LoadResult::Migrated, result);

    // Versión debe haberse actualizado a 2
    TEST_ASSERT_EQUAL_UINT16(2, out.version);

    // Campos v1 preservados
    TEST_ASSERT_EQUAL_UINT8(30,  out.hunger);
    TEST_ASSERT_EQUAL_UINT8(60,  out.happiness);
    TEST_ASSERT_EQUAL_UINT8(80,  out.health);
    TEST_ASSERT_EQUAL_UINT8(1,   out.lifeStage);   // Baby=1 heredado
    TEST_ASSERT_EQUAL_UINT32(45, out.ageMinutes);
    TEST_ASSERT_EQUAL_UINT16(0xCAFE, out.magic);

    // Campos nuevos v2 en defaults
    TEST_ASSERT_EQUAL_UINT8(50,  out.discipline);
    TEST_ASSERT_EQUAL_UINT8(50,  out.weight);
    TEST_ASSERT_EQUAL_UINT8(0,   out.poopCount);
    TEST_ASSERT_EQUAL_UINT8(0,   out.flags);
    TEST_ASSERT_EQUAL_UINT16(500, out.attentionScore);
    TEST_ASSERT_EQUAL_UINT16(0,  out.deathSustainMs);
    TEST_ASSERT_EQUAL_UINT8(0,   out.lastDeathCauseCode);
}

// =============================================================================
// 19. test_migrate_v1_to_v2_preserves_v1_fields
// migrate() sobre un estado con version=1 preserva hunger/happiness/health/
// lifeStage y actualiza version a 2.
// =============================================================================
void test_migrate_v1_to_v2_preserves_v1_fields() {
    PersistedState state;
    memset(&state, 0, sizeof(state));
    state.magic      = 0xCAFE;
    state.version    = 1;
    state.lifeStage  = 1;   // Baby
    state.hunger     = 55;
    state.happiness  = 40;
    state.health     = 70;
    state.deathCount = 3;
    state.ageMinutes = 200;

    bool ok = Persistence::migrate(state);
    TEST_ASSERT_TRUE(ok);

    // Versión actualizada
    TEST_ASSERT_EQUAL_UINT16(2, state.version);

    // Campos v1 preservados
    TEST_ASSERT_EQUAL_UINT8(55,  state.hunger);
    TEST_ASSERT_EQUAL_UINT8(40,  state.happiness);
    TEST_ASSERT_EQUAL_UINT8(70,  state.health);
    TEST_ASSERT_EQUAL_UINT8(1,   state.lifeStage);
    TEST_ASSERT_EQUAL_UINT16(3,  state.deathCount);
    TEST_ASSERT_EQUAL_UINT32(200, state.ageMinutes);
}

// =============================================================================
// 20. test_migrate_v1_to_v2_sets_new_field_defaults
// migrate() sobre un estado con version=1 asigna defaults correctos
// para los 12 bytes nuevos del bloque v2.
// =============================================================================
void test_migrate_v1_to_v2_sets_new_field_defaults() {
    PersistedState state;
    memset(&state, 0, sizeof(state));
    state.magic   = 0xCAFE;
    state.version = 1;

    // Poner "basura" en los campos v2 para verificar que se sobreescriben
    state.discipline         = 0xFF;
    state.weight             = 0xFF;
    state.poopCount          = 99;
    state.flags              = 0xFF;
    state.attentionScore     = 9999;
    state.deathSustainMs     = 9999;
    state.lastDeathCauseCode = 99;
    state.adultType          = 99;
    state.reservedV2b        = 9999;

    bool ok = Persistence::migrate(state);
    TEST_ASSERT_TRUE(ok);

    // Defaults v2 correctos
    TEST_ASSERT_EQUAL_UINT8(50,   state.discipline);
    TEST_ASSERT_EQUAL_UINT8(50,   state.weight);
    TEST_ASSERT_EQUAL_UINT8(0,    state.poopCount);
    TEST_ASSERT_EQUAL_UINT8(0,    state.flags);
    TEST_ASSERT_EQUAL_UINT16(500, state.attentionScore);
    TEST_ASSERT_EQUAL_UINT16(0,   state.deathSustainMs);
    TEST_ASSERT_EQUAL_UINT8(0,    state.lastDeathCauseCode);
    TEST_ASSERT_EQUAL_UINT8(0,    state.adultType);
    TEST_ASSERT_EQUAL_UINT16(0,   state.reservedV2b);
}

// =============================================================================
// 21. test_load_blob_with_short_size_fills_remaining_with_zero
// InMemoryStorage::read() con un archivo más corto que size rellena con 0.
// Verifica la semántica zero-pad directamente sobre el storage.
// =============================================================================
void test_load_blob_with_short_size_fills_remaining_with_zero() {
    // Escribir 10 bytes de datos conocidos
    uint8_t shortData[10] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22, 0x33, 0x44};
    g_storage->write("/state_0.bin", shortData, sizeof(shortData));

    // Leer 20 bytes — los 10 últimos deben ser 0
    uint8_t buf[20];
    memset(buf, 0x55, sizeof(buf));  // rellenar con patrón diferente de 0 para detectar fallo

    bool ok = g_storage->read("/state_0.bin", buf, sizeof(buf));
    TEST_ASSERT_TRUE(ok);

    // Primeros 10 bytes: datos originales
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_EQUAL_UINT8(shortData[i], buf[i]);
    }
    // Bytes 10..19: deben ser 0 (zero-pad)
    for (int i = 10; i < 20; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, buf[i]);
    }
}

// =============================================================================
// 22. test_save_in_v2_format_writes_40_bytes
// save() siempre escribe exactamente 40 bytes en el slot correspondiente.
// =============================================================================
void test_save_in_v2_format_writes_40_bytes() {
    PersistedState state;
    Persistence::fillDefaults(state);

    bool ok = g_persistence->save(state);
    TEST_ASSERT_TRUE(ok);

    // El archivo escrito debe tener exactamente 40 bytes
    size_t written = g_storage->fileSize("/state_0.bin");
    TEST_ASSERT_EQUAL_UINT32(40, written);
}

// =============================================================================
// main — punto de entrada Unity para env:native
// =============================================================================
int main() {
    UNITY_BEGIN();

    RUN_TEST(test_crc32_roundtrip);
    RUN_TEST(test_crc32_changes_on_byte_flip);
    RUN_TEST(test_fill_defaults_produces_valid_state);
    RUN_TEST(test_save_then_load_roundtrip);
    RUN_TEST(test_load_when_no_files_returns_none_found);
    RUN_TEST(test_load_when_all_files_corrupted_returns_all_corrupt);
    RUN_TEST(test_load_with_wrong_magic_skips_file);
    RUN_TEST(test_load_with_wrong_crc_skips_file);
    RUN_TEST(test_load_picks_newest_by_lastSavedAt);
    RUN_TEST(test_load_picks_newest_by_ageMinutes_when_epoch_zero);
    RUN_TEST(test_save_rotation_writes_to_next_slot);
    RUN_TEST(test_save_rotation_removes_oldest_after_four_writes);
    RUN_TEST(test_save_does_not_lose_old_state_on_simulated_power_loss);
    RUN_TEST(test_migrate_same_version_returns_true);
    RUN_TEST(test_migrate_higher_version_returns_false);
    RUN_TEST(test_load_unknown_version_returns_migration_failed);
    RUN_TEST(test_persistedstate_is_exactly_40_bytes);
    RUN_TEST(test_load_v1_blob_migrates_to_v2);
    RUN_TEST(test_migrate_v1_to_v2_preserves_v1_fields);
    RUN_TEST(test_migrate_v1_to_v2_sets_new_field_defaults);
    RUN_TEST(test_load_blob_with_short_size_fills_remaining_with_zero);
    RUN_TEST(test_save_in_v2_format_writes_40_bytes);

    return UNITY_END();
}
