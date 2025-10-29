#!/usr/bin/env python3
import os, zlib, json

# === Cấu hình đường dẫn ===
BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
WEB_DIR = os.path.join(BASE_DIR, "web")
OUTPUT_FILE = os.path.join(BASE_DIR, "devices/eth/web_assets.c")
CACHE_FILE = os.path.join(BASE_DIR, ".web_crc_cache.json")

# === Hàm tiện ích ===
def crc32_file(path):
    with open(path, "rb") as f:
        data = f.read()
    return zlib.crc32(data) & 0xFFFFFFFF

def embed_file(path, varname):
    with open(path, "rb") as f:
        data = f.read()
    hexdata = ','.join(f"0x%02X" % b for b in data)
    return f"const unsigned char {varname}[] = {{{hexdata}}};\nconst unsigned int {varname}_len = {len(data)};\n"

# === Hàm chính ===
def main():
    if not os.path.isdir(WEB_DIR):
        print(f"[embed_web] ERROR: web folder not found: {WEB_DIR}")
        return 1

    files = [f for f in os.listdir(WEB_DIR) if f.endswith((".html", ".css", ".js",".ico"))]
    if not files:
        print("[embed_web] No web files found.")
        return 0

    # Tải cache cũ (nếu có)
    old_crc = {}
    if os.path.exists(CACHE_FILE):
        try:
            old_crc = json.load(open(CACHE_FILE))
        except Exception:
            old_crc = {}

    # Tính CRC hiện tại
    new_crc = {}
    for f in files:
        full = os.path.join(WEB_DIR, f)
        new_crc[f] = crc32_file(full)

    # Nếu CRC không đổi -> bỏ qua
    if new_crc == old_crc and os.path.exists(OUTPUT_FILE):
        print("[embed_web] No changes detected, skipping regeneration.")
        return 0

    # Sinh lại file .c
    output = "// Auto-generated web assets\n#include <stdint.h>\n\n"
    for f in files:
        name = os.path.splitext(f)[0].replace('.', '_')
        output += embed_file(os.path.join(WEB_DIR, f), f"web_{name}")

    with open(OUTPUT_FILE, "w") as f:
        f.write(output)

    with open(CACHE_FILE, "w") as f:
        json.dump(new_crc, f, indent=2)

    print(f"[embed_web] Generated {OUTPUT_FILE} ({len(files)} file(s))")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
