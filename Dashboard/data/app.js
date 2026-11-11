// ============================================================
// ELEMENT REFS
// ============================================================

const clockEl      = document.getElementById("clock");
const clockDateEl  = document.getElementById("clock-date");
const clockNamesEl = document.getElementById("clock-names");
const weatherHeroEl = document.getElementById("weather-hero");
const forecastRowEl = document.getElementById("forecast-row");
const climateRowEl  = document.getElementById("climate-row");
const moonCardEl    = document.getElementById("moon-card");
const desktopCardEl = document.getElementById("desktop-card");
const devicesEl    = document.getElementById("devices");
const statIpEl     = document.getElementById("stat-ip");
const statNetEl    = document.getElementById("stat-net");
const statHeapEl   = document.getElementById("stat-heap");
const statEspEl    = document.getElementById("stat-esp");
const statWsEl     = document.getElementById("stat-ws");
const statWsTimeEl = document.getElementById("stat-ws-time");
const accessModeBtn = document.getElementById("access-mode-btn");
const svatekEl     = document.getElementById("svatek");
const svatekSummaryEl = document.getElementById("svatek-summary");

// ============================================================
// CONFIG
// ============================================================

const REMOTE_BASE_IP = "172.27.167.45";

const DEVICE_ACCESS = {
  "wled-pokoj": { localHost: "wled-pokoj.local", remotePort: 7070 },
  "mc":         { localHost: "mc.local",          remotePort: 9090 }
};

const WEATHER_ICONS = {
  "clear": "☀️", "partly-cloudy": "⛅", "cloudy": "☁️",
  "fog": "🌫️", "drizzle": "🌦️", "rain": "🌧️",
  "snow": "❄️", "thunder": "⛈️", "unknown": "❔"
};

// ============================================================
// STATE
// ============================================================

let socket = null;
let reconnectTimer = null;
let selectedDeviceId = null;
let accessMode = localStorage.getItem("dashAccessMode") || null;
let lastUpdateTime = null;

// ============================================================
// CLOCK TICK
// ============================================================

function tickClock() {
  const now = new Date();
  const h = String(now.getHours()).padStart(2, "0");
  const m = String(now.getMinutes()).padStart(2, "0");
  const s = String(now.getSeconds()).padStart(2, "0");
  if (clockEl) clockEl.textContent = `${h}:${m}:${s}`;
}
tickClock();
setInterval(tickClock, 1000);

// ============================================================
// UTILS
// ============================================================

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;").replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;").replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function escapeJsString(value) {
  return String(value ?? "")
    .replaceAll("\\", "\\\\").replaceAll("'", "\\'")
    .replaceAll('"', '\\"').replaceAll("\n", "\\n")
    .replaceAll("\r", "\\r");
}

function isMeaningful(value) {
  if (value === null || value === undefined) return false;
  if (typeof value === "string") { const v = value.trim(); return v !== "" && v !== "-" && v !== "unknown"; }
  if (typeof value === "number") return Number.isFinite(value) && value !== 0;
  return true;
}

function formatBytes(bytes) {
  const n = Number(bytes);
  if (!Number.isFinite(n)) return "-";
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} kB`;
  return `${(n / 1024 / 1024).toFixed(2)} MB`;
}

function formatDurationMs(ms) {
  let s = Math.floor(Number(ms || 0) / 1000);
  const d = Math.floor(s / 86400); s %= 86400;
  const h = Math.floor(s / 3600);  s %= 3600;
  const m = Math.floor(s / 60);    s %= 60;
  const parts = [];
  if (d) parts.push(`${d}d`);
  if (h) parts.push(`${h}h`);
  if (m) parts.push(`${m}m`);
  if (!parts.length) parts.push(`${s}s`);
  return parts.join(" ");
}

function formatDurationSeconds(seconds) {
  return formatDurationMs(Number(seconds || 0) * 1000);
}

function row(label, value, formatter = null) {
  if (!isMeaningful(value)) return "";
  const out = formatter ? formatter(value) : escapeHtml(value);
  return `<div class="row"><span>${escapeHtml(label)}</span><b>${out}</b></div>`;
}

function weatherIcon(key) {
  return WEATHER_ICONS[key] || WEATHER_ICONS.unknown;
}

// ============================================================
// MODE GATE
// ============================================================

function showModeGateIfNeeded() {
  const gate = document.getElementById("mode-gate");
  if (!gate) return;
  if (accessMode === "local" || accessMode === "remote") {
    gate.classList.add("hidden");
  } else {
    gate.classList.remove("hidden");
  }
  updateAccessModeButton();
}

function updateAccessModeButton() {
  if (!accessModeBtn) return;
  if (accessMode === "local") {
    accessModeBtn.innerHTML = `<span class="mode-badge-icon">⌂</span> Local`;
    accessModeBtn.className = "mode-pill local";
  } else if (accessMode === "remote") {
    accessModeBtn.innerHTML = `<span class="mode-badge-icon">⇄</span> Remote`;
    accessModeBtn.className = "mode-pill remote";
  } else {
    accessModeBtn.textContent = "Mode: -";
    accessModeBtn.className = "mode-pill";
  }
}

function setAccessMode(mode) {
  accessMode = mode;
  localStorage.setItem("dashAccessMode", mode);
  showModeGateIfNeeded();
  fetchStatusFallback();
}

function resetAccessMode() {
  accessMode = null;
  localStorage.removeItem("dashAccessMode");
  showModeGateIfNeeded();
}

// ============================================================
// DEVICE ACCESS
// ============================================================

function getDeviceAccess(dev) {
  const id = String(dev.id || "");
  return DEVICE_ACCESS[id] || { localHost: dev.host || dev.ip || "", remotePort: 80 };
}

function buildDeviceUrl(dev) {
  const cfg = getDeviceAccess(dev);
  if (accessMode === "remote") return `http://${REMOTE_BASE_IP}:${cfg.remotePort}`;
  const localHost = cfg.localHost || dev.host || dev.ip || "";
  return localHost ? `http://${localHost}` : "";
}

function getDeviceTargetLabel(dev) {
  const cfg = getDeviceAccess(dev);
  if (accessMode === "remote") return `${REMOTE_BASE_IP}:${cfg.remotePort}`;
  return cfg.localHost || dev.host || dev.ip || "-";
}

function selectDevice(id) {
  selectedDeviceId = selectedDeviceId === id ? null : id;
  fetchStatusFallback();
}

// ============================================================
// TOGGLE SECTION
// ============================================================

function toggleSection(id) {
  const section = document.getElementById(`section-${id}`);
  const icon = document.getElementById(`icon-${id}`);
  if (!section) return;
  section.classList.toggle("open");
  if (icon) icon.textContent = section.classList.contains("open") ? "⌄" : "›";
}

// ============================================================
// WEBSOCKET STATUS
// ============================================================

function setWsStatus(text, ok) {
  if (!statWsEl) return;
  const color = ok === true ? "var(--ok)" : ok === false ? "var(--bad)" : "var(--warn)";
  statWsEl.innerHTML = `<span class="ws-dot" style="background:${color};box-shadow:0 0 6px ${color}"></span> ${escapeHtml(text)}`;
  if (statWsTimeEl) statWsTimeEl.textContent = `last update: ${new Date().toLocaleTimeString()}`;
}

// ============================================================
// RENDER: SYSTEM (stat cards)
// ============================================================

function renderSystem(system) {
  if (!system) return;
  if (statIpEl)   statIpEl.textContent   = system.ip || "-";
  if (statNetEl)  statNetEl.textContent  = `${system.mdns || "dash.local"} · ${system.rssi ?? "-"} dBm`;
  if (statHeapEl) statHeapEl.textContent = formatBytes(system.heap);
  if (statEspEl)  statEspEl.textContent  = `free heap · boot #${system.bootCount ?? "-"}`;

  // Clock date line
  if (clockDateEl && system.currentFullDate) {
    const d = new Date(system.currentFullDate);
    const days = ["Neděle","Pondělí","Úterý","Středa","Čtvrtek","Pátek","Sobota"];
    const months = ["ledna","února","března","dubna","května","června","července","srpna","září","října","listopadu","prosince"];
    clockDateEl.textContent = `${days[d.getDay()]}, ${d.getDate()}. ${months[d.getMonth()]} ${d.getFullYear()}`;
  }

  if (clockNamesEl && system.currentNames) {
    clockNamesEl.textContent = `Svátek má ${system.currentNames}`;
  }
}

// ============================================================
// RENDER: SVÁTEK
// ============================================================

function renderSvatek(system) {
  if (!system) return;
  const names    = system.currentNames    || "-";
  const date     = system.currentFullDate || "-";
  const lastSent = system.lastSent        || "-";
  const lastSendOk = Boolean(system.lastSendOk);

  if (svatekSummaryEl) svatekSummaryEl.textContent = `${names} · naposledy: ${lastSent}`;

  if (svatekEl) svatekEl.innerHTML = `
    <div class="rows">
      ${row("Datum", date)}
      ${row("Dnes má svátek", names)}
      ${row("Naposledy odesláno", lastSent)}
    </div>
    <div class="actions">
      <button type="button" onclick="sendSvatek()">📨 Odeslat Discord</button>
      <button type="button" onclick="updateTime()">🔄 Update čas</button>
      <button type="button" onclick="resetLastSent()">↺ Reset last sent</button>
    </div>
  `;
}

// ============================================================
// RENDER: CLIMATE (DHT11 vnitřní / DHT22 venkovní)
// ============================================================

function renderClimate(climate) {
  if (!climateRowEl) return;
  if (!climate) {
    climateRowEl.innerHTML = `<div class="empty">Čekám na senzory...</div>`;
    return;
  }

  const STALE_S = 180;
  const FROST_C = 1.0;    // venku <= tato teplota -> "Pozor, zima"

  const trendHtml = (r, minDelta) => {
    if (!r.trendMin) return "";
    const arrow = r.trend >= minDelta ? "↑" : r.trend <= -minDelta ? "↓" : "→";
    const cls = arrow === "↑" ? "trend-up" : arrow === "↓" ? "trend-down" : "trend-flat";
    const sign = r.trend > 0 ? "+" : "";
    return `<span class="climate-trend ${cls}" title="za posledních ${r.trendMin} min">${arrow} ${sign}${Number(r.trend).toFixed(1)}°</span>`;
  };

  const frostIcon = `<svg class="frost-icon" viewBox="0 0 24 22" aria-hidden="true">
      <path d="M12 1.5 23 20.5H1z" fill="#ef4444" stroke="#fecaca" stroke-width="1.2" stroke-linejoin="round"/>
      <text x="12" y="17" text-anchor="middle" font-size="12" fill="#fff">❄</text></svg>`;

  const card = (label, icon, r, opts) => {
    if (!r || !r.valid) {
      return `<div class="climate-card climate-offline">
        <div class="climate-label">${icon} ${label}</div>
        <div class="climate-temp">--.-°C</div>
        <div class="climate-hum">senzor nedostupný</div>
      </div>`;
    }
    const stale = r.ageS < 0 || r.ageS > STALE_S;
    let warn = "";
    if (opts.frost && r.temp <= FROST_C) {
      warn = `<div class="climate-warn warn-frost">${frostIcon}<span>Pozor, zima</span></div>`;
    }
    const tempTxt = opts.integer ? Math.round(r.temp) : Number(r.temp).toFixed(1);
    return `<div class="climate-card${stale ? " climate-stale" : ""}${warn && opts.frost ? " climate-frost" : ""}">
      <div class="climate-label">${icon} ${label}${stale ? ` <span class="stale-tag">⏱ ${r.ageS < 0 ? "žádná data" : "stará data " + Math.round(r.ageS / 60) + " min"}</span>` : ""}</div>
      <div class="climate-temp">${tempTxt}°C ${trendHtml(r, opts.trendMin)}</div>
      <div class="climate-hum">💧 ${Math.round(r.hum)} %</div>
      ${warn}
    </div>`;
  };

  climateRowEl.innerHTML =
    card("Venku",  "🌳", climate.outdoor, { frost: true, trendMin: 0.5 }) +
    card("Uvnitř", "🏠", climate.indoor,  { trendMin: 1, integer: true });
}

// ============================================================
// RENDER: DESKTOP (health check + WOL)
// ============================================================

const WAKE_WINDOW_S = 180;   // musi odpovidat DESKTOP_WAKE_WINDOW_MS ve firmwaru
let wolPendingUntil = 0;     // lokalni "probouzim" hned po kliknuti, nez dorazi stav z ESP

function renderDesktop(d) {
  if (!desktopCardEl) return;
  if (!d) { desktopCardEl.innerHTML = ""; return; }

  const online = Boolean(d.online);
  const waking = !online && (
    (d.wolAgeS >= 0 && d.wolAgeS < WAKE_WINDOW_S) || Date.now() < wolPendingUntil
  );
  if (online) wolPendingUntil = 0;

  let pillCls = "pill-bad", pillTxt = "offline";
  if (!d.known)    { pillCls = "pill-warn"; pillTxt = "zjišťuji…"; }
  else if (online) { pillCls = "pill-ok";   pillTxt = "online"; }
  else if (waking) { pillCls = "pill-warn"; pillTxt = "probouzím…"; }

  const meta = [
    escapeHtml(d.ip || ""),
    d.checkAgeS >= 0 ? `kontrola před ${d.checkAgeS} s` : ""
  ].filter(Boolean).join(" · ");

  const nameColor = online ? "var(--accent)" : "var(--muted2)";
  const btn = online
    ? ""
    : `<button type="button" class="dev-open-btn" onclick="wolDesktop()" ${waking ? "disabled" : ""}>⏻ Zapnout PC</button>`;

  desktopCardEl.innerHTML = `
    <div class="dev-card">
      <div class="dev-card-top">
        <div>
          <div class="dev-name"><span class="dev-name-text" style="color:${nameColor}">Desktop PC</span></div>
          <div class="dev-meta">${meta}</div>
        </div>
        <div class="dev-right">
          ${btn}
          <span class="pill ${pillCls}">${pillTxt}</span>
        </div>
      </div>
    </div>`;
}

async function wolDesktop() {
  wolPendingUntil = Date.now() + WAKE_WINDOW_S * 1000;
  await apiPost("/api/wol");
}

// ============================================================
// RENDER: MOON (nejbližší nov / úplněk)
// ============================================================
// Střední synodický měsíc (29.530589 d) od novu 6.1.2000 18:14 UTC.
// Chyba je řádově hodiny (skutečné fáze kolísají ±~14 h), pro dny stačí.

const SYNODIC = 29.530588853;
const NEW_MOON_REF_JD = 2451550.1;

function moonInfo(now = new Date()) {
  const jd  = now.getTime() / 86400000 + 2440587.5;
  let age = (jd - NEW_MOON_REF_JD) % SYNODIC;
  if (age < 0) age += SYNODIC;
  const half = SYNODIC / 2;
  const waxing = age < half;
  const toFull = waxing ? half - age : SYNODIC - age + half;
  const toNew  = SYNODIC - age;
  const illum  = (1 - Math.cos(2 * Math.PI * age / SYNODIC)) / 2;
  return {
    age, waxing, illum,
    full: new Date(now.getTime() + toFull * 86400000),
    nov:  new Date(now.getTime() + toNew  * 86400000),
  };
}

function daysUntilLabel(target, now = new Date()) {
  const a = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  const b = new Date(target.getFullYear(), target.getMonth(), target.getDate());
  const d = Math.round((b - a) / 86400000);
  if (d <= 0) return "dnes";
  if (d === 1) return "zítra";
  if (d < 5) return `za ${d} dny`;
  return `za ${d} dní`;
}

function fmtMoonDate(d) {
  const CZ_DAYS = ["Ne","Po","Út","St","Čt","Pá","So"];
  return `${CZ_DAYS[d.getDay()]} ${d.getDate()}. ${d.getMonth() + 1}.`;
}

function renderMoon(weather) {
  if (!moonCardEl) return;
  const now = new Date();
  const m = moonInfo(now);

  const ev = (emoji, label, date) => `
    <div class="moon-event">
      <span class="moon-event-icon">${emoji}</span>
      <div>
        <div class="moon-event-label">${label}</div>
        <div class="moon-event-date">${fmtMoonDate(date)} <span class="moon-event-count">· ${daysUntilLabel(date, now)}</span></div>
      </div>
    </div>`;

  // nejbližší událost první: přibývá -> úplněk, ubývá -> nov
  const events = m.waxing
    ? ev("🌕", "Úplněk", m.full) + ev("🌑", "Nov", m.nov)
    : ev("🌑", "Nov", m.nov) + ev("🌕", "Úplněk", m.full);

  const emoji = (weather && weather.valid && weather.moonEmoji) || "🌑";
  const name  = (weather && weather.valid && weather.moonName) || "";

  moonCardEl.innerHTML = `
    <div class="moon-now">
      <div class="moon-now-icon">${emoji}</div>
      <div>
        <div class="moon-now-name">${escapeHtml(name)} <span class="moon-trend">${m.waxing ? "↗ přibývá" : "↘ ubývá"}</span></div>
        <div class="moon-now-sub">osvětleno ${Math.round(m.illum * 100)} % · ${Math.floor(m.age)}. den cyklu</div>
      </div>
    </div>
    <div class="moon-events">${events}</div>
  `;
}

// ============================================================
// RENDER: WEATHER
// ============================================================

function renderWeather(weather) {
  if (!weather || !weather.valid) {
    if (weatherHeroEl) weatherHeroEl.innerHTML = `<div class="empty">Data o počasí nejsou.</div>`;
    return;
  }

  const temp     = Math.round(weather.temp);
  const apparent = Math.round(weather.apparentTemp);
  const wind     = Math.round(weather.windSpeed);
  const icon     = weatherIcon(weather.icon);
  const condition = weather.condition || "-";

  const now = new Date();
  const nowMin = now.getHours() * 60 + now.getMinutes();
  const sunsetMin = weather.sunsetMin || 0;
  let sunCountdown = "";
  if (sunsetMin > 0) {
    const diff = sunsetMin - nowMin;
    if (diff > 0) {
      const h = Math.floor(diff / 60), m = diff % 60;
      sunCountdown = h > 0 ? `za ${h}h ${m}min` : `za ${m}min`;
    } else {
      sunCountdown = "již zapadlo";
    }
  }

  if (weatherHeroEl) weatherHeroEl.innerHTML = `
    <div class="weather-hero-left">
      <div class="weather-hero-icon">${icon}</div>
      <div>
        <div class="weather-hero-temp">${temp}°C <span class="weather-apparent">pocitově ${apparent}°C</span></div>
        <div class="weather-hero-condition">${escapeHtml(condition)} · vítr ${wind} km/h</div>
      </div>
    </div>
    <div class="weather-hero-right">
      <span>🌅 ${weather.sunrise || "--:--"}</span>
      <span>🌇 ${weather.sunset || "--:--"} ${sunCountdown ? `<span class="sun-countdown">(${sunCountdown})</span>` : ""}</span>
      <span>${weather.moonEmoji || "🌑"} ${escapeHtml(weather.moonName || "")}</span>
    </div>
  `;

  const forecast = Array.isArray(weather.forecast) ? weather.forecast : [];
  if (forecastRowEl) {
    const CZ_DAYS = ["Ne","Po","Út","St","Čt","Pá","So"];
    const CZ_MONTHS = ["led","úno","bře","dub","kvě","čvn","čvc","srp","zář","říj","lis","pro"];
    let lastDate = "";
    let html = "";
    let count = 0;
    for (const f of forecast) {
      if (count >= 12) break;
      if (f.date && f.date !== lastDate && f.hour === 0 && lastDate !== "") {
        const d = new Date(f.date + "T00:00:00");
        const label = `${CZ_DAYS[d.getDay()]} ${d.getDate()}. ${CZ_MONTHS[d.getMonth()]}`;
        html += `<div class="forecast-chip forecast-day-sep"><span class="forecast-day-label">${label}</span></div>`;
      }
      if (f.date) lastDate = f.date;
      html += `<div class="forecast-chip">
        <span class="forecast-chip-time">${String(f.hour).padStart(2,"0")}:00</span>
        <span class="forecast-chip-icon">${weatherIcon(f.icon)}</span>
        <span class="forecast-chip-temp">${Math.round(f.temp)}°</span>
      </div>`;
      count++;
    }
    forecastRowEl.innerHTML = html;
  }
}

// ============================================================
// RENDER: DEVICES
// ============================================================

function renderDevices(devices) {
  if (!devicesEl) return;
  if (!devices || !devices.length) {
    devicesEl.innerHTML = `<div class="empty">Žádná zařízení.</div>`;
    return;
  }

  devicesEl.innerHTML = devices.map(dev => {
    const online     = Boolean(dev.online);
    const openUrl    = buildDeviceUrl(dev);
    const target     = getDeviceTargetLabel(dev);
    const name       = dev.name || "Unnamed";
    const type       = dev.type || "-";
    const deviceId   = String(dev.id || "");
    const power      = Boolean(dev.power);
    const brightness = Number(dev.brightness || 0);
    const uptimeText = type === "esp"
      ? formatDurationMs(dev.uptime)
      : formatDurationSeconds(dev.uptime);

    const nameColor = online ? "var(--accent)" : "var(--muted2)";
    const nameHtml = `<span class="dev-name-text" style="color:${nameColor}">${escapeHtml(name)}</span>`;

    const metaParts = [
      escapeHtml(target),
      online && dev.rssi ? `${dev.rssi} dBm` : "",
      online && dev.uptime ? uptimeText : ""
    ].filter(Boolean);

    const openBtn = online && openUrl
      ? `<a href="${escapeHtml(openUrl)}" target="_blank" rel="noopener" onclick="event.stopPropagation()" class="dev-open-btn">Open ↗</a>`
      : `<span class="dev-open-btn disabled">Open ↗</span>`;

    // Detail rows – viditelné jen po rozkliknuti
    const detailRows = `
      <div class="dev-details" id="dev-detail-${escapeHtml(deviceId)}">
        <div class="rows">
          ${row("Host", dev.host)}
          ${row("IP", dev.ip)}
          ${online ? row("RSSI", dev.rssi, v => `${v} dBm`) : ""}
          ${online ? row("Uptime", dev.uptime, () => uptimeText) : ""}
          ${row("Verze", dev.version)}
          ${type === "wled" ? row("Brightness", brightness, v => `${v}/255`) : ""}
          ${type === "wled" ? row("Power", power ? "ON" : "OFF") : ""}
          ${type === "esp" ? row("Status", dev.status) : ""}
          ${type === "esp" ? row("Role", dev.role) : ""}
          ${type === "esp" ? row("Heap", dev.heap, formatBytes) : ""}
          ${type === "esp" ? row("MAC", dev.mac) : ""}
          ${type === "esp" ? row("Chip", dev.chip) : ""}
        </div>
        ${dev.error ? `<div class="error">${escapeHtml(dev.error)}</div>` : ""}
        <div class="actions" onclick="event.stopPropagation()">
          ${openUrl ? `<a class="primary" href="${escapeHtml(openUrl)}" target="_blank" rel="noopener">Open</a>` : ""}
          ${type === "wled" ? `<button type="button" onclick="wledToggle()">Toggle</button>` : ""}
          ${type === "wled" ? `<button type="button" onclick="wledOn()">On</button>` : ""}
          ${type === "wled" ? `<button type="button" onclick="wledOff()">Off</button>` : ""}
          ${type === "wled" ? `<button type="button" onclick="wledIdentify()">Identify</button>` : ""}
        </div>
        ${type === "wled" ? `
          <div class="slider-row" onclick="event.stopPropagation()">
            <span>Brightness</span>
            <input type="range" min="0" max="255" value="${brightness}"
              oninput="updateBrightnessLabel(this.value)"
              onchange="wledBrightness(this.value)" />
            <b id="brightness-label">${brightness}</b>
          </div>` : ""}
      </div>
    `;

    return `
      <div class="dev-card" onclick="toggleDevDetail('${escapeJsString(deviceId)}')">
        <div class="dev-card-top">
          <div>
            <div class="dev-name">${nameHtml}</div>
            <div class="dev-meta">${metaParts.join(" · ")}</div>
          </div>
          <div class="dev-right">
            ${openBtn}
            <span class="pill ${online ? "pill-ok" : "pill-bad"}">${online ? "online" : "offline"}</span>
          </div>
        </div>
        ${detailRows}
      </div>
    `;
  }).join("");
}

function updateBrightnessLabel(value) {
  const el = document.getElementById("brightness-label");
  if (el) el.textContent = value;
}

function toggleDevDetail(id) {
  const el = document.getElementById(`dev-detail-${id}`);
  if (!el) return;
  el.classList.toggle("open");
}

// ============================================================
// RENDER: ALARM
// ============================================================

function renderAlarm(alarm) {
  const overlay = document.getElementById("alarm-overlay");
  if (!overlay) return;
  if (!alarm || !alarm.active) { overlay.classList.add("hidden"); return; }
  overlay.classList.remove("hidden");
  const nameEl  = document.getElementById("alarm-device-name");
  const eventEl = document.getElementById("alarm-event-id");
  const timeEl  = document.getElementById("alarm-time");
  if (nameEl)  nameEl.textContent  = alarm.deviceName || "Panic Button";
  if (eventEl) eventEl.textContent = alarm.eventId    || "";
  if (timeEl) {
    if (alarm.receivedAtMs && alarm.receivedAtMs > 1000000000) {
      const elapsed = Math.floor((Date.now() / 1000) - alarm.receivedAtMs);
      timeEl.textContent = elapsed < 5 ? "Právě teď" : "Přijato před " + formatDurationMs(elapsed * 1000);
    } else {
      timeEl.textContent = "";
    }
  }
}

// ============================================================
// MAIN CONSUME
// ============================================================

function consumePayload(data) {
  renderSystem(data.system);
  renderSvatek(data.system);
  renderClimate(data.climate);
  renderWeather(data.weather);
  renderMoon(data.weather);
  renderDesktop(data.desktop);
  renderDevices(data.devices);
  renderAlarm(data.alarm);
  lastUpdateTime = new Date();
  setWsStatus("online", true);
}

// ============================================================
// API CALLS
// ============================================================

async function apiPost(path) {
  try {
    const res = await fetch(path, { method: "POST", cache: "no-store" });
    if (!res.ok) console.error(`POST ${path} failed`, res.status);
    await fetchStatusFallback();
  } catch (err) {
    console.error(`POST ${path} error:`, err);
  }
}

async function fetchStatusFallback() {
  try {
    const res = await fetch("/api/status", { method: "GET", cache: "no-store" });
    const data = await res.json();
    consumePayload(data);
  } catch (err) {
    console.error("Status fetch failed:", err);
  }
}

async function refreshNow()    { await apiPost("/api/refresh"); }
async function sendSvatek()    { await apiPost("/api/send"); }
async function updateTime()    { await apiPost("/api/update-time"); }
async function resetLastSent() { await apiPost("/api/reset-last-sent"); }
async function wledToggle()    { await apiPost("/api/wled/toggle"); }
async function wledOn()        { await apiPost("/api/wled/on"); }
async function wledOff()       { await apiPost("/api/wled/off"); }
async function wledIdentify()  { await apiPost("/api/wled/identify"); }

async function wledBrightness(value) {
  const bri = Math.max(0, Math.min(255, Number(value || 0)));
  await fetch(`/api/wled/brightness?value=${bri}`, { method: "POST", cache: "no-store" });
  await fetchStatusFallback();
}

// ============================================================
// WEBSOCKET
// ============================================================

function connectWebSocket() {
  if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
  const proto = location.protocol === "https:" ? "wss" : "ws";
  socket = new WebSocket(`${proto}://${location.host}/ws`);

  socket.onopen    = () => setWsStatus("online", true);
  socket.onmessage = (event) => {
    try {
      const msg = JSON.parse(event.data);
      if (msg.type === "devices") consumePayload(msg);
    } catch (err) { console.error("WS parse failed:", err); }
  };
  socket.onclose = () => {
    setWsStatus("reconnecting...", null);
    reconnectTimer = setTimeout(connectWebSocket, 2000);
  };
  socket.onerror = () => {
    setWsStatus("error", false);
    try { socket.close(); } catch (_) {}
  };
}

// ============================================================
// INIT
// ============================================================

showModeGateIfNeeded();
connectWebSocket();
fetchStatusFallback();
setInterval(fetchStatusFallback, 5000);

// ============================================================
// SLEDOVANE SVATKY (persist: /watch.json na ESP)
// ============================================================

let watchList = [];
let namesDb = null;

function normName(x) {
  return String(x).normalize("NFD").replace(/[\u0300-\u036f]/g, "").toLowerCase().trim();
}

async function loadWatch() {
  try {
    const [w, n] = await Promise.all([
      fetch("/watch.json", { cache: "no-store" }).then(r => r.ok ? r.json() : []),
      namesDb ? Promise.resolve(namesDb) : fetch("/names.json", { cache: "no-store" }).then(r => r.json())
    ]);
    watchList = Array.isArray(w) ? w : [];
    namesDb = n;
  } catch (e) { /* ignore */ }
  renderWatch();
}

async function saveWatch() {
  renderWatch();
  try {
    await fetch(`/api/watch?names=${encodeURIComponent(watchList.join(","))}`, { method: "POST", cache: "no-store" });
  } catch (e) { /* ignore */ }
}

function addWatch() {
  const inp = document.getElementById("watch-input");
  const v = inp ? inp.value.trim() : "";
  if (!v) return;
  if (!watchList.some(x => normName(x) === normName(v))) watchList.push(v);
  inp.value = "";
  saveWatch();
}

function removeWatch(i) {
  watchList.splice(i, 1);
  saveWatch();
}

function renderWatch() {
  const chips = document.getElementById("watch-chips");
  const up = document.getElementById("watch-upcoming");
  const sum = document.getElementById("watch-summary");
  if (!chips || !up || !sum) return;

  chips.innerHTML = watchList.map((n, i) =>
    `<span class="watch-chip">${escapeHtml(n)} <button type="button" onclick="removeWatch(${i})">✕</button></span>`
  ).join("") || `<div class="empty">Zatím nic nesleduješ.</div>`;

  const dayNames = ["Ne","Po","Út","St","Čt","Pá","So"];
  const wanted = new Set(watchList.map(normName));
  const hits = [];
  if (namesDb && wanted.size) {
    const now = new Date();
    for (let d = 0; d < 7; d++) {
      const dt = new Date(now.getFullYear(), now.getMonth(), now.getDate() + d);
      const key = String(dt.getMonth() + 1).padStart(2, "0") + "-" + String(dt.getDate()).padStart(2, "0");
      const entries = namesDb[key] || [];
      const tokens = entries.join(" ").split(/[^\p{L}]+/u).map(normName).filter(Boolean);
      const matched = watchList.filter(w => tokens.includes(normName(w)));
      if (matched.length) hits.push({ d, dt, matched });
    }
  }

  up.innerHTML = hits.length
    ? hits.map(h => {
        const when = h.d === 0 ? "dnes" : h.d === 1 ? "zítra" : `za ${h.d} dny`;
        const date = `${dayNames[h.dt.getDay()]} ${h.dt.getDate()}. ${h.dt.getMonth() + 1}.`;
        return row(`${date} · ${when}`, h.matched.join(", "));
      }).join("")
    : `<div class="empty">${watchList.length ? "Tento týden nikdo ze sledovaných." : "-"}</div>`;

  sum.textContent = hits.length
    ? hits.map(h => `${h.matched.join(", ")} (${h.d === 0 ? "dnes" : h.d === 1 ? "zítra" : dayNames[h.dt.getDay()]})`).join(" · ")
    : (watchList.length ? "Tento týden nikdo" : "Nic nesleduješ");
}

loadWatch();
setInterval(renderWatch, 10 * 60 * 1000);

// ============================================================
// TOPENI (IR)
// ============================================================

let heaterState = null;
let heaterSyncSel = { on: false, mode: 0 };
const HEATER_MODES = ["Ventilátor", "Topení I (1000 W)", "Topení II (2000 W)"];

function fmtHm(sec) {
  const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60);
  return h > 0 ? `${h} h ${String(m).padStart(2, "0")} min` : `${m} min`;
}

function renderHeater() {
  const st = heaterState;
  const pill = document.getElementById("heater-pill-state");
  if (!pill) return;
  if (!st) { pill.textContent = "-"; pill.className = "hpill"; return; }
  pill.textContent = st.on ? "ON" : "OFF";
  pill.className = "hpill" + (st.on ? " hp-on" : "");
  const ctl = document.getElementById("heater-pill-ctl");
  ctl.textContent = st.control === "auto" ? "Auto" : "Manual";
  ctl.className = "hpill " + (st.control === "auto" ? "hp-auto" : "hp-man");
  const info = document.getElementById("heater-info");
  info.innerHTML = st.on
    ? `<div><span class="k">Odhad režimu:</span> <b>${escapeHtml(HEATER_MODES[st.mode] || "-")}</b></div>` +
      `<div><span class="k">Zapnuto:</span> <b>${fmtHm(st.onForS)}</b></div>` +
      `<div><span class="k">Automatické vypnutí za:</span> <b>${fmtHm(st.manualLeftS)}</b></div>`
    : `<div><span class="k">Odhad stavu:</span> <b>vypnuto</b></div>`;
}

async function fetchHeater() {
  try {
    const res = await fetch("/api/heater", { cache: "no-store" });
    if (!res.ok) return;
    heaterState = await res.json();
    renderHeater();
  } catch (e) { /* offline */ }
}

async function heaterBtn(name) {
  try {
    await fetch(`/api/heater/btn?btn=${encodeURIComponent(name)}`, { method: "POST", cache: "no-store" });
  } catch (e) { /* ignore */ }
  setTimeout(fetchHeater, 900);
}

function heaterRenderSync() {
  document.getElementById("hs-on").className = "hbig" + (heaterSyncSel.on ? " sel-on" : "");
  document.getElementById("hs-off").className = "hbig" + (!heaterSyncSel.on ? " sel-off" : "");
  document.getElementById("hs-modes-wrap").style.display = heaterSyncSel.on ? "" : "none";
  document.querySelectorAll("#heater-dlg .hchip").forEach(b => {
    b.className = "hchip" + (Number(b.dataset.m) === heaterSyncSel.mode ? " sel" : "");
  });
}
function heaterOpenSync() {
  heaterSyncSel = { on: !!(heaterState && heaterState.on), mode: heaterState ? heaterState.mode : 0 };
  heaterRenderSync();
  document.getElementById("heater-dlg").hidden = false;
}
function heaterCloseSync() { document.getElementById("heater-dlg").hidden = true; }
function heaterSyncPick(on) { heaterSyncSel.on = on; heaterRenderSync(); }
function heaterSyncMode(m) { heaterSyncSel.mode = m; heaterRenderSync(); }
async function heaterSyncSave() {
  try {
    await fetch(`/api/heater/sync?on=${heaterSyncSel.on ? 1 : 0}&mode=${heaterSyncSel.mode}`, { method: "POST", cache: "no-store" });
  } catch (e) { /* ignore */ }
  heaterCloseSync();
  fetchHeater();
}

fetchHeater();
setInterval(fetchHeater, 10000);
