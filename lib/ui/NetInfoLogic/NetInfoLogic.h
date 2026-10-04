#pragma once
// =============================================================================
// NetInfoLogic.h — lógica pura de "qué se ve" en la página de red de Stats
// (renderer color, ESP32-S3). Header-only, SIN dependencias de Arduino, WiFi
// ni Arduino_GFX (mismo espíritu que MoodLogic.h / ScreenPowerLogic.h) —
// compilable en env:native para tests unitarios y en env:esp32s3 (incluido
// desde DisplayRendererColor.h).
//
// Responsabilidades:
//   - NetStatus / statusFor() / statusLabel(): texto de estado a partir del
//     WifiState de WifiManager + si hay SSID configurado en secrets.h.
//   - fitText() / formatSsid() / formatHost(): texto ASCII imprimible que cabe
//     en N caracteres, con elipsis "..." si hay que truncar.
//   - formatIp(): IPv4 en notación decimal con puntos ("-" si 0.0.0.0).
//   - rssiToBars() / formatRssi(): barras de cobertura 0-4 y "-62 dBm".
//   - maxCharsForWidth(): cuántos caracteres de la fuente base caben en px.
//
// Solo ASCII: la fuente base de Arduino_GFX (glcdfont, 6 px/char a size 1)
// no tiene glifos para UTF-8 ni para "…" (U+2026) — por eso "Conectando..."
// y la elipsis del truncado usan tres puntos ASCII, y cualquier byte no
// imprimible del SSID se sustituye por '?'.
//
// SEGURIDAD: aquí solo entran SSID, hostname, IP y RSSI. Ninguna función
// recibe ni formatea contraseñas (WIFI_PASSWORD / OTA_PASSWORD /
// WEB_AUTH_PASSWORD) — ver rules/security.md.
//
// C++11 (gnu++11 en arduino-esp32 2.x): funciones inline, constexpr de una
// sola sentencia, nada de std::string/std::optional.
// =============================================================================

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "WifiState.h"   // lib/core — enum WifiState, sin dependencias de plataforma

namespace NetInfoLogic {

// Texto que se muestra cuando un valor no está disponible (sin IP, sin RSSI,
// SSID/hostname vacíos).
static constexpr const char* kPlaceholder = "-";

// Marca de truncado. Tres puntos ASCII (ver nota de "Solo ASCII" arriba).
static constexpr const char*  kEllipsis    = "...";
static constexpr size_t       kEllipsisLen = 3;

// Tamaños de buffer recomendados (incluyen el '\0').
//   IP:   "255.255.255.255" = 15 chars.
//   RSSI: "-128 dBm"        =  8 chars.
static constexpr size_t kIpTextSize   = 16;
static constexpr size_t kRssiTextSize = 10;

// Longitud máxima de un SSID según 802.11 (bytes).
static constexpr size_t kMaxSsidLen = 32;

// Ancho de un carácter de la fuente base de Arduino_GFX a textSize=1
// (5 px de glifo + 1 px de separación). A size N ocupa 6*N px.
static constexpr uint8_t kFontCharW = 6;

// =============================================================================
// NetStatus — estado de red tal y como se le cuenta al usuario.
// =============================================================================
enum class NetStatus : uint8_t {
    NotConfigured = 0,   // WIFI_SSID vacío en secrets.h — nunca intenta conectar
    Connecting    = 1,   // WiFi.begin() lanzado, esperando IP
    Connected     = 2,   // con IP asignada
    Offline       = 3    // configurada pero sin red (caída, timeout o aún sin arrancar)
};

// statusFor — estado visible a partir del WifiState de WifiManager.
//
//   ssidConfigured == false        → NotConfigured (manda sobre el WifiState)
//   Connected                      → Connected
//   Connecting                     → Connecting
//   Disconnected                   → Offline
//   Disabled con SSID configurado  → Offline (solo pasa antes de
//                                     WifiManager::begin(): no hay red todavía)
inline NetStatus statusFor(WifiState state, bool ssidConfigured) {
    if (!ssidConfigured) return NetStatus::NotConfigured;
    switch (state) {
        case WifiState::Connected:    return NetStatus::Connected;
        case WifiState::Connecting:   return NetStatus::Connecting;
        case WifiState::Disconnected:
        case WifiState::Disabled:
            return NetStatus::Offline;
    }
    return NetStatus::Offline;
}

// statusLabel — texto ASCII del estado (nunca nullptr).
inline const char* statusLabel(NetStatus status) {
    switch (status) {
        case NetStatus::Connected:     return "Conectado";
        case NetStatus::Connecting:    return "Conectando...";
        case NetStatus::Offline:       return "Sin conexion";
        case NetStatus::NotConfigured: return "WiFi no configurada";
    }
    return "Sin conexion";
}

// maxCharsForWidth — cuántos caracteres de la fuente base caben en widthPx a
// textSize. 0 si los parámetros no tienen sentido (textSize 0, ancho <= 0).
constexpr size_t maxCharsForWidth(int widthPx, uint8_t textSize) {
    return (textSize == 0 || widthPx <= 0)
        ? 0u
        : static_cast<size_t>(widthPx) / (static_cast<size_t>(kFontCharW) * textSize);
}

// -----------------------------------------------------------------------------
// Detalle interno: siguiente carácter "pintable" de una cadena C.
//   - ASCII imprimible (0x20..0x7E)        → tal cual.
//   - Control (0x00..0x1F, 0x7F)            → '?'.
//   - Byte inicial UTF-8 (0xC0..0xFF)       → un único '?' por carácter.
//   - Byte de continuación UTF-8 (0x80..0xBF) → se omite (pertenece al '?'
//     del byte inicial; uno huérfano simplemente desaparece).
// Avanza `p`. Devuelve false al llegar al '\0'.
// -----------------------------------------------------------------------------
inline bool nextDisplayChar_(const char*& p, char& outChar) {
    while (*p != '\0') {
        const unsigned char b = static_cast<unsigned char>(*p++);
        if (b >= 0x20 && b <= 0x7E) {
            outChar = static_cast<char>(b);
            return true;
        }
        if (b >= 0x80 && b <= 0xBF) continue;
        outChar = '?';
        return true;
    }
    return false;
}

// Número de caracteres pintables de `s` (ver nextDisplayChar_).
inline size_t displayLength_(const char* s) {
    size_t n = 0;
    char   c;
    while (nextDisplayChar_(s, c)) ++n;
    return n;
}

// Copia en out los primeros `count` caracteres pintables de src. No escribe '\0'.
inline void copyDisplayChars_(const char* src, size_t count, char* out) {
    char c;
    for (size_t i = 0; i < count && nextDisplayChar_(src, c); ++i) out[i] = c;
}

// fitText — deja en `out` el texto pintable de `src` (ver nextDisplayChar_)
// limitado a `maxChars` caracteres:
//   - si cabe, se copia entero;
//   - si no cabe y maxChars >= 4, se recorta a (maxChars-3) chars + "...";
//   - si no cabe y maxChars < 4, se corta en seco (no hay sitio para "...").
// El límite real es min(maxChars, outSize-1): nunca desborda `out` y siempre
// lo termina en '\0'. src == nullptr se trata como "". Devuelve los
// caracteres escritos (sin contar el '\0'); 0 si out es nullptr/outSize 0.
inline size_t fitText(const char* src, size_t maxChars, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return 0;
    if (src == nullptr) src = "";

    const size_t cap = (maxChars < outSize - 1) ? maxChars : outSize - 1;
    const size_t len = displayLength_(src);

    size_t written;
    if (len <= cap) {
        copyDisplayChars_(src, len, out);
        written = len;
    } else if (cap > kEllipsisLen) {
        const size_t keep = cap - kEllipsisLen;
        copyDisplayChars_(src, keep, out);
        memcpy(out + keep, kEllipsis, kEllipsisLen);
        written = cap;
    } else {
        copyDisplayChars_(src, cap, out);
        written = cap;
    }
    out[written] = '\0';
    return written;
}

// fitOrPlaceholder_ — fitText, pero con kPlaceholder si src es nullptr/"".
inline size_t fitOrPlaceholder_(const char* src, size_t maxChars, char* out, size_t outSize) {
    const bool isEmpty = (src == nullptr || src[0] == '\0');
    return fitText(isEmpty ? kPlaceholder : src, maxChars, out, outSize);
}

// formatSsid — SSID listo para pintar en maxChars (kPlaceholder si vacío).
inline size_t formatSsid(const char* ssid, size_t maxChars, char* out, size_t outSize) {
    return fitOrPlaceholder_(ssid, maxChars, out, outSize);
}

// formatHost — "<hostname>.local" listo para pintar en maxChars.
// hostname llega SIN ".local" (convenio de OTA_HOSTNAME en secrets.h).
// kPlaceholder si el hostname está vacío.
inline size_t formatHost(const char* hostname, size_t maxChars, char* out, size_t outSize) {
    if (hostname == nullptr || hostname[0] == '\0') {
        return fitText(kPlaceholder, maxChars, out, outSize);
    }
    // 63 = longitud máxima de una etiqueta DNS/mDNS + ".local" + '\0'.
    char full[63 + 6 + 1];
    snprintf(full, sizeof(full), "%s.local", hostname);
    return fitText(full, maxChars, out, outSize);
}

// isIpAssigned — false para 0.0.0.0 (IPAddress por defecto / sin DHCP).
inline bool isIpAssigned(const uint8_t ip[4]) {
    return ip != nullptr && (ip[0] | ip[1] | ip[2] | ip[3]) != 0;
}

// formatIp — "a.b.c.d", o kPlaceholder si la IP no está asignada (0.0.0.0).
// Con un buffer pequeño trunca sin desbordar (snprintf). kIpTextSize basta
// para cualquier IPv4.
inline void formatIp(const uint8_t ip[4], char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return;
    if (!isIpAssigned(ip)) {
        snprintf(out, outSize, "%s", kPlaceholder);
        return;
    }
    snprintf(out, outSize, "%u.%u.%u.%u",
             static_cast<unsigned>(ip[0]), static_cast<unsigned>(ip[1]),
             static_cast<unsigned>(ip[2]), static_cast<unsigned>(ip[3]));
}

// isRssiValid — RSSI medido de verdad. WifiManager::rssi() devuelve 0 cuando
// no hay conexión, y un RSSI real de 802.11 siempre es negativo, así que
// >= 0 se trata como "sin dato".
inline bool isRssiValid(int rssiDbm) {
    return rssiDbm < 0;
}

// Umbrales de barras (dBm, inclusivos): escala habitual de los móviles.
static constexpr int kRssi4Bars = -55;   // excelente
static constexpr int kRssi3Bars = -67;   // buena (OTA/HTTP fiables)
static constexpr int kRssi2Bars = -78;   // justa
static constexpr int kRssi1Bar  = -89;   // débil (cortes probables)

// rssiToBars — 0..4 barras. 0 si el RSSI no es válido o es peor que -89 dBm.
inline uint8_t rssiToBars(int rssiDbm) {
    if (!isRssiValid(rssiDbm))  return 0;
    if (rssiDbm >= kRssi4Bars)  return 4;
    if (rssiDbm >= kRssi3Bars)  return 3;
    if (rssiDbm >= kRssi2Bars)  return 2;
    if (rssiDbm >= kRssi1Bar)   return 1;
    return 0;
}

// formatRssi — "-62 dBm", o kPlaceholder si el RSSI no es válido.
inline void formatRssi(int rssiDbm, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return;
    if (!isRssiValid(rssiDbm)) {
        snprintf(out, outSize, "%s", kPlaceholder);
        return;
    }
    snprintf(out, outSize, "%d dBm", rssiDbm);
}

}  // namespace NetInfoLogic
