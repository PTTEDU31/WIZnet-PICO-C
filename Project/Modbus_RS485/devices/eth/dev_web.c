#include <stdio.h>
#include <string.h>
#include <stdarg.h> 

#include "httpServer.h"
#include "dev_eth.h"
#include "dev_web.h"
#include "httpServer_user.h"



#define ETHERNET_BUF_MAX_SIZE (2048)
#define HTTP_SOCKET_MAX_NUM   4

static uint8_t http_tx_buf[ETHERNET_BUF_MAX_SIZE];
static uint8_t http_rx_buf[ETHERNET_BUF_MAX_SIZE];
static uint8_t http_sock_list[HTTP_SOCKET_MAX_NUM] = {0, 1, 2, 3};

extern const unsigned char web_index[];
extern const unsigned int  web_index_len;
extern const unsigned char web_style[];
extern const unsigned int  web_style_len;
extern const unsigned char web_script[];
extern const unsigned int  web_script_len;
extern const unsigned char web_favicon[];
extern const unsigned int  web_favicon_len;
extern const unsigned char  web_advanced[];
extern const unsigned char  web_advanced_script[];



char system_log_buffer[1024];
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
    reg_httpServer_webContent("/", web_index); 
    reg_httpServer_webContent("index.html", web_index);
    reg_httpServer_webContent("style.css", web_style);
    reg_httpServer_webContent("script.js", web_script);
    reg_httpServer_webContent("favicon.ico", web_favicon);
    reg_httpServer_webContent("advanced.html", web_advanced);
    reg_httpServer_webContent("advanced.js", web_advanced_script);


    httpServer_user_init();

    printf("[WEB] Web server ready.\n");
}

void dev_web_task(void)
{
    for (uint8_t i = 0; i < HTTP_SOCKET_MAX_NUM; i++)
        httpServer_run(i);
}
