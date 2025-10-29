async function loadConfig() {
  const res = await fetch("/api/config");
  const cfg = await res.json();

  deviceName.value = cfg.device_name;
  uuid.value = cfg.uuid;
  timezone.value = cfg.timezone;
  // ✅ Hiển thị giá trị HEX (ví dụ "1D")
  slaveAddr.value = cfg.psu_slave_addr.toUpperCase();

  snmpEnable.value = cfg.snmp_enable;
  ipSnmp.value = cfg.ip_snmp || "";
  trapMask.value = cfg.trap_enable_mask.toString(16).toUpperCase();
  overTemp.value = cfg.psu_overtemp_C;
  overCurrent.value = cfg.psu_overcurrent_A;
  underVolt.value = cfg.psu_in_undervolt_V;
}

async function saveConfig() {
  const body = {
    device_name: deviceName.value,
    timezone: parseInt(timezone.value),
    // ✅ Gửi dạng HEX string (không parseInt)
    psu_slave_addr: slaveAddr.value.toUpperCase(),
    snmp_enable: parseInt(snmpEnable.value),
    ip_snmp: ipSnmp.value,
    trap_enable_mask: parseInt(trapMask.value, 16),
    psu_overtemp_C: parseFloat(overTemp.value),
    psu_overcurrent_A: parseFloat(overCurrent.value),
    psu_in_undervolt_V: parseFloat(underVolt.value)
  };

  const res = await fetch("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  });

  if (res.ok) {
    alert("✅ Configuration saved successfully!");
  } else {
    alert("❌ Failed to save configuration!");
  }
}

window.onload = loadConfig;
