#pragma once
// =============================================================================
// WifiProvisioning — portal de configuración WiFi en modo Access Point.
//
// Qué hace (feat/wifi-ap-provisioning):
//   Cuando el dispositivo arranca SIN credenciales (ni en NVS ni en secrets.h),
//   en vez de quedarse offline en silencio levanta su propia red WiFi y un
//   portal cautivo. El usuario se conecta con el móvil a "Furbigotty-setup",
//   se abre automáticamente una página, elige su red de una lista (escaneo en
//   vivo), mete la contraseña y pulsa guardar. Las credenciales se persisten en
//   NVS (ver WifiCredentials) y el dispositivo se reinicia para conectarse como
//   cliente normal.
//
// Flujo técnico:
//   - WiFi.mode(WIFI_AP_STA): AP para el portal + STA para poder escanear redes
//     cercanas sin tirar el AP.
//   - SoftAP abierto (sin password) en una IP fija; un DNSServer responde a
//     TODAS las consultas con esa IP → capta el "sign-in to network" del móvil.
//   - AsyncWebServer en el puerto 80 sirve el portal, el escaneo (JSON) y el
//     guardado. (No hay conflicto con WebUI: WebUI solo arranca cuando hay
//     conexión STA establecida, estado que en modo portal nunca se alcanza.)
//
// Opt-in: todo el módulo vive tras #if defined(WIFI_PROVISIONING) en el .cpp y
// en main_esp32.cpp. Sin ese flag de build, el comportamiento es el de siempre
// (SSID vacío → offline). Así la placa original no se ve afectada.
//
// Solo ESP32 (AsyncWebServer + DNSServer + Preferences). No testeable native.
// =============================================================================

#include <stdint.h>
#include <stddef.h>   // size_t (kScanJsonCap) — stdint.h no lo garantiza

#if defined(WIFI_PROVISIONING) && defined(ARDUINO_ARCH_ESP32)

// Forward declarations para no arrastrar los headers pesados (AsyncWebServer,
// DNSServer) a quien solo incluya este .h. Las instancias viven en el .cpp.
class AsyncWebServer;
class DNSServer;

class WifiProvisioning {
public:
    WifiProvisioning() = default;
    ~WifiProvisioning();

    // Levanta el AP, el DNS cautivo y el servidor web. apName: SSID del AP que
    // verá el móvil (p.ej. "Furbigotty-setup"). Lanza el primer escaneo.
    void begin(const char* apName);

    // Llamar en cada vuelta del loop principal. Procesa el DNS cautivo, recoge
    // los resultados del escaneo asíncrono y ejecuta el reinicio diferido tras
    // guardar credenciales. Barato cuando no hay nada pendiente.
    void loop();

    // true si el portal está activo (begin() llamado y aún sin reinicio).
    bool isActive() const { return active_; }

    // SSID del AP (para mostrarlo en la pantalla del dispositivo).
    const char* apSsid() const { return apSsid_; }

    // IP del portal como texto "192.168.x.x" (para mostrarla en pantalla).
    const char* portalIp() const { return portalIp_; }

private:
    // Construye el JSON de redes a partir de los resultados de WiFi.scanNetworks.
    // Escrito desde loop() (loopTask); leído por el handler /scan (async_tcp).
    void buildScanJson_(int found);
    // Registra las rutas del servidor web.
    void setupRoutes_();

    AsyncWebServer* server_ = nullptr;
    DNSServer*      dns_     = nullptr;

    bool     active_          = false;
    bool     scanInFlight_    = false;  // hay un WiFi.scanNetworks(async) en curso
    bool     rescanRequested_ = false;  // el handler /rescan pidió re-escanear
    uint32_t rebootAtMs_      = 0;      // !=0 → reiniciar cuando millis() lo supere

    // Guardado diferido: el handler POST /save (task async_tcp) solo copia los
    // valores y levanta savePending_. La escritura real en NVS (flash) y el
    // reinicio los hace loop() (loopTask) — no se tocan flash ni reset desde la
    // task del servidor web.
    volatile bool savePending_   = false;
    char          pendingSsid_[33] = {0};
    char          pendingPass_[64] = {0};
    char          pendingHost_[33] = {0};

    char apSsid_[33]   = {0};
    char portalIp_[16] = {0};

    // Snapshot JSON del último escaneo. Lo rellena loop() (loopTask) y lo
    // devuelve tal cual el handler GET /scan (async_tcp). La carrera entre
    // ambas tasks es benigna en este contexto de configuración: en el peor caso
    // el móvil recibe una vez una lista a medio actualizar y reintenta (la UI
    // reescanea sola). Dimensionado para ~20 redes.
    static constexpr size_t kScanJsonCap = 2048;
    char scanJson_[kScanJsonCap] = "{\"scanning\":true,\"networks\":[]}";
};

#endif  // WIFI_PROVISIONING && ARDUINO_ARCH_ESP32
