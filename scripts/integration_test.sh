#!/usr/bin/env bash
# =============================================================================
# Tamagotchi — script de integracion HTTP (Fase 3 hito 8).
#
# Verifica los 8 endpoints de la API contra un dispositivo real.
# Usa solo curl + bash POSIX, sin dependencias externas.
#
# Uso:
#   HOST=tamagotchi.local AUTH_USER=admin AUTH_PASS=tu_pass ./scripts/integration_test.sh
#   HOST=192.168.x.x      AUTH_USER=admin AUTH_PASS=tu_pass ./scripts/integration_test.sh
#
# Cuidado: NO usar la variable USER del entorno; es el username del SO en macOS.
#
# Devuelve 0 si todos los tests pasan, 1 en caso contrario.
# =============================================================================

set -u  # tratar variables no definidas como error; NO -e (queremos seguir tras fallo)

HOST="${HOST:-tamagotchi.local}"
AUTH_USER="${AUTH_USER:-admin}"
AUTH_PASS="${AUTH_PASS:-}"
if [[ -z "$AUTH_PASS" ]]; then
    echo "Falta AUTH_PASS (la WEB_AUTH_PASSWORD de include/secrets.h)." >&2
    exit 1
fi
BASE="http://${HOST}"
AUTH="-u ${AUTH_USER}:${AUTH_PASS}"
TIMEOUT="${TIMEOUT:-15}"

pass=0
fail=0
failed_tests=()

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# expect_status <name> <expected_code> <curl_args...>
# Lanza curl con --write-out, compara solo el codigo HTTP.
expect_status() {
    local name="$1"; shift
    local expected="$1"; shift
    local got
    got=$(curl -s -o /dev/null --max-time "$TIMEOUT" -w "%{http_code}" "$@" || echo "000")
    if [ "$got" = "$expected" ]; then
        printf "  PASS  %-50s [%s]\n" "$name" "$got"
        pass=$((pass + 1))
    else
        printf "  FAIL  %-50s [exp %s, got %s]\n" "$name" "$expected" "$got"
        fail=$((fail + 1))
        failed_tests+=("$name")
    fi
}

# expect_body_contains <name> <expected_code> <substring> <curl_args...>
# Verifica codigo HTTP y que el body contenga el substring (jq-free).
expect_body_contains() {
    local name="$1"; shift
    local expected_code="$1"; shift
    local needle="$1"; shift
    local tmp
    tmp=$(mktemp)
    local got
    got=$(curl -s -o "$tmp" --max-time "$TIMEOUT" -w "%{http_code}" "$@" || echo "000")
    local body
    body=$(cat "$tmp")
    rm -f "$tmp"
    if [ "$got" = "$expected_code" ] && echo "$body" | grep -q "$needle"; then
        printf "  PASS  %-50s [%s, body~'%s']\n" "$name" "$got" "$needle"
        pass=$((pass + 1))
    else
        printf "  FAIL  %-50s [exp %s+'%s', got %s body=%s]\n" "$name" "$expected_code" "$needle" "$got" "${body:0:80}"
        fail=$((fail + 1))
        failed_tests+=("$name")
    fi
}

section() {
    echo
    echo "=== $1 ==="
}

# ---------------------------------------------------------------------------
# Pre-check: dispositivo accesible?
# ---------------------------------------------------------------------------
echo "Target: $BASE  (timeout=${TIMEOUT}s, user=${AUTH_USER})"
echo "Probing host..."
probe_code=$(curl -s -o /dev/null --max-time 5 -u "${AUTH_USER}:${AUTH_PASS}" -w "%{http_code}" "${BASE}/api/v1/state" || echo "000")
if [ "$probe_code" != "200" ]; then
    echo "ERROR: probe a ${BASE}/api/v1/state devolvio HTTP ${probe_code}."
    if [ "$probe_code" = "401" ]; then
        echo "  Credenciales rechazadas. Revisa AUTH_USER / AUTH_PASS."
    elif [ "$probe_code" = "000" ]; then
        echo "  Sin respuesta. Placa apagada, fuera del WiFi, o mDNS no resuelve."
        echo "  Prueba con la IP directa:  HOST=192.168.x.x $0"
    fi
    exit 1
fi
echo "OK, dispositivo responde (200)."

# ---------------------------------------------------------------------------
# 1. Autenticacion
# ---------------------------------------------------------------------------
section "Autenticacion"
expect_status "GET /api/v1/state sin auth -> 401"   401 "${BASE}/api/v1/state"
expect_status "GET /api/v1/state con auth -> 200"   200 $AUTH "${BASE}/api/v1/state"
expect_status "GET /api/v1/info sin auth -> 401"    401 "${BASE}/api/v1/info"
expect_status "GET /api/v1/info con auth -> 200"    200 $AUTH "${BASE}/api/v1/info"
expect_status "POST /api/v1/feed sin auth -> 401"   401 -X POST -H "Content-Type: application/json" -d '{"amount":10}' "${BASE}/api/v1/feed"

# ---------------------------------------------------------------------------
# 2. GET endpoints — contenido esperado
# ---------------------------------------------------------------------------
section "GET endpoints"
expect_body_contains "GET /api/v1/state contiene lifeStage" 200 "lifeStage" $AUTH "${BASE}/api/v1/state"
expect_body_contains "GET /api/v1/state contiene apiVersion" 200 "apiVersion" $AUTH "${BASE}/api/v1/state"
expect_body_contains "GET /api/v1/info contiene freeHeap"   200 "freeHeap"  $AUTH "${BASE}/api/v1/info"

# ---------------------------------------------------------------------------
# 3. POST endpoints con amount — happy path
# ---------------------------------------------------------------------------
section "POST endpoints con amount (happy path)"
for action in feed play discipline; do
    expect_body_contains "POST /api/v1/$action amount=10 -> 200" 200 "actionApplied" \
        $AUTH -X POST -H "Content-Type: application/json" -d '{"amount":10}' "${BASE}/api/v1/$action"
done

# ---------------------------------------------------------------------------
# 4. POST endpoints sin body (clean/medicate)
# ---------------------------------------------------------------------------
section "POST endpoints sin body"
expect_body_contains "POST /api/v1/clean    -> 200" 200 "actionApplied" \
    $AUTH -X POST -H "Content-Type: application/json" -d '{}' "${BASE}/api/v1/clean"
expect_body_contains "POST /api/v1/medicate -> 200" 200 "actionApplied" \
    $AUTH -X POST -H "Content-Type: application/json" -d '{}' "${BASE}/api/v1/medicate"

# ---------------------------------------------------------------------------
# 5. POST /sleep — con value: true/false
# ---------------------------------------------------------------------------
section "POST /sleep"
expect_body_contains "POST /sleep value=true  -> 200" 200 "actionApplied" \
    $AUTH -X POST -H "Content-Type: application/json" -d '{"value":true}'  "${BASE}/api/v1/sleep"
expect_body_contains "POST /sleep value=false -> 200" 200 "actionApplied" \
    $AUTH -X POST -H "Content-Type: application/json" -d '{"value":false}' "${BASE}/api/v1/sleep"

# ---------------------------------------------------------------------------
# 6. Validacion de payload — error cases
# ---------------------------------------------------------------------------
section "Validacion de payload (errores esperados)"
# amount fuera de rango -> 422
expect_status "POST /feed amount=0     -> 422" 422 \
    $AUTH -X POST -H "Content-Type: application/json" -d '{"amount":0}' "${BASE}/api/v1/feed"
expect_status "POST /feed amount=999   -> 422" 422 \
    $AUTH -X POST -H "Content-Type: application/json" -d '{"amount":999}' "${BASE}/api/v1/feed"
# amount como string -> 422 (JSON valido, pero el tipo de campo viola la regla de negocio).
# Razon: rules/api-conventions.md reserva 400 para "Bad Request" (malformado) y 422 para
# "Unprocessable Entity — validacion de negocio fallida". String donde se espera int es
# una violacion de contrato, no un error de parsing.
expect_status "POST /feed amount='10'  -> 422" 422 \
    $AUTH -X POST -H "Content-Type: application/json" -d '{"amount":"10"}' "${BASE}/api/v1/feed"
# JSON malformado -> 400
expect_status "POST /feed JSON malformado -> 400" 400 \
    $AUTH -X POST -H "Content-Type: application/json" -d '{bad json}' "${BASE}/api/v1/feed"
# Campo faltante -> 422
expect_status "POST /feed sin amount     -> 422" 422 \
    $AUTH -X POST -H "Content-Type: application/json" -d '{}' "${BASE}/api/v1/feed"
# Sleep con value como numero -> 422
expect_status "POST /sleep value=1 (no bool) -> 422" 422 \
    $AUTH -X POST -H "Content-Type: application/json" -d '{"value":1}' "${BASE}/api/v1/sleep"

# ---------------------------------------------------------------------------
# 7. Rutas inexistentes -> 404
# ---------------------------------------------------------------------------
section "Rutas inexistentes"
expect_status "GET /api/v1/nada -> 404"     404 $AUTH "${BASE}/api/v1/nada"
expect_status "POST /api/v1/nada -> 404"    404 $AUTH -X POST "${BASE}/api/v1/nada"

# ---------------------------------------------------------------------------
# 8. Frontend estatico
# ---------------------------------------------------------------------------
section "Frontend estatico (LittleFS)"
expect_body_contains "GET / sirve index.html"     200 "Tamagotchi" "${BASE}/"
expect_status        "GET /app.css -> 200"        200 "${BASE}/app.css"
expect_status        "GET /app.js  -> 200"        200 "${BASE}/app.js"

# ---------------------------------------------------------------------------
# Resumen
# ---------------------------------------------------------------------------
echo
echo "================================================================"
echo "Resultado: ${pass} PASS / ${fail} FAIL"
if [ "$fail" -gt 0 ]; then
    echo
    echo "Tests fallidos:"
    for t in "${failed_tests[@]}"; do
        echo "  - $t"
    done
    echo "================================================================"
    exit 1
fi
echo "================================================================"
exit 0
