#pragma once
// =============================================================================
// BootLoopGuardLogic.h — lógica pura de decisión del guarda anti-boot-loop
// (contador + comparación contra el tope de reinicios).
//
// SIN dependencias de Arduino.h, RTC_NOINIT_ATTR ni esp_reset_reason() —
// mismo espíritu que BootSplashLogic.h/MarqueeLogic.h: compilable en
// env:native para tests unitarios y en env:esp32s3.
//
// La persistencia entre reinicios (RTC_NOINIT_ATTR) y la detección de
// cold-boot vs soft-reset (esp_reset_reason()) son responsabilidad exclusiva
// del wrapper ESP32 — ver lib/infra/BootLoopGuard/BootLoopGuard.cpp. Ese
// wrapper delega toda la decisión aquí para no duplicarla: construye/():
// resincroniza esta clase con el contador leído de RTC_NOINIT_ATTR en cada
// begin(), y escribe de vuelta count() tras cada mutación.
// =============================================================================

#include <stdint.h>

class BootLoopGuardLogic {
public:
    BootLoopGuardLogic() = default;

    // maxRestarts : tope de reinicios permitidos antes de modo degradado
    //               (equivalente a BOOT_LOOP_MAX en config.h).
    // initialCount: contador de partida (0 en cold-boot; valor preservado
    //               de RTC_NOINIT_ATTR en soft-reset — decidido por el wrapper).
    explicit BootLoopGuardLogic(uint32_t maxRestarts, uint32_t initialCount = 0);

    // reset — (re)configura maxRestarts_ y count_. Lo usa el wrapper en
    // begin() para sincronizar esta instancia con el estado recién resuelto
    // de RTC_NOINIT_ATTR, sin reconstruir el objeto (esta instancia vive en
    // RAM normal, que SÍ se reinicializa en cada boot, a diferencia de
    // RTC_NOINIT_ATTR que sobrevive al soft-reset).
    void reset(uint32_t maxRestarts, uint32_t initialCount);

    // true  = aún no se alcanzó maxRestarts_ -- incrementa count_ y permite
    //         proceder con el reinicio.
    // false = modo degradado: count_ NO cambia (llamadas repetidas tras
    //         agotar el tope son un no-op seguro).
    bool allowRestart();

    // Restaura la confianza: resetea count_ a 0. Idempotente.
    void reportStableConnection();

    uint32_t count()       const { return count_; }
    uint32_t maxRestarts() const { return maxRestarts_; }

private:
    uint32_t maxRestarts_ = 0;
    uint32_t count_       = 0;
};
