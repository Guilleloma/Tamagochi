// =============================================================================
// test_mood_logic.cpp — tests unitarios para MoodLogic (mapeo acción→mood,
// cadena de fallback de moods y duración/frames de la animación one-shot).
//
// Framework: Unity (pio test -e native).
// MoodLogic es header-only y puro (sin Arduino.h, sin millis()) — se ejercita
// directamente; la disponibilidad de frames de cada etapa se modela con
// fixtures (StageFrames) que replican la tabla kSpriteLoad del renderer.
// =============================================================================

#include <unity.h>
#include "MoodLogic.h"

void setUp()    {}
void tearDown() {}

// =============================================================================
// Fixtures — qué moods tienen frames en cada etapa (ver kSpriteLoad en
// DisplayRendererColor.cpp). Índice = valor numérico de MoodColor.
// =============================================================================
struct StageFrames {
    bool has[kMoodCount];
};

// Baby: idle, happy, sad, eat, sleep y jump.
static StageFrames babyStage() {
    return StageFrames{{true, true, true, true, true, true}};
}

// Kid / Adult / Old: las 5 animaciones clásicas, sin jump.
static StageFrames stageWithoutJump() {
    return StageFrames{{true, true, true, true, true, false}};
}

// Egg / Ghost: solo idle.
static StageFrames idleOnlyStage() {
    return StageFrames{{true, false, false, false, false, false}};
}

// LittleFS sin flashear: ninguna animación cargada.
static StageFrames emptyStage() {
    return StageFrames{{false, false, false, false, false, false}};
}

// Constantes del firmware (DisplayRendererColor / config.h) usadas en los
// tests de duración — mismos valores que en producción.
static const uint8_t  kManifestFps   = 8;     // fps declarados en el manifest
static const uint8_t  kSlowdown      = 2;     // SPRITE_ANIM_SLOWDOWN
static const uint32_t kMinDurationMs = 1200;  // kActionAnimDurationMs

// Helper: mood devuelto por moodForAction() (falla el test si no hay mood).
static MoodColor expectMoodForAction(ActionKind action) {
    MoodColor mood = MoodColor::Idle;
    TEST_ASSERT_TRUE(MoodLogic::moodForAction(action, mood));
    return mood;
}

// =============================================================================
// moodForAction — mapeo de cada acción del menú
// =============================================================================
void should_map_feed_to_eat() {
    TEST_ASSERT_TRUE(expectMoodForAction(ActionKind::Feed) == MoodColor::Eat);
}

void should_map_play_to_jump() {
    TEST_ASSERT_TRUE(expectMoodForAction(ActionKind::Play) == MoodColor::Jump);
}

void should_map_medicate_to_happy() {
    TEST_ASSERT_TRUE(expectMoodForAction(ActionKind::Medicate) == MoodColor::Happy);
}

void should_map_discipline_to_sad() {
    TEST_ASSERT_TRUE(expectMoodForAction(ActionKind::Discipline) == MoodColor::Sad);
}

void should_map_clean_to_idle() {
    TEST_ASSERT_TRUE(expectMoodForAction(ActionKind::Clean) == MoodColor::Idle);
}

// Lights y Stats no disparan one-shot: devuelven false y no tocan outMood.
void should_return_no_mood_for_lights_and_stats() {
    MoodColor mood = MoodColor::Sad;  // centinela: no debe cambiar
    TEST_ASSERT_FALSE(MoodLogic::moodForAction(ActionKind::Lights, mood));
    TEST_ASSERT_TRUE(mood == MoodColor::Sad);
    TEST_ASSERT_FALSE(MoodLogic::moodForAction(ActionKind::Stats, mood));
    TEST_ASSERT_TRUE(mood == MoodColor::Sad);
}

// =============================================================================
// fallbackMood — siguiente eslabón de la cadena
// =============================================================================
void should_fall_back_from_jump_to_happy() {
    TEST_ASSERT_TRUE(MoodLogic::fallbackMood(MoodColor::Jump) == MoodColor::Happy);
}

void should_fall_back_from_every_other_mood_to_idle() {
    TEST_ASSERT_TRUE(MoodLogic::fallbackMood(MoodColor::Happy) == MoodColor::Idle);
    TEST_ASSERT_TRUE(MoodLogic::fallbackMood(MoodColor::Sad)   == MoodColor::Idle);
    TEST_ASSERT_TRUE(MoodLogic::fallbackMood(MoodColor::Eat)   == MoodColor::Idle);
    TEST_ASSERT_TRUE(MoodLogic::fallbackMood(MoodColor::Sleep) == MoodColor::Idle);
    TEST_ASSERT_TRUE(MoodLogic::fallbackMood(MoodColor::Idle)  == MoodColor::Idle);
}

// =============================================================================
// resolveMood — mood efectivo según los frames disponibles de la etapa
// =============================================================================

// Kid / Adult / Old no tienen "jump": Play debe verse como happy.
void should_resolve_jump_to_happy_for_kid_adult_and_old() {
    const StageFrames stage = stageWithoutJump();
    TEST_ASSERT_TRUE(MoodLogic::resolveMood(MoodColor::Jump, stage.has) == MoodColor::Happy);
}

// Egg / Ghost solo tienen idle: Jump recorre la cadena completa hasta Idle.
void should_resolve_jump_to_idle_for_egg_and_ghost() {
    const StageFrames stage = idleOnlyStage();
    TEST_ASSERT_TRUE(MoodLogic::resolveMood(MoodColor::Jump, stage.has) == MoodColor::Idle);
}

void should_keep_requested_mood_when_stage_has_its_frames() {
    const StageFrames baby = babyStage();
    TEST_ASSERT_TRUE(MoodLogic::resolveMood(MoodColor::Jump,  baby.has) == MoodColor::Jump);
    TEST_ASSERT_TRUE(MoodLogic::resolveMood(MoodColor::Happy, baby.has) == MoodColor::Happy);
    TEST_ASSERT_TRUE(MoodLogic::resolveMood(MoodColor::Eat,   baby.has) == MoodColor::Eat);
    TEST_ASSERT_TRUE(MoodLogic::resolveMood(MoodColor::Sleep, baby.has) == MoodColor::Sleep);
}

// Regresión: antes de MoodLogic, framesForMood_() caía a Idle si el mood no
// tenía frames. Sleep (y el resto de moods clásicos) deben seguir igual.
void should_resolve_sleep_to_idle_when_stage_has_no_sleep_frames() {
    const StageFrames egg = idleOnlyStage();
    TEST_ASSERT_TRUE(MoodLogic::resolveMood(MoodColor::Sleep, egg.has) == MoodColor::Idle);
    TEST_ASSERT_TRUE(MoodLogic::resolveMood(MoodColor::Eat,   egg.has) == MoodColor::Idle);
    TEST_ASSERT_TRUE(MoodLogic::resolveMood(MoodColor::Sad,   egg.has) == MoodColor::Idle);
}

void should_resolve_to_idle_when_stage_has_no_frames_at_all() {
    const StageFrames empty = emptyStage();
    TEST_ASSERT_TRUE(MoodLogic::resolveMood(MoodColor::Jump, empty.has) == MoodColor::Idle);
    TEST_ASSERT_TRUE(MoodLogic::resolveMood(MoodColor::Idle, empty.has) == MoodColor::Idle);
}

void should_resolve_out_of_range_mood_to_idle() {
    const StageFrames baby = babyStage();
    const MoodColor bogus = static_cast<MoodColor>(42);
    TEST_ASSERT_TRUE(MoodLogic::resolveMood(bogus, baby.has) == MoodColor::Idle);
}

// =============================================================================
// actionAnimDurationMs — duración del one-shot
// =============================================================================

// Salto del baby: 8 frames a 8 fps ×2 = 2000 ms (> mínimo de 1200 ms).
void should_last_full_jump_cycle_for_8_frames_at_8fps_slowdown_2() {
    TEST_ASSERT_EQUAL_UINT32(2000, MoodLogic::actionAnimDurationMs(
        8, kManifestFps, kSlowdown, kMinDurationMs));
}

// happy/sad: 4 frames = 1000 ms < mínimo → se mantiene el mínimo de siempre.
void should_keep_min_duration_for_4_frame_animation() {
    TEST_ASSERT_EQUAL_UINT32(1200, MoodLogic::actionAnimDurationMs(
        4, kManifestFps, kSlowdown, kMinDurationMs));
}

// eat: 5 frames = 1250 ms → supera ligeramente el mínimo.
void should_extend_duration_to_1250ms_for_5_frame_animation() {
    TEST_ASSERT_EQUAL_UINT32(1250, MoodLogic::actionAnimDurationMs(
        5, kManifestFps, kSlowdown, kMinDurationMs));
}

// fps=0 (manifest corrupto) no debe dividir por cero.
void should_return_min_duration_when_fps_is_zero() {
    TEST_ASSERT_EQUAL_UINT32(kMinDurationMs, MoodLogic::actionAnimDurationMs(
        8, 0, kSlowdown, kMinDurationMs));
}

void should_return_min_duration_when_frame_count_is_zero() {
    TEST_ASSERT_EQUAL_UINT32(kMinDurationMs, MoodLogic::actionAnimDurationMs(
        0, kManifestFps, kSlowdown, kMinDurationMs));
}

// =============================================================================
// oneShotFrameIndex — frame del one-shot por tiempo desde el disparo
// =============================================================================

// El salto arranca desde el suelo: frame 0 durante el primer periodo (250 ms).
void should_start_one_shot_at_frame_zero() {
    TEST_ASSERT_EQUAL_UINT8(0, MoodLogic::oneShotFrameIndex(0,   8, kManifestFps, kSlowdown));
    TEST_ASSERT_EQUAL_UINT8(0, MoodLogic::oneShotFrameIndex(249, 8, kManifestFps, kSlowdown));
}

// Cada 250 ms avanza un frame; el último (aterrizaje) se ve entero hasta
// el fin de la duración calculada (2000 ms).
void should_advance_one_shot_frame_every_250ms_and_show_last_frame() {
    TEST_ASSERT_EQUAL_UINT8(1, MoodLogic::oneShotFrameIndex(250,  8, kManifestFps, kSlowdown));
    TEST_ASSERT_EQUAL_UINT8(7, MoodLogic::oneShotFrameIndex(1750, 8, kManifestFps, kSlowdown));
    TEST_ASSERT_EQUAL_UINT8(7, MoodLogic::oneShotFrameIndex(1999, 8, kManifestFps, kSlowdown));
}

// Animaciones cortas que duran el mínimo (1200 ms) vuelven a empezar, igual
// que antes del cambio: 4 frames × 250 ms = 1000 ms → frame 0 de nuevo.
void should_wrap_one_shot_frame_when_duration_exceeds_cycle() {
    TEST_ASSERT_EQUAL_UINT8(3, MoodLogic::oneShotFrameIndex(999,  4, kManifestFps, kSlowdown));
    TEST_ASSERT_EQUAL_UINT8(0, MoodLogic::oneShotFrameIndex(1000, 4, kManifestFps, kSlowdown));
}

void should_return_frame_zero_when_fps_or_frame_count_is_zero() {
    TEST_ASSERT_EQUAL_UINT8(0, MoodLogic::oneShotFrameIndex(1000, 8, 0, kSlowdown));
    TEST_ASSERT_EQUAL_UINT8(0, MoodLogic::oneShotFrameIndex(1000, 0, kManifestFps, kSlowdown));
}

// =============================================================================
// main — punto de entrada Unity
// =============================================================================
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(should_map_feed_to_eat);
    RUN_TEST(should_map_play_to_jump);
    RUN_TEST(should_map_medicate_to_happy);
    RUN_TEST(should_map_discipline_to_sad);
    RUN_TEST(should_map_clean_to_idle);
    RUN_TEST(should_return_no_mood_for_lights_and_stats);

    RUN_TEST(should_fall_back_from_jump_to_happy);
    RUN_TEST(should_fall_back_from_every_other_mood_to_idle);

    RUN_TEST(should_resolve_jump_to_happy_for_kid_adult_and_old);
    RUN_TEST(should_resolve_jump_to_idle_for_egg_and_ghost);
    RUN_TEST(should_keep_requested_mood_when_stage_has_its_frames);
    RUN_TEST(should_resolve_sleep_to_idle_when_stage_has_no_sleep_frames);
    RUN_TEST(should_resolve_to_idle_when_stage_has_no_frames_at_all);
    RUN_TEST(should_resolve_out_of_range_mood_to_idle);

    RUN_TEST(should_last_full_jump_cycle_for_8_frames_at_8fps_slowdown_2);
    RUN_TEST(should_keep_min_duration_for_4_frame_animation);
    RUN_TEST(should_extend_duration_to_1250ms_for_5_frame_animation);
    RUN_TEST(should_return_min_duration_when_fps_is_zero);
    RUN_TEST(should_return_min_duration_when_frame_count_is_zero);

    RUN_TEST(should_start_one_shot_at_frame_zero);
    RUN_TEST(should_advance_one_shot_frame_every_250ms_and_show_last_frame);
    RUN_TEST(should_wrap_one_shot_frame_when_duration_exceeds_cycle);
    RUN_TEST(should_return_frame_zero_when_fps_or_frame_count_is_zero);

    return UNITY_END();
}
