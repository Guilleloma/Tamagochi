// =============================================================================
// AuthChecker.cpp — Verificación de autenticación Basic para la WebUI.
//
// Sin Arduino.h: testeable en host (env:native).
// =============================================================================

#include "AuthChecker.h"

// Incluimos string.h solo para strlen / strncmp de la parte fija "Basic ".
// No se usa ninguna función que dependa de Arduino.
#include <string.h>

namespace AuthChecker {

// =============================================================================
// Constantes internas
// =============================================================================

static const char kBasicPrefix[]  = "Basic ";
static const size_t kBasicPrefixLen = 6;  // strlen("Basic ")

// Tamaño máximo de credencial decodificada: user + ':' + pass + '\0'.
// Conservador: base64 overhead ~33%, 128 bytes decodificados → ~171 bytes b64.
static constexpr size_t kMaxDecodedCred = 128;

// =============================================================================
// staticEquals — comparación constant-time de dos strings.
//
// Itera tantos bytes como la longitud mayor de ambos para que el tiempo de
// ejecución no dependa de dónde está el primer mismatch (mitiga timing attacks).
// Devuelve true si ambas cadenas son idénticas.
// =============================================================================
static bool staticEquals(const char* a, size_t lenA, const char* b, size_t lenB) {
    size_t maxLen = (lenA > lenB) ? lenA : lenB;
    uint8_t diff = 0;

    for (size_t i = 0; i < maxLen; ++i) {
        uint8_t ca = (i < lenA) ? static_cast<uint8_t>(a[i]) : 0u;
        uint8_t cb = (i < lenB) ? static_cast<uint8_t>(b[i]) : 0u;
        diff |= (ca ^ cb);
    }
    // También incorporar la diferencia de longitudes para detectar prefijos.
    diff |= static_cast<uint8_t>(lenA != lenB ? 1u : 0u);
    return (diff == 0);
}

// =============================================================================
// decodeBase64 — decodificación RFC 4648.
//
// Tabla de decodificación: A-Z → 0-25, a-z → 26-51, 0-9 → 52-61, + → 62, / → 63.
// '=' es padding (se ignora). Chars inválidos → retorna 0.
// =============================================================================
size_t decodeBase64(const char* in, size_t inLen, char* out, size_t outSize) {
    if (!in || !out || outSize == 0) {
        return 0;
    }

    // Tabla de decodificación: índice = valor ASCII, contenido = valor b64 (0-63) o 0xFF si inválido.
    static const uint8_t kDecodeTable[128] = {
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 0-7
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 8-15
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 16-23
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 24-31
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 32-39 (space,!,",%,#,#,$,',&,')
        0xFF,0xFF,0xFF,  62,0xFF,0xFF,0xFF,  63,  // 40-47 ((,),*,+,,,-,.,/)
          52,  53,  54,  55,  56,  57,  58,  59,  // 48-55 (0-7)
          60,  61,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // 56-63 (8,9,:,;,<,=,>,?)
        0xFF,   0,   1,   2,   3,   4,   5,   6,  // 64-71 (@,A-G)
           7,   8,   9,  10,  11,  12,  13,  14,  // 72-79 (H-O)
          15,  16,  17,  18,  19,  20,  21,  22,  // 80-87 (P-W)
          23,  24,  25,0xFF,0xFF,0xFF,0xFF,0xFF,  // 88-95 (X,Y,Z,[,\,],^,_)
        0xFF,  26,  27,  28,  29,  30,  31,  32,  // 96-103 (`,a-g)
          33,  34,  35,  36,  37,  38,  39,  40,  // 104-111 (h-o)
          41,  42,  43,  44,  45,  46,  47,  48,  // 112-119 (p-w)
          49,  50,  51,0xFF,0xFF,0xFF,0xFF,0xFF   // 120-127 (x,y,z,{,|,},~,DEL)
    };

    size_t outLen = 0;
    uint32_t buf  = 0;
    int      bits = 0;

    for (size_t i = 0; i < inLen; ++i) {
        unsigned char c = static_cast<unsigned char>(in[i]);

        // Padding: fin de datos.
        if (c == '=') {
            break;
        }

        // Char fuera del rango ASCII o inválido en base64.
        if (c >= 128 || kDecodeTable[c] == 0xFF) {
            return 0;
        }

        buf  = (buf << 6) | kDecodeTable[c];
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            if (outLen >= outSize) {
                return 0;  // buffer overflow
            }
            out[outLen++] = static_cast<char>((buf >> bits) & 0xFF);
        }
    }

    return outLen;
}

// =============================================================================
// checkBasicAuth — verifica header Authorization: Basic.
// =============================================================================
bool checkBasicAuth(const char* authHeader,
                    const char* expectedUser,
                    const char* expectedPass) {
    // 1. Nil guard.
    if (!authHeader || !expectedUser || !expectedPass) {
        return false;
    }

    // 2. Debe empezar por "Basic " (case-sensitive, 6 chars).
    if (strncmp(authHeader, kBasicPrefix, kBasicPrefixLen) != 0) {
        return false;
    }

    const char* b64Part    = authHeader + kBasicPrefixLen;
    size_t      b64PartLen = strlen(b64Part);

    if (b64PartLen == 0) {
        return false;
    }

    // 3. Decodificar base64.
    char decoded[kMaxDecodedCred];
    size_t decodedLen = decodeBase64(b64Part, b64PartLen, decoded, kMaxDecodedCred - 1);

    if (decodedLen == 0) {
        return false;
    }
    decoded[decodedLen] = '\0';

    // 4. Buscar ':' separador user:pass.
    size_t colonPos  = decodedLen;  // inicializar a "no encontrado"
    for (size_t i = 0; i < decodedLen; ++i) {
        if (decoded[i] == ':') {
            colonPos = i;
            break;
        }
    }

    if (colonPos == decodedLen) {
        return false;  // no hay ':' → header malformado
    }

    const char* userPart    = decoded;
    size_t      userPartLen = colonPos;
    const char* passPart    = decoded + colonPos + 1;
    size_t      passPartLen = decodedLen - colonPos - 1;

    // 5. Comparación constant-time de user y pass.
    bool userOk = staticEquals(userPart, userPartLen,
                               expectedUser, strlen(expectedUser));
    bool passOk = staticEquals(passPart, passPartLen,
                               expectedPass, strlen(expectedPass));

    return userOk && passOk;
}

}  // namespace AuthChecker
