// =============================================================================
// RateLimiter.cpp — Token-bucket rate limiter por IP cliente.
//
// Sin Arduino.h: testeable en host (env:native).
// =============================================================================

#include "RateLimiter.h"

// -----------------------------------------------------------------------------
// reset — limpia todos los slots.
// -----------------------------------------------------------------------------
void RateLimiter::reset() {
    for (uint8_t i = 0; i < kMaxTrackedClients; ++i) {
        buckets_[i] = IpBucket{};
    }
}

// -----------------------------------------------------------------------------
// allow — consume 1 token del bucket del cliente. Devuelve true si OK.
// -----------------------------------------------------------------------------
bool RateLimiter::allow(uint32_t clientIp, uint32_t nowMs) {
    IpBucket& b = findOrAssign_(clientIp, nowMs);
    refill_(b, nowMs);

    bool permitted = (b.tokens > 0);
    if (permitted) {
        --b.tokens;
    }
    // Actualizar lastUseMs siempre para LRU, independientemente del resultado.
    b.lastUseMs = nowMs;
    return permitted;
}

// -----------------------------------------------------------------------------
// findOrAssign_ — busca el bucket del clientIp o asigna uno nuevo.
//   1. Busca ip existente → retorna su bucket.
//   2. Busca slot libre (ip == 0) → inicializa y retorna.
//   3. Si no hay libre, evict el bucket con lastUseMs más antiguo.
// -----------------------------------------------------------------------------
RateLimiter::IpBucket& RateLimiter::findOrAssign_(uint32_t clientIp, uint32_t nowMs) {
    // Paso 1: buscar IP existente.
    for (uint8_t i = 0; i < kMaxTrackedClients; ++i) {
        if (buckets_[i].ip == clientIp) {
            return buckets_[i];
        }
    }

    // Paso 2: buscar slot libre.
    for (uint8_t i = 0; i < kMaxTrackedClients; ++i) {
        if (buckets_[i].ip == 0) {
            buckets_[i].ip           = clientIp;
            buckets_[i].tokens       = kBucketCapacity;
            buckets_[i].lastRefillMs = nowMs;
            buckets_[i].lastUseMs    = nowMs;
            return buckets_[i];
        }
    }

    // Paso 3: evict el más antiguo por lastUseMs (LRU).
    uint8_t oldest = 0;
    for (uint8_t i = 1; i < kMaxTrackedClients; ++i) {
        if (buckets_[i].lastUseMs < buckets_[oldest].lastUseMs) {
            oldest = i;
        }
    }
    buckets_[oldest].ip           = clientIp;
    buckets_[oldest].tokens       = kBucketCapacity;
    buckets_[oldest].lastRefillMs = nowMs;
    buckets_[oldest].lastUseMs    = nowMs;
    return buckets_[oldest];
}

// -----------------------------------------------------------------------------
// refill_ — añade tokens al bucket según tiempo transcurrido.
//   - Si nowMs < lastRefillMs (clock jump hacia atrás), resetea lastRefillMs
//     defensivamente para evitar comportamiento indefinido (overflow).
//   - addTokens = elapsed / kRefillIntervalMs
//   - tokens = min(kBucketCapacity, tokens + addTokens)
//   - lastRefillMs avanza exactamente los intervalos consumidos (sin drift).
// -----------------------------------------------------------------------------
void RateLimiter::refill_(IpBucket& b, uint32_t nowMs) {
    // Defensive: clock jump hacia atrás (p.ej. overflow de millis()).
    if (nowMs < b.lastRefillMs) {
        b.lastRefillMs = nowMs;
        return;
    }

    uint32_t elapsed   = nowMs - b.lastRefillMs;
    uint32_t addTokens = elapsed / kRefillIntervalMs;

    if (addTokens > 0) {
        uint32_t newTokens = static_cast<uint32_t>(b.tokens) + addTokens;
        b.tokens = (newTokens > kBucketCapacity)
                   ? kBucketCapacity
                   : static_cast<uint8_t>(newTokens);
        // Avanzar lastRefillMs solo los intervalos exactos consumidos (sin drift).
        b.lastRefillMs += addTokens * kRefillIntervalMs;
    }
}
