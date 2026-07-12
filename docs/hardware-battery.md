# Medidor de batería + indicador de carga — guía de soldadura (ESP32-S3 Zero)

> El firmware, la UI (icono TFT + StatsView) y la API ya están completos y en producción desde `feature/battery-monitor`. **Lo único que falta es soldar el hardware.** Hasta entonces el icono permanece oculto y la API reporta `batteryValid:false` — comportamiento esperado, no un bug.

---

## 1. Topología actual y por qué VIN no sirve para medir

```
LiPo 3.7V (JST) → Cargador (LED rojo=cargando / LED azul=cargado, SIN pines CHRG/STDBY)
               → Boost step-up → 5V → VIN del ESP32-S3 Zero
```

El boost **aplana la tensión a 5V constantes** independientemente de si la batería está a 4.2V (llena) o 3.0V (casi vacía) — esa es su función. Medir VIN o 5V solo confirma "hay boost activo", no aporta nada sobre el estado de carga real de la LiPo.

La única forma de conocer el voltaje real de la batería es medir **antes** del boost, directamente en los bornes de la batería (B+/B−, o el punto equivalente en el PCB del cargador donde entra la LiPo).

---

## 2. Divisor de tensión VBAT

### Esquema

```
B+ (batería, ~3.0–4.2 V)
   │
  [R1 100kΩ]
   │
   ├──────────────→ GPIO1 (ADC1_CH0)
   │
  [R2 100kΩ]
   │
  GND (común con el ESP32-S3)
```

- Divisor **2×100k** → el punto medio ve siempre **la mitad** de VBAT (máx. ~2.175V con LiPo llena a 4.35V, dentro del fondo de escala del ADC a 11dB).
- Consumo del divisor: `4.2V / 200kΩ ≈ 21 µA` — despreciable frente al consumo del propio ESP32-S3.
- `BATTERY_DIVIDER_NUM=2 / BATTERY_DIVIDER_DEN=1` en `include/config.h` ya asume exactamente este divisor. Si usas otros valores de resistencia, ajusta esas constantes (mantener resistencias iguales; solo importa la proporción).

### Advertencias — leer antes de soldar

- **Nunca conectes B+ directamente a un GPIO.** Una LiPo llena entrega >4V; el ESP32-S3 tolera máximo 3.3V en sus pines. Conectar B+ sin el divisor **destruye el GPIO** (y probablemente el chip completo).
- **Verifica con multímetro que el punto medio del divisor lee ~VBAT/2 ANTES de conectar nada al GPIO.** Con la batería puesta, mide entre el punto medio y GND: debe dar aproximadamente la mitad del voltaje que mides directamente en B+/B−. Si no cuadra, revisa las resistencias (valores, soldadura fría, cortocircuito) antes de tocar el GPIO.
- **GND común obligatorio.** El GND del divisor debe ser el mismo GND del ESP32-S3 (no el GND "flotante" del lado batería si el cargador los aísla). Sin GND común, la lectura del ADC no tiene referencia y será basura.
- **Por qué GPIO1:**
  - Pertenece a **ADC1**, que sigue funcionando con WiFi activo (ADC2 comparte hardware con el radio WiFi y da lecturas erráticas en cuanto hay tráfico — descartado).
  - GPIO4 está ocupado por el botón (TTP223), GPIO7-12 por el bus SPI del TFT, GPIO19/20 son USB y GPIO21 el WS2812 — todos inutilizables.
  - GPIO3 es un pin de *strapping* (afecta el modo de boot) — se evita a propósito aunque estuviera libre.
  - GPIO1 y GPIO2 son los dos únicos pines "limpios" verificados en esta placa (Waveshare ESP32-S3 Zero); GPIO1 se reserva para VBAT, GPIO2 para el sensado de carga (sección 3).

---

## 3. Detección de carga — Opción A (recomendada): `BATTERY_CHARGE_SENSE_MODE=1`

El cargador del usuario **no tiene pines CHRG/STDBY** (solo LEDs), así que la señal de "está cargando" hay que sacarla de otro sitio. La opción más robusta y menos ambigua: detectar si hay **5V del USB presentes** en el pad de entrada del cargador.

### Esquema

```
Pad IN+ / VCC del cargador (5V cuando hay USB conectado, 0V si no)
   │
  [R1 100kΩ]
   │
   ├──────────────→ GPIO2
   │
  [R2 100kΩ]
   │
  GND común
```

- Mismo principio que el divisor de VBAT: **nunca 5V directo a un GPIO** — el divisor 100k/100k lo baja a ~2.5V, seguro para el pin.
- `HIGH` en GPIO2 = hay 5V en la entrada = el cable USB/boost está conectado (independientemente de si la batería ya está llena o no).
- `LOW` = sin alimentación externa = funcionando solo con batería.

**Ventaja sobre la Opción B:** el pad IN+/VCC tiene un nivel binario e inequívoco (5V o 0V), no depende de qué LED esté encendido ni de leer el propio LED con el multímetro.

---

## 4. Detección de carga — Opción B (alternativa): `BATTERY_CHARGE_SENSE_MODE=2`

Si el pad IN+/VCC no es accesible en el PCB del cargador, se puede leer el **cátodo del LED rojo** ("cargando") como señal indirecta.

### Procedimiento de verificación con multímetro (hazlo ANTES de soldar el pin al GPIO)

1. Conecta el USB al cargador con la batería a media carga (ni vacía ni llena).
2. Confirma visualmente que el **LED rojo está encendido** (cargando).
3. Mide con el multímetro entre el pad del cátodo del LED rojo y GND: debería marcar **≈0–0.4V** (el LED activo-LOW hunde el pin cerca de tierra mientras conduce).
4. Deja que la batería termine de cargar (o fuerza el estado desconectando y reconectando con batería ya llena) hasta que el **LED rojo se apague** (pasa a azul/cargado).
5. Mide de nuevo el mismo pad: debería subir a **≈5V** (el LED deja de conducir, el pad queda tirado al riel de 5V a través de la resistencia limitadora del propio LED).

**Divisor 100k/100k OBLIGATORIO en este pad también** — no basta con poner una resistencia en serie. El pad llega a ~5V cuando el LED está apagado, y eso mata el GPIO igual que B+ sin divisor. Mismo esquema de la sección 3, cambiando el origen de la señal.

### Ambigüedad sin USB conectado

Con la Opción B, si el cable USB **no está enchufado**, el LED rojo está apagado por falta de alimentación — el pad puede leer un nivel indefinido/flotante, indistinguible de "batería llena y cargador conectado". El firmware cubre este caso con la heurística de `BatteryGauge` (subida de voltaje sostenida o pendiente positiva, ver `lib/core/BatteryGauge/BatteryGauge.h`), que se fusiona con el nivel del pin cuando está disponible y decide sola cuando no. Por esto la **Opción A es la recomendada**: su señal es inequívoca en los dos casos (con y sin batería puesta).

---

## 5. Activación en firmware

El divisor de VBAT (sección 2) **se autodetecta en runtime sin recompilar** — `BatteryGauge` usa un gate de plausibilidad + estabilidad (8 muestras consecutivas dentro de rango y estables, ~16s) para salir del estado `Unknown`. Basta con soldarlo y esperar; no hace falta tocar el código.

El **pin de sensado de carga sí requiere activarlo explícitamente**, porque hasta que no esté soldado y verificado con multímetro no es seguro asumir ningún nivel. En `include/config.h`:

```c
#define BATTERY_CHARGE_PIN_ENABLED  1   // era 0 — activar tras soldar y verificar
#define PIN_CHARGE_SENSE            2   // GPIO2
#define BATTERY_CHARGE_SENSE_MODE   1   // 1=VbusPresence (Opción A) / 2=ChargeLedActiveLow (Opción B)
```

Tras el cambio:

```bash
pio run -e esp32s3_ota -t upload
```

(recompila y flashea por WiFi; no requiere `uploadfs` — no toca LittleFS/dashboard).

---

## 6. Verificación post-soldadura

1. **Monitor serie** (`pio device monitor` o el log OTA): buscar líneas `[battery] ...`. Al validar el divisor debería aparecer `[battery] valid=1 percent=NN% charging=0/1 mv=NNNN`.
2. **Pantalla TFT**: icono de batería en la **esquina superior izquierda** (espejo del icono WiFi, que está en la esquina superior derecha). Verde >50%, ámbar 21-50%, rojo ≤20% (parpadea si ≤10% y no está cargando). Rayo amarillo superpuesto si está cargando. Oculto en `OtaProgress` y en `StatsView` (ahí el dato sale como texto `Bat: NN%` en el footer).
3. **API**:
   ```bash
   curl -u "$WEB_AUTH_USER:$WEB_AUTH_PASSWORD" http://tamagotchi.local/api/v1/state | jq .data
   ```
   Esperado: `batteryValid:true`, `batteryPercent` coherente con la carga real, `batteryCharging` acorde al cable USB. `GET /api/v1/info` añade además `batteryMillivolts` para diagnóstico fino.
4. **Dashboard web**: el badge de batería (oculto por defecto) debe aparecer en el header junto al de WiFi, con el emoji 🔋/⚡ y el color según umbral.
5. **Test de banco sin batería puesta (opcional, útil para validar la cadena completa ADC→gauge→evento→icono→API antes de tener la LiPo a mano):** conecta el mismo divisor 2×100k entre el 3V3 del propio ESP32-S3 y GND, con el punto medio a GPIO1. Tras ~16s debería salir de `Unknown` mostrando ~3.3V ≈ 0-2% (icono rojo) — confirma que todo el pipeline funciona antes de tocar la LiPo real.

---

## Lista de material

- 2-4 resistencias **100kΩ, tolerancia 1%** (2 para el divisor de VBAT; 2 más si se implementa también el sensado de carga).
- Cable fino (30 AWG / wire-wrap o similar) para los puentes a GPIO1/GPIO2 y GND.
- Estaño + soldador de punta fina.
- Multímetro (imprescindible para verificar antes de conectar nada a un GPIO — ver advertencias en negrita de las secciones 2 y 4).
