#pragma once
// =============================================================================
// InMemoryStorage — mock de IStorage para tests host-native.
//
// Implementa IStorage usando std::map<string, vector<uint8_t>>.
// std::* solo en tests host (PC tiene RAM); nunca en código de producción.
//
// Semántica de read() (v2): si el archivo existe pero es más corto que size,
// se rellenan con 0 los bytes faltantes y se devuelve true. Esto permite leer
// blobs v1 (28 bytes) con sizeof(PersistedState) = 40 sin fallar.
//
// Helpers para simular escenarios de fallo:
//   corruptFile()         — flipea el byte 0 del archivo (invalida magic o CRC)
//   truncateFile()        — recorta el archivo (produce zero-pad en read)
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

    // read() con semántica zero-pad: si el archivo es más corto que size,
    // se copian los bytes disponibles y el resto del buffer se rellena con 0.
    // Devuelve false solo si el archivo no existe.
    bool read(const char* path, void* buffer, size_t size) override {
        auto it = files_.find(path);
        if (it == files_.end()) return false;

        const std::vector<uint8_t>& blob = it->second;
        size_t available = blob.size();

        // Rellenar el buffer completo a 0 primero para garantizar zero-pad
        memset(buffer, 0, size);

        // Copiar solo los bytes disponibles (puede ser menos que size para blobs v1)
        size_t toCopy = (available < size) ? available : size;
        memcpy(buffer, blob.data(), toCopy);

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

    // Recorta el archivo a newSize bytes.
    // Con la nueva semántica de read(), esto produce zero-pad al leer más bytes.
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

    // Devuelve el tamaño del archivo almacenado (en bytes), o 0 si no existe.
    size_t fileSize(const char* path) const {
        auto it = files_.find(path);
        if (it == files_.end()) return 0;
        return it->second.size();
    }

private:
    std::map<std::string, std::vector<uint8_t>> files_;
};
