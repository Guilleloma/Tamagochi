#pragma once
// =============================================================================
// LittleFsStorage — implementación de IStorage que envuelve LittleFS de Arduino.
//
// ADVERTENCIA: este archivo SOLO compila para env:esp12e.
// En env:native está excluido vía build_src_filter en platformio.ini.
//
// Uso: Persistence persistence(storage) donde storage es LittleFsStorage.
// LittleFS debe inicializarse antes con LittleFS.begin() desde main.cpp.
// =============================================================================

#include "Persistence.h"

class LittleFsStorage : public IStorage {
public:
    bool exists(const char* path) override;
    bool read(const char* path, void* buffer, size_t size) override;
    bool write(const char* path, const void* buffer, size_t size) override;
    bool remove(const char* path) override;
};
