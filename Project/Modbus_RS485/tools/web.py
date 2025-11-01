#!/usr/bin/env python3
import os, zlib, json, gzip

# === Cấu hình đường dẫn ===
BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
WEB_DIR = os.path.join(BASE_DIR, "web")
OUTPUT_FILE = os.path.join(BASE_DIR, "devices/eth/web_assets.c")
HEADER_FILE = os.path.join(BASE_DIR, "devices/eth/web_assets.h")
CACHE_FILE = os.path.join(BASE_DIR, ".web_crc_cache.json")

# === MIME types ===
MIME_TYPES = {
    '.html': 'text/html',
    '.css': 'text/css',
    '.js': 'application/javascript',
    '.ico': 'image/x-icon',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.svg': 'image/svg+xml',
    '.json': 'application/json'
}

# === Cấu hình nén ===
ENABLE_GZIP = True  # Bật/tắt nén gzip
GZIP_LEVEL = 9      # Mức nén (1-9)

# === Hàm tiện ích ===
def filename_to_varname(filename):
    """Chuyển tên file thành tên biến C hợp lệ (giữ đuôi file)"""
    # Ví dụ: "index.html" -> "index_html"
    #        "style.css" -> "style_css"
    #        "my-script.js" -> "my_script_js"
    varname = filename.replace('.', '_').replace('-', '_').replace(' ', '_')
    return varname

def crc32_file(path):
    """Tính CRC32 của file"""
    with open(path, "rb") as f:
        return zlib.crc32(f.read()) & 0xFFFFFFFF

def embed_file(path, varname, compress=False):
    """Chuyển file thành mảng C"""
    with open(path, "rb") as f:
        data = f.read()
    
    original_size = len(data)
    
    # Nén nếu được yêu cầu
    if compress:
        data = gzip.compress(data, compresslevel=GZIP_LEVEL)
    
    # Format hex đẹp (16 bytes/dòng)
    hex_lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_line = ', '.join(f"0x{b:02X}" for b in chunk)
        hex_lines.append(f"    {hex_line}")
    
    hexdata = ',\n'.join(hex_lines)
    
    compressed_size = len(data)
    compression_ratio = (1 - compressed_size/original_size) * 100 if compress else 0
    
    # ✅ Hiển thị tên file đầy đủ trong comment
    output = f"// {os.path.basename(path)}"
    if compress:
        output += f" (gzipped: {original_size} -> {compressed_size} bytes, {compression_ratio:.1f}% saved)"
    else:
        output += f" ({original_size} bytes)"
    output += f"\nconst unsigned char {varname}[] = {{\n{hexdata}\n}};\n"
    output += f"const unsigned int {varname}_len = {compressed_size};\n"
    output += f"const unsigned int {varname}_original_len = {original_size};\n\n"
    
    return output

def get_mime_type(filename):
    """Lấy MIME type từ extension"""
    ext = os.path.splitext(filename)[1].lower()
    return MIME_TYPES.get(ext, 'application/octet-stream')

def generate_header(files):
    """Sinh file .h với struct định nghĩa"""
    output = """// Auto-generated web assets header
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

"""
    
    # Extern declarations
    for f in files:
        varname = filename_to_varname(f)  # ✅ Dùng hàm mới
        output += f"extern const unsigned char web_{varname}[];\n"
        output += f"extern const unsigned int web_{varname}_len;\n"
        output += f"extern const unsigned int web_{varname}_original_len;\n"
    
    output += f"\n#define WEB_ASSETS_COUNT {len(files)}\n"
    output += "extern const web_asset_t web_assets[];\n\n"
    output += "#endif // WEB_ASSETS_H\n"
    
    return output

def generate_asset_table(files):
    """Sinh bảng tra cứu assets"""
    output = "\n// Asset lookup table\nconst web_asset_t web_assets[] = {\n"
    
    for f in files:
        varname = filename_to_varname(f)
        mime = get_mime_type(f)
        
        # ✅ FIX: Chỉ đánh dấu gzipped cho file thực sự được nén
        is_text_file = f.endswith(('.html', '.css', '.js', '.json', '.svg'))
        gzipped = "1" if (ENABLE_GZIP and is_text_file) else "0"
        
        output += f'    {{"{f}", "{mime}", web_{varname}, web_{varname}_len, web_{varname}_original_len, {gzipped}}},\n'
    
    output += "};\n"
    return output

# === Hàm chính ===
def main():
    if not os.path.isdir(WEB_DIR):
        print(f"[embed_web] ERROR: web folder not found: {WEB_DIR}")
        return 1

    # Tìm các file web
    extensions = ('.html', '.css', '.js', '.ico', '.png', '.jpg', '.svg', '.json')
    files = sorted([f for f in os.listdir(WEB_DIR) if f.endswith(extensions)])
    
    if not files:
        print("[embed_web] No web files found.")
        return 0

    print(f"[embed_web] Found {len(files)} file(s)")

    # Tải cache cũ
    old_crc = {}
    if os.path.exists(CACHE_FILE):
        try:
            with open(CACHE_FILE, encoding='utf-8') as f:
                old_crc = json.load(f)
        except Exception:
            old_crc = {}

    # Tính CRC hiện tại
    new_crc = {}
    for f in files:
        full_path = os.path.join(WEB_DIR, f)
        new_crc[f] = crc32_file(full_path)

    # Kiểm tra thay đổi
    if new_crc == old_crc and os.path.exists(OUTPUT_FILE) and os.path.exists(HEADER_FILE):
        print("[embed_web] No changes detected, skipping regeneration.")
        return 0

    print("[embed_web] Changes detected, regenerating assets...")

    # Sinh file .c
    output = """// Auto-generated web assets for RP2040
// Generated by embed_web.py
#include "web_assets.h"

"""
    
    total_original = 0
    total_compressed = 0
    
    for f in files:
        varname = filename_to_varname(f)  # ✅ Dùng hàm mới
        full_path = os.path.join(WEB_DIR, f)
        
        # Chỉ nén text files
        should_compress = ENABLE_GZIP and f.endswith(('.html', '.css', '.js', '.json', '.svg'))
        
        output += embed_file(full_path, f"web_{varname}", compress=should_compress)
        
        # Thống kê
        file_size = os.path.getsize(full_path)
        total_original += file_size
        if should_compress:
            with open(full_path, 'rb') as file:
                compressed_size = len(gzip.compress(file.read(), compresslevel=GZIP_LEVEL))
                total_compressed += compressed_size
        else:
            total_compressed += file_size

    # Thêm bảng tra cứu
    output += generate_asset_table(files)

    # Ghi file .c với UTF-8 encoding
    with open(OUTPUT_FILE, "w", encoding='utf-8') as f:
        f.write(output)

    # Ghi file .h với UTF-8 encoding
    header_content = generate_header(files)
    with open(HEADER_FILE, "w", encoding='utf-8') as f:
        f.write(header_content)

    # Lưu cache với UTF-8 encoding
    with open(CACHE_FILE, "w", encoding='utf-8') as f:
        json.dump(new_crc, f, indent=2)

    # Thống kê
    savings = (1 - total_compressed/total_original) * 100 if ENABLE_GZIP else 0
    print(f"[embed_web] Generated {OUTPUT_FILE}")
    print(f"[embed_web] Generated {HEADER_FILE}")
    print(f"[embed_web] Files: {len(files)}")
    print(f"[embed_web] Original size: {total_original:,} bytes")
    print(f"[embed_web] Compressed size: {total_compressed:,} bytes")
    if ENABLE_GZIP:
        print(f"[embed_web] Savings: {savings:.1f}%")
    
    return 0

if __name__ == "__main__":
    raise SystemExit(main())