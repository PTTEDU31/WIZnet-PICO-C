// =============================================================
//  advanced.js — Full configuration handler for PSU-Monitor WebUI
// =============================================================

// Global config object
let gConfig = {};

// ===== Helper: Fetch JSON safely =====
async function fetchConfig() {
  try {
    const res = await fetch("/api/config");
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    gConfig = await res.json();
    console.log("Loaded config:", gConfig);
    loadConfig(gConfig);
  } catch (e) {
    console.error("Failed to load config:", e);
    alert("⚠️ Cannot load configuration.");
  }
}

// ===== Helper: Save config =====
async function saveConfig() {
  try {
    const body = collectConfig();
    console.log("Saving config:", body);

    const res = await fetch("/api/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body, null, 2),
    });

    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    alert("✅ Configuration saved successfully!");
  } catch (e) {
    console.error("Failed to save:", e);
    alert("⚠️ Failed to save configuration.");
  }
}

// =============================================================
//  Collect data from HTML form
// =============================================================
function collectConfig() {
  const body = {
    // --- General info ---
    device_name: document.getElementById("deviceName")?.value.trim() ?? "PSU-Monitor",
    uuid: document.getElementById("uuid")?.value.trim() ?? "00000000-0000-0000-0000-000000000000",
    timezone: parseInt(document.getElementById("timezone")?.value ?? 7),

    // --- Network ---
    mac: document.getElementById("netMask")?.value.trim().toUpperCase() ?? "00:08:DC:12:34:56",
    ip: document.getElementById("ipAddr")?.value.trim(),
    mask: document.getElementById("netMask")?.value.trim(),
    gw: document.getElementById("gateway")?.value.trim(),
    dns: document.getElementById("dns")?.value.trim(),
    dhcp_enable: parseInt(document.getElementById("dhcpEnable")?.value ?? 0),

    // --- SNMP ---
    ip_snmp: document.getElementById("ipSnmp")?.value.trim(),
    snmp_enable: parseInt(document.getElementById("snmpEnable")?.value ?? 0),
    trap_enable_mask: parseInt(document.getElementById("trapMask")?.value ?? 7),
    sntp_enable: parseInt(document.getElementById("sntpEnable")?.value ?? 1),
    web_enable: parseInt(document.getElementById("webEnable")?.value ?? 1),

    // --- PSU ---
    psu_slave_addr: document.getElementById("slaveAddr")?.value.trim() ?? "83",
    psu_in_undervolt_V: parseFloat(document.getElementById("underVolt")?.value ?? 180),
    psu_overtemp_C: parseFloat(document.getElementById("overTemp")?.value ?? 70),
    psu_overcurrent_A: parseFloat(document.getElementById("overCurrent")?.value ?? 2.5),

    // --- Battery profile ---
    profile: {
      battery_type: parseInt(document.getElementById("battType")?.value ?? 1),
      capacity_ah: parseFloat(document.getElementById("battCapacity")?.value ?? 20),
      cutoff_low: parseFloat(document.getElementById("battCutoff")?.value ?? 21),
      warning: parseFloat(document.getElementById("battWarn")?.value ?? 22.5),
      critical: parseFloat(document.getElementById("battCritical")?.value ?? 22),

      warn_60min: document.getElementById("warn60")?.checked ? 1 : 0,
      warn_30min: document.getElementById("warn30")?.checked ? 1 : 0,
      warn_15min: document.getElementById("warn15")?.checked ? 1 : 0,
      warn_5min: document.getElementById("warn5")?.checked ? 1 : 0,
      warn_soc30: document.getElementById("warnSoc30")?.checked ? 1 : 0,
      warn_soc20: document.getElementById("warnSoc20")?.checked ? 1 : 0,
      warn_soc10: document.getElementById("warnSoc10")?.checked ? 1 : 0,

      site_identifier: document.getElementById("siteId")?.value.trim(),
      snmp_trap_dest: document.getElementById("trapDest")?.value.trim(),
      snmp_trap_port: parseInt(document.getElementById("trapPort")?.value ?? 162),

      current_sensor_offset: parseFloat(document.getElementById("currOffset")?.value ?? 0),
      voltage_sensor_scale: parseFloat(document.getElementById("voltScale")?.value ?? 1),

      auto_shutdown: document.getElementById("autoShutdown")?.checked ? 1 : 0,
      learned_capacity_mAh_avg: parseInt(document.getElementById("learnAvg")?.value ?? 0),
      learned_capacity_mAh_best: parseInt(document.getElementById("learnBest")?.value ?? 0),
    },
  };

  return body;
}

// =============================================================
//  Load configuration into form
// =============================================================
function loadConfig(cfg) {
  if (!cfg) return;

  // General
  document.getElementById("deviceName").value = cfg.device_name ?? "";
  document.getElementById("uuid").value = cfg.uuid ?? "";
  document.getElementById("timezone").value = cfg.timezone ?? 7;

  // Network
  document.getElementById("mac").value = cfg.mac ?? "";
  document.getElementById("ipAddr").value = cfg.ip ?? "";
  document.getElementById("netMask").value = cfg.mask ?? "";
  document.getElementById("gateway").value = cfg.gw ?? "";
  document.getElementById("dns").value = cfg.dns ?? "";
  document.getElementById("dhcpEnable").value = cfg.dhcp_enable ?? 0;

  // SNMP
  document.getElementById("ipSnmp").value = cfg.ip_snmp ?? "";
  document.getElementById("snmpEnable").value = cfg.snmp_enable ?? 0;
  document.getElementById("trapMask").value = cfg.trap_enable_mask ?? 0;
  document.getElementById("sntpEnable").value = cfg.sntp_enable ?? 1;
  document.getElementById("webEnable").value = cfg.web_enable ?? 1;

  // PSU
  document.getElementById("slaveAddr").value = cfg.psu_slave_addr ?? "";
  document.getElementById("underVolt").value = cfg.psu_in_undervolt_V ?? 180;
  document.getElementById("overTemp").value = cfg.psu_overtemp_C ?? 70;
  document.getElementById("overCurrent").value = cfg.psu_overcurrent_A ?? 2.5;

  // Battery profile
  const p = cfg.profile ?? {};
  document.getElementById("battType").value = p.battery_type ?? 1;
  // updateBatteryVoltageLabel();
  document.getElementById("battCapacity").value = p.capacity_ah ?? 0;
  document.getElementById("battCutoff").value = p.cutoff_low ?? 0;
  document.getElementById("battWarn").value = p.warning ?? 0;
  document.getElementById("battCritical").value = p.critical ?? 0;

  document.getElementById("warn60").checked = p.warn_60min ?? false;
  document.getElementById("warn30").checked = p.warn_30min ?? false;
  document.getElementById("warn15").checked = p.warn_15min ?? false;
  document.getElementById("warn5").checked = p.warn_5min ?? false;
  document.getElementById("warnSoc30").checked = p.warn_soc30 ?? false;
  document.getElementById("warnSoc20").checked = p.warn_soc20 ?? false;
  document.getElementById("warnSoc10").checked = p.warn_soc10 ?? false;

  document.getElementById("siteId").value = p.site_identifier ?? "";
  document.getElementById("trapDest").value = p.snmp_trap_dest ?? "";
  document.getElementById("trapPort").value = p.snmp_trap_port ?? 162;
  document.getElementById("currOffset").value = p.current_sensor_offset ?? 0;
  document.getElementById("voltScale").value = p.voltage_sensor_scale ?? 1;
  document.getElementById("autoShutdown").checked = p.auto_shutdown ?? false;
  document.getElementById("learnAvg").value = p.learned_capacity_mAh_avg ?? 0;
  document.getElementById("learnBest").value = p.learned_capacity_mAh_best ?? 0;
}

// =============================================================
//  Button bindings
// =============================================================
document.addEventListener("DOMContentLoaded", () => {
  document.getElementById("btnLoad")?.addEventListener("click", fetchConfig);
  document.getElementById("btnSave")?.addEventListener("click", saveConfig);
  fetchConfig();
});


// function updateBatteryVoltageLabel() {
//   const typeEl = document.getElementById("battType");
//   const labelEl = document.getElementById("battVoltageLabel");
//   if (!typeEl || !labelEl) return;

//   const val = parseInt(typeEl.value || 0);
//   if (val > 0) {
//     labelEl.textContent = `→ ${val * 12}V nominal`;
//   } else {
//     labelEl.textContent = "→ N/A";
//   }
// }

// // Gọi lại mỗi khi người dùng thay đổi loại pin
// document.addEventListener("DOMContentLoaded", () => {
//   const typeEl = document.getElementById("battType");
//   if (typeEl) typeEl.addEventListener("input", updateBatteryVoltageLabel);
// });
