#pragma once
// =============================================================================
// RateLimiter.h — Token-bucket rate limiter por IP cliente.
//
// Sin Arduino.h: testeable en host (env:native).
// El tiempo se inyecta como parámetro — RateLimiter nunca llama a millis().
//
// Parámetros fijos (constexpr):
//   kBucketCapacity    = 10 tokens
//   kRefillIntervalMs  = 600 ms → 1 token cada 600 ms
//   kMaxTrackedClients = 4 IPs simultáneas (LRU eviction cuando se llena)
// =============================================================================

#include <stdint.h>
#include <stddef.h>

class RateLimiter {
public:
    static constexpr uint8_t  kBucketCapacity    = 10;
    static constexpr uint32_t kRefillIntervalMs  = 600;
    static constexpr uint8_t  kMaxTrackedClients = 4;

    RateLimiter() = default;

    // Intenta consumir 1 token para el cliente identificado por clientIp.
    // Devuelve true si se permite la request, false si rate-limited (429).
    // nowMs se inyecta — no llama a millis() para ser testeable.
    bool allow(uint32_t clientIp, uint32_t nowMs);

    // Reset completo (útil para tests).
    void reset();

private:
    struct IpBucket {
        uint32_t ip           = 0;  // 0 = slot libre
        uint8_t  tokens       = kBucketCapacity;
        uint32_t lastRefillMs = 0;
        uint32_t lastUseMs    = 0;  // para LRU eviction
    };

    IpBucket buckets_[kMaxTrackedClients];

    // Busca el slot del cliente o asigna uno nuevo (LRU si tabla llena).
    // Inicializa el bucket si es nuevo (capacity tokens + lastRefillMs=nowMs).
    IpBucket& findOrAssign_(uint32_t clientIp, uint32_t nowMs);

    // Aplica refill al bucket según tiempo transcurrido. Clamp a capacity.
    void refill_(IpBucket& b, uint32_t nowMs);
};
