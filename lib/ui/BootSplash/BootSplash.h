#pragma once
// =============================================================================
// BootSplash.h — Interfaz del splash animado estilo Game Boy DMG.
//
// Incluye las constantes y tipos de BootSplashLogic, y declara playBootSplash()
// que requiere Arduino_GFX. Este header se usa desde main_esp32.cpp.
//
// env:native: NO incluir este header — usar BootSplashLogic.h directamente.
// =============================================================================

#include <Arduino_GFX_Library.h>
#include "BootSplashLogic.h"

// playBootSplash — reproduce la animación de arranque completa.
// Se llama UNA SOLA VEZ desde setup(), tras canvas->begin().
void playBootSplash(Arduino_GFX* canvas);
