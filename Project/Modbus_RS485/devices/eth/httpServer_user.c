#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "httpServer.h"
#include "httpParser.h"
#include "httpUtil.h"
#include "socket.h"
#include "wizchip_conf.h"

#include "pico/time.h"
#include "hardware/watchdog.h"

#include "cJSON.h"  // ✅ Thay đổi ở đây
#include "dev_eth.h"
#include "dev_web.h"
#include "app_config.h"
#include "../psu_data/psu_data.h"

/* Forward declaration */
extern void eth_reinit_from_config(void);
extern void safe_reboot(void);

/* ===========================================================
 * Helper: gửi phản hồi JSON hoặc TEXT
 * =========================================================== */
static void send_json(uint8_t s, const char *json)
{
    send_http_response_header(s, PTYPE_JSON, strlen(json), STATUS_OK, 0);
    send(s, (uint8_t *)json, strlen(json));
}

static void send_text(uint8_t s, const char *txt)
{
    send_http_response_header(s, PTYPE_TEXT, strlen(txt), STATUS_OK, 0);
    send(s, (uint8_t *)txt, strlen(txt));
}

/* ===========================================================
 * Helper: đọc phần body HTTP POST request và parse JSON
 * =========================================================== */
static cJSON* http_parse_body(uint8_t s, const char *in)
{
    if (!in) return NULL;

    const char *body = strstr(in, "\r\n\r\n");
    if (!body)
    {
        printf("[HTTP] No body delimiter found on socket %d\n", s);
        return NULL;
    }

    body += 4; // Bỏ qua "\r\n\r\n"
    
    cJSON *json = cJSON_Parse(body);
    if (!json)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr)
        {
            printf("[HTTP] JSON parse error: %s\n", error_ptr);
        }
        return NULL;
    }

    printf("[HTTP] Socket %d | JSON parsed successfully\n", s);
    return json;
}

/* ===========================================================
 * Helper: lấy string từ JSON object
 * =========================================================== */
static void json_get_string_safe(cJSON *obj, const char *key, char *out, size_t out_size, const char *default_val)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL)
    {
        strncpy(out, item->valuestring, out_size - 1);
        out[out_size - 1] = '\0';
    }
    else
    {
        strncpy(out, default_val, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

/* ===========================================================
 * Helper: lấy number từ JSON object
 * =========================================================== */
static int json_get_number_safe(cJSON *obj, const char *key, int default_val)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item))
    {
        return item->valueint;
    }
    return default_val;
}

static float json_get_float_safe(cJSON *obj, const char *key, float default_val)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item))
    {
        return (float)item->valuedouble;
    }
    return default_val;
}

/* ===========================================================
 * Helper: parse IP từ JSON string
 * =========================================================== */
static void json_parse_ip(cJSON *obj, const char *key, uint8_t *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL)
    {
        unsigned int b0, b1, b2, b3;
        if (sscanf(item->valuestring, "%u.%u.%u.%u", &b0, &b1, &b2, &b3) == 4)
        {
            out[0] = (uint8_t)b0;
            out[1] = (uint8_t)b1;
            out[2] = (uint8_t)b2;
            out[3] = (uint8_t)b3;
            printf("[CFG] %s: %s -> %d.%d.%d.%d\n", 
                   key, item->valuestring, out[0], out[1], out[2], out[3]);
        }
    }
}

/* ===========================================================
 * /api/status → Trả trạng thái PSU (cho dashboard)
 * =========================================================== */
static void api_status(uint8_t s, void *req)
{
    psu_data_t psu = psu_data_read();  // ✅ đọc dữ liệu PSU an toàn (thread-safe)

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "vin", psu.vin);
    cJSON_AddNumberToObject(root, "vout", psu.vout);
    cJSON_AddNumberToObject(root, "vset", psu.vset);
    cJSON_AddNumberToObject(root, "iout", psu.iout);
    cJSON_AddNumberToObject(root, "cc", psu.cc);
    cJSON_AddNumberToObject(root, "temp", psu.temp);
    cJSON_AddNumberToObject(root, "fault_raw", psu.fault.raw);
    cJSON_AddNumberToObject(root, "fan_fail", psu.fault.bits.fan_fail);
    cJSON_AddNumberToObject(root, "otp", psu.fault.bits.otp);
    cJSON_AddNumberToObject(root, "ovp", psu.fault.bits.ovp);
    cJSON_AddNumberToObject(root, "olp", psu.fault.bits.olp);
    cJSON_AddNumberToObject(root, "short_circuit", psu.fault.bits.short_circuit);
    cJSON_AddNumberToObject(root, "ac_fail", psu.fault.bits.ac_fail);
    cJSON_AddNumberToObject(root, "op_off", psu.fault.bits.op_off);

    char *json_str = cJSON_PrintUnformatted(root);
    send_json(s, json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(root);
}

/* ===========================================================
 * /api/network → GET: lấy config mạng / POST: cập nhật & lưu
 * =========================================================== */
static void api_network(uint8_t s, void *req)
{
    st_http_request *r = (st_http_request *)req;
    app_config_t cfg = *app_cfg_get();

    if (r->METHOD == METHOD_POST)
    {
        cJSON *json = http_parse_body(s, r->URI);
        if (!json)
        {
            send_json(s, "{\"error\":\"invalid JSON\"}");
            return;
        }

        printf("[WEB] /api/network POST received\n");
        
        // Parse network config từ JSON
        json_parse_ip(json, "ip", cfg.ip);
        json_parse_ip(json, "mask", cfg.sn);
        json_parse_ip(json, "gw", cfg.gw);
        json_parse_ip(json, "dns", cfg.dns);
        
        cfg.dhcp_enable = json_get_number_safe(json, "dhcp", cfg.dhcp_enable);
        printf("[CFG] DHCP: %d\n", cfg.dhcp_enable);

        cJSON_Delete(json);

        app_cfg_set(&cfg);
        bool ok = app_cfg_save();
        printf("[FLASH] Save result: %s\n", ok ? "OK" : "FAIL");

        if (ok)
        {
            send_json(s, "{\"result\":\"OK\"}");
            printf("[SYS] Config saved. System will reboot in 500ms...\n");
            fflush(stdout);
            safe_reboot();
        }
        else
        {
            send_json(s, "{\"result\":\"FAIL\"}");
        }
        return;
    }

    if (r->METHOD == METHOD_GET)
    {
        cJSON *root = cJSON_CreateObject();
        
        // Tạo IP strings
        char ip_str[16], mask_str[16], gw_str[16], dns_str[16];
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", cfg.ip[0], cfg.ip[1], cfg.ip[2], cfg.ip[3]);
        snprintf(mask_str, sizeof(mask_str), "%d.%d.%d.%d", cfg.sn[0], cfg.sn[1], cfg.sn[2], cfg.sn[3]);
        snprintf(gw_str, sizeof(gw_str), "%d.%d.%d.%d", cfg.gw[0], cfg.gw[1], cfg.gw[2], cfg.gw[3]);
        snprintf(dns_str, sizeof(dns_str), "%d.%d.%d.%d", cfg.dns[0], cfg.dns[1], cfg.dns[2], cfg.dns[3]);
        
        cJSON_AddStringToObject(root, "ip", ip_str);
        cJSON_AddStringToObject(root, "mask", mask_str);
        cJSON_AddStringToObject(root, "gw", gw_str);
        cJSON_AddStringToObject(root, "dns", dns_str);
        cJSON_AddNumberToObject(root, "dhcp", cfg.dhcp_enable);

        char *json_str = cJSON_PrintUnformatted(root);
        send_json(s, json_str);
        
        cJSON_free(json_str);
        cJSON_Delete(root);
        return;
    }

    send_json(s, "{\"error\":\"unsupported method\"}");
}

/* ===========================================================
 * /api/device → Trả thông tin thiết bị
 * =========================================================== */
static void api_device(uint8_t s, void *req)
{
    const app_config_t *cfg = app_cfg_get();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "board", "RP2040-W5500");
    cJSON_AddStringToObject(root, "firmware", "1.0.0");
    cJSON_AddStringToObject(root, "device", cfg->device_name);
    cJSON_AddStringToObject(root, "uuid", cfg->uuid);
    
    // MAC address
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             cfg->mac[0], cfg->mac[1], cfg->mac[2], 
             cfg->mac[3], cfg->mac[4], cfg->mac[5]);
    cJSON_AddStringToObject(root, "mac", mac_str);
    
    cJSON_AddStringToObject(root, "uptime", "running"); // Có thể tính toán thời gian thực
    cJSON_AddStringToObject(root, "build", __DATE__ " " __TIME__);

    char *json_str = cJSON_PrintUnformatted(root);
    send_json(s, json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(root);
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
    app_config_t cfg = *app_cfg_get();

    if (r->METHOD == METHOD_POST)
    {
        cJSON *json = http_parse_body(s, r->URI);
        if (!json)
        {
            send_json(s, "{\"error\":\"invalid JSON\"}");
            return;
        }

        printf("[WEB] /api/config POST received\n");

        // Parse config từ JSON
        json_get_string_safe(json, "device_name", cfg.device_name, sizeof(cfg.device_name), cfg.device_name);
        cfg.timezone = json_get_number_safe(json, "timezone", cfg.timezone);
        
        // Parse PSU slave address (hex)
        cJSON *psu_addr = cJSON_GetObjectItemCaseSensitive(json, "psu_slave_addr");
        if (cJSON_IsString(psu_addr) && psu_addr->valuestring != NULL)
        {
            unsigned long tmp = strtoul(psu_addr->valuestring, NULL, 16);
            if (tmp <= 0xFF)
            {
                cfg.psu_slave_addr = (uint8_t)tmp;
                printf("[CFG] psu_slave_addr = 0x%02X\n", cfg.psu_slave_addr);
            }
        }
        
        cfg.snmp_enable = json_get_number_safe(json, "snmp_enable", cfg.snmp_enable);
        
        // Parse trap enable mask (hex)
        cJSON *trap_mask = cJSON_GetObjectItemCaseSensitive(json, "trap_enable_mask");
        if (cJSON_IsString(trap_mask) && trap_mask->valuestring != NULL)
        {
            unsigned long tmp = strtoul(trap_mask->valuestring, NULL, 16);
            if (tmp <= 0xFF)
            {
                cfg.trap_enable_mask = (uint8_t)tmp;
            }
        }
        
        cfg.sntp_enable = json_get_number_safe(json, "sntp_enable", cfg.sntp_enable);
        cfg.web_enable = json_get_number_safe(json, "web_enable", cfg.web_enable);
        cfg.psu_in_undervolt_V = json_get_float_safe(json, "psu_in_undervolt_V", cfg.psu_in_undervolt_V);
        cfg.psu_overtemp_C = json_get_float_safe(json, "psu_overtemp_C", cfg.psu_overtemp_C);
        cfg.psu_overcurrent_A = json_get_float_safe(json, "psu_overcurrent_A", cfg.psu_overcurrent_A);
        
        // Parse SNMP manager IP
        json_parse_ip(json, "ip_snmp", cfg.snmp_manager_ip);

        cJSON_Delete(json);

        app_cfg_set(&cfg);
        app_cfg_save();
        send_json(s, "{\"result\":\"OK\"}");
        printf("[WEB] Config updated and saved.\n");
        return;
    }

    // GET method - trả về toàn bộ config
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddStringToObject(root, "device_name", cfg.device_name);
    cJSON_AddStringToObject(root, "uuid", cfg.uuid);
    cJSON_AddNumberToObject(root, "timezone", cfg.timezone);
    
    // MAC address
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             cfg.mac[0], cfg.mac[1], cfg.mac[2], cfg.mac[3], cfg.mac[4], cfg.mac[5]);
    cJSON_AddStringToObject(root, "mac", mac_str);
    
    // IP addresses
    char ip_str[16], mask_str[16], gw_str[16], dns_str[16], snmp_ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", cfg.ip[0], cfg.ip[1], cfg.ip[2], cfg.ip[3]);
    snprintf(mask_str, sizeof(mask_str), "%d.%d.%d.%d", cfg.sn[0], cfg.sn[1], cfg.sn[2], cfg.sn[3]);
    snprintf(gw_str, sizeof(gw_str), "%d.%d.%d.%d", cfg.gw[0], cfg.gw[1], cfg.gw[2], cfg.gw[3]);
    snprintf(dns_str, sizeof(dns_str), "%d.%d.%d.%d", cfg.dns[0], cfg.dns[1], cfg.dns[2], cfg.dns[3]);
    snprintf(snmp_ip_str, sizeof(snmp_ip_str), "%d.%d.%d.%d", 
             cfg.snmp_manager_ip[0], cfg.snmp_manager_ip[1], cfg.snmp_manager_ip[2], cfg.snmp_manager_ip[3]);
    
    cJSON_AddStringToObject(root, "ip", ip_str);
    cJSON_AddStringToObject(root, "mask", mask_str);
    cJSON_AddStringToObject(root, "gw", gw_str);
    cJSON_AddStringToObject(root, "dns", dns_str);
    cJSON_AddNumberToObject(root, "dhcp_enable", cfg.dhcp_enable);
    cJSON_AddNumberToObject(root, "snmp_enable", cfg.snmp_enable);
    cJSON_AddStringToObject(root, "ip_snmp", snmp_ip_str);
    cJSON_AddNumberToObject(root, "trap_enable_mask", cfg.trap_enable_mask);
    cJSON_AddNumberToObject(root, "sntp_enable", cfg.sntp_enable);
    cJSON_AddNumberToObject(root, "web_enable", cfg.web_enable);
    
    // PSU slave address (hex string)
    char psu_addr_str[8];
    snprintf(psu_addr_str, sizeof(psu_addr_str), "%02X", cfg.psu_slave_addr);
    cJSON_AddStringToObject(root, "psu_slave_addr", psu_addr_str);
    
    cJSON_AddNumberToObject(root, "psu_in_undervolt_V", cfg.psu_in_undervolt_V);
    cJSON_AddNumberToObject(root, "psu_overtemp_C", cfg.psu_overtemp_C);
    cJSON_AddNumberToObject(root, "psu_overcurrent_A", cfg.psu_overcurrent_A);

    char *json_str = cJSON_PrintUnformatted(root);
    send_json(s, json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(root);
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