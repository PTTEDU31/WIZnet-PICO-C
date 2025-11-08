#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "httpServer.h"
#include "httpParser.h"
#include "httpUtil.h"
#include "socket.h"
#include "wizchip_conf.h"

#include "pico/time.h"
#include "hardware/watchdog.h"

#include "cJSON.h"
#include "dev_eth.h"
#include "dev_web.h"
#include "app_config.h"
#include "../psu_data/psu_data.h"

/* ===========================================================
 * Forward decl
 * =========================================================== */
extern void eth_reinit_from_config(void);
extern void safe_reboot(void);

/* ===========================================================
 * HTTP helpers (header + CORS + JSON/TEXT)
 * =========================================================== */
static void http_send_status_json(uint8_t s, int code, const char *json)
{
    // Nếu lib http của bạn đã có API gửi status, dùng nó; ở đây gói sẵn
    send_http_response_header(s, PTYPE_JSON, (json ? (int)strlen(json) : 0), code, 0);
    // CORS (nếu header lib không tự thêm)
    // Bạn có thể bổ sung send_http_header_line(...) nếu có
    if (json && *json)
        send(s, (uint8_t *)json, (uint16_t)strlen(json));
}

static void http_send_status_text(uint8_t s, int code, const char *txt)
{
    send_http_response_header(s, PTYPE_TEXT, (txt ? (int)strlen(txt) : 0), code, 0);
    if (txt && *txt)
        send(s, (uint8_t *)txt, (uint16_t)strlen(txt));
}

/* ===========================================================
 * Body reader: ưu tiên r->content_ptr/len; fallback scan \r\n\r\n
 * =========================================================== */
typedef struct
{
    const char *ptr;
    size_t len;
} http_body_view_t;

static http_body_view_t http_get_body_view(const st_http_request *r)
{
    http_body_view_t v = {0};
    if (!r)
        return v;

// 1) Nếu framework đã parse sẵn:
#ifdef HAVE_HTTP_CONTENT_PTR
    if (r->content_ptr && r->content_length > 0)
    {
        v.ptr = (const char *)r->content_ptr;
        v.len = (size_t)r->content_length;
        return v;
    }
#endif

    // 2) Fallback: r->URI chứa nguyên request -> tìm CRLFCRLF
    if (r->URI && *r->URI)
    {
        const char *start = strstr(r->URI, "\r\n\r\n");
        if (start)
        {
            start += 4;
            // không có content-length – lấy hết phần còn lại
            v.ptr = start;
            v.len = strlen(start);
        }
    }
    return v;
}

static cJSON *http_parse_json_body(const st_http_request *r)
{
    http_body_view_t b = http_get_body_view(r);
    if (!b.ptr || b.len == 0)
        return NULL;
    return cJSON_ParseWithLength(b.ptr, b.len);
}

/* ===========================================================
 * Helpers nhỏ JSON / IP
 * =========================================================== */
static inline void strzcpy(char *dst, size_t sz, const char *src)
{
    if (!dst || !sz)
        return;
    if (!src)
    {
        dst[0] = 0;
        return;
    }
    snprintf(dst, sz, "%s", src);
}

static int json_get_i(cJSON *obj, const char *k, int defv)
{
    cJSON *j = cJSON_GetObjectItemCaseSensitive(obj, k);
    return cJSON_IsNumber(j) ? j->valueint : defv;
}
static float json_get_f(cJSON *obj, const char *k, float defv)
{
    cJSON *j = cJSON_GetObjectItemCaseSensitive(obj, k);
    return cJSON_IsNumber(j) ? (float)j->valuedouble : defv;
}
static void json_get_s(cJSON *obj, const char *k, char *out, size_t outsz, const char *defv)
{
    cJSON *j = cJSON_GetObjectItemCaseSensitive(obj, k);
    if (cJSON_IsString(j) && j->valuestring)
        strzcpy(out, outsz, j->valuestring);
    else
        strzcpy(out, outsz, defv ? defv : "");
}
static int parse_ip4(const char *s, uint8_t ip[4])
{
    if (!s)
        return 0;
    int a, b, c, d;
    if (sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
        return 0;
    if ((unsigned)a > 255 || (unsigned)b > 255 || (unsigned)c > 255 || (unsigned)d > 255)
        return 0;
    ip[0] = a;
    ip[1] = b;
    ip[2] = c;
    ip[3] = d;
    return 1;
}
static void add_ip_str(cJSON *o, const char *k, const uint8_t ip[4])
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    cJSON_AddStringToObject(o, k, buf);
}

/* ===========================================================
 * /api/status
 * =========================================================== */
static void api_status(uint8_t s, void *req)
{
    (void)req;
    app_config_t* cfg = app_cfg_get();
    psu_data_t psu = psu_data_read();

    cJSON *r = cJSON_CreateObject();
    // Điện/ nhiệt
    cJSON_AddNumberToObject(r, "vin", psu.vin);
    cJSON_AddNumberToObject(r, "vout", psu.vout);
    cJSON_AddNumberToObject(r, "vset", psu.vset);
    cJSON_AddNumberToObject(r, "iout", psu.iout);
    cJSON_AddNumberToObject(r, "cc", psu.cc);
    cJSON_AddNumberToObject(r, "temp", psu.temp);
    // Faults
    cJSON_AddNumberToObject(r, "fault_raw", psu.fault.raw);
    cJSON_AddNumberToObject(r, "fan_fail", psu.fault.bits.fan_fail);
    cJSON_AddNumberToObject(r, "otp", psu.fault.bits.otp);
    cJSON_AddNumberToObject(r, "ovp", psu.fault.bits.ovp);
    cJSON_AddNumberToObject(r, "olp", psu.fault.bits.olp);
    cJSON_AddNumberToObject(r, "short_circuit", psu.fault.bits.short_circuit);
    cJSON_AddNumberToObject(r, "ac_fail", psu.fault.bits.ac_fail);
    cJSON_AddNumberToObject(r, "op_off", psu.fault.bits.op_off);
    // Battery block
    cJSON_AddNumberToObject(r, "batt_type", cfg->psu.battery_type);
    cJSON_AddNumberToObject(r, "batt_voltage", psu.batt_voltage);
    cJSON_AddNumberToObject(r, "batt_current", psu.batt_current);
    cJSON_AddNumberToObject(r, "batt_soc", psu.batt_soc);
    cJSON_AddNumberToObject(r, "batt_runtime", psu.batt_runtime);
    cJSON_AddNumberToObject(r, "batt_capacity", psu.batt_capacity);
    cJSON_AddNumberToObject(r, "batt_temp", psu.batt_temp);
    cJSON_AddNumberToObject(r, "batt_charging", psu.batt_flags.bits.charging);
    cJSON_AddNumberToObject(r, "batt_discharging", psu.batt_flags.bits.discharging);
    cJSON_AddNumberToObject(r, "batt_full", psu.batt_flags.bits.full);
    cJSON_AddNumberToObject(r, "batt_low", psu.batt_flags.bits.low);
    cJSON_AddNumberToObject(r, "batt_critical", psu.batt_flags.bits.critical);
    cJSON_AddNumberToObject(r, "bms_fault", psu.batt_flags.bits.bms_fault);

    // ==== Learned capacity & cycle summary ====
    const psu_cycle_db_t *cyc = psu_cycles_get();
    cJSON_AddNumberToObject(r, "learned_mAh_avg", psu_learned_capacity_mAh());
    cJSON_AddNumberToObject(r, "learned_mAh_best", psu_learned_capacity_mAh_best());
    cJSON_AddNumberToObject(r, "cycle_count", cyc ? cyc->count : 0);

    if (cyc && cyc->count > 0)
    {
        // Lấy phần tử mới nhất trong ring
        int last = (cyc->head + PSU_CYCLE_WINDOW - 1) % PSU_CYCLE_WINDOW;
        const psu_cycle_stat_t *p = &cyc->ring[last];

        cJSON *last_obj = cJSON_CreateObject();
        cJSON_AddItemToObject(r, "last_cycle", last_obj);
        cJSON_AddNumberToObject(last_obj, "mAh", p->mAh);
        cJSON_AddNumberToObject(last_obj, "vmax", p->vmax);
        cJSON_AddNumberToObject(last_obj, "imax", p->imax);
        cJSON_AddNumberToObject(last_obj, "tmax", p->tmax);
        cJSON_AddNumberToObject(last_obj, "start_ms", p->start_ms);
        cJSON_AddNumberToObject(last_obj, "end_ms", p->end_ms);
        cJSON_AddNumberToObject(last_obj, "charging", p->charging);
    }

    char *js = cJSON_PrintUnformatted(r);
    http_send_status_json(s, 200, js);
    cJSON_free(js);
    cJSON_Delete(r);
}

/* ===========================================================
 * /api/network  (GET/POST)
 * =========================================================== */
static void api_network(uint8_t s, void *req_)
{
    st_http_request *req = (st_http_request *)req_;
    app_config_t cfg = *app_cfg_get();

    if (req->METHOD == METHOD_OPTIONS)
    {
        http_send_status_json(s, 204, "");
        return;
    }

    if (req->METHOD == METHOD_POST)
    {
        cJSON *j = http_parse_json_body(req);
        if (!j)
        {
            http_send_status_json(s, 400, "{\"error\":\"invalid json\"}");
            return;
        }

        // ip/mask/gw/dns (string)
        cJSON *v;
        v = cJSON_GetObjectItemCaseSensitive(j, "ip");
        if (cJSON_IsString(v) && v->valuestring)
            parse_ip4(v->valuestring, cfg.ip);
        v = cJSON_GetObjectItemCaseSensitive(j, "mask");
        if (cJSON_IsString(v) && v->valuestring)
            parse_ip4(v->valuestring, cfg.sn);
        v = cJSON_GetObjectItemCaseSensitive(j, "gw");
        if (cJSON_IsString(v) && v->valuestring)
            parse_ip4(v->valuestring, cfg.gw);
        v = cJSON_GetObjectItemCaseSensitive(j, "dns");
        if (cJSON_IsString(v) && v->valuestring)
            parse_ip4(v->valuestring, cfg.dns);

        cfg.dhcp_enable = json_get_i(j, "dhcp", cfg.dhcp_enable);
        cJSON_Delete(j);

        app_cfg_set(&cfg);
        if (app_cfg_save())
        {
            http_send_status_json(s, 200, "{\"result\":\"OK\",\"reboot_in_ms\":1500}");
            // Reboot an toàn: tùy bạn delay trong main loop, ở đây gọi hàm sẵn
            safe_reboot();
        }
        else
        {
            http_send_status_json(s, 500, "{\"error\":\"save failed\"}");
        }
        return;
    }

    if (req->METHOD == METHOD_GET)
    {
        cJSON *r = cJSON_CreateObject();
        add_ip_str(r, "ip", cfg.ip);
        add_ip_str(r, "mask", cfg.sn);
        add_ip_str(r, "gw", cfg.gw);
        add_ip_str(r, "dns", cfg.dns);
        cJSON_AddNumberToObject(r, "dhcp", cfg.dhcp_enable);

        char *js = cJSON_PrintUnformatted(r);
        http_send_status_json(s, 200, js);
        cJSON_free(js);
        cJSON_Delete(r);
        return;
    }

    http_send_status_json(s, 405, "{\"error\":\"method not allowed\"}");
}

/* ===========================================================
 * /api/device  (GET)
 * =========================================================== */
static void api_device(uint8_t s, void *req)
{
    (void)req;
    const app_config_t *cfg = app_cfg_get();

    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "board", "RP2040-W5500");
    cJSON_AddStringToObject(r, "firmware", "1.0.0");
    cJSON_AddStringToObject(r, "device", cfg->device_name);
    cJSON_AddStringToObject(r, "uuid", cfg->uuid);

    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             cfg->mac[0], cfg->mac[1], cfg->mac[2], cfg->mac[3], cfg->mac[4], cfg->mac[5]);
    cJSON_AddStringToObject(r, "mac", mac);

    cJSON_AddStringToObject(r, "uptime", "running");
    cJSON_AddStringToObject(r, "build", __DATE__ " " __TIME__);

    char *js = cJSON_PrintUnformatted(r);
    http_send_status_json(s, 200, js);
    cJSON_free(js);
    cJSON_Delete(r);
}

/* ===========================================================
 * /api/logs  (GET)
 * =========================================================== */
static void api_logs(uint8_t s, void *req)
{
    (void)req;
    const char *dummy =
        "System start OK\n"
        "Ethernet link up\n"
        "Web server initialized\n"
        "SNMP Agent running\n";
    http_send_status_text(s, 200, dummy);
}

/* ===========================================================
 * /api/config  (GET/POST) — gồm cả Battery/Profile
 * Schema GET:
 * {
 *   "device_name": "...", "uuid":"...", "timezone":7,
 *   "ip": "...", "mask":"...", "gw":"...", "dns":"...", "dhcp_enable":1,
 *   "snmp_enable":1, "ip_snmp":"...", "trap_enable_mask":7,
 *   "sntp_enable":1, "web_enable":1,
 *   "psu_slave_addr":"0A", "psu_in_undervolt_V":21.0, ...
 *   "profile": { battery_type:24, capacity_ah:40, cutoff_low:..., warning:..., critical:...,
 *                warn_60min:1, warn_30min:1, ..., site_identifier:"...", snmp_trap_dest:"...", snmp_trap_port:162,
 *                current_sensor_offset:0, voltage_sensor_scale:1.0, auto_shutdown:1 }
 * }
 * =========================================================== */
static void api_config(uint8_t s, void *req_)
{
    st_http_request *req = (st_http_request *)req_;
    app_config_t cfg = *app_cfg_get();

    if (req->METHOD == METHOD_OPTIONS)
    {
        http_send_status_json(s, 204, "");
        return;
    }

    if (req->METHOD == METHOD_POST)
    {
        cJSON *j = http_parse_json_body(req);
        if (!j)
        {
            http_send_status_json(s, 400, "{\"error\":\"invalid json\"}");
            return;
        }

        // ----------- flat fields -----------
        json_get_s(j, "device_name", cfg.device_name, sizeof(cfg.device_name), cfg.device_name);
        cfg.timezone = json_get_i(j, "timezone", cfg.timezone);
        cfg.snmp_enable = json_get_i(j, "snmp_enable", cfg.snmp_enable);
        cfg.sntp_enable = json_get_i(j, "sntp_enable", cfg.sntp_enable);
        cfg.web_enable = json_get_i(j, "web_enable", cfg.web_enable);
        cfg.psu_in_undervolt_V = json_get_f(j, "psu_in_undervolt_V", cfg.psu_in_undervolt_V);
        cfg.psu_overtemp_C = json_get_f(j, "psu_overtemp_C", cfg.psu_overtemp_C);
        cfg.psu_overcurrent_A = json_get_f(j, "psu_overcurrent_A", cfg.psu_overcurrent_A);

        // psu_slave_addr: chuỗi HEX hoặc số
        cJSON *j_psu = cJSON_GetObjectItemCaseSensitive(j, "psu_slave_addr");
        if (cJSON_IsString(j_psu) && j_psu->valuestring)
        {
            unsigned long v = strtoul(j_psu->valuestring, NULL, 16);
            if (v <= 247)
                cfg.psu_slave_addr = (uint8_t)v;
        }
        else if (cJSON_IsNumber(j_psu))
        {
            int v = j_psu->valueint;
            if (v >= 1 && v <= 247)
                cfg.psu_slave_addr = (uint8_t)v;
        }

        // trap mask: hex string hoặc number
        cJSON *j_tm = cJSON_GetObjectItemCaseSensitive(j, "trap_enable_mask");
        if (cJSON_IsString(j_tm) && j_tm->valuestring)
        {
            unsigned long v = strtoul(j_tm->valuestring, NULL, 16);
            cfg.trap_enable_mask = (uint8_t)(v & 0xFF);
        }
        else if (cJSON_IsNumber(j_tm) && j_tm->valueint >= 0)
        {
            cfg.trap_enable_mask = (uint8_t)(j_tm->valueint & 0xFF);
        }

        // SNMP manager IP
        cJSON *j_ip_snmp = cJSON_GetObjectItemCaseSensitive(j, "ip_snmp");
        if (cJSON_IsString(j_ip_snmp) && j_ip_snmp->valuestring)
            parse_ip4(j_ip_snmp->valuestring, cfg.snmp_manager_ip);

        // ----------- profile group -----------
        cJSON *p = cJSON_GetObjectItemCaseSensitive(j, "profile");
        if (cJSON_IsObject(p))
        {
            // cJSON *jt = cJSON_GetObjectItemCaseSensitive(p, "battery_type");
            // if (cJSON_IsNumber(jt))
            // {
            //     int t = jt->valueint;
            //     if (t == 1 || t == 2 || t == 3 || t == 4)
            //         cfg.psu.battery_type = (battery_type_t)t;
            // }
            // else if (cJSON_IsString(jt) && jt->valuestring)
            // {
            //     const char *s = jt->valuestring;
            //     if (!strcmp(s, "12") || !strcmp(s, "12V"))
            //         cfg.psu.battery_type = 12;
            //     else if (!strcmp(s, "24") || !strcmp(s, "24V"))
            //         cfg.psu.battery_type = 24;
            //     else if (!strcmp(s, "36") || !strcmp(s, "36V"))
            //         cfg.psu.battery_type = 36;
            //     else if (!strcmp(s, "48") || !strcmp(s, "48V"))
            //         cfg.psu.battery_type = 48;
            // }
            cfg.psu.battery_type = json_get_i(p, "battery_type", cfg.psu.battery_type);
            cfg.psu.battery_capacity_ah = json_get_f(p, "capacity_ah", cfg.psu.battery_capacity_ah);
            cfg.psu.voltage_cutoff_low = json_get_f(p, "cutoff_low", cfg.psu.voltage_cutoff_low);
            cfg.psu.voltage_warning = json_get_f(p, "warning", cfg.psu.voltage_warning);
            cfg.psu.voltage_critical = json_get_f(p, "critical", cfg.psu.voltage_critical);

            cfg.psu.warning_60min_enabled = (uint8_t)json_get_i(p, "warn_60min", cfg.psu.warning_60min_enabled);
            cfg.psu.warning_30min_enabled = (uint8_t)json_get_i(p, "warn_30min", cfg.psu.warning_30min_enabled);
            cfg.psu.warning_15min_enabled = (uint8_t)json_get_i(p, "warn_15min", cfg.psu.warning_15min_enabled);
            cfg.psu.warning_5min_enabled = (uint8_t)json_get_i(p, "warn_5min", cfg.psu.warning_5min_enabled);
            cfg.psu.warning_soc30_enabled = (uint8_t)json_get_i(p, "warn_soc30", cfg.psu.warning_soc30_enabled);
            cfg.psu.warning_soc20_enabled = (uint8_t)json_get_i(p, "warn_soc20", cfg.psu.warning_soc20_enabled);
            cfg.psu.warning_soc10_enabled = (uint8_t)json_get_i(p, "warn_soc10", cfg.psu.warning_soc10_enabled);

            json_get_s(p, "site_identifier", cfg.psu.site_identifier, sizeof(cfg.psu.site_identifier), cfg.psu.site_identifier);
            // json_get_s(p, "snmp_trap_dest", cfg.psu.snmp_trap_dest, sizeof(cfg.psu.snmp_trap_dest), cfg.psu.snmp_trap_dest);
            cJSON *p_ip_snmp = cJSON_GetObjectItemCaseSensitive(p, "snmp_trap_dest");
            if (cJSON_IsString(p_ip_snmp) && p_ip_snmp->valuestring)
                parse_ip4(p_ip_snmp->valuestring, cfg.psu.snmp_trap_dest);
            // port
            cJSON *jp = cJSON_GetObjectItemCaseSensitive(p, "snmp_trap_port");
            if (cJSON_IsNumber(jp) && jp->valueint > 0 && jp->valueint < 65536)
                cfg.psu.snmp_trap_port = (uint16_t)jp->valueint;

            cfg.psu.current_sensor_offset = json_get_f(p, "current_sensor_offset", cfg.psu.current_sensor_offset);
            cfg.psu.voltage_sensor_scale = json_get_f(p, "voltage_sensor_scale", cfg.psu.voltage_sensor_scale);
            cfg.psu.enable_auto_shutdown = (uint8_t)json_get_i(p, "auto_shutdown", cfg.psu.enable_auto_shutdown);
        }

        // === validate + save ===
        char why[96];
        if (!appcfg_validate(&cfg, why, sizeof(why)))
        {
            cJSON_Delete(j);
            char buf[160];
            snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", why);
            http_send_status_json(s, 400, buf);
            return;
        }

        app_cfg_set(&cfg);
        if (!app_cfg_save())
        {
            cJSON_Delete(j);
            http_send_status_json(s, 500, "{\"error\":\"save failed\"}");
            return;
        }

        cJSON_Delete(j);
        http_send_status_json(s, 200, "{\"result\":\"OK\"}");
        return;
    }

    if (req->METHOD == METHOD_GET)
    {
        cJSON *r = cJSON_CreateObject();

        cJSON_AddStringToObject(r, "device_name", cfg.device_name);
        cJSON_AddStringToObject(r, "uuid", cfg.uuid);
        cJSON_AddNumberToObject(r, "timezone", cfg.timezone);

        // MAC
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 cfg.mac[0], cfg.mac[1], cfg.mac[2], cfg.mac[3], cfg.mac[4], cfg.mac[5]);
        cJSON_AddStringToObject(r, "mac", mac);

        // Net flat (giữ tương thích FE cũ)
        add_ip_str(r, "ip", cfg.ip);
        add_ip_str(r, "mask", cfg.sn);
        add_ip_str(r, "gw", cfg.gw);
        add_ip_str(r, "dns", cfg.dns);
        cJSON_AddNumberToObject(r, "dhcp_enable", cfg.dhcp_enable);

        // SNMP
        add_ip_str(r, "ip_snmp", cfg.snmp_manager_ip);
        cJSON_AddNumberToObject(r, "snmp_enable", cfg.snmp_enable);
        cJSON_AddNumberToObject(r, "trap_enable_mask", cfg.trap_enable_mask);

        // Services / limits
        cJSON_AddNumberToObject(r, "sntp_enable", cfg.sntp_enable);
        cJSON_AddNumberToObject(r, "web_enable", cfg.web_enable);

        // PSU slave (HEX string)
        char psu_hex[8];
        snprintf(psu_hex, sizeof(psu_hex), "%02X", cfg.psu_slave_addr);
        cJSON_AddStringToObject(r, "psu_slave_addr", psu_hex);
        cJSON_AddNumberToObject(r, "psu_in_undervolt_V", cfg.psu_in_undervolt_V);
        cJSON_AddNumberToObject(r, "psu_overtemp_C", cfg.psu_overtemp_C);
        cJSON_AddNumberToObject(r, "psu_overcurrent_A", cfg.psu_overcurrent_A);

        // Profile group
        cJSON *p = cJSON_CreateObject();
        cJSON_AddItemToObject(r, "profile", p);
        cJSON_AddNumberToObject(p, "battery_type", cfg.psu.battery_type);
        cJSON_AddNumberToObject(p, "capacity_ah", cfg.psu.battery_capacity_ah);
        cJSON_AddNumberToObject(p, "cutoff_low", cfg.psu.voltage_cutoff_low);
        cJSON_AddNumberToObject(p, "warning", cfg.psu.voltage_warning);
        cJSON_AddNumberToObject(p, "critical", cfg.psu.voltage_critical);

        cJSON_AddNumberToObject(p, "warn_60min", cfg.psu.warning_60min_enabled);
        cJSON_AddNumberToObject(p, "warn_30min", cfg.psu.warning_30min_enabled);
        cJSON_AddNumberToObject(p, "warn_15min", cfg.psu.warning_15min_enabled);
        cJSON_AddNumberToObject(p, "warn_5min", cfg.psu.warning_5min_enabled);
        cJSON_AddNumberToObject(p, "warn_soc30", cfg.psu.warning_soc30_enabled);
        cJSON_AddNumberToObject(p, "warn_soc20", cfg.psu.warning_soc20_enabled);
        cJSON_AddNumberToObject(p, "warn_soc10", cfg.psu.warning_soc10_enabled);

        cJSON_AddStringToObject(p, "site_identifier", cfg.psu.site_identifier);
        add_ip_str(p, "snmp_trap_dest", cfg.psu.snmp_trap_dest);
        cJSON_AddNumberToObject(p, "snmp_trap_port", cfg.psu.snmp_trap_port);
        cJSON_AddNumberToObject(p, "current_sensor_offset", cfg.psu.current_sensor_offset);
        cJSON_AddNumberToObject(p, "voltage_sensor_scale", cfg.psu.voltage_sensor_scale);
        cJSON_AddNumberToObject(p, "auto_shutdown", cfg.psu.enable_auto_shutdown);
        cJSON_AddNumberToObject(p, "learned_capacity_mAh_avg", psu_learned_capacity_mAh());
        cJSON_AddNumberToObject(p, "learned_capacity_mAh_best", psu_learned_capacity_mAh_best());

        char *js = cJSON_PrintUnformatted(r);
        http_send_status_json(s, 200, js);
        cJSON_free(js);
        cJSON_Delete(r);
        return;
    }

    http_send_status_json(s, 405, "{\"error\":\"method not allowed\"}");
}

/* ===========================================================
 * /api/cycles → trả lịch sử 10 chu kỳ nạp/xả & thống kê học
 * =========================================================== */
static void api_cycles(uint8_t s, void *req_)
{
    (void)req_;
    const psu_cycle_db_t *cyc = psu_cycles_get();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "count", cyc ? cyc->count : 0);
    cJSON_AddNumberToObject(root, "learned_avg_mAh", psu_learned_capacity_mAh());
    cJSON_AddNumberToObject(root, "learned_best_mAh", psu_learned_capacity_mAh_best());

    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "items", arr);

    if (cyc && cyc->count > 0)
    {
        // Liệt kê theo thứ tự mới nhất → cũ nhất
        int idx = (cyc->head + PSU_CYCLE_WINDOW - 1) % PSU_CYCLE_WINDOW;
        for (uint8_t n = 0; n < cyc->count; ++n)
        {
            const psu_cycle_stat_t *r = &cyc->ring[idx];

            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "index", idx);
            cJSON_AddNumberToObject(o, "mAh", r->mAh);
            cJSON_AddNumberToObject(o, "vmax", r->vmax);
            cJSON_AddNumberToObject(o, "imax", r->imax);
            cJSON_AddNumberToObject(o, "tmax", r->tmax);
            cJSON_AddNumberToObject(o, "start_ms", r->start_ms);
            cJSON_AddNumberToObject(o, "end_ms", r->end_ms);
            cJSON_AddNumberToObject(o, "charging", r->charging);
            cJSON_AddItemToArray(arr, o);

            idx = (idx + PSU_CYCLE_WINDOW - 1) % PSU_CYCLE_WINDOW;
        }
    }

    char *js = cJSON_PrintUnformatted(root);
    send_http_response_header(s, PTYPE_JSON, (int)strlen(js), STATUS_OK, 0);
    send(s, (uint8_t *)js, (uint16_t)strlen(js));
    cJSON_free(js);
    cJSON_Delete(root);
}

// Định nghĩa hàm callback
static void api_reboot_handler(uint8_t s, void *req)
{
    (void)req;
    http_send_status_json(s, 200, "{\"result\":\"rebooting in 1s\"}");

    // Bật watchdog reset sau 1 giây
    watchdog_enable(1000, 1);
    while (1)
        tight_loop_contents();
}
/* ===========================================================
 * Register APIs
 * =========================================================== */
void httpServer_user_init(void)
{
    printf("[WEB] Registering user APIs...\n");
    httpServer_regAPI("api/status", api_status);
    httpServer_regAPI("api/network", api_network);
    httpServer_regAPI("api/device", api_device);
    httpServer_regAPI("api/logs", api_logs);
    httpServer_regAPI("api/config", api_config);
    httpServer_regAPI("api/cycles", api_cycles);
    httpServer_regAPI("api/reboot", api_reboot_handler);
}
