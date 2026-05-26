# Plan de Sprites — Tamagochi

Diseñado por agente designer (frontend-expert) en 2026-05-26. Output completo + ASCII de sprites en la conversación de planificación.

## Constraints

- **Display:** OLED 128x64 monocromo (SSD1306, SW I2C)
- **FPS:** 5 estables (200 ms/frame) — confirmado en Fase 0. Es mejor que el Tamagotchi original (~4 FPS).
- **Sprite principal:** 32x32 px = 128 bytes/frame PROGMEM
- **Overlays:** 16x16 px = 32 bytes/frame PROGMEM
- **Formato:** XBM, LSB first, blanco=1, negro=0

## Layout del OLED

```
┌────────────────────────────┐
│ HEADER 8px (stats iconos)  │
├────────────────────────────┤
│                            │
│   stats texto a la izq     │
│   [sprite 32x32 en x=80]   │
│                            │
├────────────────────────────┤
│ FOOTER overlays            │
└────────────────────────────┘
```

## Pipeline de assets

```
assets/sprites/*.png  (Piskel o Aseprite, 32x32 o 16x16 monocromo)
        ↓
python3 tools/png_to_xbm.py assets/sprites/ include/sprites_generated.h
        ↓
include/sprites_generated.h  (arrays PROGMEM con prefijo kSprite*)
        ↓
lib/ui/DisplayRenderer  (drawXBMP)
```

Convención de nombres:
- `baby_idle_0.png` → `kSpriteBabyIdle0`
- `overlay_sick.png` → `kSpriteOverlaySick`
- `action_food_meal.png` → `kSpriteActionFoodMeal`

## Inventario completo

### Fase 1 (etapa BABY)

| Sprite | Tamaño | Frames | ms/frame | Animación |
|---|---|---|---|---|
| `BabyIdle` | 32x32 | 2 | 400 | Loop — parpadeo |
| `BabyHappy` | 32x32 | 3 | 200 | Loop — salto |
| `BabySad` | 32x32 | 2 | 600 | Loop — gota llanto |
| `BabyEat` | 32x32 | 3 | 200 | One-shot → IDLE |
| `OverlaySleepZ` | 16x16 | 2 | 400 | Loop — z's flotando |
| `OverlaySick` | 16x16 | 1 | — | Estático |
| `OverlayPoop` | 16x16 | 1 | — | Estático |
| `OverlayHungry` | 16x16 | 2 | 300 | Loop — parpadeo on/off |

**Subtotal:** 13 frames 32x32 + 6 frames 16x16 = **1856 bytes PROGMEM**

### Fase 2 (etapas EGG, KID, ADULT, OLD, DEAD + acciones)

| Categoría | Sprites |
|---|---|
| EGG | `EggIdle` (2 frames) + `EggHatch` (4 frames one-shot) |
| KID | `KidIdle`, `KidHappy`, `KidSad` |
| ADULT | `AdultIdle`, `AdultHappy` |
| OLD | `OldIdle` (animación más lenta, 600ms) |
| DEAD | `DeadGhost` (2 frames, flotación vertical) |
| Acciones 16x16 | `ActionFoodMeal`, `ActionFoodSnack`, `ActionHeart` (2f), `ActionStar`, `ActionMedicine`, `ActionBroom` (2f) |

**Subtotal Fase 2:** ~3136 bytes

### Total Fase 1 + Fase 2: ~5 KB Flash

## Diferenciación visual por etapa

A 32x32 monocromo, el rasgo diferenciador clave es la **silueta** + 1 rasgo único:

| Etapa | Silueta | Rasgo único |
|---|---|---|
| EGG | Oval puro, sin patas | Sin cara |
| BABY | Cuerpo casi circular | Patas mínimas 2px stub |
| KID | Oval vertical más alto | Antenas 2px en cabeza |
| ADULT | Más ancho, definido | Cuernos/orejas laterales 3px |
| OLD | Bounding box bajo y ancho | Encorvado + arrugas |
| DEAD | Silueta fantasma ondulada | Sin patas, sin contorno duro |

## Composición con overlays (painter's algorithm)

```
1. clearBuffer()
2. drawXBMP(80, 20, 32, 32, spriteBase)
3. if poop:     drawXBMP(48, 28, 16, 16, kSpriteOverlayPoop)
4. if sick:     drawXBMP(80, 14, 16, 16, kSpriteOverlaySick)
5. if sleeping: drawXBMP(94, 14, 16, 16, kSpriteOverlaySleepZ[anim])
6. if hungry:   drawXBMP(64, 10, 16, 16, kSpriteOverlayHungry[anim])
7. drawStats(...)
8. sendBuffer()
```

Conflicto SICK+SLEEPING: ambos coexisten; SICK tiene prioridad visual.
Para overlay aditivo (sin borrar fondo): `u8g2.setDrawColor(1)` antes del `drawXBMP`.

## Timing — drift-free

```cpp
if (millis() - lastRenderMs >= RENDER_INTERVAL_MS) {
    lastRenderMs += RENDER_INTERVAL_MS;  // suma, no reasigna — evita acumular jitter
    renderFrame();
}
```

Las animaciones se indexan por **conteo de ticks**, no por `millis()` directo:
- `frame = (tickCount / framesPerTickInterval) % numFrames`

## ASCII de referencia — BABY IDLE Frame 0

```
································
································
································
································
·········████████████···········
·······██············██·········
······█···············█·········
·····█·················█········
·····█··███···███······█········
·····█··███···███······█········
·····█·················█········
···██·█·····█··········█·██·····
···██·█····███·········█·██·····
·····█·····█···········█········
·····█·················█········
······█···············█·········
·······██···········██··········
·········████████████···········
································
·········████·····████··········
········█····█···█····█·········
········█····█···█····█·········
·········████·····████··········
································
································
································
································
································
································
································
································
```

(ASCII completo de cada sprite en el output del designer — recuperable de la conversación cuando se diseñen los PNGs reales.)

## Próximos pasos

1. Diseñar PNGs en Piskel/Aseprite usando los diseños ASCII como referencia
2. Guardar en `assets/sprites/`
3. Ejecutar `python3 tools/png_to_xbm.py assets/sprites/ include/sprites_generated.h`
4. Incluir el header en `lib/ui/DisplayRenderer/DisplayRenderer.cpp`
