#include <stdio.h>
#include <string.h>
#include <stdarg.h> 

#include "httpServer.h"
#include "dev_eth.h"
#include "dev_web.h"
#include "httpServer_user.h"
#include "web_assets.h"

#define ETHERNET_BUF_MAX_SIZE (32*1024) // 32KB
#define HTTP_SOCKET_MAX_NUM   8

static uint8_t http_tx_buf[ETHERNET_BUF_MAX_SIZE];
static uint8_t http_rx_buf[ETHERNET_BUF_MAX_SIZE];
static uint8_t http_sock_list[HTTP_SOCKET_MAX_NUM] = {0, 1, 2, 3, 4, 5, 6, 7};

// System log buffer
char system_log_buffer[4096];
uint32_t system_log_length = 0;

void log_printf(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (system_log_length + n >= sizeof(system_log_buffer))
        system_log_length = 0; // reset vòng tròn

    memcpy(system_log_buffer + system_log_length, buf, n);
    system_log_length += n;
}

void dev_web_init(void)
{
    printf("[WEB] Initializing HTTP server...\n");

    httpServer_init(http_tx_buf, http_rx_buf, HTTP_SOCKET_MAX_NUM, http_sock_list);
    
    // ✅ Tự động đăng ký tất cả web assets
    printf("[WEB] Registering %d web assets:\n", WEB_ASSETS_COUNT);
    for (int i = 0; i < WEB_ASSETS_COUNT; i++) {
        const web_asset_t *asset = &web_assets[i];
        
        // Đăng ký với httpServer
        reg_httpServer_webContent((uint8_t *)asset->name, (uint8_t *)asset->data);
        
        // Log thông tin
        printf("  [%d] %-20s (%s, %u bytes", 
               i + 1, asset->name, asset->mime_type, asset->length);
        
        if (asset->is_gzipped) {
            float ratio = (1.0f - (float)asset->length / asset->original_length) * 100;
            printf(", gzipped %.1f%%", ratio);
        }
        printf(")\n");
    }
    
    // Đăng ký root path riêng
    const web_asset_t *index_asset = NULL;
    for (int i = 0; i < WEB_ASSETS_COUNT; i++) {
        if (strcmp(web_assets[i].name, "index.html") == 0) {
            index_asset = &web_assets[i];
            break;
        }
    }
    if (index_asset) {
        reg_httpServer_webContent((uint8_t *)"/", (uint8_t *)index_asset->data);
    }

    httpServer_user_init();

    printf("[WEB] Web server ready with %d assets.\n", WEB_ASSETS_COUNT);
}

void dev_web_task(void)
{
    for (uint8_t i = 0; i < HTTP_SOCKET_MAX_NUM; i++)
        httpServer_run(i);
}