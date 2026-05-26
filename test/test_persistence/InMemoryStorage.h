#pragma once
// =============================================================================
// InMemoryStorage — mock de IStorage para tests host-native.
//
// Implementa IStorage usando std::map<string, vector<uint8_t>>.
// std::* solo en tests host (PC tiene RAM); nunca en código de producción.
//
// Helpers para simular escenarios de fallo:
//   corruptFile()         — flipea el byte 0 del archivo (invalida magic o CRC)
//   truncateFile()        — recorta el archivo (read() devolverá false)
//   simulateWriteFailure  — si true, write() devuelve false sin escribir
//   fileCount()           — cuántos archivos hay actualmente en el "fs"
// =============================================================================

#include "Persistence.h"
#include <map>
#include <vector>
#include <string>
#include <cstring>

class InMemoryStorage : public IStorage {
public:
    bool simulateWriteFailure = false;

    bool exists(const char* path) override {
        return files_.count(path) > 0;
    }

    bool read(const char* path, void* buffer, size_t size) override {
        auto it = files_.find(path);
        // Si el archivo no existe o tiene tamaño distinto, falla
        if (it == files_.end() || it->second.size() != size) return false;
        memcpy(buffer, it->second.data(), size);
        return true;
    }

    bool write(const char* path, const void* buffer, size_t size) override {
        if (simulateWriteFailure) return false;
        files_[path] = std::vector<uint8_t>(
            static_cast<const uint8_t*>(buffer),
            static_cast<const uint8_t*>(buffer) + size
        );
        return true;
    }

    bool remove(const char* path) override {
        return files_.erase(path) > 0;
    }

    // -------------------------------------------------------------------------
    // Helpers de test
    // -------------------------------------------------------------------------

    // Flipea el byte 0 del archivo — invalida magic o CRC según el offset.
    void corruptFile(const char* path) {
        auto it = files_.find(path);
        if (it != files_.end() && !it->second.empty()) {
            it->second[0] ^= 0xFF;
        }
    }

    // Recorta el archivo a newSize bytes — hace que read() falle por size mismatch.
    void truncateFile(const char* path, size_t newSize) {
        auto it = files_.find(path);
        if (it != files_.end()) {
            it->second.resize(newSize);
        }
    }

    // Número de archivos actualmente en el storage.
    size_t fileCount() const {
        return files_.size();
    }

    // Limpia todos los archivos (útil en setUp si se reutiliza la instancia).
    void clear() {
        files_.clear();
        simulateWriteFailure = false;
    }

private:
    std::map<std::string, std::vector<uint8_t>> files_;
};
