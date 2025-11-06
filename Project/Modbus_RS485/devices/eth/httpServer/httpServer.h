/**
 * @file httpServer.h
 * @brief Define constants and functions related HTTP Web server.
 * @version 2.0 - Enhanced with web_assets.h support
 */

#ifndef __HTTPSERVER_H__
#define __HTTPSERVER_H__

#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Configuration
 ****************************************************************************/
// HTTP Server debug message enable
#define _HTTPSERVER_DEBUG_

// Initial webpage settings
#define INITIAL_WEBPAGE         "index.html"
#define M_INITIAL_WEBPAGE       "m/index.html"
#define MOBILE_INITIAL_WEBPAGE  "mobile/index.html"

// Web Server Content Storage Select
// #define _USE_SDCARD_
#ifndef _USE_SDCARD_
// #define _USE_FLASH_
#endif

#if !defined(_USE_SDCARD_) && !defined(_USE_FLASH_)
#define _NOTUSED_STORAGE_
#endif

// Watchdog timer
#define _USE_WATCHDOG_

/*****************************************************************************
 * HTTP Process states list
 ****************************************************************************/
#define STATE_HTTP_IDLE         0   /* IDLE, Waiting for data received (TCP established) */
#define STATE_HTTP_REQ_INPROC   1   /* Received HTTP request from HTTP client */
#define STATE_HTTP_REQ_DONE     2   /* The end of HTTP request parse */
#define STATE_HTTP_RES_INPROC   3   /* Sending the HTTP response to HTTP client (in progress) */
#define STATE_HTTP_RES_DONE     4   /* The end of HTTP response send (HTTP transaction ended) */

/*****************************************************************************
 * HTTP Simple Return Value
 ****************************************************************************/
#define HTTP_FAILED             0
#define HTTP_OK                 1
#define HTTP_RESET              2

/*****************************************************************************
 * HTTP Content NAME length
 ****************************************************************************/
#define MAX_CONTENT_NAME_LEN    128

/*****************************************************************************
 * HTTP Timeout
 ****************************************************************************/
#define HTTP_MAX_TIMEOUT_SEC    3   // Sec.

/*****************************************************************************
 * Storage Type Enumeration
 ****************************************************************************/
typedef enum
{
    NONE,       ///< Web storage none
    CODEFLASH,  ///< Code flash memory (embedded web assets)
    SDCARD,     ///< SD card
    DATAFLASH   ///< External data flash memory
} StorageType;

/*****************************************************************************
 * HTTP Socket Structure
 ****************************************************************************/
typedef struct _st_http_socket
{
    // const web_asset_t *asset;
    uint8_t  sock_status;
    uint8_t  file_name[MAX_CONTENT_NAME_LEN];
    uint32_t file_start;
    uint32_t file_len;
    uint32_t file_offset;    // (start addr + sent size...)
    uint8_t  storage_type;   // Storage type; Code flash, SDcard, Data flash ...
} st_http_socket;

/*****************************************************************************
 * Web Content Structure (Legacy)
 ****************************************************************************/
#define MAX_CONTENT_CALLBACK    20

typedef struct _httpServer_webContent
{
    uint8_t  *content_name;
    uint32_t content_len;
    uint8_t  *content;
} httpServer_webContent;

/*****************************************************************************
 * Core HTTP Server Functions
 ****************************************************************************/

/**
 * @brief Initialize HTTP server
 * @param tx_buf Pointer to TX buffer
 * @param rx_buf Pointer to RX buffer
 * @param cnt Number of sockets
 * @param socklist Array of socket numbers
 */
void httpServer_init(uint8_t *tx_buf, uint8_t *rx_buf, uint8_t cnt, uint8_t *socklist);

/**
 * @brief Register callback functions for MCU reset and WDT
 * @param mcu_reset Function pointer for MCU reset
 * @param wdt_reset Function pointer for WDT reset
 */
void reg_httpServer_cbfunc(void (*mcu_reset)(void), void (*wdt_reset)(void));

/**
 * @brief Main HTTP server task (call in loop)
 * @param seqnum Socket sequence number
 */
void httpServer_run(uint8_t seqnum);

/**
 * @brief Register API endpoint with callback (NEW)
 * @param path API path (e.g., "api/config")
 * @param callback Function pointer to handle request
 * 
 * Example:
 * @code
 * void handle_status(uint8_t s, void *req) {
 *     const char *json = "{\"status\":\"ok\"}";
 *     send_http_response_header(s, PTYPE_JSON, strlen(json), STATUS_OK);
 *     send(s, (uint8_t *)json, strlen(json));
 * }
 * httpServer_regAPI("api/status", handle_status);
 * @endcode
 */
void httpServer_regAPI(const char *path, void (*callback)(uint8_t s, void *req));

/**
 * @brief Register web content (DEPRECATED - use web_assets.h instead)
 * @param content_name Content file name
 * @param content Pointer to content data
 * @deprecated This function is kept for backward compatibility only.
 *             New projects should use web_assets.h generated by Python script.
 */
void reg_httpServer_webContent(uint8_t *content_name, uint8_t *content);

/**
 * @brief Find registered web content
 * @param content_name Content file name
 * @param content_num Pointer to store content number
 * @param file_len Pointer to store file length
 * @return 1 if found, 0 otherwise
 */
uint8_t find_userReg_webContent(uint8_t *content_name, uint16_t *content_num, uint32_t *file_len);

/**
 * @brief Read registered web content
 * @param content_num Content number
 * @param buf Buffer to store data
 * @param offset Offset in content
 * @param size Size to read
 * @return Number of bytes read
 */
uint16_t read_userReg_webContent(uint16_t content_num, uint8_t *buf, uint32_t offset, uint16_t size);

/**
 * @brief Display registered web content list
 * @return 1 if content exists, 0 otherwise
 */
uint8_t display_reg_webContent_list(void);

/**
 * @brief Send HTTP response header
 * @param s Socket number
 * @param content_type Content type
 * @param body_len Body length in bytes
 * @param http_status HTTP status code
 */
void send_http_response_header(uint8_t s, uint8_t content_type, uint32_t body_len, uint16_t http_status ,uint8_t gzipped);

/**
 * @brief HTTP Server 1sec Tick Timer handler
 * @note SHOULD BE register to your system 1s Tick timer handler
 */
void httpServer_time_handler(void);

/**
 * @brief Get HTTP server time count
 * @return Current time count
 */
uint32_t get_httpServer_timecount(void);

/*****************************************************************************
 * Utility Macros (NEW)
 ****************************************************************************/

/**
 * @brief Send JSON response
 * Usage: HTTP_SEND_JSON(sock, "{\"temp\":25.5}");
 */
#define HTTP_SEND_JSON(sock, json_str) do { \
    send_http_response_header(sock, PTYPE_JSON, strlen(json_str), STATUS_OK); \
    send(sock, (uint8_t *)json_str, strlen(json_str)); \
} while(0)

/**
 * @brief Send error response
 * Usage: HTTP_SEND_ERROR(sock, STATUS_NOT_FOUND, "File not found");
 */
#define HTTP_SEND_ERROR(sock, status, msg) do { \
    char _err_buf[128]; \
    int _len = snprintf(_err_buf, sizeof(_err_buf), "{\"error\":\"%s\"}", msg); \
    send_http_response_header(sock, PTYPE_JSON, _len, status); \
    send(sock, (uint8_t *)_err_buf, _len); \
} while(0)

/**
 * @brief Send plain text response
 * Usage: HTTP_SEND_TEXT(sock, "Hello World");
 */
#define HTTP_SEND_TEXT(sock, text) do { \
    send_http_response_header(sock, PTYPE_TEXT, strlen(text), STATUS_OK); \
    send(sock, (uint8_t *)text, strlen(text)); \
} while(0)

#ifdef __cplusplus
}
#endif

#endif /* __HTTPSERVER_H__ */