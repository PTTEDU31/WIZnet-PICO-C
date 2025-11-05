// app_config_storage.c
#include "app_config.h"
#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "pico/flash.h"
#include "hardware/sync.h"
#include "hardware/regs/addressmap.h"   // XIP_BASE
#include "cJSON.h"

#include "snmp_custom.h"

// ===== CONFIG FLASH LAYOUT =====
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)     // điều chỉnh theo board
#endif

#define CFG_FLASH_OFFSET    (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE) // sector cuối
#define CFG_XIP_ADDR        ((const uint8_t *)(XIP_BASE + CFG_FLASH_OFFSET))

// ===== IMAGE FORMAT =====
typedef struct {
    uint32_t magic;
    uint16_t ver;
    uint16_t len;          // sizeof(app_config_t)
    app_config_t cfg;      // payload
    uint32_t crc32;        // CRC32 của 'cfg'
} __attribute__((packed)) cfg_image_t;

static app_config_t g_cfg;

// ===== CRC32 (poly 0xEDB88320, init/xor = 0xFFFFFFFF) =====
static uint32_t __not_in_flash_func(crc32_update)(uint32_t c, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    c = ~c;
    for (size_t i = 0; i < len; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (-(int)(c & 1)));
    }
    return ~c;
}

static bool __not_in_flash_func(cfg_is_valid)(const cfg_image_t *img) {
    if (img->magic != CFG_MAGIC || img->ver != CFG_VER) return false;
    if (img->len   != sizeof(app_config_t))            return false;
    if (img->len   == 0)                               return false;
    uint32_t crc = crc32_update(0, &img->cfg, sizeof(app_config_t));
    return (crc == img->crc32);
}

// ===== PSU defaults (giữ theo code của bạn) =====
void psu_profile_apply_defaults(psu_config_t *p) {
    if (!p) return;
    switch (p->battery_type) {
    case BATT_TYPE_12V:
        p->voltage_cutoff_low = 10.8f; // emergency
        p->voltage_warning    = 11.6f; // ~30% SOC
        p->voltage_critical   = 11.3f; // ~20% SOC
        break;
    case BATT_TYPE_24V:
        p->voltage_cutoff_low = 21.6f;
        p->voltage_warning    = 23.2f;
        p->voltage_critical   = 22.6f;
        break;
    case BATT_TYPE_36V:
        p->voltage_cutoff_low = 32.4f;
        p->voltage_warning    = 34.8f;
        p->voltage_critical   = 33.9f;
        break;
    case BATT_TYPE_48V:
    default:
        p->voltage_cutoff_low = 43.2f;
        p->voltage_warning    = 46.4f;
        p->voltage_critical   = 45.2f;
        break;
    }
}

// ===== App defaults (giữ theo code của bạn) =====
void app_cfg_reset_default(void) {
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.mac[0] = 0x00; g_cfg.mac[1] = 0x08; g_cfg.mac[2] = 0xDC;
    g_cfg.mac[3] = 0x12; g_cfg.mac[4] = 0x34; g_cfg.mac[5] = 0x56;

    g_cfg.ip[0]=192; g_cfg.ip[1]=168; g_cfg.ip[2]=137; g_cfg.ip[3]=123;
    g_cfg.sn[0]=255; g_cfg.sn[1]=255; g_cfg.sn[2]=255; g_cfg.sn[3]=0;
    g_cfg.gw[0]=192; g_cfg.gw[1]=168; g_cfg.gw[2]=137; g_cfg.gw[3]=1;
    g_cfg.dns[0]=8;  g_cfg.dns[1]=8;  g_cfg.dns[2]=8;  g_cfg.dns[3]=8;
    g_cfg.dhcp_enable = 0;

    g_cfg.snmp_manager_ip[0]=192; g_cfg.snmp_manager_ip[1]=168;
    g_cfg.snmp_manager_ip[2]=137; g_cfg.snmp_manager_ip[3]=1;
    g_cfg.trap_enable_mask = 0x07;
    g_cfg.snmp_enable = 1;

    g_cfg.psu_slave_addr      = 131;
    g_cfg.psu_in_undervolt_V  = 180.0f;
    g_cfg.psu_out_overvolt_V  = 250.0f;
    g_cfg.psu_overcurrent_A   = 2.5f;
    g_cfg.psu_overtemp_C      = 70.0f;
    g_cfg.hysteresis_pct      = 2.0f;
    g_cfg.debounce_ms         = 1000;

    // Tự apply thresholds theo battery_type
    psu_profile_apply_defaults(&g_cfg.psu);

    // Trap flags mặc định
    g_cfg.psu.warning_60min_enabled = 1;
    g_cfg.psu.warning_30min_enabled = 1;
    g_cfg.psu.warning_15min_enabled = 1;
    g_cfg.psu.warning_5min_enabled  = 1;
    g_cfg.psu.warning_soc30_enabled = 1;
    g_cfg.psu.warning_soc20_enabled = 1;
    g_cfg.psu.warning_soc10_enabled = 1;

    // Site & SNMP trap dest
    strcpy(g_cfg.psu.site_identifier, "Building-5-IDF2");
    strcpy(g_cfg.psu.snmp_trap_dest,  "192.168.137.1");
    g_cfg.psu.snmp_trap_port = 162;

    // Advanced calib
    g_cfg.psu.current_sensor_offset = 0.0f;
    g_cfg.psu.voltage_sensor_scale  = 1.000f;
    g_cfg.psu.enable_auto_shutdown  = 1;

    strcpy(g_cfg.device_name, "PSU-Monitor");
    strcpy(g_cfg.uuid,        "00000000-0000-0000-0000-000000000000");
    g_cfg.timezone    = +7;
    g_cfg.web_enable  = 1;
    g_cfg.sntp_enable = 1;
    g_cfg.mqtt_enable = 0;
    g_cfg.ota_enable  = 0;
}

// ======== FLASH WRITE BLOCK (RAM) ========
typedef struct {
    uint32_t    offset;   // offset trong Flash (không cộng XIP_BASE)
    const uint8_t *data;  // con trỏ RAM
    size_t      length;   // tổng byte cần ghi (align 256B)
} flash_wr_params_t;

static void __no_inline_not_in_flash_func(call_flash_write_block)(void *param) {
    flash_wr_params_t *p = (flash_wr_params_t*)param;
    uint32_t ints = save_and_disable_interrupts();

    // Giả định offset đã thẳng hàng sector
    flash_range_erase(p->offset, FLASH_SECTOR_SIZE);

    size_t off = 0;
    while (off < p->length) {
        flash_range_program(p->offset + off, p->data + off, FLASH_PAGE_SIZE);
        off += FLASH_PAGE_SIZE;
    }

    restore_interrupts(ints);
    flash_flush_cache(); // Quan trọng: làm mới cache XIP
}

static inline size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

// Buffer RAM 1 sector (trong .bss, không để trên stack)
static uint8_t s_ram_buf[FLASH_SECTOR_SIZE] __attribute__((aligned(FLASH_PAGE_SIZE)));

// ===== SAVE / LOAD =====
bool __not_in_flash_func(app_cfg_save)(void) {
    cfg_image_t img;
    img.magic = CFG_MAGIC;
    img.ver   = CFG_VER;
    img.len   = sizeof(app_config_t);
    img.cfg   = g_cfg;
    img.crc32 = crc32_update(0, &img.cfg, sizeof(app_config_t));

    const size_t total   = sizeof(img);
    const size_t aligned = align_up(total, FLASH_PAGE_SIZE);
    if (aligned > FLASH_SECTOR_SIZE) {
        printf("[FLASH] ❌ Image too large for 1 sector (%u > %u)\n",
               (unsigned)aligned, (unsigned)FLASH_SECTOR_SIZE);
        return false;
    }

    // Build image vào RAM (đừng dùng const/flash)
    memset(s_ram_buf, 0xFF, sizeof(s_ram_buf));
    memcpy(s_ram_buf, &img, total);

    printf("\n[FLASH] Saving config via flash_safe_execute()...\n");

    flash_wr_params_t params = {
        .offset = CFG_FLASH_OFFSET,
        .data   = s_ram_buf,
        .length = aligned
    };

    int rc = flash_safe_execute(call_flash_write_block, &params, UINT32_MAX);
    if (rc != PICO_OK) {
        printf("[FLASH] ❌ Write block failed! rc=%d\n", rc);
        return false;
    }

    // Validate ngay sau khi ghi
    const cfg_image_t *chk = (const cfg_image_t *)(CFG_XIP_ADDR);
    bool ok = cfg_is_valid(chk);
    printf("[FLASH] Validation: %s (CRC=0x%08lx)\n", ok ? "✅ OK" : "❌ FAIL", chk->crc32);

    // // Debug dump (tùy chọn)
    // for (int i = 0; i < 32; ++i) printf("%02X ", ((const uint8_t*)chk)[i]); puts("");

    return ok;
}

bool app_cfg_init(void) {
    const cfg_image_t *img = (const cfg_image_t *)(CFG_XIP_ADDR);
    if (cfg_is_valid(img)) {
        g_cfg = img->cfg;
        return true;
    }
    // Không hợp lệ -> default + save
    app_cfg_reset_default();
    return app_cfg_save();
}

const app_config_t *app_cfg_get(void) { return &g_cfg; }

bool app_cfg_set(const app_config_t *in) {
    if (!in) return false;
    g_cfg = *in;
    return true;
}

// ===== Tùy chọn: compile-time guard nếu image quá lớn =====
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(cfg_image_t) <= FLASH_SECTOR_SIZE, "cfg_image_t vượt quá 1 sector FLASH!");
#endif



#define CLAMP(v,lo,hi) do{ if((v)<(lo)) (v)=(lo); else if((v)>(hi)) (v)=(hi);}while(0)

static inline void _strzcpy(char* dst, size_t dstsz, const char* src) {
    if (!dst || !dstsz) return;
    if (!src) { dst[0]=0; return; }
    snprintf(dst, dstsz, "%s", src);
}

// Giá trị mặc định hợp lý cho PSU 24V lead-acid (ví dụ)
void appcfg_defaults(app_config_t* c) {
    memset(c, 0, sizeof(*c));
    // Network
    c->mac[0]=0x02; c->mac[1]=0x00; c->mac[2]=0x00; c->mac[3]=0x00; c->mac[4]=0x00; c->mac[5]=0x01;
    c->ip[0]=192; c->ip[1]=168; c->ip[2]=1; c->ip[3]=77;
    c->gw[0]=192; c->gw[1]=168; c->gw[2]=1; c->gw[3]=1;
    c->sn[0]=255; c->sn[1]=255; c->sn[2]=255; c->sn[3]=0;
    c->dns[0]=8; c->dns[1]=8; c->dns[2]=8; c->dns[3]=8;
    c->dhcp_enable = 1;

    // SNMP
    c->snmp_manager_ip[0]=192; c->snmp_manager_ip[1]=168; c->snmp_manager_ip[2]=1; c->snmp_manager_ip[3]=100;
    c->trap_enable_mask = (TRAP_POWER_FAILURE | TRAP_OVER_TEMPERATURE | TRAP_OVER_CURRENT);
    // c->trap_enable_mask = (TRAP_POWER_FAIL | TRAP_OVER_TEMP | TRAP_OVER_CURR);

    c->snmp_enable = 1;

    // Modbus/PSU
    c->psu_slave_addr = 1;
    c->psu_in_undervolt_V = 20.0f;   // ví dụ cho 24V input
    c->psu_out_overvolt_V = 29.0f;
    c->psu_overcurrent_A  = 10.0f;
    c->psu_overtemp_C     = 70.0f;
    c->hysteresis_pct     = 5.0f;
    c->debounce_ms        = 200;

    // Battery/profile
    c->psu.battery_type = BATT_TYPE_24V;
    c->psu.battery_capacity_ah = 20.0f;
    c->psu.voltage_cutoff_low = 21.0f;
    c->psu.voltage_warning    = 22.5f;
    c->psu.voltage_critical   = 22.0f;

    c->psu.warning_60min_enabled = 1;
    c->psu.warning_30min_enabled = 1;
    c->psu.warning_15min_enabled = 1;
    c->psu.warning_5min_enabled  = 1;
    c->psu.warning_soc30_enabled = 1;
    c->psu.warning_soc20_enabled = 1;
    c->psu.warning_soc10_enabled = 1;

    _strzcpy(c->psu.site_identifier, sizeof(c->psu.site_identifier), "SITE-001");
    _strzcpy(c->psu.snmp_trap_dest, sizeof(c->psu.snmp_trap_dest), "192.168.1.100");
    c->psu.snmp_trap_port = 162;

    c->psu.current_sensor_offset = 0.0f;
    c->psu.voltage_sensor_scale  = 1.000f;
    c->psu.enable_auto_shutdown  = 1;

    // System
    _strzcpy(c->device_name, sizeof(c->device_name), "VF-PSU-MON");
    _strzcpy(c->uuid, sizeof(c->uuid), "00000000-0000-0000-0000-000000000000");
    c->timezone    = +7;
    c->web_enable  = 1;
    c->sntp_enable = 1;
    c->mqtt_enable = 0;
    c->ota_enable  = 0;

    memset(c->reserved, 0, sizeof(c->reserved));
}

// IPv4 string -> 4 bytes
static bool parse_ip(const char* s, uint8_t out[4]) {
    int a,b,c,d;
    if (!s) return false;
    if (sscanf(s, "%d.%d.%d.%d", &a,&b,&c,&d) != 4) return false;
    if ((unsigned)a>255||(unsigned)b>255||(unsigned)c>255||(unsigned)d>255) return false;
    out[0]=a; out[1]=b; out[2]=c; out[3]=d;
    return true;
}

static bool valid_ip4(const uint8_t ip[4]) {
    // đơn giản: không cho 0.0.0.0 và 255.255.255.255
    return !((ip[0]==0 && ip[1]==0 && ip[2]==0 && ip[3]==0) ||
             (ip[0]==255 && ip[1]==255 && ip[2]==255 && ip[3]==255));
}

bool appcfg_validate(const app_config_t* c, char* why, size_t why_len) {
    #define WHY(fmt,...) do{ if(why&&why_len){ snprintf(why, why_len, fmt, ##__VA_ARGS__);} }while(0)

    if (c->psu_slave_addr < 1 || c->psu_slave_addr > 247) { WHY("psu_slave_addr out of range"); return false; }
    if (c->hysteresis_pct < 0.f || c->hysteresis_pct > 50.f) { WHY("hysteresis_pct out of range"); return false; }
    if (c->debounce_ms > 600000u) { WHY("debounce_ms too large"); return false; }

    if (c->psu.battery_capacity_ah <= 0.f || c->psu.battery_capacity_ah > 1000.f) { WHY("battery_capacity_ah invalid"); return false; }
    switch (c->psu.battery_type) {
        case BATT_TYPE_12V: case BATT_TYPE_24V: case BATT_TYPE_36V: case BATT_TYPE_48V: break;
        default: WHY("battery_type invalid"); return false;
    }

    if (!(c->psu.voltage_cutoff_low < c->psu.voltage_critical + 0.0001f &&
          c->psu.voltage_critical <= c->psu.voltage_warning + 0.0001f)) {
        WHY("voltage thresholds order invalid"); return false;
    }

    if (!valid_ip4(c->ip) || !valid_ip4(c->gw) || !valid_ip4(c->sn) || !valid_ip4(c->dns)) {
        WHY("network ip/gw/sn/dns invalid"); return false;
    }
    if (c->snmp_enable && !valid_ip4(c->snmp_manager_ip)) {
        WHY("snmp_manager_ip invalid"); return false;
    }
    if (c->timezone < -12 || c->timezone > +14) { WHY("timezone invalid"); return false; }
    if (c->psu.snmp_trap_port == 0) { WHY("snmp_trap_port invalid"); return false; }

    return true;
    #undef WHY
}

static void add_ip(cJSON* parent, const char* key, const uint8_t ip[4]) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0],ip[1],ip[2],ip[3]);
    cJSON_AddStringToObject(parent, key, buf);
}

bool appcfg_to_json(const app_config_t* c, char** out_json) {
    cJSON* root = cJSON_CreateObject();

    // Network
    cJSON* jnet = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "network", jnet);
    add_ip(jnet, "ip",  c->ip);
    add_ip(jnet, "gw",  c->gw);
    add_ip(jnet, "sn",  c->sn);
    add_ip(jnet, "dns", c->dns);
    cJSON_AddNumberToObject(jnet, "dhcp_enable", c->dhcp_enable);

    // SNMP
    cJSON* jsnmp = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "snmp", jsnmp);
    add_ip(jsnmp, "manager_ip", c->snmp_manager_ip);
    cJSON_AddNumberToObject(jsnmp, "enable", c->snmp_enable);
    cJSON_AddNumberToObject(jsnmp, "trap_enable_mask", c->trap_enable_mask);

    // Modbus/PSU
    cJSON* jpsu = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "psu", jpsu);
    cJSON_AddNumberToObject(jpsu, "slave_addr", c->psu_slave_addr);
    cJSON_AddNumberToObject(jpsu, "in_undervolt_V", c->psu_in_undervolt_V);
    cJSON_AddNumberToObject(jpsu, "out_overvolt_V", c->psu_out_overvolt_V);
    cJSON_AddNumberToObject(jpsu, "overcurrent_A",  c->psu_overcurrent_A);
    cJSON_AddNumberToObject(jpsu, "overtemp_C",     c->psu_overtemp_C);
    cJSON_AddNumberToObject(jpsu, "hysteresis_pct", c->hysteresis_pct);
    cJSON_AddNumberToObject(jpsu, "debounce_ms",    (double)c->debounce_ms);

    // Battery/Profile
    cJSON* jprof = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "profile", jprof);
    cJSON_AddNumberToObject(jprof, "battery_type", c->psu.battery_type);
    cJSON_AddNumberToObject(jprof, "capacity_ah",  c->psu.battery_capacity_ah);
    cJSON_AddNumberToObject(jprof, "cutoff_low",   c->psu.voltage_cutoff_low);
    cJSON_AddNumberToObject(jprof, "warning",      c->psu.voltage_warning);
    cJSON_AddNumberToObject(jprof, "critical",     c->psu.voltage_critical);

    cJSON_AddNumberToObject(jprof, "warn_60min", c->psu.warning_60min_enabled);
    cJSON_AddNumberToObject(jprof, "warn_30min", c->psu.warning_30min_enabled);
    cJSON_AddNumberToObject(jprof, "warn_15min", c->psu.warning_15min_enabled);
    cJSON_AddNumberToObject(jprof, "warn_5min",  c->psu.warning_5min_enabled);
    cJSON_AddNumberToObject(jprof, "warn_soc30", c->psu.warning_soc30_enabled);
    cJSON_AddNumberToObject(jprof, "warn_soc20", c->psu.warning_soc20_enabled);
    cJSON_AddNumberToObject(jprof, "warn_soc10", c->psu.warning_soc10_enabled);

    cJSON_AddStringToObject(jprof, "site_identifier", c->psu.site_identifier);
    cJSON_AddStringToObject(jprof, "snmp_trap_dest",  c->psu.snmp_trap_dest);
    cJSON_AddNumberToObject(jprof, "snmp_trap_port",  c->psu.snmp_trap_port);
    cJSON_AddNumberToObject(jprof, "current_sensor_offset", c->psu.current_sensor_offset);
    cJSON_AddNumberToObject(jprof, "voltage_sensor_scale",  c->psu.voltage_sensor_scale);
    cJSON_AddNumberToObject(jprof, "auto_shutdown",         c->psu.enable_auto_shutdown);

    // System
    cJSON* jsys = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "system", jsys);
    cJSON_AddStringToObject(jsys, "device_name", c->device_name);
    cJSON_AddStringToObject(jsys, "uuid",        c->uuid);
    cJSON_AddNumberToObject(jsys, "timezone",    c->timezone);
    cJSON_AddNumberToObject(jsys, "web_enable",  c->web_enable);
    cJSON_AddNumberToObject(jsys, "sntp_enable", c->sntp_enable);
    cJSON_AddNumberToObject(jsys, "mqtt_enable", c->mqtt_enable);
    cJSON_AddNumberToObject(jsys, "ota_enable",  c->ota_enable);

    char* s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s) return false;
    *out_json = s;
    return true;
}

static void json_set_ip_opt(cJSON* obj, const char* key, uint8_t out[4], bool* touched) {
    cJSON* j = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(j) && j->valuestring) {
        uint8_t tmp[4];
        if (parse_ip(j->valuestring, tmp)) {
            memcpy(out, tmp, 4);
            *touched = true;
        }
    }
}
static void json_set_u8_opt(cJSON* obj, const char* key, uint8_t* v, bool* touched) {
    cJSON* j = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(j)) { *v = (uint8_t)j->valueint; *touched = true; }
}
static void json_set_f_opt(cJSON* obj, const char* key, float* v, bool* touched) {
    cJSON* j = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(j)) { *v = (float)j->valuedouble; *touched = true; }
}
static void json_set_u32_opt(cJSON* obj, const char* key, uint32_t* v, bool* touched) {
    cJSON* j = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(j) && j->valuedouble >= 0) { *v = (uint32_t)j->valuedouble; *touched = true; }
}
static void json_set_str_opt(cJSON* obj, const char* key, char* dst, size_t dstsz, bool* touched) {
    cJSON* j = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(j) && j->valuestring) { _strzcpy(dst, dstsz, j->valuestring); *touched = true; }
}
static void json_set_battery_type_opt(cJSON* obj, const char* key, battery_type_t* v, bool* touched) {
    cJSON* j = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!j) return;
    if (cJSON_IsNumber(j)) {
        int t = j->valueint;
        if (t==12 || t==24 || t==36 || t==48) { *v = (battery_type_t)t; *touched = true; }
    } else if (cJSON_IsString(j) && j->valuestring) {
        const char* s = j->valuestring;
        if (strcmp(s,"12")==0 || strcmp(s,"12V")==0) { *v=BATT_TYPE_12V; *touched=true; }
        else if (strcmp(s,"24")==0||strcmp(s,"24V")==0){ *v=BATT_TYPE_24V; *touched=true; }
        else if (strcmp(s,"36")==0||strcmp(s,"36V")==0){ *v=BATT_TYPE_36V; *touched=true; }
        else if (strcmp(s,"48")==0||strcmp(s,"48V")==0){ *v=BATT_TYPE_48V; *touched=true; }
    }
}

bool appcfg_apply_json(app_config_t* c, const char* body, size_t len, char* err, size_t errlen) {
    if (!body || !len) { if(err) snprintf(err,errlen,"empty body"); return false; }
    cJSON* root = cJSON_ParseWithLength(body, len);
    if (!root) { if(err) snprintf(err,errlen,"invalid json"); return false; }

    app_config_t tmp = *c; // apply vào bản copy; chỉ save khi hợp lệ
    bool touched=false, t;

    // network { ip, gw, sn, dns, dhcp_enable }
    cJSON* jnet = cJSON_GetObjectItemCaseSensitive(root, "network");
    if (cJSON_IsObject(jnet)) {
        t=false; json_set_ip_opt(jnet,"ip",  tmp.ip,  &t); touched|=t;
        t=false; json_set_ip_opt(jnet,"gw",  tmp.gw,  &t); touched|=t;
        t=false; json_set_ip_opt(jnet,"sn",  tmp.sn,  &t); touched|=t;
        t=false; json_set_ip_opt(jnet,"dns", tmp.dns, &t); touched|=t;
        t=false; json_set_u8_opt(jnet,"dhcp_enable", &tmp.dhcp_enable, &t); touched|=t;
    }

    // snmp { manager_ip, enable, trap_enable_mask }
    cJSON* jsnmp = cJSON_GetObjectItemCaseSensitive(root, "snmp");
    if (cJSON_IsObject(jsnmp)) {
        t=false; json_set_ip_opt(jsnmp,"manager_ip", tmp.snmp_manager_ip, &t); touched|=t;
        t=false; json_set_u8_opt(jsnmp,"enable", &tmp.snmp_enable, &t); touched|=t;
        t=false; json_set_u8_opt(jsnmp,"trap_enable_mask", &tmp.trap_enable_mask, &t); touched|=t;
    }

    // psu { slave_addr, in_undervolt_V, out_overvolt_V, overcurrent_A, overtemp_C, hysteresis_pct, debounce_ms }
    cJSON* jpsu = cJSON_GetObjectItemCaseSensitive(root, "psu");
    if (cJSON_IsObject(jpsu)) {
        t=false; json_set_u8_opt(jpsu,"slave_addr",&tmp.psu_slave_addr,&t); touched|=t;
        t=false; json_set_f_opt(jpsu,"in_undervolt_V",&tmp.psu_in_undervolt_V,&t); touched|=t;
        t=false; json_set_f_opt(jpsu,"out_overvolt_V",&tmp.psu_out_overvolt_V,&t); touched|=t;
        t=false; json_set_f_opt(jpsu,"overcurrent_A",&tmp.psu_overcurrent_A,&t); touched|=t;
        t=false; json_set_f_opt(jpsu,"overtemp_C",&tmp.psu_overtemp_C,&t); touched|=t;
        t=false; json_set_f_opt(jpsu,"hysteresis_pct",&tmp.hysteresis_pct,&t); touched|=t;
        t=false; json_set_u32_opt(jpsu,"debounce_ms",&tmp.debounce_ms,&t); touched|=t;
    }

    // profile { ... }
    cJSON* jprof = cJSON_GetObjectItemCaseSensitive(root, "profile");
    if (cJSON_IsObject(jprof)) {
        t=false; json_set_battery_type_opt(jprof,"battery_type",&tmp.psu.battery_type,&t); touched|=t;
        t=false; json_set_f_opt(jprof,"capacity_ah",&tmp.psu.battery_capacity_ah,&t); touched|=t;
        t=false; json_set_f_opt(jprof,"cutoff_low",&tmp.psu.voltage_cutoff_low,&t); touched|=t;
        t=false; json_set_f_opt(jprof,"warning",&tmp.psu.voltage_warning,&t); touched|=t;
        t=false; json_set_f_opt(jprof,"critical",&tmp.psu.voltage_critical,&t); touched|=t;

        t=false; json_set_u8_opt(jprof,"warn_60min",&tmp.psu.warning_60min_enabled,&t); touched|=t;
        t=false; json_set_u8_opt(jprof,"warn_30min",&tmp.psu.warning_30min_enabled,&t); touched|=t;
        t=false; json_set_u8_opt(jprof,"warn_15min",&tmp.psu.warning_15min_enabled,&t); touched|=t;
        t=false; json_set_u8_opt(jprof,"warn_5min",&tmp.psu.warning_5min_enabled,&t); touched|=t;
        t=false; json_set_u8_opt(jprof,"warn_soc30",&tmp.psu.warning_soc30_enabled,&t); touched|=t;
        t=false; json_set_u8_opt(jprof,"warn_soc20",&tmp.psu.warning_soc20_enabled,&t); touched|=t;
        t=false; json_set_u8_opt(jprof,"warn_soc10",&tmp.psu.warning_soc10_enabled,&t); touched|=t;

        t=false; json_set_str_opt(jprof,"site_identifier", tmp.psu.site_identifier, sizeof(tmp.psu.site_identifier), &t); touched|=t;
        t=false; json_set_str_opt(jprof,"snmp_trap_dest",  tmp.psu.snmp_trap_dest,  sizeof(tmp.psu.snmp_trap_dest),  &t); touched|=t;
        t=false; json_set_u32_opt(jprof,"snmp_trap_port",(uint32_t*)&tmp.psu.snmp_trap_port,&t); touched|=t;
        t=false; json_set_f_opt(jprof,"current_sensor_offset",&tmp.psu.current_sensor_offset,&t); touched|=t;
        t=false; json_set_f_opt(jprof,"voltage_sensor_scale",&tmp.psu.voltage_sensor_scale,&t); touched|=t;
        t=false; json_set_u8_opt(jprof,"auto_shutdown",&tmp.psu.enable_auto_shutdown,&t); touched|=t;
    }

    // system { device_name, uuid, timezone, web_enable, sntp_enable, mqtt_enable, ota_enable }
    cJSON* jsys = cJSON_GetObjectItemCaseSensitive(root, "system");
    if (cJSON_IsObject(jsys)) {
        t=false; json_set_str_opt(jsys,"device_name", tmp.device_name, sizeof(tmp.device_name), &t); touched|=t;
        t=false; json_set_str_opt(jsys,"uuid",        tmp.uuid, sizeof(tmp.uuid), &t); touched|=t;
        t=false; json_set_u8_opt(jsys,"web_enable",&tmp.web_enable,&t); touched|=t;
        t=false; json_set_u8_opt(jsys,"sntp_enable",&tmp.sntp_enable,&t); touched|=t;
        t=false; json_set_u8_opt(jsys,"mqtt_enable",&tmp.mqtt_enable,&t); touched|=t;
        t=false; json_set_u8_opt(jsys,"ota_enable",&tmp.ota_enable,&t); touched|=t;

        // timezone là số có thể âm
        cJSON* jt = cJSON_GetObjectItemCaseSensitive(jsys, "timezone");
        if (cJSON_IsNumber(jt)) { tmp.timezone = (int8_t)jt->valueint; touched = true; }
    }

    // Valid & commit
    char why[96];
    bool ok = appcfg_validate(&tmp, why, sizeof(why));
    cJSON_Delete(root);
    if (!ok) { if(err) snprintf(err,errlen,"%s", why); return false; }

    *c = tmp;
    return true;
}