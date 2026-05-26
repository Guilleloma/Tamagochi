// =============================================================================
// LittleFsStorage.cpp — implementación IStorage sobre LittleFS (Arduino/ESP8266).
//
// SOLO compila para env:esp12e. Cuando UNIT_TEST está definido (env:native)
// todo el cuerpo queda vacío — el guard evita dependencias de Arduino en host.
//
// LittleFS garantiza atomicidad COW: si se corta la energía durante write(),
// el archivo previo permanece intacto. Esto es la base de la rotación de slots.
// =============================================================================

#ifndef UNIT_TEST

#include "LittleFsStorage.h"
#include "Logger.h"
#include <LittleFS.h>

bool LittleFsStorage::exists(const char* path) {
    return LittleFS.exists(path);
}

bool LittleFsStorage::read(const char* path, void* buffer, size_t size) {
    File f = LittleFS.open(path, "r");
    if (!f) {
        LOG_WARN("LittleFsStorage: no se pudo abrir %s para lectura", path);
        return false;
    }

    size_t bytesRead = f.read(static_cast<uint8_t*>(buffer), size);
    f.close();

    if (bytesRead != size) {
        LOG_WARN("LittleFsStorage: lectura incompleta %s (%u/%u bytes)",
                 path, (unsigned)bytesRead, (unsigned)size);
        return false;
    }
    return true;
}

bool LittleFsStorage::write(const char* path, const void* buffer, size_t size) {
    File f = LittleFS.open(path, "w");
    if (!f) {
        LOG_ERROR("LittleFsStorage: no se pudo abrir %s para escritura", path);
        return false;
    }

    size_t bytesWritten = f.write(static_cast<const uint8_t*>(buffer), size);
    f.close();  // close() hace fsync implícito en LittleFS

    if (bytesWritten != size) {
        LOG_ERROR("LittleFsStorage: escritura incompleta %s (%u/%u bytes)",
                  path, (unsigned)bytesWritten, (unsigned)size);
        return false;
    }
    return true;
}

bool LittleFsStorage::remove(const char* path) {
    return LittleFS.remove(path);
}

#endif  // UNIT_TEST
