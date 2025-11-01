// ================== STATUS ==================
async function updateData() {
  const spinner = document.getElementById("spinner");
  spinner.style.display = "block";
  try {
    const res = await fetch("/api/status");
    if (!res.ok) throw new Error("HTTP " + res.status);
    const data = await res.json();

    document.getElementById("vin").textContent = data.vin.toFixed(2) + " V";
    document.getElementById("vout").textContent = data.vout.toFixed(2) + " V";
    document.getElementById("iout").textContent = data.iout.toFixed(3) + " A";
    document.getElementById("temp").textContent = data.temp.toFixed(1) + " °C";
    document.getElementById("fault_raw").textContent =
      "0x" + data.fault_raw.toString(16).toUpperCase().padStart(2, "0");
    document.getElementById("fault_detail").textContent =
      `FAN:${data.fan_fail ? "⚠️" : "OK"} | ` +
      `OTP:${data.otp ? "⚠️" : "OK"} | ` +
      `OVP:${data.ovp ? "⚠️" : "OK"} | ` +
      `OLP:${data.olp ? "⚠️" : "OK"} | ` +
      `AC:${data.ac_fail ? "⚠️" : "OK"}`;
  } catch (err) {
    document.getElementById("fault_raw").textContent = "Disconnected";
    document.getElementById("fault_detail").textContent = "No data";
  } finally {
    spinner.style.display = "none";
  }
}
setInterval(updateData, 10000);

// ================== PAGE LOAD ==================
window.onload = () => {
  updateData();
  loadNetwork();
  loadConfig();
  loadTheme();
  refreshDevice();
  refreshLogs();

  setInterval(() => {
    if (document.getElementById("autoRefresh").checked) refreshLogs();
  }, 5000);
};

// ================== TABS ==================
function openTab(evt, tabName) {
  document.querySelectorAll(".tabcontent").forEach(tab => (tab.style.display = "none"));
  document.querySelectorAll(".tablink").forEach(btn => btn.classList.remove("active"));
  document.getElementById(tabName).style.display = "block";
  evt.currentTarget.classList.add("active");
}

// ================== NETWORK ==================
async function loadNetwork() {
  try {
    const res = await fetch("/api/network");
    const data = await res.json();

    dhcp.value = data.dhcp;
    ip.value = data.ip;
    mask.value = data.mask;
    gw.value = data.gw;
    dns.value = data.dns;
    toggleDHCPFields();
  } catch (err) {
    console.warn("Network load failed:", err);
  }
}

function toggleDHCPFields() {
  const disabled = dhcp.value === "1";
  [ip, mask, gw, dns].forEach(el => (el.disabled = disabled));
}

dhcp.addEventListener("change", toggleDHCPFields);

async function saveNetwork() {
  const cfg = {
    dhcp: parseInt(dhcp.value),
    ip: ip.value,
    mask: mask.value,
    gw: gw.value,
    dns: dns.value,
  };

  try {
    const res = await fetch("/api/network", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(cfg),
    });

    const msg = document.getElementById("saveStatus");
    if (res.ok) {
      msg.textContent = "✅ Saved successfully!";
      msg.style.color = "green";
    } else {
      msg.textContent = "❌ Failed to save!";
      msg.style.color = "red";
    }
  } catch {
    saveStatus.textContent = "⚠️ Connection error.";
  }
}

// ================== CONFIG ==================
async function loadConfig() {
  try {
    const res = await fetch("/api/config");
    const cfg = await res.json();

    document.getElementById("deviceName").value = cfg.device_name;
    document.getElementById("uuid").value = cfg.uuid;
    document.getElementById("timezone").value = cfg.timezone;
    document.getElementById("slaveAddr").value = cfg.psu_slave_addr;
    document.getElementById("snmpEnable").value = cfg.snmp_enable;
    document.getElementById("trapMask").value = cfg.trap_enable_mask.toString(16).toUpperCase();
    document.getElementById("sntpEnable").value = cfg.sntp_enable;
    document.getElementById("webEnable").value = cfg.web_enable;
    document.getElementById("underVolt").value = cfg.psu_in_undervolt_V;
    document.getElementById("overTemp").value = cfg.psu_overtemp_C;
    document.getElementById("overCurrent").value = cfg.psu_overcurrent_A;
  } catch (err) {
    console.warn("Config load failed:", err);
  }
}

async function saveConfig() {
  const body = {
    device_name: document.getElementById("deviceName").value,
    timezone: parseInt(document.getElementById("timezone").value),
    psu_slave_addr: parseInt(document.getElementById("slaveAddr").value),
    snmp_enable: parseInt(document.getElementById("snmpEnable").value),
    trap_enable_mask: parseInt(document.getElementById("trapMask").value, 16),
    sntp_enable: parseInt(document.getElementById("sntpEnable").value),
    web_enable: parseInt(document.getElementById("webEnable").value),
    psu_in_undervolt_V: parseFloat(document.getElementById("underVolt").value),
    psu_overtemp_C: parseFloat(document.getElementById("overTemp").value),
    psu_overcurrent_A: parseFloat(document.getElementById("overCurrent").value),
  };

  try {
    const res = await fetch("/api/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const msg = document.getElementById("cfgStatus");
    if (res.ok) {
      msg.textContent = "✅ Configuration saved!";
      msg.style.color = "green";
    } else {
      msg.textContent = "❌ Failed to save configuration!";
      msg.style.color = "red";
    }
  } catch {
    cfgStatus.textContent = "⚠️ Save error (network)";
  }
}

// ================== DEVICE ==================
async function refreshDevice() {
  const sp = document.getElementById("spinnerDev");
  sp.style.display = "block";
  try {
    const res = await fetch("/api/device");
    const info = await res.json();
    mac.textContent = info.mac || "--";
    fw.textContent = info.firmware || "--";
    board.textContent = info.board || "--";
    uptime.textContent = info.uptime || "--";
    sync.textContent = new Date().toLocaleTimeString();
  } catch (err) {
    mac.textContent = "--";
  } finally {
    sp.style.display = "none";
  }
}

// ================== LOGS ==================
async function refreshLogs() {
  const sp = document.getElementById("spinnerLogs");
  const container = document.getElementById("logContainer");
  sp.style.display = "block";
  try {
    const res = await fetch("/api/logs");
    if (!res.ok) throw new Error("HTTP " + res.status);
    const text = await res.text();
    container.textContent = text || "No logs yet.";
    if (document.getElementById("autoScroll").checked)
      container.scrollTop = container.scrollHeight;
  } catch {
    container.textContent = "⚠️ Failed to fetch logs.";
  } finally {
    sp.style.display = "none";
  }
}
function clearLogs() {
  document.getElementById("logContainer").textContent = "";
}

// ================== THEME ==================
function toggleTheme() {
  const isLight = document.body.classList.toggle("light-mode");
  localStorage.setItem("theme", isLight ? "light" : "dark");
  themeToggle.textContent = isLight ? "☀️" : "🌙";
}
function loadTheme() {
  const t = localStorage.getItem("theme") || "dark";
  if (t === "light") document.body.classList.add("light-mode");
  themeToggle.textContent = t === "light" ? "☀️" : "🌙";
}
