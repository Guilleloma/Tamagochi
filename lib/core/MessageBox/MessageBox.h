#pragma once
#include <stddef.h>
#include <stdint.h>
#include <atomic>

// =============================================================================
// MessageBox — buffer SPSC para el mensaje de texto del padre (marquee).
//
// Puro — SIN Arduino.h. Compilable en host-native (pio test -e native), igual
// que EventBus/BatteryGauge.
//
// Problema que resuelve: EventBus solo transporta int32_t (ver EventBus.h), así
// que el texto del mensaje ("Es hora de ir a comer") no puede viajar por el
// bus. MessageBox es el canal dedicado: el handler HTTP POST /api/v1/message
// (task async_tcp) escribe con post(); loopTask lee con consume().
//
// ── Por qué NO hace falta mutex ──────────────────────────────────────────────
// Es un canal SPSC (Single Producer / Single Consumer):
//   - Productor: SIEMPRE el handler de WebUI, que corre en la única task
//     async_tcp de AsyncWebServer — todas las requests HTTP se serializan en
//     esa misma task, así que nunca hay dos llamadas a post() concurrentes.
//   - Consumidor: SIEMPRE loopTask, una vez por iteración de loop().
//
// La sincronización necesaria (visibilidad de buf_ antes de que pending_ se
// vea true) se resuelve con el par release/acquire:
//   - post()    escribe buf_ y PUBLICA con pending_.store(release).
//   - consume() ADQUIERE con pending_.exchange(acquire) antes de leer buf_.
// Esto garantiza que si consume() ve pending_==true, ve también el buf_ que
// ese post() escribió (happens-before), sin necesidad de un mutex.
//
// Peor caso de carrera (aceptado por diseño, no un bug): un post() nuevo
// empieza a sobrescribir buf_ mientras consume() todavía está copiando el
// buf_ del post() anterior. Esto solo puede ocurrir si ambas tasks ejecutan
// en núcleos distintos exactamente en la ventana de unos pocos ciclos de un
// memcpy de <=96 bytes — con el rate limiter de WebUI (máx ~1 request cada
// 600ms) y el loop a 5-10 FPS, la probabilidad real es despreciable. Si aun
// así ocurre, el peor resultado es un frame de texto parcialmente mezclado
// (nunca un crash ni una lectura fuera de rango, porque buf_ es un array de
// tamaño fijo siempre null-terminated) — y el escritor ya ha vuelto a marcar
// pending_=true, así que la siguiente iteración de loop() vuelve a consumir
// un buf_ limpio y completo. No se necesita mutex para un canal de baja
// frecuencia con esta tolerancia a fallos.
// =============================================================================

namespace TextSanitizer {

// utf8ToAscii — convierte texto UTF-8 (`in`) a ASCII imprimible (32..126) en
// `out` (buffer de `outSize` bytes, incluyendo el '\0' final).
//
// Reglas de conversión:
//   - ASCII imprimible (32..126): se copia tal cual.
//   - Controles (0x00-0x1F, 0x7F): se sustituyen por espacio.
//   - Latin-1 Supplement con transliteración conocida (á->a, é->e, í->i,
//     ó->o, ú->u, ñ->n, ü->u, ç->c y sus mayúsculas): se transliteran.
//   - ¿ (U+00BF) / ¡ (U+00A1): se descartan (no producen ningún char de salida).
//   - Cualquier otra secuencia multibyte (2, 3 o 4 bytes) sin mapeo conocido,
//     incluidos emoji: se descarta por completo (0 chars de salida).
//   - Bytes de continuación sueltos o cabeceras UTF-8 inválidas: se descartan
//     byte a byte (nunca crashea con UTF-8 truncado/corrupto).
//   - Al final: colapsa espacios consecutivos a uno y recorta (trim) los
//     espacios al inicio/fin del resultado.
//
// Devuelve el número de chars escritos en `out` (sin contar el '\0' final).
// `out` siempre queda null-terminated si outSize > 0.
size_t utf8ToAscii(const char* in, char* out, size_t outSize);

}  // namespace TextSanitizer

class MessageBox {
public:
    // kMaxLen: longitud máxima del mensaje YA sanitizado (ASCII).
    static constexpr size_t kMaxLen = 96;

    // kMaxRawLen: longitud máxima del texto UTF-8 crudo aceptado desde el
    // cliente, ANTES de sanitizar (bytes). Un texto crudo más largo se
    // rechaza directamente sin gastar tiempo en sanitizarlo.
    static constexpr size_t kMaxRawLen = 192;

    enum class PostResult : uint8_t {
        Ok,       // aceptado — pending_ marcado, listo para consume()
        Empty,    // el texto sanitizado quedó vacío (ej. "", "   ", solo emoji)
        TooLong   // raw > kMaxRawLen bytes, o sanitizado > kMaxLen chars
    };

    // post — productor (task async_tcp). Sanitiza utf8Text (UTF-8 -> ASCII),
    // valida longitud y, si es válido, publica el mensaje (last-wins: un post
    // exitoso siempre sobrescribe cualquier mensaje pendiente sin consumir).
    // Única fuente de verdad para la validación de longitud/vacío — el
    // handler HTTP delega aquí en vez de reimplementar las reglas.
    PostResult post(const char* utf8Text);

    // hasPending — lectura barata (sin efectos secundarios) de si hay un
    // mensaje esperando a ser consumido. Para el poll no bloqueante de loop().
    bool hasPending() const;

    // consume — consumidor (loopTask). Si no había mensaje pendiente, no
    // toca `out` y devuelve false. Si había, lo copia a `out` (respetando
    // outSize, null-terminated) y limpia pending_ atómicamente.
    bool consume(char* out, size_t outSize);

private:
    char buf_[kMaxLen + 1] = {};
    std::atomic<bool> pending_{false};
};
