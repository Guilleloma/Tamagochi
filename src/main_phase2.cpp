/**
 * main_phase2.cpp — FASE 3: harness del DisplayRendererColor con input por botón.
 *
 * NOVEDADES FASE 3:
 *   - Botón BOOT / GPIO0 del ESP32-S3 Zero, leído por InputHandler (OneButton).
 *   - Short click  → moveCursor(): avanza la selección en el menú de acción del
 *     HUD inferior (Feed → Play → Heal → …). Visual inmediato en pantalla.
 *   - Long press   → triggerEatAnimation() o ejecuta la acción seleccionada:
 *     dispara la animación de comer (EatAnimation one-shot de 625 ms).
 *   - El ciclo automático de escenas del harness se ELIMINA; la navegación
 *     entre IdleMain ↔ StatsView la dirige el botón.
 *   - Stats falso inyectado en setup para que el menú y las barras tengan algo
 *     que mostrar. El EventBus reenvía los eventos del botón al loop.
 *
 * AVISO GPIO0 / STRAPPING:
 *   GPIO0 es pin de strapping del ESP32-S3. Si está a LOW durante el ciclo de
 *   reset o power-on, el chip entra en modo bootloader en lugar de arrancar el
 *   firmware. En runtime (tras el boot) se puede leer sin problema.
 *   NO mantener pulsado BOOT al alimentar la placa ni al pulsar RESET.
 *
 * HARDWARE: mismo cableado que en Fase 2.
 *   SCL/SCK → GPIO12  SDA/MOSI → GPIO11  RES/RST → GPIO10
 *   DC → GPIO9  CS → GPIO8  BL → GPIO7
 *   Botón BOOT → GPIO0 (pull-up interno, active-LOW)
 *
 * COMPILAR: pio run -e esp32s3
 * FLASHEAR: pio run -e esp32s3 -t upload
 */

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "SpriteStore.h"
#include "EventBus.h"
#include "DisplayRendererColor.h"
#include "InputHandler.h"
#include "config.h"

// ─── Panel JD9853 compartido (subclase + secuencia de init) ──────────────────
// Refactorizado a include/jd9853_panel.h para eliminar duplicación con main_esp32.cpp.
#include "jd9853_panel.h"

// ─── Cadencia de frame ────────────────────────────────────────────────────────
static constexpr uint32_t kFrameMs = 125;  // ~8 FPS

// ─── Hardware ─────────────────────────────────────────────────────────────────
static Arduino_DataBus* tftBus = makeTftBus();
static Arduino_GFX*     gfx    = makeJD9853(tftBus);

static Arduino_Canvas* canvas = new Arduino_Canvas(240, 296, gfx);

// ─── Dominio ──────────────────────────────────────────────────────────────────
static SpriteStore store;
static EventBus    bus_evt;

// DisplayRendererColor se construye en heap para evitar init order issues.
static DisplayRendererColor* renderer = nullptr;

// InputHandler se construye en heap después de inicializar bus_evt en setup.
static InputHandler* inputHandler = nullptr;

// ─── Estado del harness ───────────────────────────────────────────────────────
static bool     harnessFailed = false;
static uint32_t lastFrameMs   = 0;

// Escena activa — el botón alterna entre IdleMain y StatsView.
static SceneColor currentScene = SceneColor::IdleMain;

// ─── Stats falso para todo el harness ────────────────────────────────────────
static Stats buildFakeStats() {
    Stats s;
    s.hunger        = 55;
    s.happiness     = 70;
    s.health        = 80;
    s.discipline    = 60;
    s.weight        = 45;
    s.poopCount     = 1;
    s.isSick        = false;
    s.isSleeping    = false;
    s.attentionScore = 600;
    return s;
}

// ─── Menú de acciones falso ───────────────────────────────────────────────────
static void injectFakeActionMenu(DisplayRendererColor* r) {
    StatsEngine eng;
    eng.setStats(buildFakeStats());
    r->refreshActionMenu(eng);
}

// ─── Error fatal en pantalla ──────────────────────────────────────────────────
static void showFatalError(const char* msg) {
    canvas->fillScreen(0x0000);
    canvas->setTextColor(0xF800);
    canvas->setTextSize(1);
    canvas->setCursor(4, 20);
    canvas->print("FATAL ERROR:");
    canvas->setCursor(4, 36);
    canvas->print(msg);
    canvas->flush();
}

// ─── Handlers de eventos de botón ────────────────────────────────────────────
//
// IMPORTANTE: EventBus solo acepta function pointers planos (sin capturas).
// Usamos el puntero global `renderer` — válido porque los handlers se
// registran en setup(), cuando renderer ya está inicializado.

// Short click: avanza el cursor del menú de acción en IdleMain.
// Si estamos en StatsView, vuelve a IdleMain para que el cambio sea visible.
static void onShortClick(const Event&) {
    Serial.println("[BTN] short click → moveCursor()");
    if (currentScene != SceneColor::IdleMain) {
        currentScene = SceneColor::IdleMain;
        renderer->setScene(currentScene);
        Serial.println("[BTN] regresando a IdleMain");
    }
    renderer->moveCursor();
}

// Long press: dispara la animación de comer (visible one-shot 625 ms).
// Si estamos en StatsView, vuelve a IdleMain primero para que se vea la anim.
static void onLongPress(const Event&) {
    Serial.println("[BTN] long press → triggerEatAnimation()");
    if (currentScene != SceneColor::IdleMain) {
        currentScene = SceneColor::IdleMain;
        renderer->setScene(currentScene);
    }
    renderer->triggerEatAnimation();
}

// ─── setup() ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[FASE3] InputHandler + DisplayRendererColor — ESP32-S3 + JD9853 240x296");
    Serial.printf("[MEM] Heap: %u B  PSRAM: %u B\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());
    Serial.printf("[BTN] GPIO%d  activeLow=%s  pullup=%s\n",
                  PIN_BUTTON,
                  BUTTON_ACTIVE_LOW    ? "true" : "false",
                  BUTTON_PULLUP_ACTIVE ? "true" : "false");
    Serial.println("[BTN] AVISO: no mantener pulsado BOOT durante reset/power-on (strapping pin).");
    Serial.println("[BTN] Short click = avanzar cursor de menu. Long press = animacion de comer.");

    // Backlight
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    // ── Init panel ────────────────────────────────────────────────────────────
    if (!gfx->begin(27000000)) {
        Serial.println("[FASE3] ERROR: gfx->begin() fallo");
        harnessFailed = true;
        return;
    }
    Serial.println("[FASE3] gfx->begin() OK");

    // ── Init canvas (framebuffer en PSRAM) ───────────────────────────────────
    if (!canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
        Serial.println("[FASE3] ERROR: canvas->begin() fallo — sin PSRAM");
        harnessFailed = true;
        return;
    }
    Serial.printf("[FASE3] Canvas OK. PSRAM libre: %u B\n", ESP.getFreePsram());

    // Splash mientras cargan sprites
    canvas->fillScreen(0x0010);
    canvas->setTextColor(0xFFFF);
    canvas->setTextSize(2);
    canvas->setCursor(20, 120);
    canvas->print("FASE 3...");
    canvas->flush();

    // ── SpriteStore ──────────────────────────────────────────────────────────
    if (!store.begin("/sprites/manifest.json")) {
        Serial.println("[FASE3] ERROR: SpriteStore.begin() fallo — sube el FS primero");
        showFatalError("SpriteStore failed");
        harnessFailed = true;
        return;
    }

    // ── DisplayRendererColor ─────────────────────────────────────────────────
    renderer = new DisplayRendererColor(*canvas, bus_evt, store);
    renderer->begin();

    Serial.printf("[FASE3] Renderer listo. PSRAM libre post-sprites: %u B\n",
                  ESP.getFreePsram());

    // Estado inicial del renderer
    renderer->setScene(currentScene);
    renderer->setStats(buildFakeStats());
    renderer->setLifeStage(LifeStage::Baby);
    renderer->setAgeMinutes(30);
    renderer->setDeathCount(0);
    renderer->setWifiState(WifiState::Connected);
    injectFakeActionMenu(renderer);

    // ── InputHandler (botón BOOT / GPIO0) ────────────────────────────────────
    // Constructor: InputHandler(EventBus& bus, uint8_t pin, bool activeLow, bool pullupActive)
    inputHandler = new InputHandler(bus_evt, PIN_BUTTON,
                                    BUTTON_ACTIVE_LOW,
                                    BUTTON_PULLUP_ACTIVE);
    inputHandler->begin();

    // Suscribir handlers de botón en el EventBus.
    // onShortClick y onLongPress son function pointers planos (sin captura) — compatibles.
    bus_evt.subscribe(EventType::ButtonShortClick, onShortClick);
    bus_evt.subscribe(EventType::ButtonLongPress,  onLongPress);

    lastFrameMs = millis();

    Serial.println("[FASE3] Listo. Short click = moveCursor, Long press = EatAnimation.");
    Serial.println("[FASE3] Arrancando loop a ~8 FPS. Heartbeat cada 5 s.");
}

// ─── loop() ──────────────────────────────────────────────────────────────────
void loop() {
    if (harnessFailed || renderer == nullptr || inputHandler == nullptr) {
        static uint32_t lastBeat = 0;
        if (millis() - lastBeat >= 3000) {
            lastBeat = millis();
            Serial.println("[LOOP] harness fallido — ver Serial para diagnostico.");
        }
        return;
    }

    // ── Tick de OneButton (debe llamarse en cada iteración del loop) ──────────
    // loop() de InputHandler delega a OneButton::tick(), que detecta gestos y
    // publica los eventos en el EventBus. Los handlers onShortClick/onLongPress
    // se invocan síncronamente desde aquí.
    inputHandler->loop();

    uint32_t now = millis();

    // ── Renderizar frame a cadencia kFrameMs (~8 FPS) ────────────────────────
    if (now - lastFrameMs >= kFrameMs) {
        lastFrameMs = now;
        renderer->render();

        // Heartbeat cada ~5 s
        static uint32_t lastHB = 0;
        if (now - lastHB >= 5000) {
            lastHB = now;
            Serial.printf("[HB] t=%lu ms  scene=%u  heap=%u  psram=%u\n",
                          now,
                          static_cast<uint8_t>(currentScene),
                          ESP.getFreeHeap(),
                          ESP.getFreePsram());
        }
    }
}
