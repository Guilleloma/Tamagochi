// =============================================================================
// MessageBox.cpp — sanitizador UTF-8->ASCII + buffer SPSC. Ver semántica
// completa (incluido el porqué de no usar mutex) en el .h.
// =============================================================================

#include "MessageBox.h"
#include <string.h>

namespace TextSanitizer {

namespace {

// transliterateLatin1 — mapeo de codepoints Latin-1 Supplement (U+00C0..U+00FF,
// decodificados de una secuencia UTF-8 de 2 bytes) a su ASCII equivalente.
// Devuelve 0 si el codepoint no tiene transliteración conocida — esto incluye
// ¿ (U+00BF) e ¡ (U+00A1), que el caller descarta explícitamente (ver abajo),
// y cualquier otro carácter Latin-1 no cubierto (se descarta como "multibyte
// desconocido", mismo tratamiento que un emoji).
char transliterateLatin1(uint32_t codepoint) {
    switch (codepoint) {
        case 0xE1: return 'a';  // á
        case 0xE9: return 'e';  // é
        case 0xED: return 'i';  // í
        case 0xF3: return 'o';  // ó
        case 0xFA: return 'u';  // ú
        case 0xC1: return 'A';  // Á
        case 0xC9: return 'E';  // É
        case 0xCD: return 'I';  // Í
        case 0xD3: return 'O';  // Ó
        case 0xDA: return 'U';  // Ú
        case 0xF1: return 'n';  // ñ
        case 0xD1: return 'N';  // Ñ
        case 0xFC: return 'u';  // ü
        case 0xDC: return 'U';  // Ü
        case 0xE7: return 'c';  // ç
        case 0xC7: return 'C';  // Ç
        default:   return 0;
    }
}

// isContinuationByte — true si b es un byte de continuación UTF-8 (10xxxxxx).
bool isContinuationByte(unsigned char b) {
    return (b & 0xC0) == 0x80;
}

}  // namespace

size_t utf8ToAscii(const char* in, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return 0;
    if (in == nullptr) {
        out[0] = '\0';
        return 0;
    }

    const size_t maxWritable = outSize - 1;  // reservar espacio para '\0'
    size_t written = 0;
    size_t i = 0;

    while (in[i] != '\0' && written < maxWritable) {
        const unsigned char b0 = static_cast<unsigned char>(in[i]);

        if (b0 < 0x80) {
            // ASCII de 1 byte: controles (incluido DEL=0x7F) -> espacio.
            const bool isControl = (b0 < 0x20) || (b0 == 0x7F);
            out[written++] = isControl ? ' ' : static_cast<char>(b0);
            i += 1;
            continue;
        }

        if ((b0 & 0xE0) == 0xC0) {
            // Cabecera de secuencia de 2 bytes (110xxxxx).
            const unsigned char b1 = static_cast<unsigned char>(in[i + 1]);
            if (in[i + 1] != '\0' && isContinuationByte(b1)) {
                const uint32_t codepoint =
                    (static_cast<uint32_t>(b0 & 0x1Fu) << 6) | (b1 & 0x3Fu);
                const char ascii = transliterateLatin1(codepoint);
                // ascii==0 cubre ¿/¡ y cualquier otro Latin-1 sin mapeo: se
                // descartan (0 chars de salida), no se detiene el escaneo.
                if (ascii != 0) {
                    out[written++] = ascii;
                }
                i += 2;
                continue;
            }
            // Continuation byte ausente ('\0', fin de string) o inválido:
            // cabecera corrupta — descartar solo este byte y seguir.
            i += 1;
            continue;
        }

        if ((b0 & 0xF0) == 0xE0) {
            // Cabecera de secuencia de 3 bytes (1110xxxx) — símbolos varios,
            // parte del rango de emoji. Sin transliteración: se descarta la
            // secuencia completa (o lo que quede de ella si el string corta antes).
            size_t seqLen = 1;
            while (seqLen < 3 && in[i + seqLen] != '\0'
                   && isContinuationByte(static_cast<unsigned char>(in[i + seqLen]))) {
                seqLen++;
            }
            i += seqLen;
            continue;
        }

        if ((b0 & 0xF8) == 0xF0) {
            // Cabecera de secuencia de 4 bytes (11110xxx) — plano astral,
            // donde vive la mayoría de emoji. Se descarta completa.
            size_t seqLen = 1;
            while (seqLen < 4 && in[i + seqLen] != '\0'
                   && isContinuationByte(static_cast<unsigned char>(in[i + seqLen]))) {
                seqLen++;
            }
            i += seqLen;
            continue;
        }

        // Byte inválido en UTF-8 (continuation suelta 0x80-0xBF, o cabecera
        // imposible 0xF8-0xFF) — descartar 1 byte y seguir sin crashear.
        i += 1;
    }

    out[written] = '\0';

    // ── Colapsar espacios consecutivos + trim ────────────────────────────────
    // Compactación in-place sobre `out`: writeIdx <= readIdx siempre, así que
    // es seguro leer y escribir en el mismo buffer sin buffer auxiliar.
    size_t readIdx = 0;
    while (readIdx < written && out[readIdx] == ' ') readIdx++;  // trim inicial

    size_t writeIdx = 0;
    bool pendingSpace = false;
    for (; readIdx < written; readIdx++) {
        if (out[readIdx] == ' ') {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace) {
            out[writeIdx++] = ' ';
            pendingSpace = false;
        }
        out[writeIdx++] = out[readIdx];
    }
    // pendingSpace descartado a propósito aquí: cualquier espacio de cola
    // pendiente es trailing whitespace (trim final), no se escribe.

    out[writeIdx] = '\0';
    return writeIdx;
}

}  // namespace TextSanitizer

// =============================================================================
// MessageBox — ver contrato completo y razonamiento de concurrencia en el .h.
// =============================================================================

MessageBox::PostResult MessageBox::post(const char* utf8Text) {
    if (utf8Text == nullptr) return PostResult::Empty;

    const size_t rawLen = strlen(utf8Text);
    if (rawLen > kMaxRawLen) return PostResult::TooLong;

    // Buffer de escaneo dimensionado a kMaxRawLen+1: cada secuencia UTF-8 de
    // N bytes produce como máximo 1 char de salida, así que el resultado
    // sanitizado NUNCA puede ser más largo que rawLen (<= kMaxRawLen). Usar
    // este tamaño (en vez de kMaxLen+1) evita truncar el resultado antes de
    // poder comprobar su longitud real — si truncáramos primero, un mensaje
    // de 150 chars sanitizados se aceptaría silenciosamente recortado a 96
    // en vez de devolver TooLong (el cliente nunca sabría que se recortó).
    char sanitized[kMaxRawLen + 1];
    const size_t sanitizedLen =
        TextSanitizer::utf8ToAscii(utf8Text, sanitized, sizeof(sanitized));

    if (sanitizedLen == 0)      return PostResult::Empty;
    if (sanitizedLen > kMaxLen) return PostResult::TooLong;

    memcpy(buf_, sanitized, sanitizedLen);
    buf_[sanitizedLen] = '\0';

    // Publicar: release garantiza que consume() (con su acquire) vea buf_ ya
    // escrito. Last-wins: no comprobamos pending_ previo a propósito — un
    // post() nuevo siempre gana sobre un mensaje anterior sin consumir.
    pending_.store(true, std::memory_order_release);
    return PostResult::Ok;
}

bool MessageBox::hasPending() const {
    return pending_.load(std::memory_order_acquire);
}

bool MessageBox::consume(char* out, size_t outSize) {
    if (!pending_.exchange(false, std::memory_order_acquire)) {
        return false;  // no había mensaje pendiente
    }
    if (out != nullptr && outSize > 0) {
        const size_t len = strlen(buf_);
        const size_t n = (len < outSize - 1) ? len : (outSize - 1);
        memcpy(out, buf_, n);
        out[n] = '\0';
    }
    return true;
}
