#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "httpServer.h"
#include "httpParser.h"
#include "httpUtil.h"
#include "socket.h"
#include "wizchip_conf.h"

#include "pico/time.h"
#include "dev_eth.h"
#include "dev_web.h"
#include "app_config.h"
#include "hardware/watchdog.h"

/* Forward declaration */
extern void eth_reinit_from_config(void);

/* ===========================================================
 * Helper: gửi phản hồi JSON hoặc TEXT
 * =========================================================== */
static void send_json(uint8_t s, const char *json)
{
    send_http_response_header(s, PTYPE_JSON, strlen(json), STATUS_OK);
    send(s, (uint8_t *)json, strlen(json));
}

static void send_text(uint8_t s, const char *txt)
{
    send_http_response_header(s, PTYPE_TEXT, strlen(txt), STATUS_OK);
    send(s, (uint8_t *)txt, strlen(txt));
}

static void parse_ip_field(const char *body, const char *key, uint8_t *out)
{
    char *p, *q, tmp[32];
    char pattern[16];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    if ((p = strstr(body, pattern)))
    {
        // Tìm dấu " mở đầu value
        p = strchr(p + strlen(pattern), '"');
        if (p)
        {
            q = strchr(p + 1, '"'); // Dấu " kết thúc
            if (q && (q - p - 1) < (int)sizeof(tmp))
            {
                memcpy(tmp, p + 1, q - p - 1);
                tmp[q - p - 1] = '\0';
                sscanf(tmp, "%hhu.%hhu.%hhu.%hhu", &out[0], &out[1], &out[2], &out[3]);
                printf("[CFG] %s: %s -> %d.%d.%d.%d\n",
                       key, tmp, out[0], out[1], out[2], out[3]);
            }
        }
    }
}

/* ===========================================================
 * Helper: đọc phần body HTTP POST request
 * =========================================================== */
static int http_get_body(uint8_t s, const char *in, char *out, size_t max_len)
{
    if (!in || !out || max_len == 0)
        return 0;

    // Tìm delimiter giữa header và body
    const char *body = strstr(in, "\r\n\r\n");
    if (!body)
    {
        printf("[HTTP] No body delimiter found on socket %d\n", s);
        out[0] = '\0';
        return 0;
    }

    body += 4; // Bỏ qua "\r\n\r\n"

    size_t body_len = strlen(body);
    if (body_len >= max_len)
        body_len = max_len - 1;

    memcpy(out, body, body_len);
    out[body_len] = '\0';

    printf("[HTTP] Socket %d | Extracted body (%u bytes)\n", s, (unsigned)body_len);
    return (int)body_len;
}

/* ===========================================================
 * /api/status  → Trả trạng thái PSU (cho dashboard)
 * =========================================================== */
static void api_status(uint8_t s, void *req)
{
    extern float g_vin, g_vout, g_cc, g_temp;

    extern uint16_t g_fault_status;
    char json[256];

    snprintf(json, sizeof(json),
             "{"
             "\"vin\":%.2f,"
             "\"vout\":%.2f,"
             "\"iout\":%.3f,"
             "\"temp\":%.1f,"
             "\"status\":%u"
             "}",
             g_vin, g_vout, g_cc, g_temp, g_fault_status);

    send_json(s, json);
}

/* ===========================================================
 * /api/network → GET: lấy config mạng / POST: cập nhật & lưu
 * =========================================================== */
static void api_network(uint8_t s, void *req)
{
    st_http_request *r = (st_http_request *)req;
    char body[256];
    char json[256];
    app_config_t cfg = *app_cfg_get();

    if (r->METHOD == METHOD_POST)
    {
        int len = http_get_body(s, r->URI, body, sizeof(body));
        if (len <= 0)
        {
            send_json(s, "{\"error\":\"empty body\"}");
            return;
        }

        printf("[WEB] /api/network POST body: %s\n", body);

        // Parse JSON đơn giản
        parse_ip_field(body, "ip", cfg.ip);
        parse_ip_field(body, "mask", cfg.sn);
        parse_ip_field(body, "gw", cfg.gw);
        parse_ip_field(body, "dns", cfg.dns);

        char *p;
        if ((p = strstr(body, "\"dhcp\"")))
            sscanf(p + 7, "%hhu", &cfg.dhcp_enable);

        printf("[CFG] DHCP: %d\n", cfg.dhcp_enable);

        // Cập nhật cấu hình
        app_cfg_set(&cfg);

        bool ok = app_cfg_save();
        printf("[FLASH] Save result: %s\n", ok ? "OK" : "FAIL");

        if (ok)
        {
            send_json(s, "{\"result\":\"OK\"}");
            printf("[SYS] Config saved. System will reboot in 500ms...\n");
            fflush(stdout);
            watchdog_reboot(0, 0, 1000);
        }
        else
        {
            send_json(s, "{\"result\":\"FAIL\"}");
        }
        return;
    }

    if (r->METHOD == METHOD_GET)
    {
        snprintf(json, sizeof(json),
                 "{"
                 "\"ip\":\"%d.%d.%d.%d\","
                 "\"mask\":\"%d.%d.%d.%d\","
                 "\"gw\":\"%d.%d.%d.%d\","
                 "\"dns\":\"%d.%d.%d.%d\","
                 "\"dhcp\":%d"
                 "}",
                 cfg.ip[0], cfg.ip[1], cfg.ip[2], cfg.ip[3],
                 cfg.sn[0], cfg.sn[1], cfg.sn[2], cfg.sn[3],
                 cfg.gw[0], cfg.gw[1], cfg.gw[2], cfg.gw[3],
                 cfg.dns[0], cfg.dns[1], cfg.dns[2], cfg.dns[3],
                 cfg.dhcp_enable);

        send_json(s, json);
        return;
    }

    send_json(s, "{\"error\":\"unsupported method\"}");
}
static void api_device(uint8_t s, void *req)
{
    const app_config_t *cfg = app_cfg_get();
    char json[256];

    snprintf(json, sizeof(json),
             "{"
             "\"board\":\"RP2040-W5500\","
             "\"firmware\":\"1.0.0\","
             "\"device\":\"%s\","
             "\"uuid\":\"%s\","
             "\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
             "\"uptime\":\"%lu s\","
             "\"build\":\"%s %s\""
             "}",
             cfg->device_name, cfg->uuid,
             cfg->mac[0], cfg->mac[1], cfg->mac[2],
             cfg->mac[3], cfg->mac[4], cfg->mac[5],
             (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000),
             __DATE__, __TIME__);

    send_json(s, json);
}

/* ===========================================================
 * /api/logs → Trả log mẫu hoặc log hệ thống
 * =========================================================== */
static void api_logs(uint8_t s, void *req)
{
    const char *dummy =
        "System start OK\n"
        "Ethernet link up\n"
        "Web server initialized\n"
        "SNMP Agent running\n";

    send_text(s, dummy);
}

/* ===========================================================
 * /api/config → GET: toàn bộ config, POST: cập nhật & lưu flash
 * =========================================================== */
static void api_config(uint8_t s, void *req)
{
    st_http_request *r = (st_http_request *)req;
    char body[512];
    char json[1024];
    app_config_t cfg = *app_cfg_get();
    char *p;

    // ======================================================
    // POST: Cập nhật cấu hình từ body JSON
    // ======================================================
    if (r->METHOD == METHOD_POST)
    {
        http_get_body(s, r->URI, body, sizeof(body));
        printf("[WEB] /api/config POST body: %s\n", body);

        // --- Parse chuỗi ---
        if ((p = strstr(body, "\"device_name\"")))
            sscanf(p + 15, "%31[^\"]", cfg.device_name);
        if ((p = strstr(body, "\"timezone\"")))
            sscanf(p + 11, "%hhd", &cfg.timezone);
        if ((p = strstr(body, "\"psu_slave_addr\"")))
        {
            char *value_start = strchr(p, ':');
            if (value_start)
            {
                // Bỏ qua khoảng trắng và dấu "
                value_start = strchr(value_start, '"');
                if (value_start)
                {
                    value_start++; // Bỏ qua dấu "
                    char *endptr;
                    unsigned long tmp = strtoul(value_start, &endptr, 16);
                    if (endptr != value_start && tmp <= 0xFF)
                    {
                        cfg.psu_slave_addr = (uint8_t)tmp;
                        printf("[CFG] psu_slave_addr = 0x%02X\n", cfg.psu_slave_addr);
                    }
                }
            }
        }

        if ((p = strstr(body, "\"snmp_enable\"")))
            sscanf(p + 14, "%hhu", &cfg.snmp_enable);
        if ((p = strstr(body, "\"trap_enable_mask\"")))
            sscanf(p + 20, "%hhx", &cfg.trap_enable_mask);
        if ((p = strstr(body, "\"sntp_enable\"")))
            sscanf(p + 14, "%hhu", &cfg.sntp_enable);
        if ((p = strstr(body, "\"web_enable\"")))
            sscanf(p + 13, "%hhu", &cfg.web_enable);
        if ((p = strstr(body, "\"psu_in_undervolt_V\"")))
            sscanf(p + 22, "%f", &cfg.psu_in_undervolt_V);
        if ((p = strstr(body, "\"psu_overtemp_C\"")))
            sscanf(p + 17, "%f", &cfg.psu_overtemp_C);
        if ((p = strstr(body, "\"psu_overcurrent_A\"")))
            sscanf(p + 20, "%f", &cfg.psu_overcurrent_A);

        // --- Parse ip_snmp an toàn ---
        if ((p = strstr(body, "\"ip_snmp\"")))
        {
            char ip_str[32] = {0};
            int b[4];
            if (sscanf(p, "\"ip_snmp\":\"%31[0-9.]\"", ip_str) == 1 &&
                sscanf(ip_str, "%d.%d.%d.%d", &b[0], &b[1], &b[2], &b[3]) == 4)
            {
                for (int i = 0; i < 4; i++)
                    cfg.snmp_manager_ip[i] = (uint8_t)b[i];
                printf("[CFG] SNMP Manager IP = %d.%d.%d.%d\n", b[0], b[1], b[2], b[3]);
            }
            else
            {
                printf("[CFG] Invalid ip_snmp field, ignored.\n");
            }
        }

        // --- Lưu lại cấu hình ---
        app_cfg_set(&cfg);
        app_cfg_save();

        send_json(s, "{\"result\":\"OK\"}");
        printf("[WEB] Config updated and saved.\n");
        return;
    }

    // ======================================================
    // GET: Trả toàn bộ cấu hình hiện tại
    // ======================================================
    snprintf(json, sizeof(json),
             "{"
             "\"device_name\":\"%s\","
             "\"uuid\":\"%s\","
             "\"timezone\":%d,"
             "\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
             "\"ip\":\"%d.%d.%d.%d\","
             "\"mask\":\"%d.%d.%d.%d\","
             "\"gw\":\"%d.%d.%d.%d\","
             "\"dns\":\"%d.%d.%d.%d\","
             "\"dhcp_enable\":%d,"
             "\"snmp_enable\":%d,"
             "\"ip_snmp\":\"%d.%d.%d.%d\","
             "\"trap_enable_mask\":%u,"
             "\"sntp_enable\":%d,"
             "\"web_enable\":%d,"
             "\"psu_slave_addr\":\"%02X\","
             "\"psu_in_undervolt_V\":%.1f,"
             "\"psu_overtemp_C\":%.1f,"
             "\"psu_overcurrent_A\":%.2f"
             "}",
             cfg.device_name, cfg.uuid, cfg.timezone,
             cfg.mac[0], cfg.mac[1], cfg.mac[2], cfg.mac[3], cfg.mac[4], cfg.mac[5],
             cfg.ip[0], cfg.ip[1], cfg.ip[2], cfg.ip[3],
             cfg.sn[0], cfg.sn[1], cfg.sn[2], cfg.sn[3],
             cfg.gw[0], cfg.gw[1], cfg.gw[2], cfg.gw[3],
             cfg.dns[0], cfg.dns[1], cfg.dns[2], cfg.dns[3],
             cfg.dhcp_enable,
             cfg.snmp_enable,
             cfg.snmp_manager_ip[0], cfg.snmp_manager_ip[1], cfg.snmp_manager_ip[2], cfg.snmp_manager_ip[3],
             cfg.trap_enable_mask,
             cfg.sntp_enable,
             cfg.web_enable,
             cfg.psu_slave_addr,
             cfg.psu_in_undervolt_V,
             cfg.psu_overtemp_C,
             cfg.psu_overcurrent_A);

    send_json(s, json);
}

/* ===========================================================
 * Register tất cả API
 * =========================================================== */
void httpServer_user_init(void)
{
    printf("[WEB] Registering user APIs...\n");
    httpServer_regAPI("api/status", api_status);
    httpServer_regAPI("api/network", api_network);
    httpServer_regAPI("api/device", api_device);
    httpServer_regAPI("api/logs", api_logs);
    httpServer_regAPI("api/config", api_config);
    printf("[WEB] User APIs registered.\n");
}
