// =============================================================================
// URTC - CAN Bootloader
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// =============================================================================
// PURPOSE: sits at the very start of flash (0x08000000-0x08007800, 30KB).
// On every boot: briefly listens on CAN for an "enter update mode" trigger.
// If it doesn't see one, and the application in the main slot passes its
// stored CRC and HMAC checks, jumps straight to it.
//
// Golden-image update model: a CAN firmware update is never written
// directly into the slot that's currently running. It's written into a
// separate backup slot first, verified there in full (size, CRC32, and an
// HMAC-SHA256 signature - not just corruption-detection but proof the image
// actually came from this project's own build process), and only then
// copied into the main slot. The main slot is never erased until a fully
// verified, known-good image is sitting in backup ready to replace it - a
// power loss at any point before that copy starts leaves the currently
// running firmware completely untouched. If a power loss happens mid-copy,
// backup (still fully intact) is copied again on the next boot until it
// succeeds; there's no window where both slots are simultaneously invalid.
//
// FLASH MAP (see STM32F303CCTx_BOOTLOADER.ld / STM32F303CCTx_APP.ld):
//   0x08000000 - 0x08007800 (30K)  this bootloader
//   0x08007800 - 0x08008000 (2K)   metadata page (this file's own read/write)
//   0x08008000 - 0x08024000 (112K) main application slot
//   0x08024000 - 0x08040000 (112K) backup/staging slot
//
// STATUS: this is a first implementation, compiled and linked clean against
// this exact chip's HAL, but a bootloader is exactly the piece of firmware
// where "compiles clean" is nowhere near "trust it on hardware yet" - the
// flash program/erase timing, the CAN bus behavior during a real multi-
// thousand-frame transfer, and the jump-to-app handoff all need to be
// verified on the real board, ideally with a way to re-flash over JTAG if
// something's off, before this is trusted with an unattended update in the
// field. See the accompanying notes for what specifically still needs
// bench-testing.
// =============================================================================

#include "stm32f3xx_hal.h"
#include <string.h>

// -----------------------------------------------------------------------
// Flash layout
// -----------------------------------------------------------------------
#define MAIN_APP_ADDR        0x08008000UL
#define BACKUP_APP_ADDR      0x08024000UL
#define APP_MAX_SIZE         (112UL * 1024UL)
#define METADATA_ADDR        0x08007800UL
#define METADATA_MAGIC_VALID 0x55415041UL // "APAU" as a magic marker, arbitrary but distinctive

// -----------------------------------------------------------------------
// Firmware identity - rejects an image built for different hardware or
// declared incompatible, before ever trusting its CRC/HMAC.
// -----------------------------------------------------------------------
#define THIS_HARDWARE_ID     0x0303CC01UL // STM32F303CCT6, URTC board revision 1
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 0

// BOOTLOADER_VERSION_* describes THIS bootloader binary itself - separate
// from FIRMWARE_VERSION_MAJOR/MINOR above, which is the version of
// whatever APPLICATION image is currently installed in the main slot (a
// completely different thing that changes independently via OTA updates,
// while this bootloader stays fixed until reflashed via SWD/JTAG).
// Versioning convention for this project: PATCH increments on every
// change to this file (0-9), then MINOR increments and PATCH resets to 0
// (so 1.0.9's next change becomes 1.1.0), same idea as MAJOR/MINOR
// rolling over eventually. Bump this every time BOOTLOADER.C changes.
#define BOOTLOADER_VERSION_MAJOR 1
#define BOOTLOADER_VERSION_MINOR 1
#define BOOTLOADER_VERSION_PATCH 1

// -----------------------------------------------------------------------
// HMAC-SHA256 signing key - shared between this bootloader and whatever
// build/signing tool prepares a firmware image for a CAN update. This is a
// placeholder default; change it before relying on this for anything, and
// keep the signing tool's copy in sync with whatever it's changed to. A
// firmware image with a correct CRC32 but wrong HMAC is exactly what this
// key is meant to catch - the CRC alone only proves "not corrupted", not
// "actually came from this project's own build".
// -----------------------------------------------------------------------
static const uint8_t HMAC_KEY[32] = {
    0x55, 0x52, 0x54, 0x43, 0x2D, 0x48, 0x59, 0x44,
    0x52, 0x41, 0x2D, 0x55, 0x4D, 0x43, 0x2D, 0x32,
    0x30, 0x32, 0x36, 0x2D, 0x43, 0x48, 0x41, 0x4E,
    0x47, 0x45, 0x2D, 0x4D, 0x45, 0x2D, 0x21, 0x21
};

// -----------------------------------------------------------------------
// CAN protocol (0x7F0-0x7F6)
// -----------------------------------------------------------------------
#define CAN_ID_ENTER_BOOTLOADER  0x7F0 // sent to the RUNNING APPLICATION (it resets into this bootloader) - handled in the app's firmware, not here
#define CAN_ID_START_UPDATE      0x7F1 // sent to THIS bootloader: DLC=8, big-endian total firmware size (4 bytes) + big-endian HardwareID (4 bytes)
#define CAN_ID_DATA              0x7F2 // sent to THIS bootloader: up to 8 bytes of firmware data, sent sequentially - goes to the BACKUP slot, never main
#define CAN_ID_PAGE_ACK          0x7F3 // sent BY this bootloader after each backup-slot page write: DLC=4, big-endian page index
#define CAN_ID_END_UPDATE        0x7F4 // sent to THIS bootloader: DLC=8, big-endian CRC32 (4 bytes) + big-endian VersionMajor/Minor (2 bytes each)
#define CAN_ID_STATUS            0x7F5 // sent BY this bootloader: DLC=1, status codes below
#define CAN_ID_HEARTBEAT         0x7F6 // sent BY this bootloader every ~1s while listening or updating: DLC=2, status byte + progress percent (0-100, 0xFF if not applicable)
#define CAN_ID_HMAC_CHUNK        0x7F7 // sent to THIS bootloader: DLC=8 x4, sent sequentially right after CAN_ID_START_UPDATE and before the first CAN_ID_DATA frame - the 32-byte expected HMAC-SHA256, 8 bytes per frame in order
#define CAN_ID_QUERY_VERSION     0x7F8 // sent to either the application or this bootloader, whichever is currently running: DLC ignored, no payload needed. Read-only - answered from either side, unlike 0x7F0 which only the application handles.
#define CAN_ID_VERSION_RESPONSE  0x7F9 // sent BY whichever answered: DLC=8, byte0 (0=application, 1=bootloader) + HardwareID (4 bytes) + version major (2 bytes) + version minor (1 byte), all big-endian
#define CAN_ID_BOOTLOADER_VERSION_RESPONSE 0x7FA // sent ONLY by the bootloader, right alongside 0x7F9, when IT is the one answering a version query - this bootloader's OWN version (major/minor/patch), not the app metadata 0x7F9 reports

#define STATUS_LISTENING     0x01 // waiting for CAN_ID_START_UPDATE
#define STATUS_ERASING       0x02 // erasing the backup slot
#define STATUS_RECEIVING     0x03 // writing firmware data into the backup slot
#define STATUS_VERIFYING     0x06 // checking size/CRC32/HMAC/HardwareID on the backup slot
#define STATUS_COPYING       0x07 // backup slot verified - erasing and copying it into the main slot
#define STATUS_VERIFY_OK     0x04 // copy to main slot complete and read-back verified; about to jump
// STATUS_VERIFY_FAIL is sent as a 2-byte frame: byte[0]=0x05 (unchanged,
// so anything only reading byte[0] still correctly sees "verification
// failed"), byte[1]=one of the VERIFY_FAIL_REASON_* codes below, added so
// the flasher tool can tell a genuinely corrupt/incomplete transfer apart
// from a deliberately-rejected wrong-hardware or wrong-key image, instead
// of every failure looking identical.
#define STATUS_VERIFY_FAIL   0x05 // size, CRC32, HMAC, or HardwareID mismatch on the backup slot - main slot untouched
#define VERIFY_FAIL_REASON_INCOMPLETE  0x01 // didn't receive the declared size or all 4 HMAC chunks before END_UPDATE arrived
#define VERIFY_FAIL_REASON_CRC32       0x02 // CRC32 over the backup slot doesn't match the one declared in END_UPDATE - transfer corruption
#define VERIFY_FAIL_REASON_HMAC        0x03 // CRC32 passed but the HMAC-SHA256 signature didn't - image isn't corrupt, but isn't provably signed with this project's key
#define VERIFY_FAIL_REASON_HARDWARE_ID 0x04 // the image's declared HardwareID doesn't match this board - checked before a single byte was written, reported here too for one consistent "why did it fail" path
#define VERIFY_FAIL_REASON_ROLLBACK    0x05 // the image is cryptographically valid but declares a version older than what's already installed - see the anti-rollback check in HandleEndUpdate
#define STATUS_ERROR         0xFF

// -----------------------------------------------------------------------
// SHA-256 / HMAC-SHA256 (FIPS 180-4 / RFC 4231) - verified against the
// official NIST SHA-256 test vectors ("abc" and the empty string) and the
// official RFC 4231 HMAC-SHA256 test case 2 before this was ever wired
// into the update flow below.
// -----------------------------------------------------------------------
typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    uint32_t datalen;
} SHA256_CTX;

// Forward declaration - the real definition is further down with the rest
// of the peripheral handles, but sha256_update (right below) needs to
// refresh it during a long hash and is defined ahead of that point.
extern IWDG_HandleTypeDef hiwdg;

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

// HMAC-SHA256 over an arbitrary flash region, read byte-by-byte rather than
// requiring the whole thing in RAM at once (the backup slot is 112KB; RAM
// here is 32KB total) - the flash controller supports direct addressed
// reads with no special unlock needed for that.
static void hmac_sha256_flash_region(const uint8_t *key, uint32_t key_len,
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

static uint8_t hmac_constant_time_compare(const uint8_t *a, const uint8_t *b, uint32_t len) {
    // XOR-accumulate rather than an early-exit compare - an early exit
    // leaks how many leading bytes matched through timing, which matters
    // for a signature check even on a small embedded target listening on a
    // shared bus. Costs nothing extra here since len is always 32.
    uint8_t diff = 0;
    for (uint32_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

CAN_HandleTypeDef hcan;
IWDG_HandleTypeDef hiwdg; // see claim 62's fix in main() and Flash_ErasePages
I2C_HandleTypeDef hi2c2;

// -----------------------------------------------------------------------
// OLED progress display - shows "UPDATING", a progress bar + percent,
// and a final "FLASH OK" / "ERROR" message during a CAN update. Pin
// assignments, I2C timing, and the SSD1306/SSD1315 init sequence are
// copied exactly from the application's own OLED_Init - same hardware,
// same verified-compatible sequence (see that function's own compatibility
// comment for the SSD1306/SSD1315 research behind it).
// -----------------------------------------------------------------------
#define OLED_SCL_PIN    GPIO_PIN_9
#define OLED_SDA_PIN    GPIO_PIN_10
#define OLED_PORT       GPIOA
#define OLED_I2C_ADDR   0x78

// Compact 27-character font (space, %, ., 0-9, and just the letters this
// bootloader's own status messages need) - most extracted from the
// application's Font5x7 table, 'P' and 'G' hand-designed and verified by
// rendering instead: Font5x7 has a known character-extraction-offset bug
// (documented where it's defined in the application source), and pulling
// 'G' from it directly returned bytes identical to this font's own
// already-verified 'H' - a live example of exactly that bug, not
// something to build on for two brand new letters.
static const uint8_t BootFont[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 0: space
    {0x23,0x13,0x08,0x64,0x62}, // 1: '%'
    {0x00,0x60,0x60,0x00,0x00}, // 2: '.'
    {0x00,0x42,0x7F,0x40,0x00}, // 3: '0'
    {0x00,0x44,0x7E,0x40,0x00}, // 4: '1'
    {0x00,0x62,0x51,0x49,0x46}, // 5: '2'
    {0x00,0x22,0x49,0x49,0x36}, // 6: '3'
    {0x18,0x14,0x12,0x7F,0x10}, // 7: '4'
    {0x00,0x27,0x45,0x45,0x39}, // 8: '5'
    {0x00,0x3E,0x49,0x49,0x32}, // 9: '6'
    {0x00,0x61,0x11,0x09,0x07}, // 10: '7'
    {0x00,0x36,0x49,0x49,0x36}, // 11: '8'
    {0x00,0x26,0x49,0x49,0x3E}, // 12: '9'
    {0x7E,0x11,0x11,0x11,0x7E}, // 13: 'A'
    {0x7F,0x41,0x41,0x22,0x1C}, // 14: 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 15: 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 16: 'F'
    {0x3E,0x41,0x41,0x49,0x3A}, // 17: 'G' (hand-designed, see comment above)
    {0x7F,0x08,0x08,0x08,0x7F}, // 18: 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 19: 'I'
    {0x7F,0x08,0x14,0x22,0x41}, // 20: 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 21: 'L'
    {0x7F,0x04,0x08,0x10,0x7F}, // 22: 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 23: 'O'
    {0x7F,0x09,0x09,0x0F,0x00}, // 24: 'P' (hand-designed, see comment above)
    {0x7F,0x09,0x19,0x29,0x46}, // 25: 'R'
    {0x00,0x26,0x49,0x49,0x32}, // 26: 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 27: 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 28: 'U'
};

static uint8_t BootFont_Lookup(char c) {
    if (c >= 'a' && c <= 'z') c -= 32; // not reachable today (every current
    // status message is already uppercase), but matches the application
    // firmware's own font lookup convention and costs nothing.
    switch (c) {
        case ' ': return 0;  case '%': return 1;  case '.': return 2;
        case '0': return 3;  case '1': return 4;  case '2': return 5;
        case '3': return 6;  case '4': return 7;  case '5': return 8;
        case '6': return 9;  case '7': return 10; case '8': return 11;
        case '9': return 12; case 'A': return 13; case 'D': return 14;
        case 'E': return 15; case 'F': return 16; case 'G': return 17;
        case 'H': return 18; case 'I': return 19; case 'K': return 20;
        case 'L': return 21; case 'N': return 22; case 'O': return 23;
        case 'P': return 24; case 'R': return 25; case 'S': return 26;
        case 'T': return 27; case 'U': return 28;
        default: return 0; // unsupported char -> blank space
    }
}

static void MX_I2C2_Init_Boot(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = OLED_SCL_PIN | OLED_SDA_PIN;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF4_I2C2; // PA9/PA10 with AF4 is I2C2, not I2C1 -
    // confirmed against DS9118 Table 14. This bootloader was making the
    // exact same mistake already found and fixed in the application
    // firmware's own OLED init - never checked here until now, meaning
    // this display would never have actually worked on real hardware.
    HAL_GPIO_Init(OLED_PORT, &gpio);

    __HAL_RCC_I2C2_CLK_ENABLE();
    hi2c2.Instance = I2C2;
    hi2c2.Init.Timing = 0x0010232A; // ~100kHz, same derivation as the application's
    hi2c2.Init.OwnAddress1 = 0;
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2 = 0;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c2);
}

// Set once by OLED_Init_Boot(); every OLED write function checks this
// first and returns immediately if the display never responded, rather
// than paying a real I2C timeout on every single command. A bootloader's
// actual job is the CAN update itself - the display is a convenience,
// and a missing/disconnected one should never be able to affect update
// timing or, worse, chain into an IWDG reset loop before CAN is ever
// reached. The 50ms->5ms timeout reduction below is a second, independent
// layer for the case this flag can't catch: a display that responded
// fine at init but fails mid-session (a damaged cable, a connector
// working loose) - even every single write stacking a full timeout in
// that scenario stays well inside the IWDG's own budget.
static uint8_t oled_present_boot = 0;

static void OLED_WriteCmd_Boot(uint8_t cmd) {
    if (!oled_present_boot) return;
    uint8_t frame[2] = {0x00, cmd};
    HAL_I2C_Master_Transmit(&hi2c2, OLED_I2C_ADDR, frame, 2, 20);
}

static void OLED_WriteData_Boot(uint8_t *buf, uint16_t len) {
    if (!oled_present_boot) return;
    if (len > 128) len = 128;
    uint8_t frame[129];
    frame[0] = 0x40;
    for (uint16_t i = 0; i < len; i++) frame[i+1] = buf[i];
    HAL_I2C_Master_Transmit(&hi2c2, OLED_I2C_ADDR, frame, len + 1, 20);
}

static void OLED_SetCursor_Boot(uint8_t page, uint8_t col) {
    OLED_WriteCmd_Boot(0xB0 + page);
    OLED_WriteCmd_Boot(0x00 + (col & 0x0F));
    OLED_WriteCmd_Boot(0x10 + (col >> 4));
}

static void OLED_Init_Boot(void) {
    MX_I2C2_Init_Boot();
    HAL_Delay(50);
    // A single, cheap probe decides whether this session touches the
    // display at all - one bounded transaction instead of every
    // subsequent command individually risking a timeout against a
    // display that was never going to answer.
    oled_present_boot = (HAL_I2C_IsDeviceReady(&hi2c2, OLED_I2C_ADDR, 2, 20) == HAL_OK) ? 1 : 0;
    if (!oled_present_boot) return;
    // Same sequence as the application's OLED_Init, verified compatible
    // with both SSD1306 and SSD1315 - see that function's own comment.
    OLED_WriteCmd_Boot(0xAE);
    OLED_WriteCmd_Boot(0xD5); OLED_WriteCmd_Boot(0x80);
    OLED_WriteCmd_Boot(0xA8); OLED_WriteCmd_Boot(0x3F);
    OLED_WriteCmd_Boot(0xD3); OLED_WriteCmd_Boot(0x00);
    OLED_WriteCmd_Boot(0x40);
    OLED_WriteCmd_Boot(0x8D); OLED_WriteCmd_Boot(0x14);
    OLED_WriteCmd_Boot(0x20); OLED_WriteCmd_Boot(0x02);
    OLED_WriteCmd_Boot(0xA1);
    OLED_WriteCmd_Boot(0xC8);
    OLED_WriteCmd_Boot(0xDA); OLED_WriteCmd_Boot(0x12);
    OLED_WriteCmd_Boot(0x81); OLED_WriteCmd_Boot(0xCF);
    OLED_WriteCmd_Boot(0xD9); OLED_WriteCmd_Boot(0xF1);
    OLED_WriteCmd_Boot(0xDB); OLED_WriteCmd_Boot(0x40);
    OLED_WriteCmd_Boot(0xA4); OLED_WriteCmd_Boot(0xA6);
    OLED_WriteCmd_Boot(0xAF);
}

static void OLED_ClearAll_Boot(void) {
    uint8_t blank[128] = {0};
    for (uint8_t p = 0; p < 8; p++) {
        OLED_SetCursor_Boot(p, 0);
        OLED_WriteData_Boot(blank, 128);
    }
}

// Prints text scaled 2x (using BootFont at native size doubled both ways)
// so status messages are legible - this display only ever shows a handful
// of short words during an update, not dense telemetry, so legibility at
// a glance matters more than fitting lots of text.
static void OLED_PrintStr2x_Boot(uint8_t page, uint8_t col, const char *str) {
    uint8_t buf_top[128] = {0};
    uint8_t buf_bot[128] = {0};
    uint16_t c = col;
    while (*str && c <= 118) {
        uint8_t idx = BootFont_Lookup(*str++);
        for (uint8_t gc = 0; gc < 5; gc++) {
            uint8_t byte = BootFont[idx][gc];
            uint8_t top = 0, bot = 0;
            for (uint8_t bit = 0; bit < 4; bit++) {
                if (byte & (1 << bit)) top |= (0x03 << (bit*2));
            }
            for (uint8_t bit = 4; bit < 7; bit++) {
                if (byte & (1 << bit)) bot |= (0x03 << ((bit-4)*2));
            }
            buf_top[c] = top; buf_top[c+1] = top;
            buf_bot[c] = bot; buf_bot[c+1] = bot;
            c += 2;
        }
        c += 1; // 1px inter-character spacing - without this, each 10px
                 // character ran directly into the next with no gap,
                 // rendering status text as a hard-to-read solid block
    }
    OLED_SetCursor_Boot(page, col);
    OLED_WriteData_Boot(buf_top, 128);
    OLED_SetCursor_Boot(page + 1, col);
    OLED_WriteData_Boot(buf_bot, 128);
}

// Progress bar: outlined box with a filled portion proportional to percent,
// plus the percentage as text alongside it.
static void OLED_DrawProgressBar_Boot(uint8_t percent) {
    if (percent > 100) percent = 100;
    uint8_t bar[110];
    uint8_t filled = (uint8_t)((uint16_t)percent * 106 / 100);
    for (uint8_t i = 0; i < 110; i++) {
        if (i == 0 || i == 109) bar[i] = 0x7F;
        else if (i <= filled) bar[i] = 0x7F;
        else bar[i] = 0x41;
    }
    OLED_SetCursor_Boot(5, 4);
    OLED_WriteData_Boot(bar, 110);
    // Percentage text, small (1x), to the right of the bar - reuses the
    // same font table without the 2x scaling used for word messages.
    char txt[5];
    uint8_t tp = 0;
    if (percent >= 100) { txt[tp++] = '1'; txt[tp++] = '0'; txt[tp++] = '0'; }
    else if (percent >= 10) { txt[tp++] = '0' + (percent/10); txt[tp++] = '0' + (percent%10); }
    else { txt[tp++] = '0' + percent; }
    txt[tp++] = '%';
    txt[tp] = 0;
    uint8_t small_buf[30] = {0};
    uint8_t sc = 0;
    for (uint8_t i = 0; txt[i]; i++) {
        uint8_t idx = BootFont_Lookup(txt[i]);
        for (uint8_t gc = 0; gc < 5; gc++) small_buf[sc++] = BootFont[idx][gc];
        sc++;
    }
    OLED_SetCursor_Boot(3, 4);
    OLED_WriteData_Boot(small_buf, sc);
}


// -----------------------------------------------------------------------
// Firmware metadata (single 2K page, one struct, one "state" field). Two
// states, two deliberate whole-page erase+rewrite points - nothing here
// gets updated incrementally within a page, since flash bits can only go
// 1->0 on a plain write; getting any bit back to 1 needs a full erase, and
// erasing this page would take the OTHER fields with it if they were both
// live at once.
//
//   META_STATE_APP_VALID: normal, steady-state. size/crc32/hmac describe
//   the image currently sitting in the main slot right now.
//
//   META_STATE_COPY_PENDING: a verified backup-slot image is being copied
//   into main; size/crc32/hmac describe the TARGET the main slot should
//   match once that finishes. Written once, right before the copy starts.
//   If a boot ever finds this state, the previous session was interrupted
//   mid-copy - the fix is just to resume the copy from backup (which was
//   never touched during it and is still fully intact), not to treat this
//   as corruption.
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

void SystemClock_Config(void) {
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
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

// -----------------------------------------------------------------------
// CAN init - same bit timing as the main application (32MHz APB1, verified
// during that firmware's own port), same PA11/PA12 AF9 pin assignment,
// same wide-open filter (this bootloader only cares about its own 0x7Fx
// range and just checks the ID in software after receiving - narrowing the
// hardware filter isn't worth the risk of a bit-alignment mistake here any
// more than it was in the main application).
// -----------------------------------------------------------------------
static void MX_CAN_Init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_CAN1_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_CAN;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    hcan.Instance = CAN;
    hcan.Init.Prescaler = 4;
    hcan.Init.Mode = CAN_MODE_NORMAL;
    hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
    hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
    hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
    hcan.Init.TimeTriggeredMode = DISABLE;
    hcan.Init.AutoBusOff = ENABLE;
    hcan.Init.AutoWakeUp = DISABLE;
    hcan.Init.AutoRetransmission = ENABLE;
    hcan.Init.ReceiveFifoLocked = DISABLE;
    hcan.Init.TransmitFifoPriority = DISABLE;
    HAL_CAN_Init(&hcan);

    CAN_FilterTypeDef sFilterConfig;
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 0; // only meaningful on dual-CAN
    // parts (splitting filter banks between CAN1/CAN2) - this chip has a
    // single CAN peripheral, so this has no effect either way, but 0 is
    // clearer than a non-zero value implying dual-CAN awareness this
    // project doesn't need.
    HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);

    HAL_CAN_Start(&hcan);
}

// A dropped CAN_SendPageAck or CAN_SendStatus leaves the master waiting
// for something that's never coming - not just a cosmetic missed status
// update, but the specific thing the master's own protocol timeout exists
// to eventually recover from. A short, bounded retry (refreshing the
// watchdog each attempt) gives a genuinely busy bus a real chance to
// clear before falling back to the drop this always could do anyway.
static uint8_t CAN_WaitForFreeMailbox(void) {
    // 50 attempts x 1ms = 50ms max - a small fraction of the ~800ms IWDG
    // window (prescaler 32, reload 999, LSI ~40kHz), but 10x more room
    // than the previous 5ms for transient bus congestion to clear before
    // a status frame gets dropped. HAL_IWDG_Refresh every iteration keeps
    // this safely clear of the watchdog regardless of how many attempts
    // it takes.
    for (uint8_t attempt = 0; attempt < 50; attempt++) {
        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) return 1;
        HAL_IWDG_Refresh(&hiwdg);
        HAL_Delay(1);
    }
    return HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0;
}

static void CAN_SendStatus(uint8_t status) {
    if (!CAN_WaitForFreeMailbox()) return; // genuinely no mailbox freed up in time - drop rather than block indefinitely
    CAN_TxHeaderTypeDef txHeader;
    uint8_t txData[1] = {status};
    uint32_t mailbox;
    txHeader.StdId = CAN_ID_STATUS;
    txHeader.IDE = CAN_ID_STD;
    txHeader.RTR = CAN_RTR_DATA;
    txHeader.DLC = 1;
    txHeader.TransmitGlobalTime = DISABLE;
    HAL_CAN_AddTxMessage(&hcan, &txHeader, txData, &mailbox);
}

static void CAN_SendVerifyFailReason(uint8_t reason) {
    // Same CAN_ID_STATUS as CAN_SendStatus, but DLC=2 instead of 1: byte[0]
    // is still STATUS_VERIFY_FAIL (0x05) so anything that only reads
    // byte[0] keeps working exactly as before, byte[1] adds the specific
    // VERIFY_FAIL_REASON_* this particular failure was.
    if (!CAN_WaitForFreeMailbox()) return;
    CAN_TxHeaderTypeDef txHeader;
    uint8_t txData[2] = {STATUS_VERIFY_FAIL, reason};
    uint32_t mailbox;
    txHeader.StdId = CAN_ID_STATUS;
    txHeader.IDE = CAN_ID_STD;
    txHeader.RTR = CAN_RTR_DATA;
    txHeader.DLC = 2;
    txHeader.TransmitGlobalTime = DISABLE;
    HAL_CAN_AddTxMessage(&hcan, &txHeader, txData, &mailbox);
}

static void CAN_SendPageAck(uint32_t page_index) {
    if (!CAN_WaitForFreeMailbox()) return; // genuinely no mailbox freed up in time - drop rather than block indefinitely
    CAN_TxHeaderTypeDef txHeader;
    uint8_t txData[4];
    txData[0] = (page_index >> 24) & 0xFF;
    txData[1] = (page_index >> 16) & 0xFF;
    txData[2] = (page_index >> 8) & 0xFF;
    txData[3] = page_index & 0xFF;
    uint32_t mailbox;
    txHeader.StdId = CAN_ID_PAGE_ACK;
    txHeader.IDE = CAN_ID_STD;
    txHeader.RTR = CAN_RTR_DATA;
    txHeader.DLC = 4;
    txHeader.TransmitGlobalTime = DISABLE;
    HAL_CAN_AddTxMessage(&hcan, &txHeader, txData, &mailbox);
}

// -----------------------------------------------------------------------
// Software CRC32 (standard polynomial 0xEDB88320, reflected, final XOR
// 0xFFFFFFFF) - the same variant used by zlib/PNG/Ethernet and available as
// a one-line call in most languages (Python's zlib.crc32, for instance),
// deliberately NOT using this chip's hardware CRC unit, which implements a
// different variant (no reflection, no final XOR) by default - using the
// hardware unit here would mean whatever tool ends up generating update
// images needs to replicate that specific variant exactly, where a
// standard library call would otherwise just work.
// -----------------------------------------------------------------------
static uint32_t crc32_table[256];
static uint8_t crc32_table_built = 0;

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

static uint32_t CRC32_Update(uint32_t crc, const uint8_t *data, uint32_t len) {
    if (!crc32_table_built) CRC32_BuildTable();
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

static uint32_t CRC32_Finalize(uint32_t crc) {
    return crc ^ 0xFFFFFFFFUL;
}

// -----------------------------------------------------------------------
// Flash routines
// -----------------------------------------------------------------------
static uint8_t Flash_ErasePages(uint32_t start_addr, uint32_t num_pages) {
    // Erase one page at a time with an IWDG refresh between each, rather
    // than one HAL_FLASHEx_Erase call for every page at once - that single
    // call loops internally with nothing able to refresh the watchdog
    // until it fully returns, and for 112K (56 pages), the whole erase
    // could plausibly run long enough on its own to trip the IWDG
    // inherited from the application.
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
// reporting success. Flash cells that write but don't actually hold their
// value ("stuck" from wear or degradation) are a real, if uncommon, failure
// mode - this catches it at write time instead of finding out only when
// the CRC/HMAC check (or worse, execution) fails later.
static uint8_t Flash_WriteVerified(uint32_t addr, const uint8_t *buf, uint32_t len) {
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t half_word = buf[i];
        if (i + 1 < len) half_word |= (buf[i+1] << 8);
        else half_word |= (0xFF << 8); // pad the odd trailing byte with the erased-flash value
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, half_word) != HAL_OK) {
            HAL_FLASH_Lock();
            return 0;
        }
        // Not needed under typical timing (a full page's programming and
        // verification loops together are well within the ~800ms IWDG
        // window that Flash_CopyRegion's own once-per-page refresh
        // already covers), but cheap insurance against clock scaling,
        // flash wait-state, or bus-contention delays stretching a single
        // page's processing time unexpectedly.
        if ((i & 0xFF) == 0) HAL_IWDG_Refresh(&hiwdg);
    }
    HAL_FLASH_Lock();
    __DSB(); // ensures the last flash write is fully complete before the read-back below trusts it

    // Read-back verification: compare what's actually in flash now against
    // what was meant to go there.
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

// Copies one flash region into another, page by page: erase destination
// page, write+verify from source, refresh IWDG, repeat. Used for the
// backup-slot -> main-slot copy once backup has fully passed verification.
// Safe to call again from scratch after an interruption - source is never
// modified by this function, only destination.
static void CAN_SendHeartbeat(uint8_t status, uint8_t progress_percent);

static uint8_t Flash_CopyRegion(uint32_t dest_addr, uint32_t src_addr, uint32_t total_len) {
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

        // The main loop is blocked inside this whole function - without
        // this, no heartbeat could fire and the OLED bar would sit frozen
        // for however long the full copy takes (several seconds for a
        // large image), even though this is the phase most worth showing
        // progress for.
        uint8_t pct = (uint8_t)(((uint64_t)(p + 1) * 100) / num_pages);
        OLED_DrawProgressBar_Boot(pct);
        CAN_SendHeartbeat(STATUS_COPYING, pct);
    }
    return 1;
}

static uint8_t Metadata_Read(FirmwareMetadata_t *out) {
    memcpy(out, (const void*)METADATA_ADDR, sizeof(FirmwareMetadata_t));
    return out->magic == METADATA_MAGIC_VALID;
}

// Writes without erasing first - callers are responsible for that (STM32F3
// flash requires an erased page before programming; writing without one
// causes a PGERR fault). Static and single-caller today
// (Metadata_EraseAndWrite, right below, which does erase first), but
// worth flagging explicitly for any future caller added within this file.
static uint8_t Metadata_Write(const FirmwareMetadata_t *meta) {
    return Flash_WriteVerified(METADATA_ADDR, (const uint8_t*)meta, sizeof(FirmwareMetadata_t));
    // Note: caller is responsible for erasing the metadata page first - see
    // Metadata_ErraseAndWrite below, which every call site actually uses.
}

static uint8_t Metadata_EraseAndWrite(const FirmwareMetadata_t *meta) {
    // Skip the erase+write entirely if the page already holds exactly
    // this content - this page's flash has the same ~10,000-cycle
    // erase/write endurance as any other, and a boot sequence that ends
    // up calling this with unchanged data (nothing here currently does,
    // but future logic might) shouldn't spend a cycle on a no-op.
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

// Sent every ~1s while listening or mid-update, so the master can tell
// "node is alive but hasn't started listening yet" apart from "node is
// completely unresponsive" - useful during bring-up and for spotting a
// bootloader that's stuck, not just during a real update.
static void CAN_SendHeartbeat(uint8_t status, uint8_t progress_percent) {
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) return;
    CAN_TxHeaderTypeDef txHeader;
    uint8_t txData[2] = {status, progress_percent};
    uint32_t mailbox;
    txHeader.StdId = CAN_ID_HEARTBEAT;
    txHeader.IDE = CAN_ID_STD;
    txHeader.RTR = CAN_RTR_DATA;
    txHeader.DLC = 2;
    txHeader.TransmitGlobalTime = DISABLE;
    HAL_CAN_AddTxMessage(&hcan, &txHeader, txData, &mailbox);
}

// Version query (0x7F8) response, sent as 0x7F9 - shared between both
// listening loops in main() below. byte0=1 marks this as the bootloader
// answering (as opposed to the application answering directly, byte0=0 -
// see the application's own 0x7F8 handler), reporting whatever the
// metadata page currently says about the main slot: a real HardwareID and
// version if something valid is installed, or all-zero HardwareID/version
// if Metadata_Read finds nothing valid - the host can tell those two
// cases apart by the all-zero HardwareID, which THIS_HARDWARE_ID never is.
static void HandleVersionQuery(void) {
    FirmwareMetadata_t meta;
    uint8_t have_meta = Metadata_Read(&meta);
    uint32_t hw_id = have_meta ? meta.hardware_id : 0;
    uint16_t ver_major = have_meta ? (uint16_t)meta.version_major : 0;
    uint8_t ver_minor = have_meta ? (uint8_t)meta.version_minor : 0;

    CAN_TxHeaderTypeDef txH;
    uint8_t txD[8];
    txD[0] = 0x01; // answering as the bootloader
    txD[1] = (uint8_t)(hw_id >> 24);
    txD[2] = (uint8_t)(hw_id >> 16);
    txD[3] = (uint8_t)(hw_id >> 8);
    txD[4] = (uint8_t)(hw_id);
    txD[5] = (uint8_t)(ver_major >> 8);
    txD[6] = (uint8_t)(ver_major);
    txD[7] = ver_minor;
    txH.StdId = CAN_ID_VERSION_RESPONSE;
    txH.IDE = CAN_ID_STD;
    txH.RTR = CAN_RTR_DATA;
    txH.DLC = 8;
    txH.TransmitGlobalTime = DISABLE;
    if (CAN_WaitForFreeMailbox()) {
        uint32_t mb;
        HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
    }

    // This bootloader's own version, sent right alongside the app
    // metadata above - a separate frame since 0x7F9's 8 bytes were
    // already fully used by the app metadata, and conflating "what
    // version is this bootloader" with "what app is installed" would
    // have made 0x7F9 answer two different questions depending on
    // context. Sent unconditionally whenever this handler runs (i.e.
    // whenever the bootloader itself is the one answering) - never sent
    // by the application, which has no way to know a currently-flashed
    // bootloader's version other than this.
    if (CAN_WaitForFreeMailbox()) {
        CAN_TxHeaderTypeDef txH2;
        uint8_t txD2[3] = {BOOTLOADER_VERSION_MAJOR, BOOTLOADER_VERSION_MINOR, BOOTLOADER_VERSION_PATCH};
        txH2.StdId = CAN_ID_BOOTLOADER_VERSION_RESPONSE;
        txH2.IDE = CAN_ID_STD;
        txH2.RTR = CAN_RTR_DATA;
        txH2.DLC = 3;
        txH2.TransmitGlobalTime = DISABLE;
        uint32_t mb2;
        HAL_CAN_AddTxMessage(&hcan, &txH2, txD2, &mb2);
    }
}

// -----------------------------------------------------------------------
// Validates the MAIN slot and, if a copy from backup was left unfinished
// by an interrupted previous session, resumes and completes it before
// re-checking. Returns 1 if the main slot ends up holding a verified,
// jumpable application; 0 otherwise (bootloader stays in listening mode).
// -----------------------------------------------------------------------
// This chip has two distinct RAM regions a valid stack pointer could
// legitimately sit in - not just the 40KB standard SRAM at 0x20000000,
// but also an 8KB CCM (Core-Coupled Memory) region at 0x10000000,
// confirmed against ST's own datasheet (DS9118) for the STM32F303xB/xC
// family. CCM is CPU-only (no DMA access) and mapped on both instruction
// and data bus - a firmware built to run its most timing-critical code
// from there (or simply place its stack there) is using this chip
// exactly as ST documents it supporting, not doing anything unusual.
//
// Start addresses come from CMSIS (SRAM_BASE/CCMDATARAM_BASE) rather
// than hardcoded literals. The sizes stay as literals - verified neither
// SRAM_SIZE nor CCMDATARAM_SIZE exists in this chip family's CMSIS
// headers, so there's no macro to derive them from.
//
// The inclusive upper bound (<=, not <) on each range is intentional,
// not an off-by-one: this project's own linker script defines
// _estack = ORIGIN(RAM) + LENGTH(RAM), i.e. exactly SRAM_BASE + 40K -
// the standard ARM Cortex-M "empty descending stack" convention, where
// the reset vector table's initial MSP value legitimately points one
// address past the last usable byte (nothing is stored there; the first
// push decrements before writing). A stricter "<" here would reject the
// exact initial SP value of any correctly-linked application using this
// project's own linker script.
static uint8_t StackPointerInValidRAM(uint32_t sp) {
    // Lower bound leaves headroom for at least a few PUSHes before the
    // very base of RAM - a stack pointer sitting exactly at the base
    // would fault on the very first PUSH (which decrements before
    // writing), so this rejects that edge case rather than treating it
    // as a plausible, valid application.
    uint8_t in_sram = (sp >= SRAM_BASE + 0x100UL && sp <= SRAM_BASE + 0xA000UL);
    uint8_t in_ccm  = (sp >= CCMDATARAM_BASE + 0x100UL && sp <= CCMDATARAM_BASE + 0x2000UL);
    return in_sram || in_ccm;
}

static uint8_t ApplicationIsValid(void) {
    FirmwareMetadata_t meta;
    if (!Metadata_Read(&meta)) {
        // No valid metadata yet - either a genuinely blank chip, or the
        // main slot was just JTAG-flashed directly (which writes the app
        // region itself but has no way to also populate this separate
        // metadata page). Distinguish the two with a plausibility check on
        // the initial stack pointer: genuinely erased flash reads as
        // 0xFFFFFFFF, not a valid RAM address. A real vector table (from
        // JTAG, or adopted here before) has a real MSP inside this chip's
        // actual 48KB RAM. If it passes, compute its CRC/HMAC right now and
        // adopt it as the trusted baseline.
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
        // Note: this adopted HMAC won't match a real signed build's HMAC on
        // the next actual OTA update attempt's comparison basis - that's
        // fine, since HMAC here only ever gates a NEW incoming image
        // against the KEY, not against this adopted baseline.
    }

    if (meta.state == META_STATE_COPY_PENDING) {
        // Previous session was interrupted mid-copy. Backup was never
        // touched during that copy, so it's still exactly what it was when
        // it passed full verification - just resume the copy from it.
        CAN_SendStatus(STATUS_COPYING);
        if (!Flash_CopyRegion(MAIN_APP_ADDR, BACKUP_APP_ADDR, meta.size)) {
            return 0; // stay in bootloader; can retry again next boot
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

static void JumpToApplication(void) {
    typedef void (*pFunction)(void);
    uint32_t app_stack = *(volatile uint32_t*)MAIN_APP_ADDR;
    uint32_t app_reset_vector = *(volatile uint32_t*)(MAIN_APP_ADDR + 4);

    // Sanity check runs before any peripheral teardown below - a failure
    // here returns to the caller with CAN still alive to fall back to
    // listening with, rather than having already disabled the CAN
    // interrupt, de-initialized CAN, reset the clock config, and
    // de-initialized HAL first. Four separate conditions, each catching a
    // different way a corrupted or wrong-slot image could otherwise still
    // pass a naive check: the stack pointer must sit in one of this
    // chip's two real RAM regions (40KB SRAM at 0x20000000, or 8KB CCM at
    // 0x10000000 - see StackPointerInValidRAM's own note on why both are
    // legitimate), the stack pointer must be 4-byte aligned per the AAPCS
    // calling convention Cortex-M requires, the reset vector's LSB must
    // be 1 (Thumb state - Cortex-M cannot execute ARM-mode instructions
    // at all, so a 0 here means an instant UsageFault on entry), and the
    // reset vector itself must actually point inside the application
    // slot rather than anywhere else a corrupted vector table might claim.
    if (!StackPointerInValidRAM(app_stack) ||
        (app_stack & 0x3UL) != 0 ||
        (app_reset_vector & 0x1UL) == 0 ||
        app_reset_vector < MAIN_APP_ADDR ||
        app_reset_vector >= MAIN_APP_ADDR + APP_MAX_SIZE) {
        return;
    }

    HAL_NVIC_DisableIRQ(CAN_RX0_IRQn);
    HAL_CAN_DeInit(&hcan);
    HAL_I2C_DeInit(&hi2c2);
    HAL_RCC_DeInit();
    HAL_DeInit();
    // Same safe-state pattern already used in HardFault_Handler below -
    // not covering an actual collision in this project's current pin
    // usage (the application reconfigures I2C2/CAN on these same pins
    // for the same purpose regardless of what state they're in), but
    // cheap insurance if a future firmware revision ever repurposes them.
    // 0xEBFFFFFF preserves PA13/PA14 (SWD) - see HardFault_Handler's own
    // note on this exact bit pattern.
    GPIOA->MODER = 0xEBFFFFFF;
    GPIOB->MODER = 0xFFFFFFFF;
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    // If SysTick happened to reach 0 right before the disable above, the
    // resulting pending interrupt survives it - CTRL=0 stops the counter,
    // it doesn't clear an already-latched pending request in the NVIC.
    // Left uncleared, that request fires the instant the application
    // calls __enable_irq(), potentially before its own SysTick_Handler's
    // dependencies (HAL_Init(), etc.) are ready for it.
    SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk;

    // Defensive: the app already relocates its own vector table as the
    // first line of main() (before HAL_Init()), and every interrupt
    // source used here (CAN RX, SysTick) is already disabled above, so
    // this line isn't covering an active gap - it's cheap insurance
    // against a future refactor reordering the app's own VTOR write.
    __disable_irq(); // masks all maskable interrupts (not HardFault, which
                      // isn't maskable) for the VTOR/MSP transition itself -
                      // the application re-enables via its own HAL_Init()
    SCB->VTOR = MAIN_APP_ADDR;
    __DSB(); // Data Synchronization Barrier - the VTOR write must complete before anything below relies on it
    __ISB(); // Instruction Synchronization Barrier - flushes the pipeline so nothing prefetched against the old vector table runs

    pFunction app_entry = (pFunction)app_reset_vector;
    __set_MSP(app_stack);
    app_entry();
}

// -----------------------------------------------------------------------
// Update flow - all writes during an update go to the BACKUP slot only.
// The main slot is never touched until backup has fully passed size, CRC32,
// HMAC-SHA256, and HardwareID verification.
// -----------------------------------------------------------------------
static uint32_t update_total_size = 0;
static uint32_t update_declared_hw_id = 0;
static uint32_t update_bytes_received = 0;
static uint32_t update_running_crc = 0xFFFFFFFFUL;
static uint8_t  update_expected_hmac[32];
static uint8_t  update_hmac_chunks_received = 0;
static uint8_t  page_buffer[FLASH_PAGE_SIZE];
static uint32_t page_buffer_fill = 0;
static uint32_t current_page_index = 0;
static uint8_t  update_in_progress = 0;
static uint32_t update_last_activity_tick = 0;
static uint8_t  update_failed = 0;
static uint32_t last_heartbeat_tick = 0 - 1000; // forces an immediate heartbeat on the first check, rather than waiting a full 1000ms - previously this meant the brief 600ms initial listening window below never sent one at all, since HAL_GetTick() this early in boot never reaches the full interval starting from a 0 baseline

static uint8_t FlushPageBuffer(void) {
    uint32_t page_addr = BACKUP_APP_ADDR + (current_page_index * FLASH_PAGE_SIZE);
    for (uint32_t i = page_buffer_fill; i < FLASH_PAGE_SIZE; i++) {
        page_buffer[i] = 0xFF;
    }
    uint8_t ok = Flash_WriteVerified(page_addr, page_buffer, FLASH_PAGE_SIZE);
    if (ok) {
        CAN_SendPageAck(current_page_index);
        current_page_index++;
        page_buffer_fill = 0;
        HAL_IWDG_Refresh(&hiwdg);
        // update_total_size > 0 is already guaranteed by this point (only
        // reachable after HandleStartUpdate validated it), but checking
        // explicitly here too costs nothing and keeps that safety visible
        // locally rather than relying on a precondition from elsewhere.
        uint8_t pct = (update_total_size > 0) ?
            (uint8_t)(((uint64_t)update_bytes_received * 100) / update_total_size) : 0;
        OLED_DrawProgressBar_Boot(pct);
    }
    return ok;
}

static void HandleStartUpdate(uint8_t *data) {
    update_total_size = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                       | ((uint32_t)data[2] << 8) | data[3];
    update_declared_hw_id = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16)
                           | ((uint32_t)data[6] << 8) | data[7];

    if (update_total_size == 0 || update_total_size > APP_MAX_SIZE) {
        CAN_SendStatus(STATUS_ERROR);
        return;
    }
    if (update_declared_hw_id != THIS_HARDWARE_ID) {
        // Rejected before touching flash at all - an image built for
        // different hardware doesn't even get a chance to erase anything.
        // Uses the same reason-coded path as the other verification
        // failures below (not the generic STATUS_ERROR) so the flasher has
        // one consistent way to learn exactly why an update was rejected.
        CAN_SendVerifyFailReason(VERIFY_FAIL_REASON_HARDWARE_ID);
        return;
    }

    OLED_ClearAll_Boot();
    OLED_PrintStr2x_Boot(0, 16, "UPDATING");
    OLED_DrawProgressBar_Boot(0);

    uint32_t pages_needed = (update_total_size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    CAN_SendStatus(STATUS_ERASING);
    if (!Flash_ErasePages(BACKUP_APP_ADDR, pages_needed)) {
        CAN_SendStatus(STATUS_ERROR);
        update_failed = 1;
        update_in_progress = 0; // explicit for clarity - every consumer of
                                 // these two flags already checks
                                 // update_failed too, so this wasn't
                                 // actually reachable as a live bug, but an
                                 // update that failed here should read as
                                 // "not in progress", not just "failed
                                 // while technically still in progress"
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }

    memset(page_buffer, 0xFF, sizeof(page_buffer)); // clears any residual
    // content from an earlier, aborted attempt - the fill logic below
    // always writes valid data up to page_buffer_fill regardless, so this
    // wasn't reachable as a live bug either, but keeps a stale attempt's
    // leftover bytes from ever being in RAM if something upstream is ever
    // refactored to (mistakenly) trust page_buffer beyond that boundary.
    update_bytes_received = 0;
    update_running_crc = 0xFFFFFFFFUL;
    update_hmac_chunks_received = 0;
    page_buffer_fill = 0;
    current_page_index = 0;
    update_in_progress = 1;
    update_failed = 0;
    update_last_activity_tick = HAL_GetTick();
    CAN_SendStatus(STATUS_RECEIVING);
}

static void HandleHmacChunk(uint8_t *data) {
    if (!update_in_progress || update_failed) return;
    if (update_hmac_chunks_received >= 4) return; // already have all 32 bytes
    memcpy(&update_expected_hmac[update_hmac_chunks_received * 8], data, 8);
    update_hmac_chunks_received++;
    update_last_activity_tick = HAL_GetTick();
}

static void HandleData(uint8_t *data, uint32_t dlc) {
    if (!update_in_progress || update_failed) return;
    // Classic CAN hardware already caps DLC at 8, but this costs nothing
    // and closes off a theoretical out-of-bounds read of the caller's
    // 8-byte rxData buffer if that ever stops being true (a future
    // CAN-FD migration, or a corrupted header field).
    if (dlc > 8) return;
    if (update_bytes_received < update_total_size) {
        update_last_activity_tick = HAL_GetTick();
    }

    uint32_t i;
    for (i = 0; i < dlc; i++) {
        if (update_bytes_received >= update_total_size) break;
        page_buffer[page_buffer_fill++] = data[i];
        update_bytes_received++;

        if (page_buffer_fill == FLASH_PAGE_SIZE) {
            if (!FlushPageBuffer()) {
                update_running_crc = CRC32_Update(update_running_crc, data, i + 1);
                CAN_SendStatus(STATUS_ERROR);
                update_failed = 1;
                OLED_ClearAll_Boot();
                OLED_PrintStr2x_Boot(2, 4, "ERROR");
                return;
            }
        }
    }
    update_running_crc = CRC32_Update(update_running_crc, data, i);
}

static void HandleEndUpdate(uint8_t *data) {
    if (!update_in_progress || update_failed) return;
    update_last_activity_tick = HAL_GetTick();

    if (page_buffer_fill > 0) {
        if (!FlushPageBuffer()) {
            CAN_SendStatus(STATUS_ERROR);
            update_failed = 1;
            OLED_ClearAll_Boot();
            OLED_PrintStr2x_Boot(2, 4, "ERROR");
            return;
        }
    }

    if (update_bytes_received != update_total_size || update_hmac_chunks_received != 4) {
        CAN_SendVerifyFailReason(VERIFY_FAIL_REASON_INCOMPLETE);
        update_in_progress = 0;
        update_failed = 1;
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }

    uint32_t expected_crc = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                           | ((uint32_t)data[2] << 8) | data[3];
    uint16_t version_major = ((uint16_t)data[4] << 8) | data[5];
    uint16_t version_minor = ((uint16_t)data[6] << 8) | data[7];
    uint32_t actual_crc = CRC32_Finalize(update_running_crc);

    CAN_SendStatus(STATUS_VERIFYING);
    OLED_ClearAll_Boot();
    OLED_PrintStr2x_Boot(0, 16, "UPDATING");

    if (actual_crc != expected_crc) {
        CAN_SendVerifyFailReason(VERIFY_FAIL_REASON_CRC32);
        update_in_progress = 0;
        update_failed = 1;
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }

    uint8_t actual_hmac[32];
    hmac_sha256_flash_region(HMAC_KEY, 32, BACKUP_APP_ADDR, update_total_size, actual_hmac);
    if (!hmac_constant_time_compare(actual_hmac, update_expected_hmac, 32)) {
        // CRC32 passed but the signature didn't - the image isn't corrupt,
        // but it also isn't provably from this project's own build process.
        // Same rejection as any other verification failure: main slot was
        // never touched, so there's nothing to recover.
        CAN_SendVerifyFailReason(VERIFY_FAIL_REASON_HMAC);
        update_in_progress = 0;
        update_failed = 1;
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }

    // Anti-rollback: a cryptographically valid image is still rejected if
    // it declares a version older than what's already running. Without
    // this, a validly-signed-at-the-time image with a since-discovered
    // vulnerability could be replayed indefinitely - HMAC alone only
    // proves "signed with this project's key," not "not superseded."
    // Only enforced when there's an existing valid version to roll back
    // from - a blank board or one recovering from corrupted metadata has
    // nothing to compare against, so any validly-signed image is accepted
    // as it always was.
    FirmwareMetadata_t current_meta;
    if (Metadata_Read(&current_meta) && current_meta.state == META_STATE_APP_VALID) {
        uint32_t current_version = (current_meta.version_major << 16) | current_meta.version_minor;
        uint32_t new_version = ((uint32_t)version_major << 16) | version_minor;
        if (new_version < current_version) {
            CAN_SendVerifyFailReason(VERIFY_FAIL_REASON_ROLLBACK);
            update_in_progress = 0;
            update_failed = 1;
            OLED_ClearAll_Boot();
            OLED_PrintStr2x_Boot(2, 4, "ERROR");
            return;
        }
    }

    // Backup slot is now fully verified: size matched, CRC32 matched, HMAC
    // matched, HardwareID was checked before a single byte was written.
    // Record the copy as pending BEFORE touching the main slot - if power
    // is lost partway through the copy below, the next boot's
    // ApplicationIsValid() finds this state and resumes from backup
    // (untouched by any of this) rather than treating a partially-copied
    // main slot as something to try to run.
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
        CAN_SendStatus(STATUS_ERROR);
        update_in_progress = 0;
        update_failed = 1;
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }

    CAN_SendStatus(STATUS_COPYING);
    OLED_ClearAll_Boot();
    OLED_PrintStr2x_Boot(0, 4, "UPDATING");
    OLED_DrawProgressBar_Boot(0);
    if (!Flash_CopyRegion(MAIN_APP_ADDR, BACKUP_APP_ADDR, update_total_size)) {
        // Main slot may be partially written at this point, but that's
        // fine - metadata still says META_STATE_COPY_PENDING, backup is
        // still fully intact, and the next boot (or another attempt right
        // now, if we retried in a loop) resumes the copy from it.
        CAN_SendStatus(STATUS_ERROR);
        update_in_progress = 0;
        update_failed = 1;
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }
    OLED_DrawProgressBar_Boot(100);

    FirmwareMetadata_t done = pending;
    done.state = META_STATE_APP_VALID;
    if (!Metadata_EraseAndWrite(&done)) {
        CAN_SendStatus(STATUS_ERROR);
        update_in_progress = 0;
        update_failed = 1;
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }

    OLED_ClearAll_Boot();
    OLED_PrintStr2x_Boot(2, 16, "FLASH OK");
    CAN_SendStatus(STATUS_VERIFY_OK);
    HAL_Delay(600); // let the success message actually be seen before resetting
    NVIC_SystemReset();
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_CAN_Init();
    OLED_Init_Boot(); // for the update progress display

    // NVIC_SystemReset() does not stop the IWDG - it's "independent"
    // specifically so a reset from something else can't silence it. The
    // application always starts one during normal operation, so a
    // 0x7F0-triggered reset into this bootloader inherits an
    // already-ticking ~800ms countdown. Same config as the app
    // (Prescaler=32, Reload=999, ~800ms @ ~40kHz LSI) so this is a no-op
    // if it's already running with these values, and a fresh start if this
    // is a first boot with no application having run yet.
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 999;
    HAL_IWDG_Init(&hiwdg);

    uint8_t app_valid = ApplicationIsValid();

    // Listening window: ~600ms. Long enough for a master that wants to
    // trigger an update to get a frame in reliably, short enough not to be
    // a noticeable boot delay for normal operation.
    uint32_t listen_start = HAL_GetTick();
    uint8_t enter_update_mode = 0;

    while ((HAL_GetTick() - listen_start) < 600) {
        HAL_IWDG_Refresh(&hiwdg);
        CAN_RxHeaderTypeDef rxHeader;
        uint8_t rxData[8];
        if (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) > 0) {
            if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rxHeader, rxData) == HAL_OK) {
                // A Remote Transmission Request frame's DLC field states a
                // requested length without the frame actually carrying that
                // many (or any) data bytes - rxData for one would be
                // whatever was left over from before, not real payload.
                // Every command below expects real data, so RTR (and
                // anything not standard 11-bit ID, which this protocol
                // never uses) is rejected before any of them run.
                if (rxHeader.RTR != CAN_RTR_DATA || rxHeader.IDE != CAN_ID_STD) {
                    continue;
                }
                if (rxHeader.StdId == CAN_ID_START_UPDATE && rxHeader.DLC == 8) {
                    enter_update_mode = 1;
                    HandleStartUpdate(rxData);
                    break;
                } else if (rxHeader.StdId == CAN_ID_QUERY_VERSION) {
                    HandleVersionQuery();
                }
            }
        }
        if (HAL_GetTick() - last_heartbeat_tick >= 1000) {
            last_heartbeat_tick = HAL_GetTick();
            CAN_SendHeartbeat(STATUS_LISTENING, 0xFF);
        }
    }

    if (!enter_update_mode && app_valid) {
        JumpToApplication();
        // if we get here, JumpToApplication's sanity check rejected the
        // image despite passing every stored check (shouldn't happen, but
        // fall through to the listening loop below rather than doing
        // nothing forever)
    }

    // Either an update was requested, or there's no valid application to
    // jump to - stay here and listen indefinitely.
    CAN_SendStatus(STATUS_LISTENING);
    while (1) {
        HAL_IWDG_Refresh(&hiwdg);
        CAN_RxHeaderTypeDef rxHeader;
        uint8_t rxData[8];
        if (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) > 0) {
            if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rxHeader, rxData) == HAL_OK) {
                // Same reasoning as the initial listening window above - an
                // RTR frame's DLC states a requested length without real
                // payload behind it, and every command here expects actual
                // data.
                if (rxHeader.RTR != CAN_RTR_DATA || rxHeader.IDE != CAN_ID_STD) {
                    continue;
                }
                if (rxHeader.StdId == CAN_ID_START_UPDATE && rxHeader.DLC == 8) {
                    HandleStartUpdate(rxData);
                } else if (rxHeader.StdId == CAN_ID_HMAC_CHUNK && rxHeader.DLC == 8) {
                    HandleHmacChunk(rxData);
                } else if (rxHeader.StdId == CAN_ID_DATA) {
                    HandleData(rxData, rxHeader.DLC);
                } else if (rxHeader.StdId == CAN_ID_END_UPDATE && rxHeader.DLC == 8) {
                    HandleEndUpdate(rxData);
                } else if (rxHeader.StdId == CAN_ID_QUERY_VERSION) {
                    HandleVersionQuery();
                }
            }
        }

        if (HAL_GetTick() - last_heartbeat_tick >= 1000) {
            last_heartbeat_tick = HAL_GetTick();
            uint8_t pct = 0xFF;
            uint8_t hb_status = STATUS_LISTENING;
            if (update_in_progress && !update_failed && update_total_size > 0) {
                pct = (uint8_t)(((uint64_t)update_bytes_received * 100) / update_total_size);
                hb_status = STATUS_RECEIVING;
            }
            CAN_SendHeartbeat(hb_status, pct);
        }

        // Without this, an update interrupted mid-transfer (CAN cable
        // unplugged, master crashed, etc.) left the bootloader waiting for
        // more data forever - the backup slot flash was already erased by
        // HandleStartUpdate, so there was nothing to fall back to, and the
        // only way out was a manual power cycle. 10s of silence during an
        // active transfer now aborts back to a clean listening state on its
        // own - the main slot was never touched by any of this, so nothing
        // is at risk, and the master can just retry the whole update.
        if (update_in_progress && !update_failed &&
            (HAL_GetTick() - update_last_activity_tick > 10000)) {
            update_in_progress = 0;
            update_failed = 0;
            OLED_ClearAll_Boot();
            OLED_PrintStr2x_Boot(2, 4, "ERROR");
            CAN_SendStatus(STATUS_LISTENING);
        }
    }
}

void HardFault_Handler(void) {
    // Same reasoning as the main application's handler: force every pin on
    // GPIOA/GPIOB to a safe, disconnected state. The bootloader doesn't
    // drive any actuators itself, but if it's running at all something
    // has already gone wrong with the application, so erring toward
    // "everything off" here too costs nothing.
    GPIOA->MODER = 0xEBFFFFFF;
    GPIOB->MODER = 0xFFFFFFFF;
    while (1) { }
}

// NOTE: this bootloader polls HAL_CAN_GetRxFifoFillLevel() directly in its
// main loop rather than using interrupt-driven reception - the hardware
// FIFO fills autonomously regardless of interrupt configuration, and a
// bootloader has nothing else competing for CPU time the way the main
// application does, so polling is simpler here with no real downside. No
// CAN_RX0_IRQHandler is defined for the same reason - it would never fire,
// since neither HAL_NVIC_EnableIRQ nor HAL_CAN_ActivateNotification are
// called for this peripheral.
//
// HAL_GetTick() depends on SysTick actually incrementing it - without this
// handler, every HAL_Delay() call here (including the listening-window
// timeout check and the status-frame settling delay before
// NVIC_SystemReset()) would spin forever instead of ever completing.
void SysTick_Handler(void) {
    HAL_IncTick();
}
