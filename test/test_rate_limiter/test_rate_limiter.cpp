// =============================================================================
// test_rate_limiter.cpp — tests para RateLimiter (token-bucket por IP).
//
// Framework: Unity (pio test -e native).
// RateLimiter es lógica pura con tiempo inyectado: no necesita mock de hardware.
//
// Hito 4 (Fase 3): 9 tests según sección 9.3 del plan.
// =============================================================================

#include <unity.h>
#include "RateLimiter.h"

static RateLimiter* g_rl = nullptr;

void setUp() {
    g_rl = new RateLimiter();
}

void tearDown() {
    delete g_rl;
    g_rl = nullptr;
}

// IPs de test arbitrarias (uint32_t)
static constexpr uint32_t kIpA = 0xC0A80001;  // 192.168.0.1
static constexpr uint32_t kIpB = 0xC0A80002;  // 192.168.0.2
static constexpr uint32_t kIpC = 0xC0A80003;  // 192.168.0.3
static constexpr uint32_t kIpD = 0xC0A80004;  // 192.168.0.4
static constexpr uint32_t kIpE = 0xC0A80005;  // 192.168.0.5

// =============================================================================
// 1. bucket_starts_full
// Un cliente nuevo tiene kBucketCapacity tokens disponibles:
// los primeros kBucketCapacity allows deben ser todos true.
// =============================================================================
void test_bucket_starts_full() {
    for (uint8_t i = 0; i < RateLimiter::kBucketCapacity; ++i) {
        TEST_ASSERT_TRUE(g_rl->allow(kIpA, 1000));
    }
}

// =============================================================================
// 2. bucket_consumes_token_per_request
// Tras 5 allows, el bucket tiene exactamente 5 tokens restantes:
// el 6º, 7º, ... 10º allow deben ser true, el 11º false.
// =============================================================================
void test_bucket_consumes_token_per_request() {
    const uint32_t now = 5000;
    for (uint8_t i = 0; i < 5; ++i) {
        TEST_ASSERT_TRUE(g_rl->allow(kIpA, now));
    }
    // Quedan 5 tokens: los siguientes 5 deben ser permitidos.
    for (uint8_t i = 0; i < 5; ++i) {
        TEST_ASSERT_TRUE(g_rl->allow(kIpA, now));
    }
    // Bucket vacío: el siguiente debe ser rechazado.
    TEST_ASSERT_FALSE(g_rl->allow(kIpA, now));
}

// =============================================================================
// 3. bucket_refuses_when_empty
// Tras kBucketCapacity allows en el mismo instante (mismo nowMs),
// el (capacity+1)-ésimo allow devuelve false.
// =============================================================================
void test_bucket_refuses_when_empty() {
    const uint32_t now = 0;
    for (uint8_t i = 0; i < RateLimiter::kBucketCapacity; ++i) {
        TEST_ASSERT_TRUE(g_rl->allow(kIpA, now));
    }
    TEST_ASSERT_FALSE(g_rl->allow(kIpA, now));
}

// =============================================================================
// 4. bucket_refills_at_rate
// Tras vaciar el bucket, esperar kRefillIntervalMs añade 1 token:
// el siguiente allow debe ser permitido.
// =============================================================================
void test_bucket_refills_at_rate() {
    const uint32_t t0 = 10000;

    // Vaciar el bucket.
    for (uint8_t i = 0; i < RateLimiter::kBucketCapacity; ++i) {
        g_rl->allow(kIpA, t0);
    }
    TEST_ASSERT_FALSE(g_rl->allow(kIpA, t0));

    // Avanzar exactamente kRefillIntervalMs → +1 token.
    uint32_t t1 = t0 + RateLimiter::kRefillIntervalMs;
    TEST_ASSERT_TRUE(g_rl->allow(kIpA, t1));

    // Ahora está vacío de nuevo (0 tokens).
    TEST_ASSERT_FALSE(g_rl->allow(kIpA, t1));
}

// =============================================================================
// 5. bucket_refills_capped_at_capacity
// Tras mucho tiempo sin consumir tokens, el bucket no supera kBucketCapacity.
// Verificamos haciendo kBucketCapacity+1 allows: los primeros 10 OK, el 11º false.
// =============================================================================
void test_bucket_refills_capped_at_capacity() {
    const uint32_t t0 = 0;
    const uint32_t tBig = 10000000;  // 10000 segundos → 16666 tokens potenciales sin cap

    // Un allow inicial para registrar la IP y fijar lastRefillMs.
    g_rl->allow(kIpA, t0);

    // Vaciar completamente para partir de 0.
    for (uint8_t i = 0; i < RateLimiter::kBucketCapacity - 1; ++i) {
        g_rl->allow(kIpA, t0);
    }
    // El bucket ahora está en 0 tokens.
    TEST_ASSERT_FALSE(g_rl->allow(kIpA, t0));

    // Avanzar mucho tiempo: refill debe quedar clamped en kBucketCapacity.
    for (uint8_t i = 0; i < RateLimiter::kBucketCapacity; ++i) {
        TEST_ASSERT_TRUE(g_rl->allow(kIpA, tBig));
    }
    // El 11º allow (capacity+1) debe fallar: cap está en kBucketCapacity.
    TEST_ASSERT_FALSE(g_rl->allow(kIpA, tBig));
}

// =============================================================================
// 6. multiple_clients_independent
// IP A puede consumir todos sus tokens sin afectar al bucket de IP B.
// =============================================================================
void test_multiple_clients_independent() {
    const uint32_t now = 0;

    // Vaciar el bucket de A.
    for (uint8_t i = 0; i < RateLimiter::kBucketCapacity; ++i) {
        g_rl->allow(kIpA, now);
    }
    TEST_ASSERT_FALSE(g_rl->allow(kIpA, now));  // A rate-limited

    // B sigue con bucket lleno.
    for (uint8_t i = 0; i < RateLimiter::kBucketCapacity; ++i) {
        TEST_ASSERT_TRUE(g_rl->allow(kIpB, now));
    }
    TEST_ASSERT_FALSE(g_rl->allow(kIpB, now));  // B rate-limited solo ahora
}

// =============================================================================
// 7. lru_evicts_oldest_when_full
// Con kMaxTrackedClients IPs distintas, al añadir una (kMaxTrackedClients+1)-ésima
// el slot del IP menos recientemente usado se reutiliza.
// =============================================================================
void test_lru_evicts_oldest_when_full() {
    // kMaxTrackedClients = 4
    const uint32_t t0 = 1000;

    // Registrar kMaxTrackedClients IPs con distintos timestamps
    // para crear un orden LRU claro. A es el más antiguo.
    g_rl->allow(kIpA, t0 + 0);   // lastUseMs = t0
    g_rl->allow(kIpB, t0 + 1);
    g_rl->allow(kIpC, t0 + 2);
    g_rl->allow(kIpD, t0 + 3);   // lastUseMs = t0 + 3 (más reciente)

    // La tabla está llena. Al añadir kIpE, se debe evictar kIpA (oldest).
    const uint32_t t1 = t0 + 100;
    TEST_ASSERT_TRUE(g_rl->allow(kIpE, t1));  // E entra, A es evictado

    // kIpA ya no tiene bucket propio: su próxima request crea bucket nuevo
    // (con tokens = capacity). No queremos que falle por haber "quemado" tokens antes.
    // Sólo verificamos que E funciona y no crashea.
    TEST_ASSERT_TRUE(g_rl->allow(kIpE, t1));  // E tiene 8 tokens restantes (usó 2)
}

// =============================================================================
// 8. eviction_does_not_punish_new_client
// El cliente que ocupa el slot evictado comienza con bucket lleno (kBucketCapacity).
// =============================================================================
void test_eviction_does_not_punish_new_client() {
    const uint32_t t0 = 0;

    // Llenar todos los slots.
    g_rl->allow(kIpA, t0);
    g_rl->allow(kIpB, t0 + 10);
    g_rl->allow(kIpC, t0 + 20);
    g_rl->allow(kIpD, t0 + 30);

    // Vaciar el bucket de A para que quede en 0 tokens antes de la eviction.
    for (uint8_t i = 0; i < RateLimiter::kBucketCapacity - 1; ++i) {
        g_rl->allow(kIpA, t0);
    }

    // A tiene 0 tokens. Ahora registrar E → se evicta A.
    const uint32_t t1 = t0 + 100;
    // E debe tener bucket lleno → kBucketCapacity allows consecutivos OK.
    for (uint8_t i = 0; i < RateLimiter::kBucketCapacity; ++i) {
        TEST_ASSERT_TRUE(g_rl->allow(kIpE, t1));
    }
    TEST_ASSERT_FALSE(g_rl->allow(kIpE, t1));  // ahora vacío
}

// =============================================================================
// 9. clock_jump_does_not_overflow
// Si nowMs salta hacia atrás (simulando overflow de millis o reinicio del reloj),
// el limiter no queda en estado indefinido ni produce tokens negativos.
// =============================================================================
void test_clock_jump_does_not_overflow() {
    const uint32_t tFuture = 1000;
    const uint32_t tPast   = 50;  // simula "retroceso"

    // Consumir 5 tokens con timestamp futuro.
    for (uint8_t i = 0; i < 5; ++i) {
        TEST_ASSERT_TRUE(g_rl->allow(kIpA, tFuture));
    }

    // Clock jump hacia atrás: no debe crashear, no debe dar tokens negativos.
    // El resultado puede ser true o false, pero no debe producir UB.
    bool result = g_rl->allow(kIpA, tPast);
    (void)result;  // no validamos el valor, solo que no hubo crash/UB

    // Y el siguiente allow con timestamp mayor (normal) debe funcionar sin problemas.
    TEST_ASSERT_TRUE(g_rl->allow(kIpA, tFuture + 1));
}

// =============================================================================
// main — punto de entrada Unity para env:native
// =============================================================================
int main() {
    UNITY_BEGIN();

    RUN_TEST(test_bucket_starts_full);
    RUN_TEST(test_bucket_consumes_token_per_request);
    RUN_TEST(test_bucket_refuses_when_empty);
    RUN_TEST(test_bucket_refills_at_rate);
    RUN_TEST(test_bucket_refills_capped_at_capacity);
    RUN_TEST(test_multiple_clients_independent);
    RUN_TEST(test_lru_evicts_oldest_when_full);
    RUN_TEST(test_eviction_does_not_punish_new_client);
    RUN_TEST(test_clock_jump_does_not_overflow);

    return UNITY_END();
}
