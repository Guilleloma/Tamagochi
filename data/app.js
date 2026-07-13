// app.js — Tamagotchi WebUI. Vanilla JS, sin librerías.

var POLL_INTERVAL_MS  = 3000;
var BACKOFF_STEPS_MS  = [3000, 6000, 12000, 30000];
var OFFLINE_THRESHOLD = 2;

// Poll propio de /api/v1/info (panel "Sistema") — deliberadamente mas lento
// que POLL_INTERVAL_MS porque es diagnostico, no estado de juego.
//
// RateLimiter (lib/infra/WebUI/RateLimiter.h) es un token-bucket POR IP
// compartido entre TODOS los endpoints /api/v1/* (10 tokens, +1/600ms =
// ~1.667 req/s en regimen sostenido). El poll de /state ya consume 1/3s =
// 0.333 req/s. Sumando este poll a 10s (0.1 req/s) el total sostenido queda
// en 0.433 req/s — un 26% del presupuesto del bucket, dejando de sobra los
// ~1.23 req/s restantes para clics de acciones del usuario sin acercarse a
// 429. No hizo falta subir a 15s.
var SYSTEM_POLL_INTERVAL_MS = 10000;

var STAGE_EMOJI = {
  Egg:   '\u{1F95A}',
  Baby:  '\u{1F476}',
  Kid:   '\u{1F466}',
  Adult: '\u{1F9D1}',
  Old:   '\u{1F474}',
  Dead:  '\u{1F480}'
};

// Estado
var state = null;
var lastUpdateTime = null;
var consecutiveFailures = 0;
var pollTimerId = null;
var systemPollTimerId = null;

// DOM refs
var elSprite, elStage, elAge, elBadges,
    elBarHunger, elBarHappiness, elBarHealth, elBarDisc,
    elValHunger, elValHappiness, elValHealth, elValDisc,
    elValWeight, elValPoop,
    elLastUpdate, elOfflineBadge, elWifiIndicator, elBatteryIndicator,
    elToast,
    elBtnFeed, elBtnPlay, elBtnClean, elBtnMedicate,
    elBtnDiscipline, elBtnSleep, elBtnHeal, elBtnReboot,
    elSysFirmware, elSysUptime, elSysHeap, elSysRssi,
    elSysIp, elSysHostname, elSysMac,
    elSysBattStatus, elSysBattRaw, elSysBattFilteredRow, elSysBattFiltered;

function cacheDomRefs() {
  elSprite        = document.getElementById('pet-sprite');
  elStage         = document.getElementById('pet-stage');
  elAge           = document.getElementById('pet-age');
  elBadges        = document.getElementById('pet-badges');
  elBarHunger     = document.getElementById('bar-hunger');
  elBarHappiness  = document.getElementById('bar-happiness');
  elBarHealth     = document.getElementById('bar-health');
  elBarDisc       = document.getElementById('bar-discipline');
  elValHunger     = document.getElementById('val-hunger');
  elValHappiness  = document.getElementById('val-happiness');
  elValHealth     = document.getElementById('val-health');
  elValDisc       = document.getElementById('val-discipline');
  elValWeight     = document.getElementById('val-weight');
  elValPoop       = document.getElementById('val-poop');
  elLastUpdate    = document.getElementById('last-update');
  elOfflineBadge  = document.getElementById('offline-badge');
  elWifiIndicator = document.getElementById('wifi-indicator');
  elBatteryIndicator = document.getElementById('battery-indicator');
  elToast         = document.getElementById('toast');
  elBtnFeed       = document.getElementById('btn-feed');
  elBtnPlay       = document.getElementById('btn-play');
  elBtnClean      = document.getElementById('btn-clean');
  elBtnMedicate   = document.getElementById('btn-medicate');
  elBtnDiscipline = document.getElementById('btn-discipline');
  elBtnSleep      = document.getElementById('btn-sleep');
  elBtnHeal       = document.getElementById('btn-heal');
  elBtnReboot     = document.getElementById('btn-reboot');
  elSysIp         = document.getElementById('sys-ip');
  elSysHostname   = document.getElementById('sys-hostname');
  elSysMac        = document.getElementById('sys-mac');
  elSysFirmware   = document.getElementById('sys-firmware');
  elSysUptime     = document.getElementById('sys-uptime');
  elSysHeap       = document.getElementById('sys-heap');
  elSysRssi       = document.getElementById('sys-rssi');
  elSysBattStatus       = document.getElementById('sys-batt-status');
  elSysBattRaw          = document.getElementById('sys-batt-raw');
  elSysBattFilteredRow  = document.getElementById('sys-batt-filtered-row');
  elSysBattFiltered     = document.getElementById('sys-batt-filtered');
}

function formatAge(min) {
  if (min < 60) return min + 'm';
  var h = Math.floor(min / 60), m = min % 60;
  return m > 0 ? (h + 'h ' + m + 'm') : (h + 'h');
}

function setBar(fillEl, value) {
  var pct = Math.min(100, Math.max(0, value || 0));
  fillEl.style.width = pct + '%';
  fillEl.parentElement.setAttribute('aria-valuenow', pct);
}

function renderState(data) {
  if (!data) return;
  state = data;
  lastUpdateTime = Date.now();

  elSprite.textContent = STAGE_EMOJI[data.lifeStage] || '\u{1F9A0}';
  elStage.textContent  = data.lifeStage || '?';
  elAge.textContent    = formatAge(data.ageMinutes || 0);

  setBar(elBarHunger,    data.hunger);
  setBar(elBarHappiness, data.happiness);
  setBar(elBarHealth,    data.health);
  setBar(elBarDisc,      data.discipline);

  elValHunger.textContent    = data.hunger     || 0;
  elValHappiness.textContent = data.happiness  || 0;
  elValHealth.textContent    = data.health     || 0;
  elValDisc.textContent      = data.discipline || 0;
  elValWeight.textContent    = data.weight     !== undefined ? data.weight : '?';
  elValPoop.textContent      = data.poopCount  || 0;

  elBadges.innerHTML = '';
  if (data.isSick) {
    var b = document.createElement('span');
    b.className = 'badge badge--error';
    b.textContent = '\u{1F912} Sick';
    elBadges.appendChild(b);
  }
  if (data.isSleeping) {
    var b2 = document.createElement('span');
    b2.className = 'badge badge--sleep';
    b2.textContent = '\u{1F4A4} Sleeping';
    elBadges.appendChild(b2);
  }

  if (data.isSleeping) {
    elBtnSleep.classList.add('btn--active');
    elBtnSleep.setAttribute('aria-pressed', 'true');
  } else {
    elBtnSleep.classList.remove('btn--active');
    elBtnSleep.setAttribute('aria-pressed', 'false');
  }

  consecutiveFailures = 0;
  elOfflineBadge.hidden = true;
  elWifiIndicator.textContent = '● Online';
  elWifiIndicator.className   = 'badge badge--ok';

  renderBattery(data);
}

function renderBattery(data) {
  if (!data.batteryValid) {
    elBatteryIndicator.hidden = true;
    return;
  }

  var pct = data.batteryPercent || 0;
  var cls = 'badge--ok';
  if (pct <= 20)      cls = 'badge--error';
  else if (pct <= 50) cls = 'badge--warn';

  var icon = data.batteryCharging ? '\u{26A1} ' : '\u{1F50B} ';
  elBatteryIndicator.textContent = icon + pct + '%';
  elBatteryIndicator.className   = 'badge ' + cls;
  elBatteryIndicator.setAttribute('aria-label',
    'Bateria ' + pct + '%' + (data.batteryCharging ? ', cargando' : ''));
  elBatteryIndicator.hidden = false;
}

// Panel "Sistema" — telemetria de GET /api/v1/info (firmware, red, RAM,
// diagnostico de bateria en crudo). Ver renderSystem()/fetchInfo() abajo.

// Firmware se codifica como 0xMMmm (ver include/config.h FIRMWARE_VERSION) —
// se muestra en hex de 4 digitos para que coincida con lo que se ve en el
// codigo/monitor serie (p.ej. 0x0300), no como decimal sin contexto.
function formatFirmwareVersion(v) {
  var hex = (v || 0).toString(16).toUpperCase();
  while (hex.length < 4) hex = '0' + hex;
  return '0x' + hex;
}

function formatUptime(ms) {
  var totalMinutes = Math.floor((ms || 0) / 60000);
  var h = Math.floor(totalMinutes / 60);
  var m = totalMinutes % 60;
  return h > 0 ? (h + 'h ' + m + 'm') : (m + 'm');
}

// Umbrales pedidos: >-60 buena / -60..-75 media / <-75 mala. El limite
// superior de cada tramo es estrictamente "mayor que" (p.ej. -60 exacto cae
// en "media", -75 exacto cae en "mala") para que los tramos no se solapen.
function wifiQualityLabel(rssi) {
  if (rssi > -60) return 'buena';
  if (rssi > -75) return 'media';
  return 'mala';
}

function renderSystem(data) {
  if (!data) return;

  // ipAddress/hostname/macAddress: SIEMPRE presentes en /api/v1/info, pero
  // pueden llegar como '' si el WiFi cae (ver WebUI::handleGetInfo_) — se
  // muestra '?' en ese caso en vez de una celda vacia y confusa.
  elSysIp.textContent       = data.ipAddress  || '?';
  elSysHostname.textContent = data.hostname   || '?';
  elSysMac.textContent      = data.macAddress || '?';

  elSysFirmware.textContent = formatFirmwareVersion(data.firmwareVersion);
  elSysUptime.textContent   = formatUptime(data.uptimeMs);
  elSysHeap.textContent     = Math.round((data.freeHeap || 0) / 1024) + ' KB';

  var rssi = data.wifiRssi;
  elSysRssi.textContent = (rssi === undefined || rssi === null)
    ? '?'
    : (rssi + ' dBm (' + wifiQualityLabel(rssi) + ')');

  renderSystemBattery(data);
}

// Estado del sensor — pensado para diagnosticar una soldadura reciente del
// divisor de VBAT (ver docs/hardware-battery.md):
//   "OK"              -> batteryValid true, gate ya confirmado.
//   "Detectando (N/8)" -> raw dentro del rango plausible del BatteryGauge
//                         (2800-4350mV, ver BatteryGauge.h) pero el gate aun
//                         no acumulo las 8 muestras consecutivas requeridas.
//   "No detectado"    -> raw fuera de ese rango (pin flotante, nada soldado,
//                         o fuera de rango por otra causa de hardware).
function renderSystemBattery(data) {
  var raw = data.batteryRawMillivolts || 0;
  elSysBattRaw.textContent = raw + ' mV';

  if (data.batteryValid) {
    elSysBattStatus.textContent = 'OK';
    elSysBattFilteredRow.hidden = false;
    elSysBattFiltered.textContent =
      (data.batteryPercent || 0) + '% / ' + (data.batteryMillivolts || 0) + ' mV' +
      (data.batteryCharging ? ' (cargando)' : '');
    return;
  }

  elSysBattFilteredRow.hidden = true;
  var plausible = raw >= 2800 && raw <= 4350;  // ver minPlausibleMv/maxPlausibleMv
  elSysBattStatus.textContent = plausible
    ? ('Detectando... (' + (data.batteryGateProgress || 0) + '/8)')
    : 'No detectado';
}

function updateAgeLabel() {
  if (!lastUpdateTime) { elLastUpdate.textContent = 'Sin datos aun'; return; }
  var s = Math.round((Date.now() - lastUpdateTime) / 1000);
  elLastUpdate.textContent = 'Actualizado hace ' + s + 's';
}

// Polling

function getBackoffMs() {
  return BACKOFF_STEPS_MS[Math.min(consecutiveFailures, BACKOFF_STEPS_MS.length - 1)];
}

function schedulePoll(delayMs) {
  if (pollTimerId !== null) clearTimeout(pollTimerId);
  pollTimerId = setTimeout(function() { pollTimerId = null; fetchState(); }, delayMs);
}

function stopPolling() {
  if (pollTimerId !== null) { clearTimeout(pollTimerId); pollTimerId = null; }
}

function handlePollError() {
  consecutiveFailures++;
  if (consecutiveFailures >= OFFLINE_THRESHOLD) {
    elOfflineBadge.hidden = false;
    elWifiIndicator.textContent = '○ Offline';
    elWifiIndicator.className   = 'badge badge--error';
  }
  schedulePoll(getBackoffMs());
}

function fetchState() {
  if (document.visibilityState !== 'visible') return;
  fetch('/api/v1/state', { method: 'GET', credentials: 'same-origin' })
    .then(function(res) {
      if (res.ok) {
        return res.json().then(function(j) { renderState(j.data); schedulePoll(POLL_INTERVAL_MS); });
      }
      if (res.status === 401) { schedulePoll(POLL_INTERVAL_MS); return; }
      handlePollError();
    })
    .catch(handlePollError);
}

// Poll independiente de /api/v1/info para el panel "Sistema". Deliberadamente
// simple (sin backoff ni badge de Offline: ese rol ya lo cumple fetchState())
// — si falla, reintenta al mismo intervalo fijo; es un panel de diagnostico
// secundario, no el indicador principal de conectividad.
function scheduleSystemPoll(delayMs) {
  if (systemPollTimerId !== null) clearTimeout(systemPollTimerId);
  systemPollTimerId = setTimeout(function() { systemPollTimerId = null; fetchInfo(); }, delayMs);
}

function stopSystemPolling() {
  if (systemPollTimerId !== null) { clearTimeout(systemPollTimerId); systemPollTimerId = null; }
}

function fetchInfo() {
  if (document.visibilityState !== 'visible') return;
  fetch('/api/v1/info', { method: 'GET', credentials: 'same-origin' })
    .then(function(res) {
      if (res.ok) {
        return res.json().then(function(j) { renderSystem(j.data); scheduleSystemPoll(SYSTEM_POLL_INTERVAL_MS); });
      }
      scheduleSystemPoll(SYSTEM_POLL_INTERVAL_MS);
    })
    .catch(function() { scheduleSystemPoll(SYSTEM_POLL_INTERVAL_MS); });
}

// Acciones

async function doAction(endpoint, body) {
  try {
    var res = await fetch(endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
      credentials: 'same-origin'
    });
    if (res.ok) {
      var j = await res.json();
      renderState(j.data);
      flashFeedback('OK', 'success');
    } else if (res.status === 429) {
      flashFeedback('Demasiado rapido', 'warn');
    } else if (res.status !== 401) {
      var ej = await res.json().catch(function() { return { error: { message: 'Error ' + res.status } }; });
      flashFeedback((ej.error && ej.error.message) || 'Error', 'error');
    }
  } catch (e) {
    flashFeedback('Sin conexion', 'error');
  }
}

// Reinicio remoto — mismo patron de fetch que doAction(), pero la respuesta
// de /api/v1/reboot ({data:{rebooting:true}}) NO es el estado del Tamagotchi,
// asi que no se pasa a renderState(). El dispositivo persiste antes de
// responder y se reinicia ~750ms despues (ver WebUI::handlePostReboot_).
async function doReboot() {
  if (!confirm('Reiniciar el dispositivo ahora? La partida se guarda antes de reiniciar.')) {
    return;
  }
  try {
    var res = await fetch('/api/v1/reboot', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      credentials: 'same-origin'
    });
    if (res.ok) {
      flashFeedback('Reiniciando...', 'warn');
      // El dispositivo tarda unos segundos en volver a estar disponible tras
      // el reinicio — se detiene el polling actual y se reintenta mas tarde
      // en vez de dejar que el backoff normal (pensado para caidas breves de
      // red) se dispare de inmediato con el primer fallo. Mismo tratamiento
      // para el poll de /info del panel Sistema.
      stopPolling();
      schedulePoll(5000);
      stopSystemPolling();
      scheduleSystemPoll(5000);
    } else if (res.status === 429) {
      flashFeedback('Demasiado rapido', 'warn');
    } else if (res.status !== 401) {
      var ej = await res.json().catch(function() { return { error: { message: 'Error ' + res.status } }; });
      flashFeedback((ej.error && ej.error.message) || 'Error', 'error');
    }
  } catch (e) {
    flashFeedback('Sin conexion', 'error');
  }
}

// Toast

var toastTimer = null;
function flashFeedback(msg, kind) {
  elToast.textContent = msg;
  elToast.className   = 'toast toast--visible toast--' + kind;
  elToast.hidden      = false;
  if (toastTimer) clearTimeout(toastTimer);
  toastTimer = setTimeout(function() {
    elToast.classList.remove('toast--visible');
    toastTimer = setTimeout(function() { elToast.hidden = true; toastTimer = null; }, 220);
  }, 2000);
}

// Botones

function wireButtons() {
  elBtnFeed.addEventListener('click',       function() { doAction('/api/v1/feed',       { amount: 20 }); });
  elBtnPlay.addEventListener('click',       function() { doAction('/api/v1/play',       { amount: 15 }); });
  elBtnClean.addEventListener('click',      function() { doAction('/api/v1/clean',      {}); });
  elBtnMedicate.addEventListener('click',   function() { doAction('/api/v1/medicate',   {}); });
  elBtnDiscipline.addEventListener('click', function() { doAction('/api/v1/discipline', { amount: 10 }); });
  elBtnHeal.addEventListener('click',       function() { doAction('/api/v1/heal',       { amount: 20 }); });
  elBtnSleep.addEventListener('click', function() {
    doAction('/api/v1/sleep', { value: !(state && state.isSleeping) });
  });
  if (elBtnReboot) elBtnReboot.addEventListener('click', doReboot);
}

// Visibility

document.addEventListener('visibilitychange', function() {
  if (document.visibilityState === 'visible') {
    stopPolling(); fetchState();
    stopSystemPolling(); fetchInfo();
  } else {
    stopPolling();
    stopSystemPolling();
  }
});

// Init

document.addEventListener('DOMContentLoaded', function() {
  cacheDomRefs();
  wireButtons();
  setInterval(updateAgeLabel, 1000);
  fetchState();
  fetchInfo();
});
