// Helper: kiểm tra MAC hợp lệ (AA:BB:CC:DD:EE:FF)
function macIsValid(mac) {
  return /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/.test((mac || "").trim());
}

async function loadConfig() {
  // 1) Config chung + Battery/PSU
  const res = await fetch("/api/config");
  if (!res.ok) throw new Error("GET /api/config failed");
  const cfg = await res.json();

  // --- General ---
  deviceName.value = cfg.device_name ?? "";
  uuid.value = cfg.uuid ?? "";
  timezone.value = cfg.timezone ?? 0;

  // psu_slave_addr từ server là "1D" (string) hoặc số -> luôn hiển thị HEX in hoa
  const slaveHex = (typeof cfg.psu_slave_addr === "string")
    ? cfg.psu_slave_addr
    : Number(cfg.psu_slave_addr || 0).toString(16);
  slaveAddr.value = slaveHex.toUpperCase();

  snmpEnable.value = cfg.snmp_enable ?? 0;
  ipSnmp.value = cfg.ip_snmp || "";
  trapMask.value = (cfg.trap_enable_mask ?? 0).toString(16).toUpperCase();
  overTemp.value = cfg.psu_overtemp_C ?? "";
  overCurrent.value = cfg.psu_overcurrent_A ?? "";
  underVolt.value = cfg.psu_in_undervolt_V ?? "";

  // --- Battery / PSU (5 field) ---
  if (document.getElementById("advPsuType")) {
    advPsuType.value = cfg.psu_type ?? 48;
    advBattAh.value = cfg.battery_capacity_Ah ?? 20.0;
    advBattCutoff.value = cfg.battery_cutoff_V ?? 48.0;
    advBattFull.value = cfg.battery_full_V ?? 58.4;
    advRuntimeFactor.value = cfg.runtime_factor ?? 1.00;
  }

  // 2) MAC: ưu tiên lấy từ /api/network (nếu backend đã trả mac)
  //    nếu không có thì fallback /api/device
  let macStr = "";
  try {
    const rn = await fetch("/api/network");
    if (rn.ok) {
      const nw = await rn.json();
      if (nw.mac) macStr = nw.mac;
    }
  } catch (_) {}
  if (!macStr) {
    try {
      const rd = await fetch("/api/device");
      if (rd.ok) {
        const dev = await rd.json();
        macStr = dev.mac || "";
      }
    } catch (_) {}
  }
  if (document.getElementById("advMac")) {
    advMac.value = (macStr || "").toUpperCase();
  }
}

async function saveConfig() {
  // ===== Body cho /api/config (General + Battery/PSU) =====
  const body = {
    device_name: deviceName.value,
    timezone: parseInt(timezone.value),

    // Gửi dạng HEX string (upper-case) cho slave addr
    psu_slave_addr: (slaveAddr.value || "00").toUpperCase(),

    snmp_enable: parseInt(snmpEnable.value),
    ip_snmp: ipSnmp.value.trim(),
    trap_enable_mask: parseInt(trapMask.value || "0", 16),

    psu_overtemp_C: parseFloat(overTemp.value),
    psu_overcurrent_A: parseFloat(overCurrent.value),
    psu_in_undervolt_V: parseFloat(underVolt.value),

    // --- Battery / PSU fields ---
    psu_type: parseInt(advPsuType?.value ?? 48),
    battery_capacity_Ah: parseFloat(advBattAh?.value ?? 0),
    battery_cutoff_V: parseFloat(advBattCutoff?.value ?? 0),
    battery_full_V: parseFloat(advBattFull?.value ?? 0),
    runtime_factor: Math.max(
      0.50,
      Math.min(1.50, parseFloat(advRuntimeFactor?.value ?? 1.0))
    ),
  };

  // Gửi /api/config
  const res = await fetch("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });

  if (!res.ok) {
    alert("❌ Failed to save configuration!");
    return;
  }

  // ===== Gửi MAC qua /api/network (nếu có nhập và hợp lệ) =====
  const macField = document.getElementById("advMac");
  if (macField) {
    const mac = macField.value.trim().toUpperCase();
    if (mac && macIsValid(mac)) {
      try {
        const r2 = await fetch("/api/network", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ mac }),
        });
        if (!r2.ok) throw new Error();
      } catch {
        alert("⚠️ Config saved, nhưng lưu MAC thất bại.");
        return;
      }
    } else if (mac) {
      alert("⚠️ MAC không hợp lệ (định dạng AA:BB:CC:DD:EE:FF). Đã bỏ qua đặt MAC.");
    }
  }

  alert("✅ Configuration saved successfully!");
}

// Tự động tải config khi vào trang
window.onload = loadConfig;
