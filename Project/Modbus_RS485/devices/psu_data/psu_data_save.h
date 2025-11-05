#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "psu_data.h"   // cần psu_cycle_db_t + getter/setter

#ifdef __cplusplus
extern "C" {
#endif

// ===== Vị trí lưu trong flash =====
// Mặc định: dùng sector thứ 3 từ cuối để tránh đụng vùng cấu hình khác.
// Bạn có thể override ở CMake: -DPSU_CYCLES_FLASH_SECTOR_INDEX=3
#ifndef PSU_CYCLES_FLASH_SECTOR_INDEX
#define PSU_CYCLES_FLASH_SECTOR_INDEX  3
#endif

// Lưu / đọc 10 chu kỳ với MAGIC + VERSION + CRC32
bool psu_cycles_save_to_flash(void);
bool psu_cycles_load_from_flash(void);

// Xoá trắng (erase) sector chứa cycles
bool psu_cycles_erase_flash(void);

#ifdef __cplusplus
}
#endif
