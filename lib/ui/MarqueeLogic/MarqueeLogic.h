#pragma once
// =============================================================================
// MarqueeLogic.h — lógica pura del scroll horizontal (marquee) del mensaje
// del padre. Header-only, SIN dependencias de Arduino_GFX ni de hardware
// (mismo espíritu que BootSplashLogic.h, ver ese archivo para el estilo).
//
// Compilable en env:native para tests unitarios y en env:esp32s3 (incluido
// desde DisplayRendererColor.cpp).
//
// Geometría del scroll (coordenadas de pantalla, eje X):
//
//   phase=0                                  phase=cycle
//   |<-- screenW -->|                        |
//   [ pantalla vacía ][TEXTO............] -> texto sale por la izquierda
//        textX=screenW (fuera, por la der.)     textX=-textPxW (fuera, por la izq.)
//
//   - El texto arranca (offset=0) con su borde izquierdo en x=screenW: fuera
//     de pantalla, a la derecha, a punto de entrar.
//   - Cada frame avanza `offset` px hacia la izquierda (offsetPx, function
//     del tiempo transcurrido — NO del número de frames, para desacoplar del
//     FPS real de render).
//   - Un ciclo completo (cyclePx = textPxW + screenW) es la distancia total
//     que recorre el texto desde "totalmente fuera por la derecha" hasta
//     "totalmente fuera por la izquierda".
//   - passCount cuenta cuántos ciclos completos ya se han recorrido — se usa
//     para limitar el marquee a un número máximo de vueltas (ver
//     DisplayRendererColor::kMessageMaxPasses).
//
// Todas las funciones son puras (sin estado, sin llamadas a millis()) — el
// tiempo entra siempre por parámetro (elapsedMs), igual que BatteryGauge.
// =============================================================================

#include <stdint.h>

namespace MarqueeLogic {

// offsetPx — desplazamiento horizontal acumulado (px) desde que arrancó el
// marquee, dado el tiempo transcurrido y la velocidad de scroll.
//
// offsetPx = elapsedMs * speedPxPerSec / 1000.
//
// uint64_t intermedio: con elapsedMs hasta 60000 (kMessageDurationMs) y
// speedPxPerSec típico 60, el producto máximo (~3.6M) cabe sobrado en
// uint32_t, pero usamos 64 bits en el cálculo intermedio para no depender de
// ese límite si en el futuro cambia la velocidad o la duración máxima.
inline uint32_t offsetPx(uint32_t elapsedMs, uint32_t speedPxPerSec) {
    const uint64_t product = static_cast<uint64_t>(elapsedMs) * speedPxPerSec;
    return static_cast<uint32_t>(product / 1000u);
}

// cyclePx — longitud completa (px) de un ciclo de scroll: la distancia que
// recorre el texto desde que entra por la derecha hasta que sale por la
// izquierda (textPxW + screenW).
inline uint32_t cyclePx(uint16_t textPxW, uint16_t screenW) {
    return static_cast<uint32_t>(textPxW) + static_cast<uint32_t>(screenW);
}

// textX — posición X (px) del borde izquierdo del texto en el frame actual.
//
// textX = screenW - (offset % cycle)
//
// offset=0            -> textX=screenW      (fuera de pantalla, por la der.)
// offset=cycle-1       -> textX≈-textPxW+1  (a punto de salir del todo por la izq.)
// offset=cycle, 2*cycle... -> vuelve a screenW (el módulo reinicia el ciclo).
//
// Guard cycle==0 (texto vacío / geometría inválida): devuelve screenW sin
// dividir por cero — el caller (drawMessageView_) no debería llegar aquí con
// un mensaje vacío (MessageBox::post ya rechaza vacíos), pero la función
// pura no debe asumir eso.
inline int32_t textX(uint32_t offset, uint16_t screenW, uint32_t cycle) {
    if (cycle == 0) return static_cast<int32_t>(screenW);
    const uint32_t phase = offset % cycle;
    return static_cast<int32_t>(screenW) - static_cast<int32_t>(phase);
}

// passCount — número de ciclos completos ya recorridos (offset / cycle).
// Se usa para limitar el marquee a kMessageMaxPasses vueltas antes de
// darlo por terminado (ver DisplayRendererColor::messageActive()).
inline uint32_t passCount(uint32_t offset, uint32_t cycle) {
    if (cycle == 0) return 0;
    return offset / cycle;
}

// firstVisibleChar — índice del primer carácter de la cadena que puede estar
// potencialmente visible en pantalla en el frame actual, para que el render
// no tenga que iterar (ni llamar setCursor) desde el carácter 0 con un
// cursor muy negativo cuando el texto ya lleva rato desplazándose.
//
// NOTA — desviación del plan original: la firma pedida era
// firstVisibleChar(offset, cycle, charW) (3 parámetros). Añadimos screenW
// como parámetro explícito porque es matemáticamente imprescindible: cycle
// (=textPxW+screenW) mezcla ambas magnitudes por SUMA, así que screenW no es
// recuperable a partir de cycle a solas, y sin screenW no hay forma de saber
// dónde cae x=0 en la cadena. Ver derivación completa abajo.
//
// Derivación: un carácter en índice i se dibuja en
//   x_i = textX + i*charW = (screenW - phase) + i*charW
// Dejó de ser "potencialmente visible" (ver drawMessageView_, rango
// x∈[-charW, screenW)) en cuanto x_i < -charW, es decir, es visible mientras
//   x_i >= -charW
//   (screenW - phase) + i*charW >= -charW
//   i >= (phase - screenW - charW) / charW
// Si el numerador es <= 0, ni siquiera el carácter 0 ha empezado a salir por
// la izquierda -> firstVisibleChar=0. Si es > 0, redondeamos hacia arriba
// (ceil) porque cualquier i por debajo de ese cociente todavía cumple
// x_i < -charW (ya invisible).
inline uint32_t firstVisibleChar(uint32_t offset, uint16_t screenW,
                                 uint32_t cycle, uint16_t charW) {
    if (cycle == 0 || charW == 0) return 0;

    const uint32_t phase = offset % cycle;

    // int64_t: phase, screenW y charW son todos pequeños (<= ~65 KB), pero
    // usamos 64 bits para que la resta nunca dependa de wraparound de
    // unsigned aunque el resultado intermedio sea negativo.
    const int64_t numerator = static_cast<int64_t>(phase)
                             - static_cast<int64_t>(screenW)
                             - static_cast<int64_t>(charW);
    if (numerator <= 0) return 0;

    // Ceil division — numerator y charW son ambos > 0 en este punto.
    const int64_t idx = (numerator + charW - 1) / charW;
    return static_cast<uint32_t>(idx);
}

}  // namespace MarqueeLogic
