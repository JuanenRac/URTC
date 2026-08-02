// =============================================================================
// URTC Expansion Slave Bootloader (STM32F303CBT6) - monolithic build
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Single-file form of this bootloader, kept in sync with the partitioned
// form under partitioned/ on every change - same "no patch perception"
// dual-maintenance rule already established for the main board's own
// firmware and bootloader. Content below is assembled from (in this
// order, matching each module's real dependency order so nothing is used
// before it's defined): slaveboot_common.h (shared types/defines),
// slaveboot_crypto.c (SHA-256/HMAC), slaveboot_flash.c (CRC32/flash/
// metadata), slaveboot_protocol.c (I2C-link protocol/validation/jump),
// slaveboot_main.c (entry point and I2C1 slave interrupt handling).
// =============================================================================
#include "stm32f3xx_hal.h"
#include <string.h>



// -----------------------------------------------------------------------
// Shared peripheral handles - defined once in slaveboot_main.c, declared
// extern here so every module that needs one can see it without each
// module guessing at ownership.
// -----------------------------------------------------------------------
extern IWDG_HandleTypeDef hiwdg;
// I2C1: the LINK bus to the main board's own STM32F303CC, hardware I2C in
// SLAVE mode here (the main board is master on this same physical bus,
// same pins/pull-ups the main board's own EXP_I2C2_SCL/SDA already
// describes - this chip is just what's listening on the other end).
extern I2C_HandleTypeDef hi2c1;

// -----------------------------------------------------------------------
// Flash layout - 128KB total (STM32F303CBT6), scaled down from the main
// board's own 256KB A/B scheme, same 2KB page size (same xB/xC density
// line, confirmed against the official ST datasheet - shares its flash
// controller behavior with the main board's own F303CC, just less of it).
// 18KB bootloader (vs. the main board's own 30KB) - smaller since this
// one has no OLED to drive and a much smaller command set, but not as
// small as first estimated: the real compiled size (crypto + flash +
// protocol + I2C slave HAL) came in at ~13KB once actually built and
// linked, not assumed - this 18KB leaves roughly 5KB of headroom above
// that rather than being sized to fit with nothing to spare.
// -----------------------------------------------------------------------
#define BOOTLOADER_FLASH_SIZE 18432UL           // 18KB, 9 pages
#define METADATA_ADDR        0x08004800UL       // 2KB, 1 page
#define MAIN_APP_ADDR         0x08005000UL      // 54KB, 27 pages
#define BACKUP_APP_ADDR       0x08012800UL      // 54KB, 27 pages
#define APP_MAX_SIZE          (54UL * 1024UL)
#define METADATA_MAGIC_VALID 0x55415041UL // "APAU" - same marker as the main board's own bootloader, no reason for these to differ

// -----------------------------------------------------------------------
// Firmware identity - rejects an image built for different hardware or
// declared incompatible, before ever trusting its CRC/HMAC. Distinct from
// THIS_HARDWARE_ID in the main board's own bootloader (0x0303CC01) - a
// firmware image meant for one board must never be mistakenly accepted by
// the other, and this ID is the check that catches that.
// -----------------------------------------------------------------------
#define THIS_HARDWARE_ID     0x0303CB01UL // STM32F303CBT6, expansion slave revision 1
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 0

// BOOTLOADER_VERSION_* describes THIS bootloader binary itself - separate
// from FIRMWARE_VERSION_MAJOR/MINOR above, which is the version of
// whatever slave APPLICATION image is currently installed in the main
// slot. Same versioning convention as the main board's own bootloader
// (PATCH increments on every change to this file, rolling over to MINOR
// at 9), but this is its own independent version number, starting fresh
// at 1.0.0 - this is a brand new bootloader, not a continuation of the
// main board's own version history.
#define BOOTLOADER_VERSION_MAJOR 1
#define BOOTLOADER_VERSION_MINOR 0
#define BOOTLOADER_VERSION_PATCH 0

// -----------------------------------------------------------------------
// HMAC-SHA256 signing key - deliberately different from the main board's
// own HMAC_KEY (bootloader_crypto.c's own comment on that one already
// says as much: change it before relying on it). A firmware image signed
// for the main board must never verify successfully against this chip's
// key, or vice versa - two different keys is what keeps a mismatched
// image from an all-too-easy accidental cross-flash from ever passing
// verification on the wrong board. Same placeholder-until-you-change-it
// status as the main board's own key.
// -----------------------------------------------------------------------
extern const uint8_t HMAC_KEY[32];

// -----------------------------------------------------------------------
// I2C LINK protocol (register-style, not CAN framed) - the main board's
// STM32F303CC is master, writes/reads these as ordinary I2C register
// accesses (write the register address, then either write data or repeat-
// start into a read) exactly like talking to any other I2C peripheral -
// this chip doesn't know or care that the ultimate source of an update is
// the Flasher over CAN-OTA; from here, it's just I2C register traffic.
// Numbered 0x00-0x0F deliberately low and out of the way of anything an
// application-mode register map might want later (see slaveboot_main.c's
// own note on where this address range sits relative to the running
// application's own I2C register map, once that exists).
// -----------------------------------------------------------------------
#define I2C_SLAVE_ADDRESS        0x42 // 7-bit address on the link bus - distinct from every other address already in use elsewhere in this project (F-RAM 0x50, MLX90640 0x33, ADS1115 0x48-0x4B - no collision risk sharing a bus with those, though this link bus is physically separate from that one regardless)

#define REG_STATUS               0x00 // read-only, 1 byte - status codes below
#define REG_START_UPDATE         0x01 // write-only, 8 bytes: big-endian total size (4) + big-endian HardwareID (4)
#define REG_HMAC_EXPECTED        0x02 // write-only, 32 bytes: the expected HMAC-SHA256 of the complete image, sent once, before any REG_DATA writes
#define REG_DATA                 0x03 // write-only, up to 32 bytes per transaction, sent sequentially - goes to the BACKUP slot, never main; unlike the main board's own CAN-framed 8-byte-per-frame limit, I2C has no equivalent per-transaction ceiling here, so this moves in much larger pieces
#define REG_END_UPDATE           0x04 // write-only, 8 bytes: big-endian CRC32 (4) + big-endian VersionMajor (2) + big-endian VersionMinor (2)
#define REG_PROGRESS             0x05 // read-only, 1 byte: 0-100, 0xFF if not currently updating
#define REG_QUERY_VERSION        0x06 // read-only, 10 bytes: byte0 (0=application,1=bootloader) + HardwareID (4) + version major (2) + version minor (2, all big-endian) - mirrors the main board's own 0x7F9 response fields, minus the CAN framing they need and this doesn't

#define STATUS_LISTENING     0x01 // waiting for REG_START_UPDATE
#define STATUS_ERASING       0x02 // erasing the backup slot
#define STATUS_RECEIVING     0x03 // writing firmware data into the backup slot
#define STATUS_VERIFYING     0x06 // checking size/CRC32/HMAC/HardwareID on the backup slot
#define STATUS_COPYING       0x07 // backup slot verified - erasing and copying it into the main slot
#define STATUS_VERIFY_OK     0x04 // copy to main slot complete and read-back verified; about to jump
#define STATUS_VERIFY_FAIL   0x05 // size, CRC32, HMAC, or HardwareID mismatch on the backup slot - main slot untouched
#define VERIFY_FAIL_REASON_INCOMPLETE  0x01
#define VERIFY_FAIL_REASON_CRC32       0x02
#define VERIFY_FAIL_REASON_HMAC        0x03
#define VERIFY_FAIL_REASON_HARDWARE_ID 0x04
#define VERIFY_FAIL_REASON_ROLLBACK    0x05
#define STATUS_ERROR         0xFF

// -----------------------------------------------------------------------
// Firmware metadata (single 2K page, one struct, one "state" field) -
// identical layout to the main board's own FirmwareMetadata_t (same field
// order, same sizes) so the same struct definition, the same
// SavedState_Checksum-style CRC-8 pattern, and the same signing-tool
// logic can be reused nearly verbatim, differing only in which
// THIS_HARDWARE_ID and HMAC_KEY end up embedded in a given signed image.
// -----------------------------------------------------------------------
#define META_STATE_APP_VALID     1
#define META_STATE_COPY_PENDING  2

typedef struct {
    uint32_t magic;
    uint32_t state;
    uint32_t hardware_id;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t size;
    uint32_t crc32;
    uint8_t  hmac[32];
} FirmwareMetadata_t;



// Single definition matching slaveboot_common.h's extern declaration -
// placeholder default; change it before relying on this for anything,
// and keep the signing tool's copy in sync with whatever it's changed to.
const uint8_t HMAC_KEY[32] = {
    0x55, 0x52, 0x54, 0x43, 0x2D, 0x48, 0x59, 0x44,
    0x52, 0x41, 0x2D, 0x55, 0x4D, 0x43, 0x2D, 0x32,
    0x30, 0x32, 0x36, 0x2D, 0x43, 0x48, 0x41, 0x4E,
    0x47, 0x45, 0x2D, 0x4D, 0x45, 0x2D, 0x21, 0x21
};

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    uint32_t datalen;
} SHA256_CTX;

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define SHA_ROTR(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define SHA_CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA_MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA_EP0(x) (SHA_ROTR(x,2) ^ SHA_ROTR(x,13) ^ SHA_ROTR(x,22))
#define SHA_EP1(x) (SHA_ROTR(x,6) ^ SHA_ROTR(x,11) ^ SHA_ROTR(x,25))
#define SHA_SIG0(x) (SHA_ROTR(x,7) ^ SHA_ROTR(x,18) ^ ((x) >> 3))
#define SHA_SIG1(x) (SHA_ROTR(x,17) ^ SHA_ROTR(x,19) ^ ((x) >> 10))

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[]) {
    uint32_t a,b,c,d,e,f,g,h,i,j,t1,t2,m[64];
    for (i=0,j=0; i<16; ++i,j+=4)
        m[i] = ((uint32_t)data[j]<<24)|((uint32_t)data[j+1]<<16)|((uint32_t)data[j+2]<<8)|(data[j+3]);
    for ( ; i<64; ++i)
        m[i] = SHA_SIG1(m[i-2]) + m[i-7] + SHA_SIG0(m[i-15]) + m[i-16];
    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];
    for (i=0; i<64; ++i) {
        t1 = h + SHA_EP1(e) + SHA_CH(e,f,g) + sha256_k[i] + m[i];
        t2 = SHA_EP0(a) + SHA_MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static void sha256_init(SHA256_CTX *ctx) {
    ctx->datalen = 0; ctx->bitlen = 0;
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85; ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c; ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t data[], uint32_t len) {
    for (uint32_t i=0; i<len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
        // A single call here can span the full 112KB backup slot (via
        // hmac_sha256_flash_region) - comfortably inside the IWDG's ~800ms
        // window under nominal timing, but LSI's own accuracy is
        // notoriously loose across temperature, and this costs nothing to
        // guard regardless. Every 4096 bytes keeps the refresh frequent
        // without adding meaningful overhead to the hot loop.
        if ((i & 0xFFF) == 0) {
            HAL_IWDG_Refresh(&hiwdg);
        }
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[]) {
    uint32_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 64);
    }
    ctx->bitlen += (uint64_t)ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);       ctx->data[62] = (uint8_t)(ctx->bitlen>>8);
    ctx->data[61] = (uint8_t)(ctx->bitlen>>16);   ctx->data[60] = (uint8_t)(ctx->bitlen>>24);
    ctx->data[59] = (uint8_t)(ctx->bitlen>>32);   ctx->data[58] = (uint8_t)(ctx->bitlen>>40);
    ctx->data[57] = (uint8_t)(ctx->bitlen>>48);   ctx->data[56] = (uint8_t)(ctx->bitlen>>56);
    sha256_transform(ctx, ctx->data);
    for (i=0; i<4; ++i) {
        hash[i]    = (uint8_t)(ctx->state[0] >> (24-i*8));
        hash[i+4]  = (uint8_t)(ctx->state[1] >> (24-i*8));
        hash[i+8]  = (uint8_t)(ctx->state[2] >> (24-i*8));
        hash[i+12] = (uint8_t)(ctx->state[3] >> (24-i*8));
        hash[i+16] = (uint8_t)(ctx->state[4] >> (24-i*8));
        hash[i+20] = (uint8_t)(ctx->state[5] >> (24-i*8));
        hash[i+24] = (uint8_t)(ctx->state[6] >> (24-i*8));
        hash[i+28] = (uint8_t)(ctx->state[7] >> (24-i*8));
    }
}

void hmac_sha256_flash_region(const uint8_t *key, uint32_t key_len,
                              uint32_t flash_addr, uint32_t len,
                              uint8_t out[32]) {
    uint8_t k_ipad[64], k_opad[64];
    uint8_t key_block[64] = {0};
    if (key_len > 64) {
        SHA256_CTX kctx;
        sha256_init(&kctx);
        sha256_update(&kctx, key, key_len);
        sha256_final(&kctx, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }
    for (int i = 0; i < 64; i++) {
        k_ipad[i] = key_block[i] ^ 0x36;
        k_opad[i] = key_block[i] ^ 0x5c;
    }
    uint8_t inner_hash[32];
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, (const uint8_t*)flash_addr, len);
    sha256_final(&ctx, inner_hash);

    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, inner_hash, 32);
    sha256_final(&ctx, out);
}

uint8_t hmac_constant_time_compare(const uint8_t *a, const uint8_t *b, uint32_t len) {
    // XOR-accumulate rather than an early-exit compare - an early exit
    // leaks how many leading bytes matched through timing, which matters
    // for a signature check even on a small embedded target listening on a
    // shared bus. Costs nothing extra here since len is always 32.
    uint8_t diff = 0;
    for (uint32_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}



// Defined here, declared extern in slaveboot_flash.h - read by the I2C
// register-read handler in slaveboot_main.c whenever the link-bus
// master polls REG_PROGRESS during a long copy.
volatile uint8_t update_progress_percent = 0xFF;

static uint32_t crc32_table[256];
static uint8_t crc32_table_built = 0;

// Software CRC32 (standard polynomial 0xEDB88320, reflected, final XOR
// 0xFFFFFFFF) - same variant as the main board's own bootloader, so the
// same signing tool logic applies to both without modification.
static void CRC32_BuildTable(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (uint8_t k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_built = 1;
}

uint32_t CRC32_Update(uint32_t crc, const uint8_t *data, uint32_t len) {
    if (!crc32_table_built) CRC32_BuildTable();
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

uint32_t CRC32_Finalize(uint32_t crc) {
    return crc ^ 0xFFFFFFFFUL;
}

// -----------------------------------------------------------------------
// Flash routines
// -----------------------------------------------------------------------
uint8_t Flash_ErasePages(uint32_t start_addr, uint32_t num_pages) {
    // One page at a time with an IWDG refresh between each - the backup
    // slot here is 58KB (29 pages), smaller than the main board's own
    // 112KB/56-page slot, so this margin is even more comfortable here,
    // but the reasoning (a single erase-everything call can't refresh the
    // watchdog mid-loop) is identical.
    for (uint32_t i = 0; i < num_pages; i++) {
        FLASH_EraseInitTypeDef eraseInit;
        uint32_t pageError;
        eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
        eraseInit.PageAddress = start_addr + (i * FLASH_PAGE_SIZE);
        eraseInit.NbPages = 1;

        HAL_FLASH_Unlock();
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);
        HAL_StatusTypeDef res = HAL_FLASHEx_Erase(&eraseInit, &pageError);
        HAL_FLASH_Lock();
        if (res != HAL_OK || pageError != 0xFFFFFFFF) return 0;
        HAL_IWDG_Refresh(&hiwdg);
    }
    return 1;
}

// Writes len bytes (rounded up to a half-word) starting at addr, then reads
// every half-word back and compares it against the source buffer before
// reporting success - identical reasoning and implementation to the main
// board's own Flash_WriteVerified.
uint8_t Flash_WriteVerified(uint32_t addr, const uint8_t *buf, uint32_t len) {
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t half_word = buf[i];
        if (i + 1 < len) half_word |= (buf[i+1] << 8);
        else half_word |= (0xFF << 8);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, half_word) != HAL_OK) {
            HAL_FLASH_Lock();
            return 0;
        }
        if ((i & 0xFF) == 0) HAL_IWDG_Refresh(&hiwdg);
    }
    HAL_FLASH_Lock();
    __DSB();

    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t expected = buf[i];
        if (i + 1 < len) expected |= (buf[i+1] << 8);
        else expected |= (0xFF << 8);
        uint16_t actual = *(volatile uint16_t*)(addr + i);
        if (actual != expected) return 0;
        if ((i & 0xFF) == 0) HAL_IWDG_Refresh(&hiwdg);
    }
    return 1;
}

// Copies backup slot into main slot, page by page. Same algorithm as the
// main board's own Flash_CopyRegion; the only change is what happens with
// progress - no OLED bar, no active CAN push, just updating the module-
// level percent that REG_PROGRESS reads back on request.
uint8_t Flash_CopyRegion(uint32_t dest_addr, uint32_t src_addr, uint32_t total_len) {
    uint32_t num_pages = (total_len + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    for (uint32_t p = 0; p < num_pages; p++) {
        uint32_t page_dest = dest_addr + (p * FLASH_PAGE_SIZE);
        uint32_t page_src  = src_addr  + (p * FLASH_PAGE_SIZE);
        uint32_t this_page_len = FLASH_PAGE_SIZE;
        if ((p + 1) * FLASH_PAGE_SIZE > total_len) {
            this_page_len = total_len - (p * FLASH_PAGE_SIZE);
        }

        FLASH_EraseInitTypeDef eraseInit;
        uint32_t pageError;
        eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
        eraseInit.PageAddress = page_dest;
        eraseInit.NbPages = 1;
        HAL_FLASH_Unlock();
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);
        HAL_StatusTypeDef res = HAL_FLASHEx_Erase(&eraseInit, &pageError);
        HAL_FLASH_Lock();
        if (res != HAL_OK || pageError != 0xFFFFFFFF) return 0;

        if (!Flash_WriteVerified(page_dest, (const uint8_t*)page_src, this_page_len)) return 0;
        HAL_IWDG_Refresh(&hiwdg);

        update_progress_percent = (uint8_t)(((uint64_t)(p + 1) * 100) / num_pages);
    }
    return 1;
}

uint8_t Metadata_Read(FirmwareMetadata_t *out) {
    memcpy(out, (const void*)METADATA_ADDR, sizeof(FirmwareMetadata_t));
    return out->magic == METADATA_MAGIC_VALID;
}

static uint8_t Metadata_Write(const FirmwareMetadata_t *meta) {
    return Flash_WriteVerified(METADATA_ADDR, (const uint8_t*)meta, sizeof(FirmwareMetadata_t));
}

uint8_t Metadata_EraseAndWrite(const FirmwareMetadata_t *meta) {
    FirmwareMetadata_t current;
    if (Metadata_Read(&current) && memcmp(&current, meta, sizeof(FirmwareMetadata_t)) == 0) {
        return 1;
    }
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t pageError;
    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.PageAddress = METADATA_ADDR;
    eraseInit.NbPages = 1;
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);
    HAL_StatusTypeDef res = HAL_FLASHEx_Erase(&eraseInit, &pageError);
    HAL_FLASH_Lock();
    if (res != HAL_OK || pageError != 0xFFFFFFFF) return 0;
    return Metadata_Write(meta);
}



// Defined here, read by the I2C1 interrupt handler's REG_STATUS read path.
volatile uint8_t current_status = STATUS_LISTENING;

void BuildVersionQueryResponse(uint8_t out[10]) {
    FirmwareMetadata_t meta;
    uint8_t have_meta = Metadata_Read(&meta);
    uint32_t hw_id = have_meta ? meta.hardware_id : 0;
    uint16_t ver_major = have_meta ? (uint16_t)meta.version_major : 0;
    uint16_t ver_minor = have_meta ? (uint16_t)meta.version_minor : 0;

    out[0] = 0x01; // answering as the bootloader, same convention as the main board's own 0x7F9 byte0
    out[1] = (uint8_t)(hw_id >> 24);
    out[2] = (uint8_t)(hw_id >> 16);
    out[3] = (uint8_t)(hw_id >> 8);
    out[4] = (uint8_t)(hw_id);
    out[5] = (uint8_t)(ver_major >> 8);
    out[6] = (uint8_t)(ver_major);
    out[7] = (uint8_t)(ver_minor >> 8);
    out[8] = (uint8_t)(ver_minor);
    out[9] = BOOTLOADER_VERSION_MAJOR; // this bootloader's own version - the main board's equivalent sends this as a separate CAN frame (0x7FA) since 0x7F9's 8 bytes were already full; a register read has no such length ceiling, so it just rides along as one extra byte here
}

// -----------------------------------------------------------------------
// Validates the MAIN slot and, if a copy from backup was left unfinished
// by an interrupted previous session, resumes and completes it before
// re-checking. Returns 1 if the main slot ends up holding a verified,
// jumpable application; 0 otherwise (bootloader stays in listening mode).
//
// Same two real RAM regions and same reasoning as the main board's own
// StackPointerInValidRAM - this chip is the same xB/xC density line
// (STM32F303CB), confirmed against the same ST datasheet family (DS9118)
// that already covers the main board's own F303CC: 40KB standard SRAM at
// 0x20000000, 8KB CCM (CPU-only, no DMA) at 0x10000000.
// -----------------------------------------------------------------------
static uint8_t StackPointerInValidRAM(uint32_t sp) {
    uint8_t in_sram = (sp >= SRAM_BASE + 0x100UL && sp <= SRAM_BASE + 0xA000UL);
    uint8_t in_ccm  = (sp >= CCMDATARAM_BASE + 0x100UL && sp <= CCMDATARAM_BASE + 0x2000UL);
    return in_sram || in_ccm;
}

uint8_t ApplicationIsValid(void) {
    FirmwareMetadata_t meta;
    if (!Metadata_Read(&meta)) {
        // No valid metadata yet - same reasoning as the main board's own
        // bootloader: distinguish a genuinely blank chip (erased flash
        // reads 0xFFFFFFFF, not a valid RAM address) from one that was
        // JTAG-flashed directly, by checking whether the stack pointer at
        // the start of the main slot plausibly sits inside real RAM.
        uint32_t app_stack = *(volatile uint32_t*)MAIN_APP_ADDR;
        if (!StackPointerInValidRAM(app_stack)) return 0;

        FirmwareMetadata_t adopted = {0};
        adopted.magic = METADATA_MAGIC_VALID;
        adopted.state = META_STATE_APP_VALID;
        adopted.hardware_id = THIS_HARDWARE_ID;
        adopted.version_major = FIRMWARE_VERSION_MAJOR;
        adopted.version_minor = FIRMWARE_VERSION_MINOR;
        adopted.size = APP_MAX_SIZE;
        uint32_t crc = 0xFFFFFFFFUL;
        crc = CRC32_Update(crc, (const uint8_t*)MAIN_APP_ADDR, APP_MAX_SIZE);
        adopted.crc32 = CRC32_Finalize(crc);
        hmac_sha256_flash_region(HMAC_KEY, 32, MAIN_APP_ADDR, APP_MAX_SIZE, adopted.hmac);
        return Metadata_EraseAndWrite(&adopted) ? 1 : 0;
    }

    if (meta.state == META_STATE_COPY_PENDING) {
        current_status = STATUS_COPYING;
        if (!Flash_CopyRegion(MAIN_APP_ADDR, BACKUP_APP_ADDR, meta.size)) {
            return 0;
        }
        FirmwareMetadata_t done = meta;
        done.state = META_STATE_APP_VALID;
        if (!Metadata_EraseAndWrite(&done)) return 0;
        meta = done;
    }

    if (meta.hardware_id != THIS_HARDWARE_ID) return 0;
    if (meta.size == 0 || meta.size > APP_MAX_SIZE) return 0;

    uint32_t crc = 0xFFFFFFFFUL;
    crc = CRC32_Update(crc, (const uint8_t*)MAIN_APP_ADDR, meta.size);
    crc = CRC32_Finalize(crc);
    if (crc != meta.crc32) return 0;

    uint8_t actual_hmac[32];
    hmac_sha256_flash_region(HMAC_KEY, 32, MAIN_APP_ADDR, meta.size, actual_hmac);
    if (!hmac_constant_time_compare(actual_hmac, meta.hmac, 32)) return 0;

    return 1;
}

void JumpToApplication(void) {
    typedef void (*pFunction)(void);
    uint32_t app_stack = *(volatile uint32_t*)MAIN_APP_ADDR;
    uint32_t app_reset_vector = *(volatile uint32_t*)(MAIN_APP_ADDR + 4);

    // Same four checks, same ordering, same reasoning as the main board's
    // own JumpToApplication - see its own comment for the full rationale
    // on each one. Sanity check still runs before any peripheral teardown,
    // so a failure here returns to the caller with I2C1 still alive to
    // keep listening on.
    if (!StackPointerInValidRAM(app_stack) ||
        (app_stack & 0x3UL) != 0 ||
        (app_reset_vector & 0x1UL) == 0 ||
        app_reset_vector < MAIN_APP_ADDR ||
        app_reset_vector >= MAIN_APP_ADDR + APP_MAX_SIZE) {
        return;
    }

    HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
    HAL_I2C_DeInit(&hi2c1);
    HAL_RCC_DeInit();
    HAL_DeInit();
    // Same safe-state pattern as the main board's own bootloader -
    // 0xEBFFFFFF preserves PA13/PA14 (SWD).
    GPIOA->MODER = 0xEBFFFFFF;
    GPIOB->MODER = 0xFFFFFFFF;
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk;

    __disable_irq();
    SCB->VTOR = MAIN_APP_ADDR;
    __DSB();
    __ISB();

    pFunction app_entry = (pFunction)app_reset_vector;
    __set_MSP(app_stack);
    app_entry();
}

// -----------------------------------------------------------------------
// Update flow - all writes during an update go to the BACKUP slot only.
// Same state machine as the main board's own bootloader_protocol.c;
// REG_DATA moves up to 32 bytes per transaction rather than CAN's fixed
// 8-byte DLC ceiling, so HandleData's inner loop bound changes to match,
// but the page-buffer-then-flush logic underneath is unchanged.
// -----------------------------------------------------------------------
uint32_t update_total_size = 0;
static uint32_t update_declared_hw_id = 0;
uint32_t update_bytes_received = 0;
static uint32_t update_running_crc = 0xFFFFFFFFUL;
static uint8_t  update_expected_hmac[32];
static uint8_t  update_hmac_received = 0;
static uint8_t  page_buffer[FLASH_PAGE_SIZE];
static uint32_t page_buffer_fill = 0;
static uint32_t current_page_index = 0;
uint8_t  update_in_progress = 0;
uint8_t  update_failed = 0;

static uint8_t FlushPageBuffer(void) {
    uint32_t page_addr = BACKUP_APP_ADDR + (current_page_index * FLASH_PAGE_SIZE);
    for (uint32_t i = page_buffer_fill; i < FLASH_PAGE_SIZE; i++) {
        page_buffer[i] = 0xFF;
    }
    uint8_t ok = Flash_WriteVerified(page_addr, page_buffer, FLASH_PAGE_SIZE);
    if (ok) {
        current_page_index++;
        page_buffer_fill = 0;
        HAL_IWDG_Refresh(&hiwdg);
        update_progress_percent = (update_total_size > 0) ?
            (uint8_t)(((uint64_t)update_bytes_received * 100) / update_total_size) : 0;
    }
    return ok;
}

void HandleStartUpdate(uint8_t *data) {
    update_total_size = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                       | ((uint32_t)data[2] << 8) | data[3];
    update_declared_hw_id = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16)
                           | ((uint32_t)data[6] << 8) | data[7];

    if (update_total_size == 0 || update_total_size > APP_MAX_SIZE) {
        current_status = STATUS_ERROR;
        return;
    }
    if (update_declared_hw_id != THIS_HARDWARE_ID) {
        current_status = STATUS_VERIFY_FAIL;
        return;
    }

    uint32_t pages_needed = (update_total_size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    current_status = STATUS_ERASING;
    if (!Flash_ErasePages(BACKUP_APP_ADDR, pages_needed)) {
        current_status = STATUS_ERROR;
        update_failed = 1;
        update_in_progress = 0;
        return;
    }

    memset(page_buffer, 0xFF, sizeof(page_buffer));
    update_bytes_received = 0;
    update_running_crc = 0xFFFFFFFFUL;
    update_hmac_received = 0;
    page_buffer_fill = 0;
    current_page_index = 0;
    update_in_progress = 1;
    update_failed = 0;
    update_progress_percent = 0;
    current_status = STATUS_RECEIVING;
}

void HandleHmacExpected(uint8_t *data) {
    if (!update_in_progress || update_failed) return;
    memcpy(update_expected_hmac, data, 32); // REG_HMAC_EXPECTED is always exactly 32 bytes in one transaction - no chunking needed the way CAN's 8-byte frames required
    update_hmac_received = 1;
}

void HandleData(uint8_t *data, uint32_t len) {
    if (!update_in_progress || update_failed) return;
    if (len > 32) return; // REG_DATA's own documented ceiling - guards against a malformed/oversized transaction the same way the main board's own DLC>8 check does for CAN

    uint32_t i;
    for (i = 0; i < len; i++) {
        if (update_bytes_received >= update_total_size) break;
        page_buffer[page_buffer_fill++] = data[i];
        update_bytes_received++;

        if (page_buffer_fill == FLASH_PAGE_SIZE) {
            if (!FlushPageBuffer()) {
                update_running_crc = CRC32_Update(update_running_crc, data, i + 1);
                current_status = STATUS_ERROR;
                update_failed = 1;
                return;
            }
        }
    }
    update_running_crc = CRC32_Update(update_running_crc, data, i);
}

void HandleEndUpdate(uint8_t *data) {
    if (!update_in_progress || update_failed) return;

    if (page_buffer_fill > 0) {
        if (!FlushPageBuffer()) {
            current_status = STATUS_ERROR;
            update_failed = 1;
            return;
        }
    }

    if (update_bytes_received != update_total_size || !update_hmac_received) {
        current_status = STATUS_VERIFY_FAIL;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    uint32_t expected_crc = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                           | ((uint32_t)data[2] << 8) | data[3];
    uint16_t version_major = ((uint16_t)data[4] << 8) | data[5];
    uint16_t version_minor = ((uint16_t)data[6] << 8) | data[7];
    uint32_t actual_crc = CRC32_Finalize(update_running_crc);

    current_status = STATUS_VERIFYING;

    if (actual_crc != expected_crc) {
        current_status = STATUS_VERIFY_FAIL;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    uint8_t actual_hmac[32];
    hmac_sha256_flash_region(HMAC_KEY, 32, BACKUP_APP_ADDR, update_total_size, actual_hmac);
    if (!hmac_constant_time_compare(actual_hmac, update_expected_hmac, 32)) {
        current_status = STATUS_VERIFY_FAIL;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    // Anti-rollback - same reasoning as the main board's own bootloader:
    // a cryptographically valid image is still rejected if it declares a
    // version older than what's already running, so a validly-signed
    // image with a since-discovered vulnerability can't be replayed
    // indefinitely.
    FirmwareMetadata_t current_meta;
    if (Metadata_Read(&current_meta) && current_meta.state == META_STATE_APP_VALID) {
        uint32_t current_version = (current_meta.version_major << 16) | current_meta.version_minor;
        uint32_t new_version = ((uint32_t)version_major << 16) | version_minor;
        if (new_version < current_version) {
            current_status = STATUS_VERIFY_FAIL;
            update_in_progress = 0;
            update_failed = 1;
            return;
        }
    }

    FirmwareMetadata_t pending = {0};
    pending.magic = METADATA_MAGIC_VALID;
    pending.state = META_STATE_COPY_PENDING;
    pending.hardware_id = update_declared_hw_id;
    pending.version_major = version_major;
    pending.version_minor = version_minor;
    pending.size = update_total_size;
    pending.crc32 = actual_crc;
    memcpy(pending.hmac, actual_hmac, 32);
    if (!Metadata_EraseAndWrite(&pending)) {
        current_status = STATUS_ERROR;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    current_status = STATUS_COPYING;
    update_progress_percent = 0;
    if (!Flash_CopyRegion(MAIN_APP_ADDR, BACKUP_APP_ADDR, update_total_size)) {
        // Main slot may be partially written, but metadata still says
        // META_STATE_COPY_PENDING and backup is untouched - same recovery
        // path as the main board's own bootloader: next boot's
        // ApplicationIsValid() resumes the copy from backup.
        current_status = STATUS_ERROR;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    FirmwareMetadata_t done = pending;
    done.state = META_STATE_APP_VALID;
    if (!Metadata_EraseAndWrite(&done)) {
        current_status = STATUS_ERROR;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    current_status = STATUS_VERIFY_OK;
    update_progress_percent = 100;
    HAL_Delay(200); // brief pause so a master polling REG_STATUS has a real chance to observe STATUS_VERIFY_OK before the reset below
    NVIC_SystemReset();
}



IWDG_HandleTypeDef hiwdg;
I2C_HandleTypeDef hi2c1;

// -----------------------------------------------------------------------
// I2C1 slave-side register-protocol state. rx_buffer's size (33) is
// exactly 1 register-address byte + REG_DATA's own documented 32-byte
// ceiling - the largest single write this protocol ever needs to accept
// in one transaction (REG_HMAC_EXPECTED is the same 33-byte total: 1 +
// 32). tx_buffer's size (10) matches REG_QUERY_VERSION's own 10-byte
// response, the largest read.
// -----------------------------------------------------------------------
static uint8_t rx_buffer[33];
static uint8_t tx_buffer[10];
static uint8_t pending_read_register = 0xFF; // 0xFF = none set yet; a read before any register write has a value to serve reads REG_STATUS by default, chosen deliberately below rather than left undefined

static void MX_GPIO_Init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    // I2C1 on PB6(SCL)/PB7(SDA) - this chip's default I2C1 remap, open-
    // drain with the external pull-ups already required on this link bus
    // regardless of which side is master (the main board's own
    // ExpansionI2C_* already assumes pull-ups are present for its own
    // bit-banged master role on the other end of this same physical bus).
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

static void SystemClock_Config(void) {
    // HSI (8MHz) /2 prescaler (fixed on this family's PLL input mux) x16
    // = 64MHz - see this file's own top-of-file note on why HSI rather
    // than HSE. 64MHz comfortably covers this chip's actual workload
    // (I2C1 slave + I2C2 master polling 2 sensor chips + PWM generation)
    // without pushing for this chip's own 72MHz ceiling, which only HSE
    // can reach anyway (HSI/2 x16 tops out at 64MHz; there's no faster
    // combination available without an external crystal).
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                 | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2; // I2C1 lives on APB1, max 36MHz - 64/2=32MHz, within spec
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2); // 2 wait states required above 48MHz at 3.3V per this chip's own datasheet
}

static void MX_IWDG_Init(void) {
    // Same reload/prescaler as the main board's own bootloader - ~800ms
    // window at LSI's nominal ~40kHz, same margin reasoning applies
    // (every flash operation in slaveboot_flash.c already refreshes this
    // well inside that window).
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 999;
    HAL_IWDG_Init(&hiwdg);
}

static void MX_I2C1_Init_Slave(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.Timing = 0x2000090E; // 100kHz standard mode at 32MHz APB1 clock - computed with STM32CubeMX's own I2C timing calculator for this exact PCLK1, not hand-derived, since this register's bitfields don't map to a simple formula on this I2C peripheral generation
    hi2c1.Init.OwnAddress1 = (I2C_SLAVE_ADDRESS << 1); // HAL expects the 7-bit address pre-shifted into bits [7:1]
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE; // clock stretching left enabled deliberately - flash operations (erase/write/verify, tens of microseconds to milliseconds) run inside the same handlers this interrupt calls, and a master expecting instant responses without stretching would see NACKs or garbled data during those windows otherwise
    HAL_I2C_Init(&hi2c1);

    __HAL_RCC_I2C1_CLK_ENABLE();
    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
}

void I2C1_EV_IRQHandler(void) {
    HAL_I2C_EV_IRQHandler(&hi2c1);
}

void I2C1_ER_IRQHandler(void) {
    HAL_I2C_ER_IRQHandler(&hi2c1);
}

// -----------------------------------------------------------------------
// HAL_I2C "listen mode" callbacks - this project's chosen pattern for a
// slave that can't know a write transaction's length in advance (register
// writes here range from a 1-byte read-pointer-only write up to
// REG_HMAC_EXPECTED/REG_DATA's own 33-byte ceiling). Always request the
// buffer's own full size on every receive regardless of what the master
// actually intends to send - HAL_I2C_SlaveRxCpltCallback fires on the
// master's own STOP/repeated-START regardless of whether the requested
// count was fully reached, and XferCount's remaining-bytes value at that
// point is what tells this code how many bytes actually arrived.
// -----------------------------------------------------------------------
void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection, uint16_t AddrMatchCode) {
    (void)AddrMatchCode;
    if (TransferDirection == I2C_DIRECTION_RECEIVE) {
        // Master is about to WRITE to us.
        HAL_I2C_Slave_Seq_Receive_IT(hi2c, rx_buffer, sizeof(rx_buffer), I2C_LAST_FRAME);
    } else {
        // Master is about to READ from us - serve whatever
        // pending_read_register currently holds (set by a prior 1-byte
        // write, or REG_STATUS by default if none has ever been set -
        // see this file's own top-of-function-group note).
        uint8_t len = 1;
        switch (pending_read_register) {
            case REG_STATUS:
                tx_buffer[0] = current_status;
                len = 1;
                break;
            case REG_PROGRESS:
                tx_buffer[0] = update_progress_percent;
                len = 1;
                break;
            case REG_QUERY_VERSION:
                BuildVersionQueryResponse(tx_buffer);
                len = 10;
                break;
            default:
                // An unrecognized pending register (protocol error on the
                // master's side, or genuinely nothing set yet) gets
                // REG_STATUS's own answer rather than transmitting
                // garbage - always something coherent to read back, never
                // uninitialized buffer contents.
                tx_buffer[0] = current_status;
                len = 1;
                break;
        }
        HAL_I2C_Slave_Seq_Transmit_IT(hi2c, tx_buffer, len, I2C_LAST_FRAME);
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    uint32_t bytes_received = sizeof(rx_buffer) - hi2c->XferCount;
    if (bytes_received == 0) return; // shouldn't happen (AddrCallback always requests >0), guarded anyway rather than risk reading rx_buffer[0] from a transaction that never actually sent anything

    uint8_t reg = rx_buffer[0];
    if (bytes_received == 1) {
        // Register-pointer-only write - the master is telling us what
        // it's about to read next, not writing data to reg.
        pending_read_register = reg;
    } else {
        switch (reg) {
            case REG_START_UPDATE:
                if (bytes_received == 9) HandleStartUpdate(&rx_buffer[1]);
                break;
            case REG_HMAC_EXPECTED:
                if (bytes_received == 33) HandleHmacExpected(&rx_buffer[1]);
                break;
            case REG_DATA:
                HandleData(&rx_buffer[1], bytes_received - 1);
                break;
            case REG_END_UPDATE:
                if (bytes_received == 9) HandleEndUpdate(&rx_buffer[1]);
                break;
            default:
                break; // unrecognized register - ignored rather than NACKed after the fact (I2C has no clean way to retroactively NACK a transaction already accepted at the address-match stage)
        }
    }
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    (void)hi2c; // nothing to do - the transmitted bytes are already what mattered; listening resumes via HAL_I2C_ListenCpltCallback below
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c) {
    // Fires once the full transaction (address match through STOP) is
    // done - listen mode disables itself when this fires, by HAL's own
    // design, so it's re-armed here on every single transaction rather
    // than being something enabled once at init and forgotten.
    HAL_I2C_EnableListen_IT(hi2c);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    // Bus error, arbitration lost, or similar - re-arm listening rather
    // than leaving this chip silently deaf to the link bus until the next
    // full power cycle. Doesn't attempt to salvage whatever transaction
    // was in flight (state machine functions like HandleData already
    // check update_in_progress/update_failed themselves, so a genuinely
    // corrupted partial write is caught on the next REG_END_UPDATE's own
    // size/CRC/HMAC checks rather than needing special handling here).
    HAL_I2C_EnableListen_IT(hi2c);
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_IWDG_Init();
    MX_I2C1_Init_Slave();

    if (ApplicationIsValid()) {
        JumpToApplication();
        // Only reachable if JumpToApplication's own sanity checks
        // rejected the app despite ApplicationIsValid() passing - falls
        // through to listening below rather than looping forever with
        // nothing listening on the link bus.
    }

    HAL_I2C_EnableListen_IT(&hi2c1);

    while (1) {
        HAL_IWDG_Refresh(&hiwdg);
        HAL_Delay(50);
    }
}
