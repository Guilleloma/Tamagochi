/**
 * SpriteStore.cpp — implementación del loader de sprites LittleFS → PSRAM
 *
 * Ver SpriteStore.h para la documentación de diseño y convenciones de rutas.
 */

#include "SpriteStore.h"
#include <string.h>

// ─── Helpers internos ────────────────────────────────────────────────────────

/**
 * Extrae el directorio de un path (equivalente a dirname(3)).
 * p.ej. "/sprites/manifest.json" → "/sprites"
 *       "/manifest.json"         → ""
 * Escribe en dst (size bytes). Si no hay separador, escribe "".
 */
static void extractDirname(const char* path, char* dst, size_t size) {
    const char* lastSlash = strrchr(path, '/');
    if (!lastSlash || lastSlash == path) {
        // Sin directorio padre o el slash es la raíz
        strncpy(dst, "", size);
        dst[size - 1] = '\0';
        return;
    }
    size_t len = (size_t)(lastSlash - path);
    if (len >= size) len = size - 1;
    memcpy(dst, path, len);
    dst[len] = '\0';
}

// ─── SpriteStore::begin() ────────────────────────────────────────────────────

bool SpriteStore::begin(const char* manifestPath) {
    _ready = false;

    // Montar LittleFS — format_if_failed=false para no destruir un FS válido
    // si el firmware arranca sin haber hecho uploadfs.
    if (!LittleFS.begin(false)) {
        Serial.println("[SpriteStore] ERROR: LittleFS.begin() fallo. "
                       "Asegurate de haber subido el filesystem (pio -t uploadfs).");
        return false;
    }
    Serial.println("[SpriteStore] LittleFS montado OK");

    // Calcular directorio base a partir del path del manifest
    extractDirname(manifestPath, _baseDir, sizeof(_baseDir));
    Serial.printf("[SpriteStore] baseDir = '%s'\n", _baseDir);

    // Abrir el manifest
    File f = LittleFS.open(manifestPath, "r");
    if (!f) {
        Serial.printf("[SpriteStore] ERROR: no se puede abrir '%s'\n", manifestPath);
        return false;
    }

    // Parsear JSON
    DeserializationError err = deserializeJson(_manifest, f);
    f.close();

    if (err) {
        Serial.printf("[SpriteStore] ERROR: fallo al parsear JSON: %s\n", err.c_str());
        return false;
    }

    // Validación mínima del esquema (ArduinoJson v7: usar is<T>() en lugar de containsKey)
    if (!_manifest["sprites"].is<JsonArray>()) {
        Serial.println("[SpriteStore] ERROR: manifest sin campo 'sprites' array");
        return false;
    }

    int spriteCount = _manifest["sprites"].as<JsonArray>().size();
    int version     = _manifest["version"] | 0;
    Serial.printf("[SpriteStore] manifest v%d parseado OK — %d sprite(s)\n",
                  version, spriteCount);

    _ready = true;
    return true;
}

// ─── SpriteStore::buildFramePath() ───────────────────────────────────────────

bool SpriteStore::buildFramePath(const char* spriteId, const char* frameRelPath,
                                  char* buf, size_t bufSize) const {
    // Formato final: <_baseDir>/<spriteId>/<frameRelPath>
    // e.g.: /sprites/0e273289-.../walk/0.bin
    int written = snprintf(buf, bufSize, "%s/%s/%s", _baseDir, spriteId, frameRelPath);
    if (written < 0 || (size_t)written >= bufSize) {
        Serial.printf("[SpriteStore] ERROR: path demasiado largo para '%s/%s/%s'\n",
                      _baseDir, spriteId, frameRelPath);
        return false;
    }
    return true;
}

// ─── SpriteStore::loadAnimation() ────────────────────────────────────────────

bool SpriteStore::loadAnimation(const char* spriteId,
                                 const char* animation,
                                 ColorSpriteFrames& out) {
    if (!_ready) {
        Serial.println("[SpriteStore] ERROR: begin() no fue llamado o fallo");
        return false;
    }

    // Inicializar out a zero para que freeAnimation() sea seguro si falla a mitad
    memset(&out, 0, sizeof(out));

    // Buscar el sprite por id en el manifest
    JsonArray sprites = _manifest["sprites"].as<JsonArray>();
    JsonObject spriteObj;
    bool found = false;

    for (JsonObject s : sprites) {
        const char* sid = s["id"] | "";
        if (strcmp(sid, spriteId) == 0) {
            spriteObj = s;
            found = true;
            break;
        }
    }

    if (!found) {
        Serial.printf("[SpriteStore] ERROR: sprite '%s' no encontrado en el manifest\n",
                      spriteId);
        return false;
    }

    // Leer dimensiones del sprite
    out.w = (uint16_t)(spriteObj["w"] | 0);
    out.h = (uint16_t)(spriteObj["h"] | 0);

    // Resolver el nombre de carpeta del sprite en LittleFS.
    // El campo "folder" es el nombre real del directorio en LittleFS
    // (puede diferir del "id" cuando el id es un UUID de 36 chars,
    //  que excede LFS_NAME_MAX=31 del tool-mklittlefs v0.2.3 del ESP8266).
    // Si "folder" no está presente, se usa el "id" como fallback.
    const char* spriteFolderRaw = spriteObj["folder"] | "";
    const char* spriteFolder = (spriteFolderRaw[0] != '\0') ? spriteFolderRaw : spriteId;

    if (out.w == 0 || out.h == 0) {
        Serial.printf("[SpriteStore] ERROR: sprite '%s' tiene dimensiones invalidas (%ux%u)\n",
                      spriteId, out.w, out.h);
        return false;
    }

    // Buscar la animación dentro del sprite (ArduinoJson v7: is<T>() en lugar de containsKey)
    if (!spriteObj["animations"][animation].is<JsonObject>()) {
        Serial.printf("[SpriteStore] ERROR: animacion '%s' no encontrada en sprite '%s'\n",
                      animation, spriteId);
        return false;
    }

    JsonObject animObj = spriteObj["animations"][animation];
    out.fps = (uint8_t)(animObj["fps"] | 8);

    JsonArray framesArr = animObj["frames"].as<JsonArray>();
    if (framesArr.isNull() || framesArr.size() == 0) {
        Serial.printf("[SpriteStore] ERROR: animacion '%s' sin frames\n", animation);
        return false;
    }

    uint8_t frameCount = (uint8_t)framesArr.size();
    if (frameCount > SPRITE_STORE_MAX_FRAMES) {
        Serial.printf("[SpriteStore] WARN: animacion '%s' tiene %u frames, "
                      "truncando a %u\n",
                      animation, frameCount, SPRITE_STORE_MAX_FRAMES);
        frameCount = SPRITE_STORE_MAX_FRAMES;
    }

    // Tamaño de cada frame en bytes: w * h * 2 bytes/pixel (RGB565)
    const size_t frameBytes = (size_t)out.w * out.h * 2;

    Serial.printf("[SpriteStore] Cargando '%s' (folder='%s') anim='%s' — "
                  "%u frames × %u bytes = %u bytes total\n",
                  spriteId, spriteFolder, animation,
                  frameCount, (unsigned)frameBytes, (unsigned)(frameCount * frameBytes));

    // Cargar cada frame
    char framePath[128];
    for (uint8_t i = 0; i < frameCount; i++) {
        const char* relPath = framesArr[i] | "";

        if (!buildFramePath(spriteFolder, relPath, framePath, sizeof(framePath))) {
            // Error ya logueado en buildFramePath; liberar lo asignado hasta ahora
            out.count = i;
            freeAnimation(out);
            return false;
        }

        // Reservar buffer en PSRAM
        // ps_malloc() garantiza que la memoria va a PSRAM (requiere -D BOARD_HAS_PSRAM)
        uint16_t* buf = (uint16_t*)ps_malloc(frameBytes);
        if (!buf) {
            Serial.printf("[SpriteStore] ERROR: ps_malloc(%u) fallo en frame %u. "
                          "PSRAM libre: %u bytes\n",
                          (unsigned)frameBytes, i, ESP.getFreePsram());
            out.count = i;
            freeAnimation(out);
            return false;
        }

        // Leer .bin desde LittleFS
        File f = LittleFS.open(framePath, "r");
        if (!f) {
            Serial.printf("[SpriteStore] ERROR: no se puede abrir '%s'\n", framePath);
            free(buf);
            out.count = i;
            freeAnimation(out);
            return false;
        }

        size_t bytesRead = f.read((uint8_t*)buf, frameBytes);
        f.close();

        if (bytesRead != frameBytes) {
            Serial.printf("[SpriteStore] ERROR: '%s' leyo %u bytes, esperaba %u\n",
                          framePath, (unsigned)bytesRead, (unsigned)frameBytes);
            free(buf);
            out.count = i;
            freeAnimation(out);
            return false;
        }

        out.frames[i] = buf;
        Serial.printf("[SpriteStore]   frame %u OK — %s (%u bytes en PSRAM)\n",
                      i, framePath, (unsigned)frameBytes);
    }

    out.count = frameCount;

    Serial.printf("[SpriteStore] '%s/%s' cargado: %u frames, %ux%u px, %u fps. "
                  "PSRAM libre restante: %u bytes\n",
                  spriteFolder, animation, out.count, out.w, out.h, out.fps,
                  ESP.getFreePsram());

    return true;
}

// ─── SpriteStore::freeAnimation() ────────────────────────────────────────────

void SpriteStore::freeAnimation(ColorSpriteFrames& anim) {
    for (uint8_t i = 0; i < anim.count; i++) {
        if (anim.frames[i]) {
            free(anim.frames[i]);
            anim.frames[i] = nullptr;
        }
    }
    memset(&anim, 0, sizeof(anim));
}
