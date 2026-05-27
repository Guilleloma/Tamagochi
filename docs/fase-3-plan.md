# Fase 3 — Web UI + WiFi + OTA (Plan de implementación)

> **Autor:** SuperDev (arquitectura)
> **Ejecutor:** backend-expert (y frontend-expert para `data/*.html|css|js`)
> **Branch:** `feature/fase-3-web-ui` (la crea cicd desde `main` antes de empezar)
> **Estimación:** 4-5 sesiones de desarrollo
> **Pre-requisitos:** Fase 2 + 2.5 + applyPlay mergeadas en `main` y verificadas en hardware

Este documento es la **fuente única de verdad** para implementar Fase 3. Los ejecutores deben seguirlo de arriba abajo, respetando el orden de hitos de la sección 11. Cualquier desviación se consulta con Maestro antes de codear.

---

## 0. Resumen ejecutivo

Fase 3 saca el firmware del modo "isla offline" y lo conecta al mundo:

- **WiFi STA** no bloqueante con credenciales hardcoded en `include/secrets.h` (gitignored). Si falla, el Tamagochi sigue funcional offline. Sin portal cautivo (eso era Fase 1.5, que se ha plegado en Fase 3 para no duplicar trabajo de provisioning).
- **ArduinoOTA** con password obligatoria. Cierra el hito 8 diferido de Fase 1.
- **mDNS** publica el dispositivo como `tamagochi.local` para que el móvil lo encuentre sin conocer la IP.
- **AsyncWebServer + ESPAsyncTCP** sirviendo:
  - **SPA estática** desde LittleFS (`/`, `/app.css`, `/app.js`, `/favicon.ico`) — caché agresiva
  - **REST API** `/api/v1/*` (state + 7 acciones del dominio + `/info`) con formato de error estándar `{error:{code,message}}` y status codes correctos
- **HTTP Basic Auth** sobre todo `/api/v1/*` y, opcionalmente, sobre la raíz. Credenciales en `secrets.h`.
- **Rate limiting** token-bucket por IP, en memoria, suficiente para frenar clientes lentos o bucles accidentales (no DDoS real).
- **Indicador WiFi** en OLED (icono 8×6 px en una esquina) — sin perturbar el sprite principal.
- **Indicador OTA** ya existe en `DisplayRenderer::drawOtaProgress_()` desde Fase 1 (se reutiliza).
- **Tests host-native nuevos** sobre parsing/serialización JSON, validación de input, evaluación de auth, lógica de rate-limiter. No se testea el transporte HTTP ni LittleFS (Arduino-only).
- **Tests integración** con `curl` + scripts Python ejecutados desde el host contra el ESP en LAN.

Verificación de aceptación: el móvil controla al Tamagochi desde el navegador → cambios reflejados en OLED en ≤ 1 tick (≤ 200 ms); 100 req/min × 10 min sin reinicios, sin leak de heap (free heap final ≥ free heap inicial − 2 KB); con WiFi caído, el firmware sigue funcionando standalone; binario flasheado por OTA sin perder estado persistido.

---

## 1. Validaciones previas y decisiones críticas

Estas decisiones cierran ambigüedades del plan maestro **antes** de codear. Cambiarlas a mitad de Fase 3 cuesta refactor cruzado entre `WebUI`, `WifiManager` y `main.cpp`.

### 1.1. HTTP server: ESPAsyncWebServer vs ESP8266WebServer

**Decisión: `ESPAsyncWebServer` + `ESPAsyncTCP`.**

| Aspecto | ESPAsyncWebServer | ESP8266WebServer |
|---|---|---|
| Modelo | Event-driven, no bloquea el loop | Sincrónico, bloquea `loop()` mientras procesa request |
| Encaje con loop principal | Convive con `StatsEngine::tick()`, `LifeFSM::tick()`, `DisplayRenderer::render()` sin afectar FPS | Cada request de 30-80ms reduce FPS efectivo |
| Soporte de WebSockets | Sí, gratis (no se usa en Fase 3 — polling es suficiente) | Manual |
| Tamaño compilado | ~38 KB Flash adicionales | ~20 KB |
| Dependencia | `ESPAsyncTCP` (mantenida) | `Wire`/`ESP8266WiFi` (built-in) |
| Riesgos conocidos | Heap fragmentation con respuestas grandes y JSON dinámico | Bloqueo de loop documentado |

**Justificación:** el plan maestro especifica explícitamente AsyncWebServer (sección "Stack técnico"). Más importante: la regla `rules/error-handling.md` exige "Nunca dejar un proceso async en estado indefinido". Con servidor síncrono dentro de un loop que también corre LifeFSM 1Hz, render 5FPS y persist 5min, basta un cliente lento para arruinar la animación. El coste en Flash (~18 KB extra) cabe con holgura — Fase 2 cerró con ~30% de 1 MB, sobran 700 KB.

**Versión fijada:** `me-no-dev/ESPAsyncTCP@^1.2.2` y `me-no-dev/ESP Async WebServer@^1.2.3`. Ambas vivas en PlatformIO registry. Si el LDF no las resuelve por usar guion, declararlas como URL en `lib_deps`:

```ini
lib_deps =
    ; ... existentes ...
    https://github.com/me-no-dev/ESPAsyncTCP.git
    https://github.com/me-no-dev/ESPAsyncWebServer.git
```

### 1.2. WiFi credentials: política `secrets.h`

Reaprovecha el patrón establecido en Fase 1 para OTA y lo extiende. Cumple `rules/security.md` y `rules/git-workflow.md`.

| Aspecto | Política |
|---|---|
| Archivo de credenciales | `include/secrets.h` — **gitignored**, NO se commitea |
| Template versionado | `include/secrets.h.example` — checked-in, valores placeholder |
| Campos requeridos | `WIFI_SSID`, `WIFI_PASSWORD`, `OTA_PASSWORD`, `OTA_HOSTNAME`, `WEB_AUTH_USER`, `WEB_AUTH_PASSWORD` |
| Compilación sin `secrets.h` | `#error "Crea include/secrets.h desde secrets.h.example"` antes de cualquier `#include` en `WifiManager.cpp` y `WebUI.cpp` |
| Rotación de credenciales | Manual: editar `secrets.h`, recompilar, flashear (vía OTA si la red WiFi previa sigue válida) |
| Logging de credenciales | **Prohibido**. Log enmascarado: `LOG_INFO("WiFi SSID=%s pass=****", WIFI_SSID)` |

`secrets.h.example` debe documentar en comentarios:

```cpp
// WiFi STA — credenciales de la red doméstica
#define WIFI_SSID       "YourSSID"
#define WIFI_PASSWORD   "YourPassword"

// OTA — password obligatoria; cámbiala si te flashean el dispositivo
#define OTA_PASSWORD    "change-me-ota"
#define OTA_HOSTNAME    "tamagochi"          // mDNS → tamagochi.local

// HTTP Basic Auth para /api/v1/*
#define WEB_AUTH_USER     "tama"
#define WEB_AUTH_PASSWORD "change-me-web"
```

**Verificación:** QA debe correr `git ls-files | grep -i secrets.h$` antes de mergear. Solo debe aparecer `secrets.h.example`.

### 1.3. Política de WiFi degradado (no bloqueante)

**Regla de oro: si WiFi falla, el Tamagochi sigue jugable.** El usuario puede no tener Internet, puede estar de viaje, el router puede haberse muerto — nada de eso justifica un dispositivo bricked.

| Caso | Comportamiento |
|---|---|
| `secrets.h` SSID vacío | `WifiManager::begin()` no intenta conectar. Loguea INFO, marca `state = Disabled`. WebUI no se inicializa. OTA no se inicializa |
| Conexión OK | `state = Connected`. mDNS up. AsyncWebServer escuchando en 80. OTA en 8266 |
| Timeout 15 s sin conexión | `state = Disconnected`. Sigue intentando en background con backoff (60 s entre reintentos). LOG_WARN. WebUI **NO** se inicia hasta que conecte |
| Conexión perdida en runtime | Evento `WIFI_DISCONNECTED`. WebUI permanece levantado (clientes obtendrán timeout natural). Si vuelve `WIFI_CONNECTED`, reanudar mDNS sin reiniciar AsyncWebServer |
| Sin DHCP/IP nunca | Tras 3 ciclos de timeout, abandona y queda en `state = Disconnected` con reintentos cada 5 min hasta el siguiente reset |

**Bloqueante en setup:** **0 ms**. `WifiManager::begin()` solo configura `WiFi.mode(STA)` + `WiFi.begin(SSID, PASS)` y arranca el ticker de poll. No espera. Cualquier inicialización dependiente (WebUI, OTA, mDNS) se hace en respuesta al evento `WIFI_CONNECTED`, no en `setup()`.

### 1.4. mDNS

Hostname desde `OTA_HOSTNAME` (default `tamagochi`). Levanta `MDNS.begin(OTA_HOSTNAME)` + `MDNS.addService("http", "tcp", 80)` solo cuando WiFi está `Connected`. Al desconectar, `MDNS.end()`.

Si dos dispositivos del mismo modelo conviven en la LAN, el segundo emite WARN y se autodesigna `tamagochi-<chipId>`. **Decisión:** **no** implementar la autonumeración en Fase 3 — un Tamagochi por hogar es el caso real. Documentar en README como limitación conocida.

### 1.5. Auth: HTTP Basic Auth

**Decisión: HTTP Basic Auth sobre HTTPS, salvo que HTTPS no exista (el caso real: ESP8266 sin TLS práctico).**

| Aspecto | Política |
|---|---|
| Algoritmo | RFC 7617 Basic Auth — base64(user:pass) en header `Authorization` |
| Endpoints protegidos | `/api/v1/*` (todos) |
| Endpoints abiertos | `/` (SPA index), `/app.css`, `/app.js`, `/favicon.ico` — el HTML es público, las acciones no |
| TLS | **No** se implementa HTTPS en Fase 3 (BearSSL en ESP8266 es viable pero consume ~25 KB RAM extra y complica el server). Documentar en README: "usar solo en LAN de confianza" |
| Cookies | **No**. Stateless. Cada request lleva su header |
| Rate limit ante fallo de auth | Sí — el bucket cuenta también 401s (sección 1.6) |
| Códigos | 401 con header `WWW-Authenticate: Basic realm="Tamagochi"` |

**Justificación:** OAuth/JWT son overkill para 1 usuario, 1 red doméstica. Basic Auth + LAN trust es suficiente. La regla `rules/security.md` exige "Tokens con expiración" — Basic Auth no tiene token, pero la credencial vive solo en LittleFS del propio dispositivo y en el navegador del móvil del usuario, no se transmite a terceros.

**Advertencia documentada:** si Guillermo expone el ESP a Internet por port-forward, Basic Auth sobre HTTP plain es inseguro. Solución correcta: VPN al hogar (Tailscale/WireGuard) — fuera de scope de Fase 3.

### 1.6. Rate limiting: token bucket por IP

**Decisión: token bucket simple en memoria, **por IP cliente** (no global), con capacidad pequeña.**

| Parámetro | Valor |
|---|---|
| Capacidad del bucket | **10 tokens** |
| Refill | **1 token cada 600 ms** (≈ 100 req/min sostenidos, ≈ 10 burst) |
| Almacenamiento | `RateLimiter::IpBucket buckets_[kMaxTrackedClients]` — array fijo, **sin malloc** |
| `kMaxTrackedClients` | **4** (suficiente para LAN doméstica: 1-2 móviles + 1 portátil) |
| Política al llenarse el array | LRU — eliminar el bucket menos reciente y dar uno nuevo al cliente entrante |
| Coste por request | O(kMaxTrackedClients) = O(4) — despreciable |
| Status code al rechazar | **429 Too Many Requests** + header `Retry-After: 1` |
| Conteo de fallos de auth | Sí — un 401 también consume token, para evitar brute-force de contraseñas |

Cumple `rules/security.md`: "Rate limiting en todos los endpoints públicos". El plan maestro pide 100 req/min × 10 min sin crash — capacidad 10 + refill 600ms = 100 req/min sostenibles por cliente. Cumple.

**No protege contra DDoS distribuido.** Documentado como limitación.

### 1.7. JSON: ArduinoJson v7 con `JsonDocument` estático

**Decisión: ArduinoJson 7.x. Cada handler usa un `JsonDocument` local en stack, con capacidad pre-calculada según el peor caso de su payload.**

| Aspecto | Política |
|---|---|
| Versión | `bblanchon/ArduinoJson@^7` |
| Heap fragmentation | Mitigada por **no usar `DynamicJsonDocument`** y dimensionar bien — todos los payloads cabe en `< 512 bytes` |
| Tamaño doc | Stack `JsonDocument doc;` (v7 ya gestiona internamente; antes era `StaticJsonDocument<N>`) |
| Errores de parse | 400 BAD_JSON con `error.code = "BAD_JSON"` y `error.message` con detalle |
| Errores de schema | 422 INVALID_PAYLOAD con `error.code` específico (ver sección 4.4) |
| Floats | Evitar — todos los stats son `uint8_t`. Si se introduce un float (RSSI), serializar con 1 decimal |

### 1.8. Frontend: HTML/CSS/JS en LittleFS

**Decisión: SPA estática servida desde LittleFS (no embebida en PROGMEM).**

| Razón | Detalle |
|---|---|
| Iteración rápida | `pio run -t uploadfs` sube solo `data/` sin reflashear el firmware (12 s vs 60 s) |
| Tamaño | HTML+CSS+JS minificados objetivo **< 20 KB total**. LittleFS partition por defecto en ESP8266 con 4MB Flash da ~2.9 MB libres — con persistencia rotativa ocupando ~256 bytes, sobra holgura |
| Caché | Headers `Cache-Control: public, max-age=3600` para `app.css`, `app.js`, `favicon.ico`. `index.html` sin caché (`Cache-Control: no-cache`) para poder forzar actualización del front sin re-flashear |
| Compresión | **No** se sirve gzip — el ahorro de ~3 KB no compensa la complejidad de pre-comprimir en build. Si en algún momento el front pasa de 20 KB, activar `AsyncWebServer::setStaticHandler().setLastModified()` + servir `.gz` |
| Encoding | UTF-8 servido con `Content-Type: text/html; charset=utf-8` para que los strings españoles renderen bien |

`data/index.html` se versiona en git (no es secreto). El cargado a LittleFS es manual con `pio run -t uploadfs` o automático en CI (Fase 4).

### 1.9. Naming convention (heredado)

Sin cambios respecto a Fases 1-2. Lo aplicable en Fase 3:

| Elemento | Convención | Ejemplo Fase 3 |
|---|---|---|
| Módulos en `lib/infra/` | PascalCase | `lib/infra/WifiManager/` |
| Clases | PascalCase | `class WebUI`, `class RateLimiter` |
| Métodos y variables | camelCase | `bool isWifiConnected()`, `uint8_t bucketsUsed_` |
| Constantes C++ | `k` + PascalCase | `constexpr uint16_t kMaxJsonDoc = 512;` |
| Endpoints REST | kebab-case en URL, camelCase en JSON | `/api/v1/state`, `{ "ageMinutes": 42 }` |
| Códigos de error | UPPER_SNAKE_CASE | `INVALID_AMOUNT`, `WIFI_DISABLED` |

### 1.10. Política de errores (heredado, extendido)

Cumple `rules/error-handling.md` y `rules/api-conventions.md`:

- **Validar todo input** del cliente antes de tocar dominio (`amount` entre 1-100, `action` válida, JSON parseable).
- **Formato estándar único**: `{"error": {"code": "...", "message": "...", "details": []}}`. Sin excepciones.
- **Nunca exponer stack traces** ni nombres de funciones internas.
- **Loguear todo error 4xx** (básico: timestamp + IP + path + code) y todo 5xx (detallado: + heap libre + stack si aplica).
- **Nunca swallow**: si `MDNS.begin()` devuelve false, LOG_WARN y seguir (mDNS es opcional, no crítico).

### 1.11. Política de testing (heredado, extendido)

Tests **obligatorios** para mergear Fase 3:

- **JsonEncoder/Decoder unitario**: serializar `Stats` a JSON → parsear de vuelta → comparar campo a campo.
- **Validador de payloads**: para cada endpoint POST, 1 happy path + ≥ 1 error case (missing field, wrong type, out-of-range).
- **RateLimiter**: 11 requests consecutivos del mismo cliente → el 11º se rechaza; tras 600 ms el contador rellena; LRU evicta al menos reciente cuando se supera `kMaxTrackedClients`.
- **AuthChecker**: header válido pasa; header inválido falla; ausencia falla; user válido + pass inválido falla y consume token.

Lo que **NO** se testea host-native:
- `WifiManager` (depende de `ESP8266WiFi.h` Arduino-only).
- `AsyncWebServer` transporte HTTP (Arduino + lwIP).
- `OTAManager` (depende de `ArduinoOTA.h`).
- LittleFS real (se mockea con `IStorage`, ya hecho en Fase 1).

Para los módulos Arduino-only se hace **verificación manual en hardware** + scripts de integración Python (sección 10).

Framework: Unity, igual que Fases 1-2.

---

## 2. Árbol completo de archivos

### 2.1. Nuevos

```
Tamagochi/
├── include/
│   ├── config.h                                [MODIFICAR] añadir constantes WiFi/Web/RateLimit
│   ├── secrets.h.example                       [MODIFICAR] añadir WEB_AUTH_USER, WEB_AUTH_PASSWORD
│   └── secrets.h                               [LOCAL] añadir nuevos campos (gitignored)
├── lib/
│   ├── infra/
│   │   ├── OTAManager/                         [YA EXISTE, MODIFICAR] activar en Fase 3
│   │   │   ├── OTAManager.h
│   │   │   └── OTAManager.cpp
│   │   ├── WifiManager/                        [NUEVO]
│   │   │   ├── WifiManager.h
│   │   │   └── WifiManager.cpp
│   │   └── WebUI/                              [NUEVO]
│   │       ├── WebUI.h
│   │       ├── WebUI.cpp                       [orquestación AsyncWebServer + handlers]
│   │       ├── JsonCodec.h                     [puro, testeable host]
│   │       ├── JsonCodec.cpp
│   │       ├── PayloadValidator.h              [puro, testeable host]
│   │       ├── PayloadValidator.cpp
│   │       ├── RateLimiter.h                   [puro, testeable host]
│   │       ├── RateLimiter.cpp
│   │       ├── AuthChecker.h                   [puro, testeable host]
│   │       └── AuthChecker.cpp
│   └── ui/
│       └── DisplayRenderer/                    [MODIFICAR] icono WiFi y reuso drawOtaProgress_
├── data/                                       [NUEVO directorio]
│   ├── index.html                              [SPA — frontend-expert]
│   ├── app.css                                 [estilos móvil-first]
│   ├── app.js                                  [polling + acciones]
│   └── favicon.ico                             [16×16 monocromo]
├── src/
│   └── main.cpp                                [MODIFICAR] cablear WifiManager, OTAManager, WebUI
├── test/
│   ├── test_json_codec/                        [NUEVO]
│   │   └── test_json_codec.cpp
│   ├── test_payload_validator/                 [NUEVO]
│   │   └── test_payload_validator.cpp
│   ├── test_rate_limiter/                      [NUEVO]
│   │   └── test_rate_limiter.cpp
│   └── test_auth_checker/                      [NUEVO]
│       └── test_auth_checker.cpp
├── tools/
│   ├── api_smoke.sh                            [NUEVO] curl-based smoke test
│   └── api_stress.py                           [NUEVO] 100 req/min × 10 min
├── platformio.ini                              [MODIFICAR] añadir libs async + env native deps
├── .gitignore                                  [VERIFICAR] confirma `include/secrets.h` listado
└── docs/
    └── fase-3-plan.md                          [este documento]
```

### 2.2. Tocados pero no rediseñados

- `lib/core/EventBus/EventBus.h` — añadir `EventType::WifiStateChanged`, `EventType::OtaStarted`, `EventType::OtaProgress`, `EventType::OtaEnded`.
- `lib/ui/DisplayRenderer/DisplayRenderer.{h,cpp}` — añadir método `setWifiState(WifiState)` y renderizado del icono (8×6 px en `R0`, esquina superior derecha).
- `lib/domain/StatsEngine` y `lib/domain/LifeFSM` — **no se tocan**. Las acciones (`applyFeed`, `applyPlay`, `applyClean`, etc.) ya están listas. WebUI solo las invoca.

### 2.3. Tamaño estimado de cada módulo

| Módulo | Header (líneas) | Implementación (líneas) | RAM estática | Flash |
|---|---|---|---|---|
| `WifiManager` | 60 | 180 | ~60 B | ~2 KB |
| `OTAManager` (activar) | 30 | 60 | ~40 B | ~1 KB |
| `WebUI` (orquestación) | 80 | 400 | ~120 B + AsyncWebServer (~3 KB) | ~6 KB |
| `JsonCodec` | 40 | 180 | 0 (stack) | ~2 KB |
| `PayloadValidator` | 50 | 220 | 0 | ~2 KB |
| `RateLimiter` | 60 | 140 | ~96 B (4 buckets × 24) | ~1 KB |
| `AuthChecker` | 30 | 80 | 0 | ~0.5 KB |
| AsyncWebServer + ESPAsyncTCP (libs) | — | — | ~4 KB | ~38 KB |
| **Total Fase 3** | ~350 | ~1260 | **~7-8 KB extra** | **~50-55 KB extra** |

Tras Fase 3, footprint estimado:
- **RAM**: 40% actual + ~10% Fase 3 (~50%) — sigue con ~40 KB libres
- **Flash**: 30% actual + ~5-6% (~36%) — sigue con ~650 KB libres

Margen amplio. Si se rebasara, los primeros candidatos a optimizar son: usar `AsyncWebServer::setStaticHandler` con archivos directamente (sin handlers custom), y/o usar `JsonDocument` más pequeños.

---

## 3. Arquitectura del módulo WebUI

### 3.1. Diagrama de dependencias

```
┌─────────────────────┐
│      main.cpp       │── construye singletons y cablea EventBus
└──────────┬──────────┘
           │
   ┌───────┴────────┬─────────────┬──────────────┐
   ▼                ▼             ▼              ▼
┌──────┐     ┌──────────┐   ┌──────────┐   ┌──────────┐
│WiFi  │     │  OTA     │   │ WebUI    │   │ Display  │
│Mgr   │     │  Manager │   │          │   │ Renderer │
└───┬──┘     └────┬─────┘   └────┬─────┘   └────┬─────┘
    │             │              │              │
    │    publica  │   publica    │ publica      │ subscribe
    ▼             ▼              ▼              │
┌────────────────────────────────────┐ ←────────┘
│            EventBus                │
└────────────────────────────────────┘
                ▲              │
                │ subscribe    │ publica acciones
                │              ▼
              ┌─────────────────────┐
              │  StatsEngine /      │
              │  LifeFSM            │
              └─────────────────────┘
```

**Principios clave:**

1. **WebUI no llama directamente a StatsEngine.** Cada acción HTTP se traduce en un `Event` y se publica al bus. `main.cpp` (o un suscriptor del bus) lo recoge y ejecuta la acción real en `StatsEngine`/`LifeFSM`. **Justificación**: mantiene la separación de capas (`infra` no conoce `domain`) y permite testear handlers HTTP sin instanciar dominio.

2. **Si `EventBus` resulta complejo de extender para llevar payloads**, alternativa: WebUI guarda punteros opaques a interfaces (`IGameActions*`) que main.cpp implementa con métodos que llaman a StatsEngine. Pero la opción 1 (EventBus) es coherente con lo que Fase 1-2 ya hacen.

3. **El estado se lee directamente** desde StatsEngine al construir el JSON de `/api/v1/state` — esa parte sí necesita acceso de lectura. Pattern: `WebUI` recibe `const StatsEngine&` y `const LifeFSM&` en su constructor. Solo lectura. No escribe.

### 3.2. API pública de `WebUI`

```cpp
class WebUI {
public:
    WebUI(EventBus& bus, const StatsEngine& engine, const LifeFSM& fsm);

    // Inicializa el AsyncWebServer y monta endpoints.
    // Debe llamarse SOLO cuando WiFi esté Connected.
    // Idempotente: si ya está running, no-op.
    void begin();

    // Para tear-down si WiFi se cae.
    // Cierra el server y libera handlers (mDNS se gestiona aparte).
    void end();

    bool isRunning() const { return running_; }

    // Llamado desde loop() — limpia buckets vencidos del rate limiter
    // (no es estrictamente necesario, pero evita que un bucket inactivo
    // se quede ocupando un slot indefinidamente)
    void loop();

private:
    void mountRoutes_();
    void handleGetState_(AsyncWebServerRequest* req);
    void handleGetInfo_(AsyncWebServerRequest* req);
    void handlePostAction_(AsyncWebServerRequest* req, const char* action);
    // ... etc
};
```

### 3.3. API pública de `WifiManager`

```cpp
enum class WifiState : uint8_t {
    Disabled = 0,    // secrets.h sin SSID
    Connecting = 1,  // intentando
    Connected = 2,
    Disconnected = 3 // intentó y falló, reintentará
};

class WifiManager {
public:
    WifiManager(EventBus& bus);

    // Non-blocking. Solo configura modo STA + WiFi.begin().
    // No espera resultado. El estado se publica vía EventBus.
    void begin();

    // Polea estado de conexión, reintentos con backoff, publica eventos.
    // Llamar cada ~500-1000 ms desde el loop principal.
    void loop();

    WifiState state() const { return state_; }
    int8_t rssi() const;           // -127 si no conectado
    IPAddress localIp() const;     // 0.0.0.0 si no conectado

    void forceReconnect();          // para debug serial
};
```

### 3.4. API pública de `OTAManager`

(Ya existe header de Fase 1 con la declaración. En Fase 3 se activa la implementación real.)

```cpp
class OTAManager {
public:
    OTAManager(EventBus& bus);

    // Llamar SOLO cuando WiFi.status() == WL_CONNECTED.
    // Configura hostname, password, callbacks.
    void begin();

    // Cada loop, < 1 ms si no hay actualización en curso.
    void loop();

    bool isUpdating() const { return updating_; }
};
```

Callbacks publican al bus: `OtaStarted`, `OtaProgress` (con porcentaje), `OtaError`, `OtaEnded`. `DisplayRenderer` (que ya tiene `drawOtaProgress_`) suscribe a estos eventos y pinta la pantalla de progreso.

### 3.5. Diagrama de secuencia (acción del usuario en el móvil)

```
Móvil          AsyncSrv      WebUI       PayloadValid    RateLimiter   AuthCheck      EventBus     StatsEngine
  │                │             │              │              │            │              │            │
  │─POST /feed────▶│             │              │              │            │              │            │
  │                │─handle()───▶│              │              │            │              │            │
  │                │             │─check()─────────────────────────────────▶│              │            │
  │                │             │◀────────────────────────────────────────│ 200 ok       │            │
  │                │             │─allow()──────────────────▶│              │              │            │
  │                │             │◀──────────────────────────│ 200 ok       │              │            │
  │                │             │─validate()──▶│              │            │              │            │
  │                │             │◀─────────────│ 200 ok       │            │              │            │
  │                │             │─publish(Feed)──────────────────────────────────────────▶│            │
  │                │             │                                                         │─applyFeed─▶│
  │                │             │                                                         │            │─tick()
  │                │             │                                                         │◀───────────│
  │                │             │ (no espera respuesta, devuelve 200 con state actual)    │            │
  │                │             │─snapshot────────────────────────────────────────────────────────────▶│
  │                │             │◀────────────────────────────────────────────────────────────────────│
  │                │◀─202────────│                                                         │            │
  │◀───────────────│ JSON state  │                                                         │            │
```

**Detalle importante:** la respuesta del POST devuelve **el estado tras aplicar la acción** (no solo "OK"). Así el frontend no tiene que hacer un polling extra inmediato. Status code **202 Accepted** (no 200) para señalar que la acción se aplicó al modelo pero el render del OLED aún no se ha sincronizado (sucede en el siguiente tick de `loop()`).

**Alternativa más simple:** devolver siempre **200 OK** con el snapshot. Decisión: **200**. Sobrediseñar con 202 vs 200 no aporta y simplifica el frontend.

### 3.6. Integración con renderer (icono WiFi)

`DisplayRenderer` ya soporta overlays asíncronos (Fase 2). Añadir:

- Sprite `kSpriteWifiOn` (8×6 px): pequeñas ondas concéntricas
- Sprite `kSpriteWifiOff` (8×6 px): la misma forma con una `X` cruzando

Posición: esquina **superior derecha** del display rotado `R1` (portrait, así que esa es físicamente la esquina superior cuando el OLED se ve por el lado correcto). Coordenadas concretas las define `DisplayRenderer.cpp` en función del layout actual.

`DisplayRenderer::setWifiState(WifiState)` actualiza el flag interno; el siguiente `render()` lo pinta encima del sprite base (no encima de overlays SICK/POOP — esos llevan prioridad sobre el icono Wi-Fi).

**Si WiFi está `Disabled` (sin SSID en secrets.h)**: no se pinta nada. El icono solo aparece si la intención era conectar.

---

## 4. REST API completa

### 4.1. Endpoints

| Método | Ruta | Auth | Descripción | Status éxito |
|---|---|---|---|---|
| GET | `/` | No | SPA index.html | 200 |
| GET | `/app.css` | No | Estilos | 200 |
| GET | `/app.js` | No | Lógica frontend | 200 |
| GET | `/favicon.ico` | No | Favicon 16×16 | 200 |
| GET | `/api/v1/state` | **Sí** | Snapshot completo | 200 |
| GET | `/api/v1/info` | **Sí** | Telemetría dispositivo | 200 |
| POST | `/api/v1/feed` | **Sí** | Alimentar | 200 |
| POST | `/api/v1/play` | **Sí** | Jugar | 200 |
| POST | `/api/v1/clean` | **Sí** | Limpiar caca | 200 |
| POST | `/api/v1/medicate` | **Sí** | Curar enfermedad | 200 |
| POST | `/api/v1/discipline` | **Sí** | Disciplinar | 200 |
| POST | `/api/v1/sleep` | **Sí** | Dormir on/off | 200 |
| POST | `/api/v1/heal` | **Sí** | Aplicar heal genérico | 200 |
| `*` | `/api/v1/*` (no listada) | — | Endpoint inexistente | 404 |

**Decisión consciente:** **no** se incluye `/api/v1/reset` (huevo nuevo manual) en Fase 3 — la resurrección automática de Fase 2 lo hace innecesario y dar un botón "reset" expone a accidentes. Si Guillermo lo quiere después, se añade en Fase 4-5.

### 4.2. GET `/api/v1/state` — request/response

**Request:**
- Headers: `Authorization: Basic <base64>`
- Body: vacío

**Response 200** (≤ 320 bytes):

```json
{
  "data": {
    "lifeStage": "Baby",
    "ageMinutes": 142,
    "hunger": 38,
    "happiness": 65,
    "health": 90,
    "discipline": 50,
    "weight": 12,
    "poopCount": 1,
    "isSick": false,
    "isSleeping": false,
    "attentionScore": 420,
    "deathCount": 0,
    "turbo": false
  },
  "meta": {
    "apiVersion": "v1",
    "timestamp": 1741234567
  }
}
```

- `lifeStage`: string serializado de `LifeStage` enum (`"Egg"`, `"Baby"`, ...).
- `timestamp`: `millis()` del dispositivo (no Unix epoch — sin NTP en Fase 3). Sirve para detectar reinicios desde el frontend.
- `turbo`: refleja `#ifdef TURBO_MODE` — el frontend muestra banner si true.

**Errores:** 401 (no auth), 429 (rate limit), 500 (error interno).

### 4.3. POST `/api/v1/feed` (y resto de acciones con `amount`)

**Request:**
- Headers: `Authorization`, `Content-Type: application/json`
- Body:

```json
{ "amount": 20 }
```

| Campo | Tipo | Rango | Default si omitido | Notas |
|---|---|---|---|---|
| `amount` | uint8 | 1-100 | **No** se asume default — campo obligatorio. Si falta → 422 `MISSING_FIELD` | Cumple `rules/error-handling.md`: "fail fast" |

**Response 200** (devuelve el `state` actualizado, mismo schema que `/api/v1/state`):

```json
{
  "data": { ...nuevo state... },
  "meta": { ... },
  "actionApplied": {
    "type": "feed",
    "amount": 20
  }
}
```

`actionApplied` solo aparece en respuestas a POST de acción. Sirve para que el frontend confirme qué se aplicó (útil cuando se hacen optimistic UI updates).

### 4.4. Endpoints sin `amount`

| Endpoint | Body request | Notas |
|---|---|---|
| `POST /api/v1/clean` | `{}` (vacío) o ausente | No requiere `amount` — `applyClean()` no lo toma |
| `POST /api/v1/medicate` | `{}` | Idem `applyMedicate()` |

**Política:** body ausente o JSON `{}` o `null` → válido. Cualquier JSON malformado → 400 BAD_JSON.

### 4.5. POST `/api/v1/sleep`

**Request:**
```json
{ "value": true }
```

| Campo | Tipo | Valores | Notas |
|---|---|---|---|
| `value` | bool | `true` / `false` | true = dormir, false = despertar |

**Response 200**: state actualizado + `actionApplied: { "type": "sleep", "value": true }`.

### 4.6. GET `/api/v1/info`

**Response 200** (≤ 200 bytes):

```json
{
  "data": {
    "firmwareVersion": "0x0300",
    "uptimeMs": 142567,
    "freeHeap": 38240,
    "maxFreeBlock": 30016,
    "wifiSsid": "MyHome",
    "wifiRssi": -54,
    "wifiIp": "192.168.1.42",
    "turbo": false,
    "buildDate": "2026-05-27"
  }
}
```

- `firmwareVersion`: el `#define FIRMWARE_VERSION` actual (sufijo `FE` indica turbo, ver Fase 2).
- `buildDate`: `__DATE__` reformateado a ISO 8601 (`YYYY-MM-DD`).

**Útil para:**
- Monitoreo manual de heap durante el stress test (sección 10).
- Diagnóstico remoto sin abrir el serial.

### 4.7. Formato de error estándar (cumple `rules/api-conventions.md`)

```json
{
  "error": {
    "code": "INVALID_AMOUNT",
    "message": "amount must be in range 1..100, got 150",
    "details": []
  }
}
```

| Código de error | HTTP | Cuándo |
|---|---|---|
| `BAD_JSON` | 400 | Body no es JSON válido o no parseable |
| `MISSING_FIELD` | 422 | Campo obligatorio ausente |
| `INVALID_AMOUNT` | 422 | `amount` fuera de rango o tipo incorrecto |
| `INVALID_VALUE` | 422 | Otros campos inválidos (sleep.value no bool, etc.) |
| `UNKNOWN_ACTION` | 404 | Endpoint POST no reconocido |
| `WIFI_DEGRADED` | 503 | Petición llegó pero WiFi caído durante procesamiento (raro pero posible) |
| `LIFE_FSM_BUSY` | 409 | Acción incompatible con estado actual (ej: feed cuando está sleeping) — **opcional**, ver §4.8 |
| `RATE_LIMITED` | 429 | Token bucket vacío |
| `UNAUTHORIZED` | 401 | Auth faltante o inválida |
| `INTERNAL_ERROR` | 500 | Excepción no controlada |

### 4.8. Acciones incompatibles con el estado (LIFE_FSM_BUSY)

**Decisión: en Fase 3 NO se bloquean acciones por estado.** Si el usuario manda `feed` mientras está sleeping, la acción se aplica igualmente (igual que en Fase 2 hace el botón). Mantener simetría entre web y botón.

Si más tarde se ve que es confuso, se introduce `LIFE_FSM_BUSY` (409) cuando aplique. Documentado pero no implementado.

### 4.9. Headers de respuesta comunes

| Header | Valor |
|---|---|
| `Content-Type` | `application/json; charset=utf-8` (API) / `text/html; charset=utf-8` (`/`) |
| `Server` | `Tamagochi/0.4.0` (oculta detalles internos — cumple `security.md`) |
| `Cache-Control` | `no-cache` (API) / `public, max-age=3600` (estáticos) |
| `WWW-Authenticate` | `Basic realm="Tamagochi"` (solo en 401) |
| `Retry-After` | `1` (solo en 429) |

---

## 5. Frontend HTML/CSS/JS (`data/`)

Lo ejecuta **frontend-expert** en paralelo o en serie. La especificación queda aquí para que no requiera más briefing.

### 5.1. Layout móvil-first

```
┌─────────────────────────┐
│  Tamagochi  [Wi-Fi: ✓]  │ ← header con indicador conexión
├─────────────────────────┤
│                         │
│   ┌───────────────┐     │
│   │   Sprite      │     │ ← placeholder (no se renderiza
│   │   stage       │     │   el sprite real — solo etiqueta)
│   └───────────────┘     │
│   Stage: Baby           │
│   Age: 142 min          │
│                         │
├─────────────────────────┤
│  Stats                  │
│  Hunger:    ██████░░    │
│  Happiness: ████████    │
│  Health:    █████████   │
│  Discipline:████░░░░    │
│  Weight: 12   Poop: 1   │
│  [SICK]                 │ ← badge si isSick
├─────────────────────────┤
│  [Feed] [Play] [Clean]  │ ← 7 botones grid 2×4 (último vacío)
│  [Med]  [Disc] [Sleep]  │
│  [Heal]                 │
├─────────────────────────┤
│ Last update: 1s ago     │ ← polling status
│ [Offline]               │ ← si polling falla
└─────────────────────────┘
```

### 5.2. Polling

- **GET `/api/v1/state`** cada **3 s** mientras la pestaña esté visible (`document.visibilityState === "visible"`).
- Si **no** está visible (móvil bloqueado, pestaña en background), pausar polling — ahorra batería y datos.
- Si la pestaña vuelve a ser visible, request inmediato.

### 5.3. Backoff exponencial ante errores

- Error 1 → reintentar a los 3 s (normal).
- Error 2 consecutivo → 6 s.
- Error 3 → 12 s.
- Error 4-N → 30 s (cap).
- Cualquier respuesta exitosa resetea el contador.
- Mostrar badge `[Offline]` desde el error 2 en adelante.

### 5.4. Modal de credenciales

La primera vez que el frontend recibe **401**, muestra modal:

```
"Tamagochi pide autenticación"
User: [___________]
Pass: [___________]
[OK] [Cancelar]
```

El navegador maneja Basic Auth nativamente. El JS solo necesita capturar 401 y, si pasa, dejar que el navegador haga el reintento. Una vez aceptadas las credenciales por el usuario, el navegador las recuerda en sesión.

### 5.5. Botones de acción (7)

Cada botón hace POST al endpoint correspondiente:

| Botón | Endpoint | Body |
|---|---|---|
| Feed | `/api/v1/feed` | `{"amount":20}` |
| Play | `/api/v1/play` | `{"amount":15}` |
| Clean | `/api/v1/clean` | `{}` |
| Medicate | `/api/v1/medicate` | `{}` |
| Discipline | `/api/v1/discipline` | `{"amount":10}` |
| Sleep | `/api/v1/sleep` | `{"value":true}` (toggle con state local) |
| Heal | `/api/v1/heal` | `{"amount":20}` |

**Cantidades hardcoded por simplicidad**. En el futuro pueden parametrizarse en UI.

Al recibir la respuesta exitosa, **actualizar el state local con la respuesta** (no esperar al próximo poll). Optimistic UI.

### 5.6. Estilos (`app.css`)

- Mobile-first, target ancho ≥ 320 px (cualquier móvil moderno).
- Sin frameworks. CSS plano.
- Botones grandes touch-friendly (mínimo 44×44 px — Apple HIG).
- Paleta: 2-3 colores planos (sin gradients) para que pese poco.
- Font: `system-ui, sans-serif`.

**Peso objetivo:** HTML < 4 KB, CSS < 4 KB, JS < 8 KB. Total < 20 KB sin minificar.

---

## 6. Integración con OLED (icono WiFi)

### 6.1. Sprites nuevos

Añadir a `include/sprites_generated.h` (regenerar con `tools/png_to_xbm.py`):

```cpp
extern const uint8_t kSpriteWifiOn[]  PROGMEM;   // 8×6 px (6 bytes)
extern const uint8_t kSpriteWifiOff[] PROGMEM;   // 8×6 px (6 bytes)
```

Diseño: 3 arcos concéntricos pequeños (Wi-Fi clásico). Off: el mismo con una `X` superpuesta.

### 6.2. Layout

El display está en `U8G2_R1` (portrait, 64×128 efectivo). Esquina **superior derecha** = (56, 0) en coordenadas del buffer rotado.

`DisplayRenderer::render_()`:

```cpp
// Después de pintar el sprite base + overlays existentes:
if (wifiState_ != WifiState::Disabled) {
    const uint8_t* sprite = (wifiState_ == WifiState::Connected)
        ? kSpriteWifiOn
        : kSpriteWifiOff;
    u8g2_.drawXBMP(56, 0, 8, 6, sprite);
}
```

**Política de blink:**
- `Connected` → icono fijo.
- `Connecting` → blink 500ms on / 500ms off.
- `Disconnected` → icono `WifiOff` fijo.
- `Disabled` → no se pinta nada.

### 6.3. OTA progress (reutilizar)

Ya existe `DisplayRenderer::drawOtaProgress_(uint8_t percent)` (Fase 1). En Fase 3 se conecta:

`OTAManager` publica `OtaProgress` cada 5% de progreso. `DisplayRenderer` suscribe y:

1. Cambia a `Scene::OtaProgress`.
2. Pinta sprite de "actualizando" + barra de progreso.
3. Cuando recibe `OtaEnded`, restaura escena anterior (pero ESP probablemente reinicie justo después — eso es esperado).

### 6.4. Interacción con prioridades de overlay (Fase 2)

Recordatorio de prioridades de Fase 2: `DEAD > SICK > POOP > SLEEPING > base`. El **icono WiFi NO** es un overlay full-screen — es un sprite de 8×6 en una esquina. **No** interfiere con la cola de prioridades. Se pinta **siempre** (sobre cualquier escena), excepto en `Scene::OtaProgress` donde se oculta para no ensuciar la pantalla de update.

---

## 7. WiFi non-blocking — detalles de implementación

### 7.1. Flujo en `WifiManager::begin()`

```cpp
void WifiManager::begin() {
    if (strlen(WIFI_SSID) == 0) {
        state_ = WifiState::Disabled;
        LOG_INFO("[wifi] SSID empty in secrets.h — WiFi disabled");
        return;
    }

    WiFi.persistent(false);              // no escribe creds en flash interna del ESP
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.hostname(OTA_HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    state_ = WifiState::Connecting;
    connectStartedAtMs_ = millis();
    LOG_INFO("[wifi] connecting to SSID=%s", WIFI_SSID);
    // NO esperamos aquí. El loop() polea.
}
```

### 7.2. Flujo en `WifiManager::loop()`

```cpp
void WifiManager::loop() {
    uint32_t now = millis();
    if (now - lastPollMs_ < kWifiPollIntervalMs) return;
    lastPollMs_ = now;

    wl_status_t st = WiFi.status();
    WifiState desired = state_;

    if (st == WL_CONNECTED) {
        desired = WifiState::Connected;
    } else if (state_ == WifiState::Connecting) {
        if (now - connectStartedAtMs_ > kWifiConnectTimeoutMs) {
            // 15s timeout → marcar disconnected, reintentar más tarde
            desired = WifiState::Disconnected;
            disconnectAtMs_ = now;
        }
    } else if (state_ == WifiState::Disconnected) {
        if (now - disconnectAtMs_ > kWifiRetryBackoffMs) {
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            desired = WifiState::Connecting;
            connectStartedAtMs_ = now;
        }
    } else if (state_ == WifiState::Connected && st != WL_CONNECTED) {
        // perdimos conexión en runtime
        desired = WifiState::Disconnected;
        disconnectAtMs_ = now;
    }

    if (desired != state_) {
        LOG_INFO("[wifi] state %u → %u", (unsigned)state_, (unsigned)desired);
        state_ = desired;
        bus_.publish({EventType::WifiStateChanged, (uint16_t)state_});
    }
}
```

Constantes:
- `kWifiPollIntervalMs = 500`
- `kWifiConnectTimeoutMs = 15000`
- `kWifiRetryBackoffMs = 60000`

### 7.3. Reacción de WebUI/OTA a eventos

En `main.cpp`:

```cpp
bus.subscribe(EventType::WifiStateChanged, [&](const Event& e){
    WifiState s = (WifiState)(e.payload & 0xFF);
    renderer.setWifiState(s);
    if (s == WifiState::Connected) {
        if (!webUI.isRunning()) webUI.begin();
        ota.begin();
        MDNS.begin(OTA_HOSTNAME);
        MDNS.addService("http", "tcp", 80);
    } else {
        // Cualquier estado distinto de Connected
        if (webUI.isRunning()) webUI.end();
        // OTA y mDNS se quedan apagados pasivamente — sus loop() no harán nada útil
    }
});
```

### 7.4. Cooperatividad con `loop()` principal

`WifiManager::loop()` se llama cada vuelta. Su trabajo real (`if (now - lastPollMs_ < kWifiPollIntervalMs) return;`) cuesta < 1µs en el 99% de las veces. En el 1%, hace `WiFi.status()` (~5-50µs) y publica un evento.

`AsyncWebServer` no necesita `loop()` — su procesamiento es event-driven dentro de lwIP/ESPAsyncTCP. Cero coste de polling.

`OTAManager::loop()` → `ArduinoOTA.handle()` — cuesta < 5µs sin actualización en curso.

**Coste total Fase 3 por vuelta de loop():** **< 10µs**. Render sigue a 5 FPS sin problema.

---

## 8. OTA — activación

### 8.1. Activar `OTAManager`

Header ya existe de Fase 1. La implementación en Fase 1 quedó en stub. En Fase 3 se completa:

```cpp
void OTAManager::begin() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);   // NO el plain — ver §8.2
    ArduinoOTA.setPort(8266);

    ArduinoOTA.onStart([this]() {
        updating_ = true;
        bus_.publish({EventType::OtaStarted, 0});
        LOG_INFO("[ota] update started");
    });
    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
        uint8_t pct = (uint8_t)((progress * 100u) / total);
        bus_.publish({EventType::OtaProgress, pct});
    });
    ArduinoOTA.onEnd([this]() {
        updating_ = false;
        bus_.publish({EventType::OtaEnded, 0});
        LOG_INFO("[ota] update ended");
    });
    ArduinoOTA.onError([this](ota_error_t error) {
        updating_ = false;
        bus_.publish({EventType::OtaError, (uint16_t)error});
        LOG_ERROR("[ota] error %u", error);
    });

    ArduinoOTA.begin();
    LOG_INFO("[ota] ready hostname=%s.local", OTA_HOSTNAME);
}

void OTAManager::loop() {
    ArduinoOTA.handle();
}
```

### 8.2. Password en MD5

`ArduinoOTA.setPassword(plain)` acepta el plaintext (lo hashea internamente). Para evitar que el plaintext quede en el binario:

```cpp
ArduinoOTA.setPasswordHash(MD5(OTA_PASSWORD).toHexString());
```

**Decisión:** usar `setPassword(plain)` por simplicidad. El binario ya contiene `WEB_AUTH_PASSWORD` también — si un atacante tiene el binario, las dos passwords se asumen comprometidas. La regla `security.md` dice "no hardcodear" — `secrets.h` es la solución consensuada en Fase 1 (gitignored, fuera del repo).

### 8.3. Render del progreso OTA en OLED

Ya descrito en §6.3. `DisplayRenderer` suscribe a `OtaStarted/Progress/Ended/Error` y cambia a `Scene::OtaProgress`.

### 8.4. Compatibilidad con persistencia

ArduinoOTA flashea solo el segmento del firmware. **LittleFS no se toca** durante OTA. Por tanto, el estado persistido (PersistedState v2) sobrevive a la actualización. **Validar empíricamente**: flashear v0.4.0 sobre un dispositivo con un huevo vivo y comprobar que tras reset el huevo sigue.

**Excepción:** si la actualización cambia el schema (`version` salta de 2 a 3), `Persistence::load()` invocará `migrate_v2_to_v3()`. Eso es responsabilidad del schema, no de OTA. Fase 3 mantiene **schema v2 sin cambios**.

---

## 9. Tests host-native nuevos

### 9.1. `test/test_json_codec/test_json_codec.cpp`

Cubre serialización del state y deserialización de payloads.

| Test | Qué verifica |
|---|---|
| `serialize_state_produces_valid_json` | `JsonCodec::serializeState(stats, lifeFsm, out)` → JSON parseable, todos los campos presentes |
| `serialize_state_camel_case_fields` | Los campos del JSON están en camelCase (`ageMinutes`, no `age_minutes`) |
| `serialize_state_lifestage_as_string` | `lifeStage` se serializa como `"Baby"` y no como `1` |
| `serialize_state_below_buffer_limit` | El JSON resultante cabe en 512 bytes con todos los stats al máximo |
| `serialize_info_includes_required_fields` | `/api/v1/info` incluye firmwareVersion, uptimeMs, freeHeap, wifiRssi |
| `parse_feed_payload_valid` | `{"amount":20}` → estructura `{amount=20}` |
| `parse_feed_payload_missing_amount` | `{}` → error MISSING_FIELD |
| `parse_feed_payload_amount_out_of_range` | `{"amount":150}` → error INVALID_AMOUNT |
| `parse_feed_payload_amount_wrong_type` | `{"amount":"twenty"}` → error INVALID_AMOUNT |
| `parse_sleep_payload_valid_true` | `{"value":true}` → `value=true` |
| `parse_sleep_payload_valid_false` | `{"value":false}` → `value=false` |
| `parse_sleep_payload_invalid_value` | `{"value":42}` → error INVALID_VALUE |
| `parse_clean_accepts_empty_body` | `""` o `"{}"` → válido |
| `parse_malformed_json` | `"not json"` → error BAD_JSON |

### 9.2. `test/test_payload_validator/test_payload_validator.cpp`

Cubre las funciones puras de validación de cada acción.

| Test | Qué verifica |
|---|---|
| `validate_amount_in_range_accepts_1_50_100` | 1, 50, 100 son válidos |
| `validate_amount_rejects_0` | 0 → INVALID_AMOUNT |
| `validate_amount_rejects_101` | 101 → INVALID_AMOUNT |
| `validate_amount_rejects_negative` | -5 → INVALID_AMOUNT |
| `validate_action_name_known` | `"feed"`, `"play"`, ..., `"heal"` → ok |
| `validate_action_name_unknown` | `"foo"`, `"reset"` → UNKNOWN_ACTION |

### 9.3. `test/test_rate_limiter/test_rate_limiter.cpp`

| Test | Qué verifica |
|---|---|
| `bucket_starts_full` | Cliente recién visto tiene 10 tokens |
| `bucket_consumes_token_per_request` | Tras 5 requests, quedan 5 tokens |
| `bucket_refuses_when_empty` | Tras 10 requests consecutivos, el 11º se rechaza |
| `bucket_refills_at_rate` | Pasados 600 ms, +1 token disponible |
| `bucket_refills_capped_at_capacity` | Pasados 10 s, no se acumulan más de 10 tokens |
| `multiple_clients_independent` | IP A y IP B tienen buckets independientes |
| `lru_evicts_oldest_when_full` | Tras `kMaxTrackedClients` distintos, el siguiente IP evicta el bucket menos reciente |
| `eviction_does_not_punish_new_client` | El IP entrante tras eviction empieza con bucket lleno |
| `clock_jump_does_not_overflow` | Si `nowMs` salta hacia atrás (overflow `millis`), el bucket no se queda negativo |

**Tiempo mockeado:** `RateLimiter` recibe `uint32_t nowMs` en cada llamada (no llama a `millis()` directamente). En tests se pasa un contador controlado.

### 9.4. `test/test_auth_checker/test_auth_checker.cpp`

| Test | Qué verifica |
|---|---|
| `accepts_valid_basic_auth_header` | `"Basic <base64('tama:change-me-web')>"` con creds correctas → 200 |
| `rejects_missing_authorization` | header ausente → 401 |
| `rejects_malformed_basic_header` | `"Basic ???"` → 401 |
| `rejects_wrong_scheme` | `"Bearer xyz"` → 401 |
| `rejects_wrong_user` | `"Basic <base64('other:change-me-web')>"` → 401 |
| `rejects_wrong_password` | `"Basic <base64('tama:wrong')>"` → 401 |
| `constant_time_comparison` | Test indirecto: la implementación NO usa `strcmp` directamente (revisión código manual) — cumple security.md preventivamente. Si se quiere test real, medir tiempos con `assert_close` (out of scope) |

### 9.5. Actualización de `platformio.ini` para tests nuevos

```ini
[env:native]
; ... existente ...
build_flags =
    ; ... existentes ...
    -Ilib/infra/WebUI
build_src_filter =
    ; ... existente ...
    -<../lib/infra/WebUI/WebUI.cpp>       ; orquestación AsyncWebServer (Arduino-only)
    ; JsonCodec/PayloadValidator/RateLimiter/AuthChecker SI se compilan en native
```

### 9.6. Total estimado de tests

| Categoría | Fase 2 (post) | Fase 3 | Delta |
|---|---|---|---|
| StatsEngine + LifeFSM + Persistence | 73 | 73 | 0 (no cambia) |
| JsonCodec | 0 | 14 | +14 |
| PayloadValidator | 0 | 6 | +6 |
| RateLimiter | 0 | 9 | +9 |
| AuthChecker | 0 | 6 | +6 |
| **Total** | **73** | **~108** | **+35** |

**Target: ≥ 100 tests verdes al final de Fase 3.**

---

## 10. Tests de integración (curl + Python)

### 10.1. `tools/api_smoke.sh`

Bash + curl. Comprueba que cada endpoint responde con el código esperado.

```bash
#!/usr/bin/env bash
# Smoke test for /api/v1/* — requires Tamagochi reachable at $TAMAGOCHI_HOST
set -euo pipefail
HOST="${TAMAGOCHI_HOST:-tamagochi.local}"
AUTH="${TAMAGOCHI_AUTH:-tama:change-me-web}"
PASS=0; FAIL=0

assert_status() {
    local method=$1 path=$2 expected=$3 body=${4:-}
    local actual
    if [ -z "$body" ]; then
        actual=$(curl -s -o /dev/null -w "%{http_code}" -u "$AUTH" -X "$method" "http://$HOST$path")
    else
        actual=$(curl -s -o /dev/null -w "%{http_code}" -u "$AUTH" \
            -H "Content-Type: application/json" -d "$body" -X "$method" "http://$HOST$path")
    fi
    if [ "$actual" = "$expected" ]; then echo "OK  $method $path → $actual"; PASS=$((PASS+1))
    else echo "FAIL $method $path → expected $expected, got $actual"; FAIL=$((FAIL+1)); fi
}

assert_status GET /api/v1/state 200
assert_status GET /api/v1/info 200
assert_status POST /api/v1/feed 200 '{"amount":20}'
assert_status POST /api/v1/play 200 '{"amount":15}'
assert_status POST /api/v1/clean 200 '{}'
assert_status POST /api/v1/medicate 200 '{}'
assert_status POST /api/v1/discipline 200 '{"amount":10}'
assert_status POST /api/v1/sleep 200 '{"value":true}'
assert_status POST /api/v1/sleep 200 '{"value":false}'
assert_status POST /api/v1/heal 200 '{"amount":20}'

# Errores
assert_status POST /api/v1/feed 422 '{}'                    # MISSING_FIELD
assert_status POST /api/v1/feed 422 '{"amount":150}'        # INVALID_AMOUNT
assert_status POST /api/v1/feed 400 'not-json'              # BAD_JSON
assert_status GET /api/v1/state 401 ""                      # sin auth (curl sin -u)
assert_status POST /api/v1/foo 404 '{}'                     # UNKNOWN_ACTION

echo "─── Smoke test ─── PASS=$PASS FAIL=$FAIL ───"
exit $FAIL
```

### 10.2. `tools/api_stress.py`

Python 3, sin dependencias externas (solo `urllib`, `json`, `time`). Lanza 100 req/min × 10 min y reporta:

- Tasa real de éxito vs error
- Free heap inicial vs final (vía `GET /api/v1/info`)
- Tiempos de respuesta p50/p95/p99
- Si free heap final < heap inicial − 2 KB → **FAIL** (memory leak)
- Si alguna request devuelve 5xx → **FAIL**

Pseudocódigo:

```python
import urllib.request, json, time, statistics, base64, sys

HOST = "tamagochi.local"
AUTH = base64.b64encode(b"tama:change-me-web").decode()
HEADERS = {"Authorization": f"Basic {AUTH}"}

def call(method, path, body=None):
    req = urllib.request.Request(f"http://{HOST}{path}", method=method, headers=HEADERS)
    if body: req.add_header("Content-Type", "application/json"); req.data = body.encode()
    t0 = time.monotonic()
    resp = urllib.request.urlopen(req, timeout=5)
    return time.monotonic() - t0, resp.status, json.loads(resp.read())

initial = call("GET", "/api/v1/info")[2]["data"]
heap_start = initial["freeHeap"]
print(f"heap_start = {heap_start}")

samples_ms = []
errs = 0
for i in range(1000):                              # 100 req/min × 10 min = 1000
    try:
        dt, code, _ = call("POST", "/api/v1/feed", '{"amount":5}')
        if code >= 400: errs += 1
        samples_ms.append(dt * 1000)
    except Exception as e:
        errs += 1
    time.sleep(0.6)                                # ~100 req/min

final = call("GET", "/api/v1/info")[2]["data"]
heap_end = final["freeHeap"]
leak = heap_start - heap_end

print(f"errors = {errs}, leak = {leak} B")
print(f"p50 = {statistics.median(samples_ms):.1f} ms")
print(f"p95 = {statistics.quantiles(samples_ms, n=20)[18]:.1f} ms")
sys.exit(1 if (errs > 0 or leak > 2048) else 0)
```

### 10.3. Test de degradación WiFi

Manual + observación serial:

1. Arrancar el firmware con WiFi conectado. Hacer `curl /api/v1/state` → 200.
2. Apagar el router (o cambiar `WIFI_PASSWORD` a algo inválido).
3. Esperar 30 s. Verificar en serial: `[wifi] state Connected → Disconnected`.
4. Verificar OLED: icono cambia a `WifiOff`. Sprite del Tamagochi sigue animando.
5. Pulsar el botón físico (short/long). Verificar que las acciones desde el botón siguen funcionando.
6. Re-encender router. Esperar ≤ 60 s. Verificar `[wifi] state Disconnected → Connected` y reaparición de mDNS.

### 10.4. Test de OTA

1. `pio run -e esp12e -t upload --upload-port tamagochi.local`
2. Verificar prompt de password — introducir `OTA_PASSWORD`.
3. Durante upload, OLED debe mostrar pantalla `OtaProgress` con barra.
4. Tras reset automático, verificar que el huevo/baby sigue vivo (estado preservado).
5. Probar OTA con password incorrecta → debe fallar (negociación). En serie: `[ota] error 4` (auth failed).

---

## 11. Hitos / commits atómicos sugeridos

Cada commit < 500 líneas, compila con `pio run -e esp12e`, pasa `pio test -e native`. Orden:

| # | Tipo | Título | Archivos clave | Dependencias |
|---|---|---|---|---|
| 1 | Feature | `Feature: secrets.h.example actualizado + .gitignore + WifiManager + indicador OLED` | `include/secrets.h.example`, `.gitignore`, `lib/infra/WifiManager/*`, `lib/ui/DisplayRenderer/*`, `include/config.h`, `src/main.cpp` | — |
| 2 | Feature | `Feature: OTAManager activado con password y progress en OLED` | `lib/infra/OTAManager/*`, `lib/ui/DisplayRenderer/*` (suscribe a OtaProgress), `src/main.cpp` | #1 (WiFi conectado) |
| 3 | Feature + Test | `Feature: JsonCodec + PayloadValidator + tests host` | `lib/infra/WebUI/JsonCodec.*`, `lib/infra/WebUI/PayloadValidator.*`, `test/test_json_codec/*`, `test/test_payload_validator/*`, `platformio.ini` | #1 (LifeStage del dominio ya existe) |
| 4 | Feature + Test | `Feature: AuthChecker + RateLimiter + tests host` | `lib/infra/WebUI/AuthChecker.*`, `lib/infra/WebUI/RateLimiter.*`, `test/test_auth_checker/*`, `test/test_rate_limiter/*` | — |
| 5 | Feature | `Feature: WebUI con AsyncWebServer + endpoints GET /api/v1/{state,info}` | `lib/infra/WebUI/WebUI.*`, `platformio.ini` (libs async), `src/main.cpp` | #1, #3, #4 |
| 6 | Feature | `Feature: WebUI endpoints POST /api/v1/{feed,play,heal,clean,medicate,discipline,sleep}` | `lib/infra/WebUI/WebUI.cpp`, `lib/core/EventBus/EventBus.h` (eventos nuevos para acciones), `src/main.cpp` (handler que aplica al StatsEngine) | #5 |
| 7 | Feature | `Feature: frontend HTML/CSS/JS en LittleFS + serving estático + favicon` | `data/index.html`, `data/app.css`, `data/app.js`, `data/favicon.ico`, `lib/infra/WebUI/WebUI.cpp` (static handler) | #5 |
| 8 | Test | `Test: scripts integración api_smoke.sh + api_stress.py` | `tools/api_smoke.sh`, `tools/api_stress.py` | #7 |
| 9 | Docs | `Docs: README Fase 3 + plan cierre + sección frontend` | `README.md`, `docs/fase-3-plan.md` (notas finales) | — |

**Estrategia de PR:** una sola `feature/fase-3-web-ui → main` al final. cicd hace `--no-ff` + tag `v0.4.0-fase3`.

**Frontend en paralelo:** los commits #5-#6 son backend-expert; commit #7 es frontend-expert (o backend-expert si frontend-expert no está disponible). Pueden trabajarse en paralelo si Guillermo así lo decide.

---

## 12. Riesgos específicos de Fase 3

| # | Riesgo | Mitigación |
|---|---|---|
| 1 | **Heap fragmentation** con AsyncWebServer + JSON dinámicos tras horas de uptime | `JsonDocument` v7 en stack, no DynamicJsonDocument. Monitor `getFreeHeap()` + `getMaxFreeBlockSize()` cada 30 s (ya existe en Fase 1). Reset limpio si `freeHeap < 8 KB`. Stress test verifica leak < 2 KB en 10 min |
| 2 | **`secrets.h` commiteado por accidente** | `.gitignore` con `include/secrets.h` ya desde Fase 1. Verificar con `git check-ignore -v include/secrets.h` antes de cada PR. QA hook recomendado: `git ls-files \| grep -E 'secrets\.h$' \| grep -v example` debe estar vacío |
| 3 | **Race condition** entre `loop()` (lifeFSM.tick + statsEngine.tick + render) y handlers HTTP que aplican acciones desde otro contexto | ESP8266 es **single-core cooperativo**: AsyncWebServer ejecuta sus callbacks **entre `yield()`s** del loop principal, nunca preempt. Mientras los handlers no hagan I/O bloqueante (no lo hacen), no hay race real. Los handlers solo publican Events — la aplicación a StatsEngine sucede en el siguiente tick del subscriber, garantizando orden serial |
| 4 | **WebUI inicializado antes de WiFi** → crash en `AsyncWebServer.begin()` sin red | `WebUI::begin()` se invoca **solo** desde el handler de `WifiStateChanged → Connected`. Si nunca llega Connected, WebUI nunca arranca. Verificar en código que no hay `webUI.begin()` en `setup()` |
| 5 | **mDNS conflict** si hay dos Tamagochis en la misma LAN | Documentado en README como "un Tamagochi por hogar". No se implementa autonumeración en Fase 3 |
| 6 | **Frontend pesado** en LittleFS → cuesta tiempo subir + ocupa partition | Target < 20 KB sin minificar. Sin frameworks. Si crece, considerar minificar en `tools/` antes de `uploadfs` |
| 7 | **Rate limit no protege contra DDoS distribuido** | Documentado. Solución correcta sería un firewall/router que limite por IP — fuera de scope del firmware |
| 8 | **Basic Auth en HTTP plain** | Documentado en README: "uso solo en LAN de confianza". Si se necesita exposición a Internet → VPN (Tailscale), fuera de scope |
| 9 | **OTA flashea sobre estado inconsistente** durante una transición LifeFSM | Improbable: el flasheo lleva ~20 s, la transición es atómica en RAM. La persistencia rotativa (Fase 1) ya garantiza atomicidad — si el ESP se resetea a mitad de un `Persistence::save()`, la copia previa sigue íntegra |
| 10 | **`Cache-Control: max-age=3600` para estáticos** dificulta iteración sobre frontend | `index.html` lleva `no-cache`. Los demás archivos: Ctrl+Shift+R en el navegador fuerza recarga. En desarrollo aceptable |
| 11 | **`ESPAsyncWebServer` y `ESPAsyncTCP` están en mantenimiento limitado** (forks comunitarios) | Conocido. Si en Fase 4-5 surge un bug bloqueante, considerar migrar a `ESPAsyncWebServer` mantenido por `mathieucarbou` (drop-in compatible). Fijar versión por hash en lib_deps |
| 12 | **`hostname()` no funciona en algunos firmwares de routers** (no aparece en lista DHCP con `tamagochi`) | Workaround: usar IP directa. Documentar en README cómo descubrirla (vía router admin panel o `arp -a`) |
| 13 | **`__DATE__` en `firmwareInfo` cambia entre builds** rompiendo cualquier cacheo determinista | Aceptable — `buildDate` es un campo de debug, no se usa para lógica |
| 14 | **`secrets.h.example` quedó desactualizado en Fase 1** (sin WEB_AUTH_*) | Commit #1 actualiza el template. Verificar en QA que `secrets.h.example` contiene **todos** los nuevos `#define` |
| 15 | **`WiFi.persistent(false)` vs auto-reconnect** | Probado en Fase 1 sin problemas. La combinación `persistent(false) + setAutoReconnect(true)` es válida — auto-reconnect usa la última config en RAM, no la persistida |

---

## 13. Checklist final para los ejecutores

Antes de abrir PR (lo coordina QA + CI/CD):

- [ ] Todos los commits respetan formato `Prefix: descripción` (cumple `rules/git-workflow.md`)
- [ ] Ningún commit supera 500 líneas
- [ ] `pio run -e esp12e` compila limpio con `FIRMWARE_VERSION == 0x0300`
- [ ] `pio run -e esp12e_turbo` compila limpio con `FIRMWARE_VERSION == 0x03FE`
- [ ] `pio test -e native` pasa con **≥ 100 tests verdes**
- [ ] `git ls-files | grep -E 'secrets\.h$' | grep -v example` → vacío (no se commiteó secrets.h)
- [ ] `include/secrets.h.example` incluye **TODOS** los #define necesarios: WIFI_SSID, WIFI_PASSWORD, OTA_PASSWORD, OTA_HOSTNAME, WEB_AUTH_USER, WEB_AUTH_PASSWORD
- [ ] `tools/api_smoke.sh` pasa contra hardware en LAN (PASS=N, FAIL=0)
- [ ] `tools/api_stress.py` pasa: 1000 requests, errs=0, leak<2KB
- [ ] OLED muestra icono Wi-Fi correctamente (Connected/Disconnected)
- [ ] Frontend carga en móvil vía `http://tamagochi.local/`, recibe credenciales nativas, muestra stats, acciones funcionan
- [ ] Acción desde el móvil afecta al OLED en ≤ 1 s
- [ ] WiFi caído (router apagado) → Tamagochi sigue jugable con botón físico
- [ ] WiFi caído → icono OLED cambia a `WifiOff`
- [ ] OTA `pio run -t upload --upload-port tamagochi.local` funciona, password se valida, OLED muestra progreso
- [ ] Tras OTA, estado persistido se conserva (huevo/baby sigue vivo)
- [ ] README actualizado con: instrucciones de provisioning secrets.h, comandos OTA, lista endpoints, troubleshooting WiFi
- [ ] No hay `TODO crítico` sin justificar
- [ ] Ningún `#include <Arduino.h>` en los headers de `lib/infra/WebUI/{JsonCodec,PayloadValidator,RateLimiter,AuthChecker}.h` (deben ser puros host-testable)

Si **alguna** marca queda sin cumplir, no se mergea.

---

## 14. Lo que NO entra en Fase 3 (anti-scope creep)

- **WiFiManager** con portal cautivo → no necesario, `secrets.h` cumple el propósito.
- **`configTime()` / NTP** → diferido a Fase 4 si surge necesidad real (Fase 3 no usa wall-clock UTC para nada).
- **WebSockets** → polling es suficiente. WS añade complejidad sin ganancia clara.
- **HTTPS / TLS** → ESP8266 + BearSSL consume ~25 KB RAM. Documentado: LAN trust + futuro VPN.
- **PowerManager** (auto-dim, light sleep) → Fase 4.
- **OTA signed updates** → Fase 4 si se quiere endurecer.
- **Multi-Tamagochi LAN** / mDNS autonumeración → Fase 5.
- **Histórico CSV** / minijuegos / skins → Fase 5.
- **Endpoint `/api/v1/reset`** → no se ofrece manualmente. Resurrección automática (Fase 2) cubre el caso.
- **Internacionalización del frontend** → solo español, alineado con el firmware.
- **Métricas Prometheus / push a InfluxDB** → si emerge, futuro.

Si surge la tentación de meter algo "ya que estoy": **no**. Cada cosa en su fase.

---

## 15. Coordinación con otros agentes

- **frontend-expert:** entrega `data/index.html`, `data/app.css`, `data/app.js`, `data/favicon.ico` (commit #7). Sigue layout y polling de §5. **No bloqueante** para los commits #1-#6 (backend) — el backend puede testearse con curl.
- **backend-expert:** ejecuta commits #1-#6 + #8 + #9. Sigue este plan al pie de la letra.
- **qa:** invocado tras commits 1, 4, 5, 6, 7 (cada vez que algo es testeable) y al final para validación completa (smoke + stress + escenarios manuales en hardware).
- **cicd:** crea branch al inicio (`feature/fase-3-web-ui` desde `main`), gestiona merge `--no-ff` + tag `v0.4.0-fase3` al final. Borra branch local/remota tras merge.
- **product-manager (Atlas):** no requiere intervención salvo cambio de scope.
- **superdev (yo):** disponible si surge bloqueo arquitectónico — especialmente si el footprint de heap se acerca a límites o si AsyncWebServer da problemas de fragmentación bajo carga.

---

## 16. Métricas de éxito de Fase 3

Al mergear, el firmware debe demostrar:

| Métrica | Objetivo |
|---|---|
| Tests host-native verdes | ≥ 100 |
| Endpoints REST funcionales | 9 (state, info + 7 acciones) |
| Autenticación HTTP Basic | Funcional, 401 sin creds |
| Rate limiting | Token bucket 10/600ms operativo |
| Latencia HTTP p95 | ≤ 200 ms |
| Stress: 1000 reqs en 10 min | 0 errors, leak < 2 KB |
| OLED: icono WiFi | Funcional (3 estados visuales) |
| OTA con password | Flashea desde LAN, password obligatoria, OLED muestra progreso |
| Persistencia tras OTA | 100% — huevo sobrevive a la actualización |
| WiFi caído → standalone | Botón físico sigue funcionando, sin reinicios |
| RAM libre tras boot con WiFi+WebUI | ≥ 30 KB (≥ 50% del total) |
| Flash usado | ≤ 40% del total |
| Frontend size en LittleFS | ≤ 20 KB total |
| Tamagochi visible en `tamagochi.local` desde el móvil | Sí |

---

Fin del plan.
