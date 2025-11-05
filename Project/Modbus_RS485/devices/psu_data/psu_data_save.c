#include <string.h>
#include "stdlib.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "psu_data_save.h"
#include "psu_data.h"

// ====== Chọn sector theo chỉ số từ cuối ======
#ifndef PSU_CYCLES_FLASH_SECTOR_INDEX
#define PSU_CYCLES_FLASH_SECTOR_INDEX  3
#endif

//    (tính từ đầu không gian flash)
#define FLASH_ADDR_FROM_END(idx)   ( (uint32_t)(PICO_FLASH_SIZE_BYTES - (uint32_t)(idx) * FLASH_SECTOR_SIZE) )
#define PSU_CYCLES_FLASH_OFFSET    FLASH_ADDR_FROM_END(PSU_CYCLES_FLASH_SECTOR_INDEX)
#define PSU_CYCLES_FLASH_ADDR      ((const uint8_t *)(XIP_BASE + PSU_CYCLES_FLASH_OFFSET))


#define PSU_CYCLES_MAGIC   0x4359434Cu  // 'CYCL'
#define PSU_CYCLES_VER     0x0001u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;     // sizeof(psu_cycle_db_t)
    psu_cycle_db_t db; // bản sao 10 chu kỳ
    uint32_t crc32;    // CRC trên [magic..db]
} psu_cycles_store_t;

// CRC32 poly 0xEDB88320
static uint32_t crc32u(const void *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t *p = (const uint8_t*)data;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
    }
    return ~crc;
}

static inline const psu_cycles_store_t* flash_view(void)
{
    return (const psu_cycles_store_t*)PSU_CYCLES_FLASH_ADDR;
}

bool psu_cycles_erase_flash(void)
{
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(PSU_CYCLES_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    return true;
}

bool psu_cycles_save_to_flash(void)
{
    // Lấy snapshot từ RAM
    const psu_cycle_db_t* cur = psu_cycles_get();
    if (!cur) return false;

    psu_cycles_store_t st = {
        .magic   = PSU_CYCLES_MAGIC,
        .version = PSU_CYCLES_VER,
        .size    = (uint16_t)sizeof(psu_cycle_db_t),
        .db      = *cur
    };
    st.crc32 = crc32u(&st, offsetof(psu_cycles_store_t, crc32));

    // Đệm theo page 256B
    const size_t raw_len = sizeof(st);
    const size_t page_len = ((raw_len + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
    uint8_t *pagebuf = (uint8_t*)malloc(page_len);
    if (!pagebuf) return false;
    memset(pagebuf, 0xFF, page_len);
    memcpy(pagebuf, &st, raw_len);

    // Erase + Program
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(PSU_CYCLES_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(PSU_CYCLES_FLASH_OFFSET, pagebuf, (uint32_t)page_len);
    restore_interrupts(ints);

    free(pagebuf);
    return true;
}

bool psu_cycles_load_from_flash(void)
{
    const psu_cycles_store_t* st = flash_view();

    if (st->magic   != PSU_CYCLES_MAGIC)   return false;
    if (st->version != PSU_CYCLES_VER)     return false;
    if (st->size    != sizeof(psu_cycle_db_t)) return false;

    uint32_t crc = crc32u(st, offsetof(psu_cycles_store_t, crc32));
    if (crc != st->crc32) return false;

    // Ghi ngược lại vào RAM (g_cycles) qua API setter
    psu_cycles_overwrite(&st->db);
    return true;
}
