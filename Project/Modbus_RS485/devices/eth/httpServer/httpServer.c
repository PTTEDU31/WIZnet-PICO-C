#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "socket.h"
#include "wizchip_conf.h"

#include "httpServer.h"
#include "httpParser.h"
#include "httpUtil.h"
#include "web_assets.h"

#ifdef _USE_SDCARD_
#include "ff.h"
#endif

#ifndef DATA_BUF_SIZE
#define DATA_BUF_SIZE 4096
#endif

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/
static uint8_t HTTPSock_Num[_WIZCHIP_SOCK_NUM_] = {0};
static st_http_request *http_request;
static st_http_request *parsed_http_request;
static uint8_t *http_response;

// API Callback Management
#define MAX_API_CALLBACKS 20
typedef struct {
    const char *path;
    void (*callback)(uint8_t s, void *req);
} http_api_entry;

static http_api_entry api_table[MAX_API_CALLBACKS];
static uint8_t api_count = 0;

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/
uint8_t *pHTTP_TX;
uint8_t *pHTTP_RX;

volatile uint32_t httpServer_tick_1s = 0;
st_http_socket HTTPSock_Status[_WIZCHIP_SOCK_NUM_] = {{STATE_HTTP_IDLE}};

#ifdef _USE_SDCARD_
FIL fs;
FRESULT fr;
#endif

/*****************************************************************************
 * Private functions
 ****************************************************************************/
static void httpServer_Sockinit(uint8_t cnt, uint8_t *socklist);
static uint8_t getHTTPSocketNum(uint8_t seqnum);
static int8_t getHTTPSequenceNum(uint8_t socket);
static int8_t http_disconnect(uint8_t sn);

static void http_process_handler(uint8_t s, st_http_request *p_http_request);
static void send_http_response_body(uint8_t s, uint8_t *uri_name, uint8_t *buf, 
                                     uint32_t start_addr, uint32_t file_len);
static void send_http_response_cgi(uint8_t s, uint8_t *buf, uint8_t *http_body, 
                                    uint16_t file_len);

/*****************************************************************************
 * Callback functions
 ****************************************************************************/
void default_mcu_reset(void) { ; }
void default_wdt_reset(void) { ; }
void (*HTTPServer_ReStart)(void) = default_mcu_reset;
void (*HTTPServer_WDT_Reset)(void) = default_wdt_reset;

/*****************************************************************************
 * Socket Management Functions
 ****************************************************************************/
static void httpServer_Sockinit(uint8_t cnt, uint8_t *socklist)
{
    for (uint8_t i = 0; i < cnt; i++) {
        HTTPSock_Num[i] = socklist[i];
    }
}

static uint8_t getHTTPSocketNum(uint8_t seqnum)
{
    return HTTPSock_Num[seqnum];
}

static int8_t getHTTPSequenceNum(uint8_t socket)
{
    for (uint8_t i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
        if (HTTPSock_Num[i] == socket)
            return i;
    }
    return -1;
}

/*****************************************************************************
 * Initialization Functions
 ****************************************************************************/
void httpServer_init(uint8_t *tx_buf, uint8_t *rx_buf, uint8_t cnt, uint8_t *socklist)
{
    pHTTP_TX = tx_buf;
    pHTTP_RX = rx_buf;
    httpServer_Sockinit(cnt, socklist);
    
    printf("[HTTP] Server initialized with %d sockets\n", cnt);
}

void reg_httpServer_cbfunc(void (*mcu_reset)(void), void (*wdt_reset)(void))
{
    if (mcu_reset)
        HTTPServer_ReStart = mcu_reset;
    if (wdt_reset)
        HTTPServer_WDT_Reset = wdt_reset;
}

/*****************************************************************************
 * API Registration
 ****************************************************************************/
void httpServer_regAPI(const char *path, void (*callback)(uint8_t s, void *req))
{
    if (api_count >= MAX_API_CALLBACKS) {
        printf("[HTTP] ERROR: API table full (max %d)\n", MAX_API_CALLBACKS);
        return;
    }
    
    api_table[api_count].path = path;
    api_table[api_count].callback = callback;
    api_count++;
    
    printf("[HTTP] Registered API: /%s\n", path);
}

/*****************************************************************************
 * Web Assets Management (NEW - Using web_assets.h)
 ****************************************************************************/
static const web_asset_t* find_web_asset(const char *uri_name)
{
    // Handle root path
    if (strcmp(uri_name, "/") == 0 || strcmp(uri_name, "") == 0) {
        uri_name = "index.html";
    }
    
    // Remove leading slash if present
    if (uri_name[0] == '/') {
        uri_name++;
    }
    
    // Search in assets table
    for (int i = 0; i < WEB_ASSETS_COUNT; i++) {
        if (strcmp(web_assets[i].name, uri_name) == 0) {
            return &web_assets[i];
        }
    }
    
    return NULL;
}

/*****************************************************************************
 * HTTP Response Functions
 ****************************************************************************/
void send_http_response_header(uint8_t s, uint8_t content_type, uint32_t body_len, 
                                uint16_t http_status, uint8_t gzipped)
{
    switch (http_status) {
    case STATUS_OK:
        if ((content_type != PTYPE_CGI) && (content_type != PTYPE_XML)) {
#ifdef _HTTPSERVER_DEBUG_
            printf("> HTTPSocket[%d] : HTTP Response Header - STATUS_OK\n", s);
#endif
            make_http_response_head((char *)http_response, content_type, body_len,gzipped);
        } else {
#ifdef _HTTPSERVER_DEBUG_
            printf("> HTTPSocket[%d] : HTTP Response Header - NONE / CGI or XML\n", s);
#endif
            http_status = 0;
        }
        break;
        
    case STATUS_BAD_REQ:
#ifdef _HTTPSERVER_DEBUG_
        printf("> HTTPSocket[%d] : HTTP Response Header - STATUS_BAD_REQ\n", s);
#endif
        memcpy(http_response, ERROR_REQUEST_PAGE, sizeof(ERROR_REQUEST_PAGE));
        break;
        
    case STATUS_NOT_FOUND:
#ifdef _HTTPSERVER_DEBUG_
        printf("> HTTPSocket[%d] : HTTP Response Header - STATUS_NOT_FOUND\n", s);
#endif
        memcpy(http_response, ERROR_HTML_PAGE, sizeof(ERROR_HTML_PAGE));
        break;
        
    default:
        break;
    }

    if (http_status) {
#ifdef _HTTPSERVER_DEBUG_
        printf("> HTTPSocket[%d] : [Send] HTTP Response Header [%d] bytes\n", 
               s, (uint16_t)strlen((char *)http_response));
#endif
        send(s, http_response, strlen((char *)http_response));
    }
}

static void send_http_response_body(uint8_t s, uint8_t *uri_name, uint8_t *buf, 
                                     uint32_t start_addr, uint32_t file_len)
{
    int8_t get_seqnum = getHTTPSequenceNum(s);
    if (get_seqnum == -1) return;

    uint32_t send_len;
    uint8_t flag_datasend_end = 0;

    // First part of response
    if (!HTTPSock_Status[get_seqnum].file_len) {
        if (file_len > DATA_BUF_SIZE - 1) {
            HTTPSock_Status[get_seqnum].file_start = start_addr;
            HTTPSock_Status[get_seqnum].file_len = file_len;
            send_len = DATA_BUF_SIZE - 1;
            
            memset(HTTPSock_Status[get_seqnum].file_name, 0x00, MAX_CONTENT_NAME_LEN);
            strcpy((char *)HTTPSock_Status[get_seqnum].file_name, (char *)uri_name);
#ifdef _HTTPSERVER_DEBUG_
            printf("> HTTPSocket[%d] : HTTP Response body - file [%s] len [%ld] bytes\n", 
                   s, HTTPSock_Status[get_seqnum].file_name, file_len);
#endif
        } else {
            send_len = file_len;
#ifdef _HTTPSERVER_DEBUG_
            printf("> HTTPSocket[%d] : HTTP Response end - file len [%ld] bytes\n", s, send_len);
#endif
        }
    } 
    // Remaining parts
    else {
        send_len = HTTPSock_Status[get_seqnum].file_len - HTTPSock_Status[get_seqnum].file_offset;
        
        if (send_len > DATA_BUF_SIZE - 1) {
            send_len = DATA_BUF_SIZE - 1;
        } else {
#ifdef _HTTPSERVER_DEBUG_
            printf("> HTTPSocket[%d] : HTTP Response end - file len [%ld] bytes\n", 
                   s, HTTPSock_Status[get_seqnum].file_len);
#endif
            flag_datasend_end = 1;
        }
#ifdef _HTTPSERVER_DEBUG_
        printf("> HTTPSocket[%d] : HTTP Response body - send len [%ld] bytes\n", s, send_len);
#endif
    }

    // Read data based on storage type
    if (HTTPSock_Status[get_seqnum].storage_type == CODEFLASH) {
        // Read from embedded web assets
        const web_asset_t *asset = (const web_asset_t *)start_addr;
        uint32_t offset = HTTPSock_Status[get_seqnum].file_offset;
        
        if (asset && asset->data) {
            memcpy(buf, asset->data + offset, send_len);
            *(buf + send_len) = 0;
        } else {
            send_len = 0;
        }
    }
#ifdef _USE_SDCARD_
    else if (HTTPSock_Status[get_seqnum].storage_type == SDCARD) {
        uint16_t blocklen;
        fr = f_read(&fs, buf, send_len, (void *)&blocklen);
        if (fr != FR_OK) {
            send_len = 0;
#ifdef _HTTPSERVER_DEBUG_
            printf("> HTTPSocket[%d] : [FatFs] Error code: %d (File Read Failed)\n", s, fr);
#endif
        } else {
            *(buf + send_len) = 0;
        }
    }
#endif
    else {
        send_len = 0;
    }

    // Send data to client
#ifdef _HTTPSERVER_DEBUG_
    printf("> HTTPSocket[%d] : [Send] HTTP Response body [%ld] bytes\n", s, send_len);
#endif

    if (send_len) {
        send(s, buf, send_len);
    } else {
        flag_datasend_end = 1;
    }

    // Update status
    if (flag_datasend_end) {
        HTTPSock_Status[get_seqnum].file_start = 0;
        HTTPSock_Status[get_seqnum].file_len = 0;
        HTTPSock_Status[get_seqnum].file_offset = 0;
    } else {
        HTTPSock_Status[get_seqnum].file_offset += send_len;
#ifdef _HTTPSERVER_DEBUG_
        printf("> HTTPSocket[%d] : HTTP Response body - offset [%ld]\n", 
               s, HTTPSock_Status[get_seqnum].file_offset);
#endif
    }

#ifdef _USE_SDCARD_
    f_close(&fs);
#endif
}

static void send_http_response_cgi(uint8_t s, uint8_t *buf, uint8_t *http_body, 
                                    uint16_t file_len)
{
    uint16_t send_len;
    
#ifdef _HTTPSERVER_DEBUG_
    printf("> HTTPSocket[%d] : HTTP Response Header + Body - CGI\n", s);
#endif
    
    send_len = sprintf((char *)buf, "%s%d\r\n\r\n%s", RES_CGIHEAD_OK, file_len, http_body);
    
#ifdef _HTTPSERVER_DEBUG_
    printf("> HTTPSocket[%d] : HTTP Response Header + Body - send len [%d] bytes\n", s, send_len);
#endif
    
    send(s, buf, send_len);
}

static int8_t http_disconnect(uint8_t sn)
{
    setSn_CR(sn, Sn_CR_DISCON);
    while (getSn_CR(sn));
    return SOCK_OK;
}

/*****************************************************************************
 * HTTP Request Processing (IMPROVED)
 ****************************************************************************/
static void http_process_handler(uint8_t s, st_http_request *p_http_request)
{
    uint8_t uri_buf[MAX_URI_SIZE] = {0};
    uint8_t *uri_name;
    uint16_t http_status = 0;
    int8_t get_seqnum;
    uint8_t content_found = 0;
    uint32_t file_len = 0;

    if ((get_seqnum = getHTTPSequenceNum(s)) == -1) return;

    http_response = pHTTP_RX;

    // Parse URI
    get_http_uri_name(p_http_request->URI, uri_buf);
    uri_name = uri_buf;

    // Handle root path
    if (!strcmp((char *)uri_name, "/")) {
        strcpy((char *)uri_name, INITIAL_WEBPAGE);
    }

    // Method processing
    switch (p_http_request->METHOD) {
    case METHOD_ERR:
        http_status = STATUS_BAD_REQ;
        send_http_response_header(s, 0, 0, http_status,0);
        break;

    case METHOD_HEAD:
    case METHOD_GET:
        // Check if it's an API request
        if (strncmp((char *)uri_name, "api/", 4) == 0) {
            for (uint8_t i = 0; i < api_count; i++) {
                if (!strcmp((char *)uri_name, api_table[i].path)) {
                    printf("[HTTP] Handling API: /%s\n", uri_name);
                    api_table[i].callback(s, p_http_request);
                    return;
                }
            }
            // API not found
            const char *err = "{\"error\":\"API not found\"}";
            send_http_response_header(s, PTYPE_JSON, strlen(err), STATUS_NOT_FOUND ,0 );
            send(s, (uint8_t *)err, strlen(err));
            return;
        }

        // Find content type
        find_http_uri_type(&p_http_request->TYPE, uri_name);

#ifdef _HTTPSERVER_DEBUG_
        printf("\n> HTTPSocket[%d] : HTTP Method GET\n", s);
        printf("> HTTPSocket[%d] : Request Type = %d\n", s, p_http_request->TYPE);
        printf("> HTTPSocket[%d] : Request URI = %s\n", s, uri_name);
#endif

        // Handle CGI requests
        if (p_http_request->TYPE == PTYPE_CGI) {
            content_found = http_get_cgi_handler(uri_name, pHTTP_TX, &file_len);
            if (content_found && (file_len <= (DATA_BUF_SIZE - (strlen(RES_CGIHEAD_OK) + 8)))) {
                send_http_response_cgi(s, http_response, pHTTP_TX, (uint16_t)file_len);
            } else {
                send_http_response_header(s, PTYPE_CGI, 0, STATUS_NOT_FOUND,0);
            }
        }
        // Handle static content
        else {
            const web_asset_t *asset = find_web_asset((char *)uri_name);
            if (asset) {
                content_found = 1;
                file_len = asset->length;
                HTTPSock_Status[get_seqnum].storage_type = CODEFLASH;
                http_status = STATUS_OK;
                
#ifdef _HTTPSERVER_DEBUG_
                printf("> HTTPSocket[%d] : Found asset [%s] - %lu bytes", 
                       s, asset->name, file_len);
                if (asset->is_gzipped) {
                    printf(" (gzipped)");
                }
                printf("\n");
#endif
            }
#ifdef _USE_SDCARD_
            // Fallback to SD card
            else if ((fr = f_open(&fs, (const char *)uri_name, FA_READ)) == 0) {
                content_found = 1;
                file_len = fs.fsize;
                HTTPSock_Status[get_seqnum].storage_type = SDCARD;
                http_status = STATUS_OK;
                
#ifdef _HTTPSERVER_DEBUG_
                printf("> HTTPSocket[%d] : Found file on SD [%s] - %lu bytes\n", 
                       s, uri_name, file_len);
#endif
            }
#endif
            else {
                content_found = 0;
                http_status = STATUS_NOT_FOUND;
#ifdef _HTTPSERVER_DEBUG_
                printf("> HTTPSocket[%d] : Content not found [%s]\n", s, uri_name);
#endif
            }

            // Send HTTP header
            if (http_status) {
#ifdef _HTTPSERVER_DEBUG_
                printf("> HTTPSocket[%d] : Requested content len = [%ld] bytes\n", s, file_len);
#endif
                send_http_response_header(s, p_http_request->TYPE, file_len, http_status,
										 asset ? asset->is_gzipped : 0);
            }

            // Send HTTP body
            if (http_status == STATUS_OK) {
                send_http_response_body(s, uri_name, http_response, (uint32_t)asset, file_len);
            }
        }
        break;

    case METHOD_POST:
        mid((char *)p_http_request->URI, "/", " HTTP", (char *)uri_buf);
        uri_name = uri_buf;
        find_http_uri_type(&p_http_request->TYPE, uri_name);

        // Check if it's an API request
        if (strncmp((char *)uri_name, "api/", 4) == 0) {
            for (uint8_t i = 0; i < api_count; i++) {
                if (!strcmp((char *)uri_name, api_table[i].path)) {
                    printf("[HTTP] Handling POST API: /%s\n", uri_name);
                    api_table[i].callback(s, p_http_request);
                    return;
                }
            }
            const char *err = "{\"error\":\"API not found\"}";
            send_http_response_header(s, PTYPE_JSON, strlen(err), STATUS_NOT_FOUND,0);
            send(s, (uint8_t *)err, strlen(err));
            return;
        }

#ifdef _HTTPSERVER_DEBUG_
        printf("\n> HTTPSocket[%d] : HTTP Method POST\n", s);
        printf("> HTTPSocket[%d] : Request URI = %s Type = %d\n", s, uri_name, p_http_request->TYPE);
#endif

        if (p_http_request->TYPE == PTYPE_CGI) {
            content_found = http_post_cgi_handler(uri_name, p_http_request, http_response, &file_len);
            
#ifdef _HTTPSERVER_DEBUG_
            printf("> HTTPSocket[%d] : [CGI: %s] / Response len [%ld] bytes\n", 
                   s, content_found ? "Content found" : "Content not found", file_len);
#endif
            
            if (content_found && (file_len <= (DATA_BUF_SIZE - (strlen(RES_CGIHEAD_OK) + 8)))) {
                send_http_response_cgi(s, pHTTP_TX, http_response, (uint16_t)file_len);
                
                if (content_found == HTTP_RESET) {
                    HTTPServer_ReStart();
                }
            } else {
                send_http_response_header(s, PTYPE_CGI, 0, STATUS_NOT_FOUND,0);
            }
        } else {
            send_http_response_header(s, 0, 0, STATUS_NOT_FOUND,0);
        }
        break;

    default:
        http_status = STATUS_BAD_REQ;
        send_http_response_header(s, 0, 0, http_status,0);
        break;
    }
}

/*****************************************************************************
 * Main HTTP Server Loop
 ****************************************************************************/
void httpServer_run(uint8_t seqnum)
{
    uint8_t s = getHTTPSocketNum(seqnum);
    uint16_t len;
    uint32_t gettime = 0;

#ifdef _HTTPSERVER_DEBUG_
    uint8_t destip[4] = {0};
    uint16_t destport = 0;
#endif

    http_request = (st_http_request *)pHTTP_RX;
    parsed_http_request = (st_http_request *)pHTTP_TX;

    switch (getSn_SR(s)) {
    case SOCK_ESTABLISHED:
        if (getSn_IR(s) & Sn_IR_CON) {
            setSn_IR(s, Sn_IR_CON);
        }

        switch (HTTPSock_Status[seqnum].sock_status) {
        case STATE_HTTP_IDLE:
            if ((len = getSn_RX_RSR(s)) > 0) {
                if (len > DATA_BUF_SIZE) len = DATA_BUF_SIZE;
                len = recv(s, (uint8_t *)http_request, len);
                *(((uint8_t *)http_request) + len) = '\0';

                parse_http_request(parsed_http_request, (uint8_t *)http_request);
                
#ifdef _HTTPSERVER_DEBUG_
                getSn_DIPR(s, destip);
                destport = getSn_DPORT(s);
                printf("\n> HTTPSocket[%d] : HTTP Request from %d.%d.%d.%d:%d\n", 
                       s, destip[0], destip[1], destip[2], destip[3], destport);
#endif

                http_process_handler(s, parsed_http_request);

                gettime = get_httpServer_timecount();
                while (getSn_TX_FSR(s) != getSn_TxMAX(s)) {
                    if ((get_httpServer_timecount() - gettime) > 3) {
#ifdef _HTTPSERVER_DEBUG_
                        printf("> HTTPSocket[%d] : TX Buffer clear timeout\n", s);
#endif
                        break;
                    }
                }

                if (HTTPSock_Status[seqnum].file_len > 0)
                    HTTPSock_Status[seqnum].sock_status = STATE_HTTP_RES_INPROC;
                else
                    HTTPSock_Status[seqnum].sock_status = STATE_HTTP_RES_DONE;
            }
            break;

        case STATE_HTTP_RES_INPROC:
#ifdef _HTTPSERVER_DEBUG_
            printf("> HTTPSocket[%d] : [State] STATE_HTTP_RES_INPROC\n", s);
#endif
            send_http_response_body(s, 0, http_response, 0, 0);
            
            if (HTTPSock_Status[seqnum].file_len == 0)
                HTTPSock_Status[seqnum].sock_status = STATE_HTTP_RES_DONE;
            break;

        case STATE_HTTP_RES_DONE:
#ifdef _HTTPSERVER_DEBUG_
            printf("> HTTPSocket[%d] : [State] STATE_HTTP_RES_DONE\n", s);
#endif
            HTTPSock_Status[seqnum].file_len = 0;
            HTTPSock_Status[seqnum].file_offset = 0;
            HTTPSock_Status[seqnum].file_start = 0;
            HTTPSock_Status[seqnum].sock_status = STATE_HTTP_IDLE;

#ifdef _USE_WATCHDOG_
            HTTPServer_WDT_Reset();
#endif
            http_disconnect(s);
            break;

        default:
            break;
        }
        break;

    case SOCK_CLOSE_WAIT:
#ifdef _HTTPSERVER_DEBUG_
        printf("> HTTPSocket[%d] : CLOSE_WAIT\n", s);
#endif
        disconnect(s);
        break;

    case SOCK_CLOSED:
#ifdef _HTTPSERVER_DEBUG_
        printf("> HTTPSocket[%d] : CLOSED\n", s);
#endif
        if (socket(s, Sn_MR_TCP, HTTP_SERVER_PORT, 0x00) == s) {
#ifdef _HTTPSERVER_DEBUG_
            printf("> HTTPSocket[%d] : OPEN\n", s);
#endif
        }
        break;

    case SOCK_INIT:
        listen(s);
        break;

    case SOCK_LISTEN:
        break;

    default:
        break;
    }

#ifdef _USE_WATCHDOG_
    HTTPServer_WDT_Reset();
#endif
}

/*****************************************************************************
 * Time Handler
 ****************************************************************************/
void httpServer_time_handler(void)
{
    httpServer_tick_1s++;
}

uint32_t get_httpServer_timecount(void)
{
    return httpServer_tick_1s;
}

/*****************************************************************************
 * Legacy Functions (Kept for backward compatibility)
 ****************************************************************************/
void reg_httpServer_webContent(uint8_t *content_name, uint8_t *content)
{
    // This function is now deprecated in favor of web_assets.h
    // Kept for backward compatibility only
#ifdef _HTTPSERVER_DEBUG_
    printf("[HTTP] Warning: reg_httpServer_webContent() is deprecated. Use web_assets.h instead.\n");
#endif
}

uint8_t display_reg_webContent_list(void)
{
    if (WEB_ASSETS_COUNT == 0) {
        printf(">> Web content file not found\n");
        return 0;
    }

    printf("\n=== List of Web Assets ===\n");
    for (int i = 0; i < WEB_ASSETS_COUNT; i++) {
        const web_asset_t *asset = &web_assets[i];
        printf(" [%d] %-20s (%s, %u bytes", 
               i + 1, asset->name, asset->mime_type, asset->length);
        
        if (asset->is_gzipped) {
            float ratio = (1.0f - (float)asset->length / asset->original_length) * 100;
            printf(", gzipped %.1f%%", ratio);
        }
        printf(")\n");
    }
    printf("==========================\n\n");
    
    return 1;
}