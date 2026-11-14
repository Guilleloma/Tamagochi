// =============================================================================
// WifiProvisioning.cpp — portal cautivo AP para configurar WiFi. ESP32-only.
// =============================================================================

#ifndef UNIT_TEST
#if defined(WIFI_PROVISIONING) && defined(ARDUINO_ARCH_ESP32)

#include "WifiProvisioning.h"
#include "WifiCredentials.h"
#include "Logger.h"
#include "secrets.h"   // OTA_HOSTNAME: valor por defecto del hostname mDNS

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <string.h>

namespace {

// IP del portal. 192.168.4.1 es la que el core asigna por defecto al SoftAP;
// la fijamos explícitamente para que DNS, página e instrucciones coincidan.
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApGw(192, 168, 4, 1);
const IPAddress kApMask(255, 255, 255, 0);
constexpr uint16_t kDnsPort = 53;

// Página del portal. Autocontenida (sin recursos externos: no hay internet en
// el AP). JS: sondea /scan, pinta la lista de redes, al tocar una rellena el
// SSID, y envía SSID+password a /save.
const char kPortalHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="es"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Furbigotty WiFi</title>
<style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
  background:#0e1116;color:#e6edf3;display:flex;justify-content:center;
  padding:24px 16px env(safe-area-inset-bottom)}
.card{width:100%;max-width:420px}
h1{font-size:20px;margin:0 0 2px;display:flex;align-items:center;gap:8px}
.sub{color:#8b949e;font-size:13px;margin:0 0 18px}
.lbl{font-size:12px;color:#8b949e;text-transform:uppercase;letter-spacing:.04em;margin:16px 0 8px}
.list{display:flex;flex-direction:column;gap:6px;min-height:52px}
.net{display:flex;align-items:center;gap:10px;padding:12px 14px;border:1px solid #273040;
  border-radius:12px;background:#161b22;cursor:pointer;transition:.12s}
.net:hover{border-color:#3b82f6}
.net.sel{border-color:#3b82f6;background:#13233f}
.net .nm{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.net .mt{color:#8b949e;font-size:12px;display:flex;align-items:center;gap:6px}
.bars{display:inline-flex;align-items:flex-end;gap:2px;height:14px}
.bars i{width:3px;background:#3b82f6;border-radius:1px;opacity:.25}
.bars i.on{opacity:1}
input{width:100%;padding:12px 14px;border-radius:12px;border:1px solid #273040;
  background:#161b22;color:#e6edf3;font-size:16px}
input:focus{outline:none;border-color:#3b82f6}
button{width:100%;margin-top:18px;padding:13px;border:0;border-radius:12px;
  background:#3b82f6;color:#fff;font-size:16px;font-weight:600;cursor:pointer}
button:disabled{opacity:.5;cursor:default}
.ghost{background:#21262d;color:#c9d1d9;margin-top:10px}
.msg{margin-top:16px;padding:12px 14px;border-radius:12px;font-size:14px;display:none}
.msg.ok{display:block;background:#113524;border:1px solid #1f8f4e;color:#7ee2a8}
.msg.err{display:block;background:#3a1618;border:1px solid #b03a3f;color:#ff9ea2}
.spin{color:#8b949e;font-size:13px;padding:8px 2px}
.hint{color:#8b949e;font-size:12px;margin-top:8px;line-height:1.4}
.hint b{color:#c9d1d9}
</style></head><body><div class="card">
<h1>🐾 Furbigotty</h1>
<p class="sub">Elige tu red WiFi y escribe la contraseña.</p>

<div class="lbl">Redes encontradas</div>
<div id="list" class="list"><div class="spin">Buscando redes…</div></div>
<button id="rescan" class="ghost" type="button">↻ Volver a buscar</button>

<form id="f">
  <div class="lbl">Red seleccionada</div>
  <input id="ssid" name="ssid" placeholder="Toca una red o escríbela" autocomplete="off">
  <div class="lbl">Contraseña</div>
  <input id="pass" name="pass" type="password" placeholder="Contraseña de la red" autocomplete="off">
  <div class="lbl">Nombre en la red (mDNS)</div>
  <input id="host" name="host" placeholder="tamagotchi" autocomplete="off"
         maxlength="24" pattern="[A-Za-z0-9-]+">
  <div class="hint">Accederás al dispositivo como <b><span id="hp">tamagotchi</span>.local</b>. Solo letras, números y guiones.</div>
  <button id="save" type="submit">Guardar y conectar</button>
</form>
<div id="msg" class="msg"></div>
</div>
<script>
var listEl=document.getElementById('list'),ssidEl=document.getElementById('ssid'),
    msgEl=document.getElementById('msg'),hostEl=document.getElementById('host'),
    hpEl=document.getElementById('hp'),selName=null;
// Prefijar el hostname actual (configurado o por defecto) y actualizar la
// vista previa "<nombre>.local" al escribir.
fetch('/cfg').then(function(r){return r.json();}).then(function(j){
  if(j.host){hostEl.value=j.host;hpEl.textContent=j.host;}
}).catch(function(){});
hostEl.addEventListener('input',function(){
  hpEl.textContent=(hostEl.value||'tamagotchi');});
function bars(rssi){var q=Math.max(0,Math.min(4,Math.round((rssi+100)/12)));
  var s='<span class="bars">';for(var i=0;i<4;i++)s+='<i class="'+(i<q?'on':'')+'" style="height:'+(5+i*3)+'px"></i>';return s+'</span>';}
function esc(s){return s.replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];});}
function render(nets){
  if(!nets.length){listEl.innerHTML='<div class="spin">No se han encontrado redes.</div>';return;}
  listEl.innerHTML='';
  nets.forEach(function(n){
    var d=document.createElement('div');d.className='net'+(n.ssid===selName?' sel':'');
    d.innerHTML='<span class="nm">'+esc(n.ssid)+'</span><span class="mt">'+(n.enc?'🔒':'')+bars(n.rssi)+'</span>';
    d.onclick=function(){selName=n.ssid;ssidEl.value=n.ssid;
      document.querySelectorAll('.net').forEach(function(e){e.classList.remove('sel');});
      d.classList.add('sel');document.getElementById('pass').focus();};
    listEl.appendChild(d);
  });
}
function poll(){
  fetch('/scan').then(function(r){return r.json();}).then(function(j){
    if(j.networks&&j.networks.length)render(j.networks);
    else if(j.scanning)listEl.innerHTML='<div class="spin">Buscando redes…</div>';
    else listEl.innerHTML='<div class="spin">No se han encontrado redes.</div>';
    if(j.scanning)setTimeout(poll,1500);
  }).catch(function(){setTimeout(poll,2000);});
}
document.getElementById('rescan').onclick=function(){
  listEl.innerHTML='<div class="spin">Buscando redes…</div>';
  fetch('/rescan').then(function(){setTimeout(poll,1200);});
};
document.getElementById('f').onsubmit=function(ev){
  ev.preventDefault();
  if(!ssidEl.value){msgEl.className='msg err';msgEl.textContent='Elige o escribe una red.';return;}
  var btn=document.getElementById('save');btn.disabled=true;btn.textContent='Guardando…';
  var body=new URLSearchParams();body.set('ssid',ssidEl.value);body.set('pass',document.getElementById('pass').value);body.set('host',hostEl.value);
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body.toString()})
   .then(function(r){return r.json();}).then(function(j){
     if(j.ok){msgEl.className='msg ok';
       msgEl.textContent='¡Guardado! El dispositivo se reiniciará y se conectará a "'+j.ssid+'". Ya puedes cerrar esta página.';}
     else{msgEl.className='msg err';msgEl.textContent=j.error||'No se pudo guardar.';btn.disabled=false;btn.textContent='Guardar y conectar';}
   }).catch(function(){
     // El reinicio puede cortar la respuesta: lo tratamos como éxito probable.
     msgEl.className='msg ok';msgEl.textContent='Guardado. El dispositivo se está reiniciando…';
   });
};
poll();
</script></body></html>)HTML";

// Escapa un SSID para incrustarlo en JSON (comillas, backslash y controles).
void appendJsonString(String& out, const String& s) {
    out += '"';
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if ((uint8_t)c < 0x20) { out += ' '; }  // controles → espacio
        else out += c;
    }
    out += '"';
}

// Sanitiza un hostname a una etiqueta DNS/mDNS válida: minúsculas, solo
// [a-z0-9-], sin guion al principio/final, máx 24 chars. Devuelve "" si tras
// limpiar no queda nada (el llamante usa entonces el default). Escrito a `out`
// (cap incluye el '\0').
void sanitizeHostname(const char* in, char* out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < cap - 1 && j < 24; ++i) {
        char c = in[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');  // a minúsculas
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) continue;
        if (c == '-' && j == 0) continue;  // sin guion inicial
        out[j++] = c;
    }
    while (j > 0 && out[j - 1] == '-') --j;  // sin guion final
    out[j] = '\0';
}

}  // namespace

WifiProvisioning::~WifiProvisioning() {
    delete server_;
    delete dns_;
}

// -----------------------------------------------------------------------------
void WifiProvisioning::begin(const char* apName) {
    strncpy(apSsid_, apName, sizeof(apSsid_) - 1);

    // AP_STA: el AP sirve el portal y la parte STA permite escanear redes
    // cercanas sin tumbar el AP.
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(kApIp, kApGw, kApMask);
    WiFi.softAP(apSsid_);                 // AP abierto (sin password)
    WiFi.softAPIP().toString().toCharArray(portalIp_, sizeof(portalIp_));

    LOG_INFO("[provision] AP '%s' activo — portal en http://%s", apSsid_, portalIp_);

    // DNS cautivo: responde a cualquier dominio con la IP del portal, para que
    // el móvil abra el "iniciar sesión en la red" automáticamente.
    dns_ = new DNSServer();
    dns_->setErrorReplyCode(DNSReplyCode::NoError);
    dns_->start(kDnsPort, "*", kApIp);

    server_ = new AsyncWebServer(80);
    setupRoutes_();
    server_->begin();

    // Primer escaneo asíncrono.
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
    scanInFlight_ = true;
    active_       = true;
}

// -----------------------------------------------------------------------------
void WifiProvisioning::setupRoutes_() {
    // Portal.
    server_->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", kPortalHtml);
    });

    // Lista de redes del último escaneo (JSON).
    server_->on("/scan", HTTP_GET, [this](AsyncWebServerRequest* req) {
        req->send(200, "application/json", scanJson_);
    });

    // Fuerza un nuevo escaneo (lo dispara loop() cuando el actual termina).
    server_->on("/rescan", HTTP_GET, [this](AsyncWebServerRequest* req) {
        rescanRequested_ = true;
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // Config actual: hostname mDNS efectivo (NVS o OTA_HOSTNAME) para prefijar
    // el formulario.
    server_->on("/cfg", HTTP_GET, [](AsyncWebServerRequest* req) {
        char host[33];
        WifiCredentials::effectiveHostname(host, sizeof(host), OTA_HOSTNAME);
        String body = "{\"host\":";
        appendJsonString(body, String(host));
        body += "}";
        req->send(200, "application/json", body);
    });

    // Guardado de credenciales. Solo copia los valores y marca savePending_;
    // la escritura en NVS y el reinicio los hace loop() (ver header).
    server_->on("/save", HTTP_POST, [this](AsyncWebServerRequest* req) {
        if (!req->hasParam("ssid", true) || req->getParam("ssid", true)->value().isEmpty()) {
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"SSID vacío\"}");
            return;
        }
        String ssid = req->getParam("ssid", true)->value();
        String pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : String("");
        String host = req->hasParam("host", true) ? req->getParam("host", true)->value() : String("");

        strncpy(pendingSsid_, ssid.c_str(), sizeof(pendingSsid_) - 1);
        pendingSsid_[sizeof(pendingSsid_) - 1] = '\0';
        strncpy(pendingPass_, pass.c_str(), sizeof(pendingPass_) - 1);
        pendingPass_[sizeof(pendingPass_) - 1] = '\0';
        // Hostname sanitizado a etiqueta mDNS válida. Vacío → se guarda vacío y
        // WifiCredentials seguirá usando el default (OTA_HOSTNAME).
        sanitizeHostname(host.c_str(), pendingHost_, sizeof(pendingHost_));
        savePending_ = true;  // loop() lo recoge (password NUNCA se loguea)

        String body = "{\"ok\":true,\"ssid\":";
        appendJsonString(body, ssid);
        body += "}";
        req->send(200, "application/json", body);
    });

    // Portal cautivo: cualquier otra ruta devuelve la página (dispara el popup
    // de "iniciar sesión en la red" en iOS/Android).
    server_->onNotFound([](AsyncWebServerRequest* req) {
        req->send(200, "text/html", kPortalHtml);
    });
}

// -----------------------------------------------------------------------------
void WifiProvisioning::buildScanJson_(int found) {
    String out;
    out.reserve(kScanJsonCap);
    out = "{\"scanning\":false,\"networks\":[";
    int n = (found > 0) ? found : 0;
    bool first = true;
    for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;  // oculta/anónima — no se puede teclear a ciegas
        if (!first) out += ',';
        first = false;
        out += "{\"ssid\":";
        appendJsonString(out, ssid);
        out += ",\"rssi\":";
        out += String((int)WiFi.RSSI(i));
        out += ",\"enc\":";
        out += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "false" : "true";
        out += '}';
        if (out.length() > kScanJsonCap - 128) break;  // no desbordar el buffer
    }
    out += "]}";
    strncpy(scanJson_, out.c_str(), kScanJsonCap - 1);
    scanJson_[kScanJsonCap - 1] = '\0';
}

// -----------------------------------------------------------------------------
void WifiProvisioning::loop() {
    if (!active_) return;

    dns_->processNextRequest();

    // Guardado diferido pedido por el handler /save.
    if (savePending_) {
        savePending_ = false;
        WifiCredentials::save(pendingSsid_, pendingPass_, pendingHost_);
        memset(pendingPass_, 0, sizeof(pendingPass_));  // no dejar la password en RAM
        LOG_INFO("[provision] credenciales guardadas — reiniciando en 1.5 s");
        rebootAtMs_ = millis() + 1500;  // margen para que la respuesta HTTP salga
    }

    // Reinicio diferido tras guardar.
    if (rebootAtMs_ != 0 && (int32_t)(millis() - rebootAtMs_) >= 0) {
        LOG_INFO("[provision] reiniciando para conectar en modo cliente");
        delay(50);
        ESP.restart();
    }

    // Gestión del escaneo asíncrono.
    int sc = WiFi.scanComplete();
    if (scanInFlight_ && sc >= 0) {
        buildScanJson_(sc);
        WiFi.scanDelete();
        scanInFlight_ = false;
        LOG_INFO("[provision] escaneo completado: %d redes", sc);
    } else if (scanInFlight_ && sc == WIFI_SCAN_FAILED) {
        // Reintento silencioso: dejar scanning=true para que la UI siga sondeando.
        scanInFlight_ = false;
        rescanRequested_ = true;
    }

    if (rescanRequested_ && !scanInFlight_) {
        rescanRequested_ = false;
        strncpy(scanJson_, "{\"scanning\":true,\"networks\":[]}", kScanJsonCap - 1);
        WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
        scanInFlight_ = true;
    }
}

#endif  // WIFI_PROVISIONING && ARDUINO_ARCH_ESP32
#endif  // UNIT_TEST
