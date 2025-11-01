// Auto-generated web assets header
#ifndef WEB_ASSETS_H
#define WEB_ASSETS_H

#include <stdint.h>

typedef struct {
    const char *name;
    const char *mime_type;
    const unsigned char *data;
    unsigned int length;
    unsigned int original_length;
    int is_gzipped;
} web_asset_t;

extern const unsigned char web_advanced_html[];
extern const unsigned int web_advanced_html_len;
extern const unsigned int web_advanced_html_original_len;
extern const unsigned char web_advanced_js[];
extern const unsigned int web_advanced_js_len;
extern const unsigned int web_advanced_js_original_len;
extern const unsigned char web_favicon_ico[];
extern const unsigned int web_favicon_ico_len;
extern const unsigned int web_favicon_ico_original_len;
extern const unsigned char web_index_html[];
extern const unsigned int web_index_html_len;
extern const unsigned int web_index_html_original_len;
extern const unsigned char web_script_js[];
extern const unsigned int web_script_js_len;
extern const unsigned int web_script_js_original_len;
extern const unsigned char web_style_css[];
extern const unsigned int web_style_css_len;
extern const unsigned int web_style_css_original_len;

#define WEB_ASSETS_COUNT 6
extern const web_asset_t web_assets[];

#endif // WEB_ASSETS_H
