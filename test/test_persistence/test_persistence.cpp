// =============================================================================
// test_persistence.cpp — 17 tests obligatorios para Persistence (hito 3, Fase 1).
//
// Framework: Unity (pio test -e native).
// Mock: InMemoryStorage (sin LittleFS, sin Arduino).
//
// Cada test tiene setUp/tearDown que crean/destruyen instancias limpias.
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

    // Flipar un byte en el payload (hunger, offset 17)
    state.hunger ^= 0xFF;
    uint32_t after = Persistence::computeCrc32(&state, offsetof(PersistedState, crc32));

    TEST_ASSERT_NOT_EQUAL(original, after);
}

// =============================================================================
// 3. test_fill_defaults_produces_valid_state
// fillDefaults produce un PersistedState con magic correcto, version=1,
// stats sanos y CRC válido.
// =============================================================================
void test_fill_defaults_produces_valid_state() {
    PersistedState state;
    Persistence::fillDefaults(state);

    TEST_ASSERT_EQUAL_UINT16(0xCAFE, state.magic);
    TEST_ASSERT_EQUAL_UINT16(1,      state.version);
    TEST_ASSERT_EQUAL_UINT8(1,       state.lifeStage);  // Baby
    TEST_ASSERT_EQUAL_UINT8(50,      state.hunger);
    TEST_ASSERT_EQUAL_UINT8(50,      state.happiness);
    TEST_ASSERT_EQUAL_UINT8(100,     state.health);
    TEST_ASSERT_EQUAL_UINT16(0,      state.deathCount);
    TEST_ASSERT_EQUAL_UINT16(0,      state.reserved);

    // CRC coherente
    uint32_t expected = Persistence::computeCrc32(&state, offsetof(PersistedState, crc32));
    TEST_ASSERT_EQUAL_UINT32(expected, state.crc32);
}

// =============================================================================
// 4. test_save_then_load_roundtrip
// Guardar un estado y cargarlo devuelve exactamente los mismos datos.
// =============================================================================
void test_save_then_load_roundtrip() {
    PersistedState original;
    Persistence::fillDefaults(original);
    original.hunger    = 75;
    original.happiness = 30;
    original.health    = 88;
    original.ageMinutes = 120;

    bool saved = g_persistence->save(original);
    TEST_ASSERT_TRUE(saved);

    PersistedState loaded;
    LoadResult result = g_persistence->load(loaded);

    TEST_ASSERT_EQUAL(LoadResult::Ok, result);
    TEST_ASSERT_EQUAL_UINT8(75,  loaded.hunger);
    TEST_ASSERT_EQUAL_UINT8(30,  loaded.happiness);
    TEST_ASSERT_EQUAL_UINT8(88,  loaded.health);
    TEST_ASSERT_EQUAL_UINT32(120, loaded.ageMinutes);
}

// =============================================================================
// 5. test_load_when_no_files_returns_none_found
// Sin ningún archivo en storage, load() devuelve NoneFound y rellena defaults.
// =============================================================================
void test_load_when_no_files_returns_none_found() {
    PersistedState out;
    LoadResult result = g_persistence->load(out);

    TEST_ASSERT_EQUAL(LoadResult::NoneFound, result);
    // Debe haber rellenado defaults
    TEST_ASSERT_EQUAL_UINT16(0xCAFE, out.magic);
    TEST_ASSERT_EQUAL_UINT8(50, out.hunger);
    TEST_ASSERT_EQUAL_UINT8(100, out.health);
}

// =============================================================================
// 6. test_load_when_all_files_corrupted_returns_all_corrupt
// Si hay archivos pero todos tienen CRC inválido, load() devuelve AllCorrupt.
// =============================================================================
void test_load_when_all_files_corrupted_returns_all_corrupt() {
    // Escribir un estado válido, luego corromperlo manualmente en el storage
    PersistedState state;
    Persistence::fillDefaults(state);
    g_persistence->save(state);

    // Corromper el slot 0 (que es donde fue a parar el primer save)
    g_storage->corruptFile("/state_0.bin");

    PersistedState out;
    LoadResult result = g_persistence->load(out);

    TEST_ASSERT_EQUAL(LoadResult::AllCorrupt, result);
    // Debe haber rellenado defaults de todas formas
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
    // Escribir directamente en el storage (sin pasar por save() que fijaría CRC de magic correcto)
    uint8_t buf[sizeof(state)];
    memcpy(buf, &state, sizeof(state));
    g_storage->write("/state_0.bin", buf, sizeof(buf));

    PersistedState out;
    LoadResult result = g_persistence->load(out);

    // El slot existe pero se descarta por magic → AllCorrupt (hay archivo pero inválido)
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
// Tras 4 saves, el slot más antiguo (el que se sobreescribirá a continuación)
// ha sido borrado para no acumular más de ~3 slots simultáneos.
// =============================================================================
void test_save_rotation_removes_oldest_after_four_writes() {
    PersistedState state;
    Persistence::fillDefaults(state);

    // 4 saves rotativos: slots 0, 1, 2, 3
    // Cada save borra (slotToWrite+1)%4 después de escribir:
    //   save→0: escribe 0, borra 1 (no existe, OK)
    //   save→1: escribe 1, borra 2 (no existe, OK)
    //   save→2: escribe 2, borra 3 (no existe, OK)
    //   save→3: escribe 3, borra 0 ← slot 0 desaparece

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
// Si write() falla (simula corte de energía a mitad), el estado anterior
// sigue siendo cargable.
// =============================================================================
void test_save_does_not_lose_old_state_on_simulated_power_loss() {
    PersistedState good;
    Persistence::fillDefaults(good);
    good.hunger    = 42;
    good.ageMinutes = 99;

    // Primer save OK
    bool ok = g_persistence->save(good);
    TEST_ASSERT_TRUE(ok);

    // Simular corte de energía en el segundo save
    g_storage->simulateWriteFailure = true;
    PersistedState corrupted;
    Persistence::fillDefaults(corrupted);
    corrupted.hunger = 1;
    g_persistence->save(corrupted);  // falla silenciosamente

    g_storage->simulateWriteFailure = false;

    // El estado bueno sigue cargable
    PersistedState loaded;
    LoadResult result = g_persistence->load(loaded);

    TEST_ASSERT_EQUAL(LoadResult::Ok, result);
    TEST_ASSERT_EQUAL_UINT8(42, loaded.hunger);
    TEST_ASSERT_EQUAL_UINT32(99, loaded.ageMinutes);
}

// =============================================================================
// 14. test_migrate_same_version_returns_true
// migrate() con version == kCurrentVersion devuelve true sin modificar el estado.
// =============================================================================
void test_migrate_same_version_returns_true() {
    PersistedState state;
    Persistence::fillDefaults(state);

    TEST_ASSERT_EQUAL_UINT16(1, state.version);
    bool result = Persistence::migrate(state);
    TEST_ASSERT_TRUE(result);
    // No debe haber modificado nada
    TEST_ASSERT_EQUAL_UINT16(1, state.version);
    TEST_ASSERT_EQUAL_UINT8(50, state.hunger);
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
    // Escribir manualmente un estado con versión futura y CRC correcto
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
    TEST_ASSERT_EQUAL_UINT8(50, out.hunger);  // default, no 77
}

// =============================================================================
// 17. test_persistedstate_is_exactly_28_bytes
// Verificación en runtime (complementa el static_assert en Persistence.h).
// =============================================================================
void test_persistedstate_is_exactly_28_bytes() {
    TEST_ASSERT_EQUAL_UINT32(28, sizeof(PersistedState));
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
    RUN_TEST(test_persistedstate_is_exactly_28_bytes);

    return UNITY_END();
}
