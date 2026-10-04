#pragma once
// =============================================================================
// MoodLogic.h — lógica pura de "qué animación del personaje se ve" en el
// renderer color (ESP32-S3). Header-only, SIN dependencias de Arduino ni de
// Arduino_GFX (mismo espíritu que MarqueeLogic.h / ScreenPowerLogic.h) —
// compilable en env:native para tests unitarios y en env:esp32s3 (incluido
// desde DisplayRendererColor.h).
//
// Responsabilidades:
//   - MoodColor / kMoodCount: eje "mood" de las animaciones de cada etapa.
//   - moodForAction():  acción del menú → mood de la animación one-shot.
//   - fallbackMood() / resolveMood(): cadena de fallback cuando la etapa no
//     tiene frames para el mood pedido (p.ej. solo el baby tiene "jump").
//   - actionAnimDurationMs(): duración de la animación one-shot para que un
//     ciclo completo de la animación quepa entero (el salto no se corta).
//   - oneShotFrameIndex(): frame a pintar según el tiempo transcurrido desde
//     el disparo (arranca siempre en el frame 0, sin deriva por el FPS real).
//
// C++11 (gnu++11 en arduino-esp32 2.x): funciones inline, nada de constexpr
// multi-sentencia ni std::optional.
// =============================================================================

#include <stdint.h>
#include "StatsEngine.h"   // ActionKind — header portable (solo <stdint.h>)

// =============================================================================
// MoodColor — estado emocional visible / animación del personaje.
// El valor numérico es el índice en StageSprites::frames[] y en la tabla
// kSpriteLoad del renderer: NO reordenar sin actualizar ambos.
// =============================================================================
enum class MoodColor : uint8_t {
    Idle  = 0,
    Happy = 1,
    Sad   = 2,
    Eat   = 3,
    Sleep = 4,  // dormido — lo fuerza computeMood_() con isSleeping
    Jump  = 5   // salto one-shot de Play (solo etapas con animación "jump")
};

// Número de moods posibles (tamaño del eje mood en StageSprites).
static constexpr uint8_t kMoodCount = 6;

namespace MoodLogic {

// moodForAction — mood de la animación one-shot que dispara cada acción.
//
//   Feed       → Eat
//   Play       → Jump   (cae a Happy en etapas sin "jump", ver resolveMood)
//   Medicate   → Happy
//   Discipline → Sad
//   Clean      → Idle   (limpieza neutra; el badge de caca ya desapareció)
//   Lights     → ninguno (el sueño es un estado permanente, no un one-shot)
//   Stats      → ninguno (no es una acción sobre el pet)
//
// Devuelve true y escribe outMood si la acción tiene animación one-shot;
// devuelve false (outMood sin tocar) si no la tiene.
inline bool moodForAction(ActionKind action, MoodColor& outMood) {
    switch (action) {
        case ActionKind::Feed:       outMood = MoodColor::Eat;   return true;
        case ActionKind::Play:       outMood = MoodColor::Jump;  return true;
        case ActionKind::Medicate:   outMood = MoodColor::Happy; return true;
        case ActionKind::Discipline: outMood = MoodColor::Sad;   return true;
        case ActionKind::Clean:      outMood = MoodColor::Idle;  return true;
        case ActionKind::Lights:
        case ActionKind::Stats:
            return false;
    }
    return false;
}

// fallbackMood — siguiente eslabón cuando la etapa no tiene frames para
// `mood`. Jump cae a Happy (el salto es una forma de alegría); todo lo demás
// cae a Idle, que es el ancla final (Idle → Idle).
inline MoodColor fallbackMood(MoodColor mood) {
    return (mood == MoodColor::Jump) ? MoodColor::Happy : MoodColor::Idle;
}

// resolveMood — mood efectivo a pintar dada la disponibilidad de frames de
// la etapa (hasFrames[i] == true si el mood i tiene count > 0).
//
// Recorre la cadena requested → fallbackMood → ... hasta encontrar un mood
// con frames: Jump → Happy → Idle, Sleep → Idle, Eat → Idle, etc.
// Si ni siquiera Idle tiene frames devuelve Idle igualmente (el caller
// decide cómo pintar "sin sprite"). Un `requested` fuera de rango se trata
// como un mood sin frames y acaba en Idle.
inline MoodColor resolveMood(MoodColor requested,
                             const bool (&hasFrames)[kMoodCount]) {
    MoodColor mood = requested;
    // Cota de saltos: la cadena más larga (Jump→Happy→Idle) tiene 3
    // eslabones; kMoodCount es una cota superior segura ante ciclos.
    for (uint8_t hop = 0; hop < kMoodCount; ++hop) {
        const uint8_t idx = static_cast<uint8_t>(mood);
        if (idx < kMoodCount && hasFrames[idx]) return mood;
        if (mood == MoodColor::Idle) break;
        mood = fallbackMood(mood);
    }
    return MoodColor::Idle;
}

// frameDurationMs — ms que dura cada frame de una animación con `fps`
// declarados en el manifest, ralentizada por `slowdown`
// (SPRITE_ANIM_SLOWDOWN). Misma fórmula que DisplayRendererColor::render()
// usa para avanzar animFrame_: (1000 / fps) * slowdown.
// fps == 0 (manifest corrupto) → 0: el caller decide el fallback.
inline uint32_t frameDurationMs(uint8_t fps, uint8_t slowdown) {
    if (fps == 0) return 0;
    return (1000u / fps) * slowdown;
}

// actionAnimDurationMs — duración de la animación one-shot de una acción:
//
//   max(minMs, frameCount * (1000 / fps) * slowdown)
//
// Garantiza que se vea un ciclo completo de la animación (p.ej. el salto de
// 8 frames a 8 fps ×2 = 2000 ms) sin bajar nunca de minMs (las animaciones
// cortas siguen durando lo de siempre). fps == 0 → minMs.
inline uint32_t actionAnimDurationMs(uint8_t frameCount, uint8_t fps,
                                     uint8_t slowdown, uint32_t minMs) {
    const uint32_t cycleMs = static_cast<uint32_t>(frameCount)
                           * frameDurationMs(fps, slowdown);
    return (cycleMs > minMs) ? cycleMs : minMs;
}

// oneShotFrameIndex — frame a pintar de una animación one-shot tras
// `elapsedMs` desde su disparo.
//
//   (elapsedMs / frameDurationMs) % frameCount
//
// Se calcula por TIEMPO desde el disparo (no con el contador global
// animFrame_) para que el one-shot arranque siempre en el frame 0 y no
// acumule deriva: el contador global avanza en el primer render que supera
// el periodo de frame, así que con render a ~66 ms cada frame de 250 ms dura
// en realidad ~264 ms y el último frame del salto se cortaba o no llegaba a
// verse dentro de actionAnimDurationMs. Con el índice por tiempo, el frame k
// se ve siempre entre k*frameMs y (k+1)*frameMs.
//
// El módulo conserva el comportamiento previo de las animaciones cortas que
// duran minMs (vuelven a empezar en vez de congelarse en el último frame).
// frameCount == 0 o fps == 0 → 0.
inline uint8_t oneShotFrameIndex(uint32_t elapsedMs, uint8_t frameCount,
                                 uint8_t fps, uint8_t slowdown) {
    const uint32_t frameMs = frameDurationMs(fps, slowdown);
    if (frameCount == 0 || frameMs == 0) return 0;
    return static_cast<uint8_t>((elapsedMs / frameMs) % frameCount);
}

}  // namespace MoodLogic
