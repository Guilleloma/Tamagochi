/**
 * SpriteStore.h — Loader de sprites desde LittleFS a PSRAM (ESP32-S3)
 *
 * Responsabilidad única: parsear el manifest de Pixel Sprites y cargar los
 * buffers de frames en PSRAM para que el sketch los renderice directamente.
 *
 * --- Layout de ficheros en LittleFS ---
 *
 *   /sprites/manifest.json
 *   /sprites/<spriteId>/walk/0.bin
 *   /sprites/<spriteId>/walk/1.bin
 *   ...
 *
 * El manifest usa el campo "id" de cada sprite como nombre de carpeta.
 * Los paths en "frames" son relativos a esa carpeta de sprite.
 * Ejemplo: frame "walk/0.bin" de sprite "0e273289-..." → /sprites/0e273289-.../walk/0.bin
 *
 * --- Por qué PSRAM ---
 *
 * Un frame 48×48 RGB565 = 4 608 bytes.
 * 8 frames walk = 36 864 bytes ≈ 36 KB.
 * La SRAM interna del ESP32-S3 (~320 KB usable) se agota rápido con
 * varios animaciones cargadas. PSRAM (2 MB libre en Waveshare S3 Zero)
 * es el destino natural. ps_malloc() asigna ahí directamente.
 *
 * --- Límite de nombre LittleFS vs tool-mklittlefs ---
 *
 * CONFIG_LITTLEFS_OBJ_NAME_LEN = 64 en el firmware ESP32-S3 (sdkconfig IDF 4.4.x).
 * PERO tool-mklittlefs v0.2.3 (el que PlatformIO instala para esp32) está compilado
 * con LFS_NAME_MAX=31 — herencia del ESP8266. Esto significa que al empaquetar la
 * imagen con `pio -t uploadfs`, cualquier componente de ruta > 31 chars falla
 * silenciosamente (el archivo no se añade a la imagen).
 *
 * El UUID "0e273289-be9b-4048-a2c3-acb89579dcdc" tiene 36 chars → excede el límite
 * del tool. Solución: la carpeta del sprite en LittleFS se llama "wiz" (≤31 chars)
 * y el manifest incluye un campo "folder" con ese nombre corto. El campo "id" conserva
 * el UUID completo para identificación; "folder" es el directorio real en LittleFS.
 *
 * Si en el futuro se actualiza tool-mklittlefs a una versión con LFS_NAME_MAX=64,
 * el campo "folder" puede apuntar al UUID directamente.
 *
 * --- Uso desde el sketch ---
 *
 *   #include "SpriteStore.h"
 *   SpriteStore store;
 *   store.begin("/sprites/manifest.json");
 *   ColorSpriteFrames walk;
 *   store.loadAnimation("0e273289-be9b-4048-a2c3-acb89579dcdc", "walk", walk);
 *   // usar walk.frames[i], walk.w, walk.h, walk.fps, walk.count
 *   store.freeAnimation(walk);
 */

#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Máximo número de frames soportados por animación.
// El manifest puede tener hasta 16 frames; ampliar aquí si fuera necesario.
static constexpr uint8_t SPRITE_STORE_MAX_FRAMES = 16;

/**
 * Conjunto de frames cargados en PSRAM para una animación concreta.
 * Todos los punteros válidos están en [0, count).
 */
struct ColorSpriteFrames {
    uint16_t* frames[SPRITE_STORE_MAX_FRAMES]; // punteros a buffers en PSRAM
    uint8_t   count;  // número de frames cargados
    uint16_t  w;      // ancho del sprite en píxeles
    uint16_t  h;      // alto del sprite en píxeles
    uint8_t   fps;    // frames por segundo declarados en el manifest
};

/**
 * SpriteStore — carga y mantiene frames de sprites en PSRAM.
 *
 * No es thread-safe (no es necesario en Arduino loop-driven).
 * No cachea; cada llamada a loadAnimation() re-lee LittleFS.
 */
class SpriteStore {
public:
    SpriteStore() = default;

    /**
     * Inicializa SpriteStore:
     *   - Monta LittleFS (format_if_failed = false, así falla limpio si el FS
     *     no está flasheado en lugar de reformatear y perder datos).
     *   - Abre y parsea el manifest JSON.
     *
     * @param manifestPath  Ruta absoluta dentro de LittleFS, e.g. "/sprites/manifest.json"
     * @return true si el manifest fue parseado correctamente, false en caso contrario.
     */
    bool begin(const char* manifestPath);

    /**
     * Carga todos los frames de una animación en PSRAM.
     *
     * Resuelve los paths de frames como:
     *   <baseDir>/<folder>/<frameRelPath>
     * donde baseDir = directorio del manifest (e.g. "/sprites")
     *       folder  = campo "folder" del sprite en el manifest (si existe),
     *                 o campo "id" como fallback
     *       frameRelPath = e.g. "walk/0.bin"
     *
     * El campo "folder" existe porque tool-mklittlefs v0.2.3 (LFS_NAME_MAX=31)
     * no puede empaquetar componentes de ruta > 31 chars. El UUID (36 chars) del
     * sprite se mapea a un nombre corto ("wiz") en LittleFS via el campo "folder".
     *
     * @param spriteId   ID del sprite (campo "id" del manifest).
     * @param animation  Nombre de la animación (e.g. "walk").
     * @param out        Struct que recibirá los punteros PSRAM y metadata.
     * @return true si todos los frames se cargaron; false con log en Serial si algo falla.
     */
    bool loadAnimation(const char* spriteId, const char* animation, ColorSpriteFrames& out);

    /**
     * Libera todos los buffers PSRAM de un ColorSpriteFrames.
     * Resetea todos los campos a 0/null.
     */
    void freeAnimation(ColorSpriteFrames& anim);

private:
    // Directorio base del manifest (e.g. "/sprites").
    // Calculado en begin() como dirname(manifestPath).
    char _baseDir[64];

    // Documento JSON del manifest, mantenido en memoria para poder
    // consultar varios sprites sin re-leer el fichero.
    // JsonDocument (ArduinoJson v7) — gestión dinámica de memoria.
    // Para uso en microcontroladores con memoria limitada el document
    // crece con el JSON parseado; 2KB es suficiente para el manifest actual.
    JsonDocument _manifest;

    bool _ready = false;

    // Construye la ruta completa de un frame:
    //   <_baseDir>/<spriteId>/<frameRelPath>
    // Escribe en buf (tamaño bufSize). Devuelve false si no cabe.
    bool buildFramePath(const char* spriteId, const char* frameRelPath,
                        char* buf, size_t bufSize) const;
};
