/* ============================================================================
 * TempGBA4PSP-mod — Boxart module with transparent on-device cache + Carousel
 * ============================================================================
 *
 * Problem:
 *   Savestate screenshots load instantly because they are raw RGB565 data.
 *   Boxart PNGs are slow because every scroll triggers: file open → zlib
 *   inflate → filter undo → color conversion → nearest-neighbour downscale.
 *
 * Solution:
 *   After the first PNG decode, write the final 141×141 RGB565 pixels to a
 *   .raw cache file. On every subsequent load, fread() the raw pixels directly
 *   — same speed as a savestate screenshot.
 *
 * Cache invalidation:
 *   The cache header stores the source PNG's file size AND modification time.
 *   If the user replaces the PNG (even with a same-sized file), the mtime
 *   mismatch auto-deletes the stale cache and rebuilds it.
 *
 * Memory safety:
 *   No LRU cache (crashed on real hardware). One buffer at a time.
 *   All decode temporaries are freed before returning.
 * ============================================================================ */

#include "common.h"
#include "boxart.h"

extern char dir_boxart[MAX_PATH];
extern char dir_boxart_folders[MAX_PATH];

/* ------------------------------------------------------------------ */
/*  Module state — standard list view                                 */
/* ------------------------------------------------------------------ */

static u16 *boxart_buffer = NULL;   /* 141×141 RGB565 pixels, ready to blit */
static u32 boxart_loaded = 0;       /* 0 = nothing loaded, 1 = buffer valid */

/* Magic number for cache file header — 'BOXR' in little-endian */
#define BOXART_CACHE_MAGIC  0x524F5842

/* ========================================================================
 * Cache file header
 *
 * Stored at the start of every .raw file. Used to validate that the cache
 * still matches its source PNG. If any field mismatches, the cache is
 * deleted and rebuilt from PNG.
 * ======================================================================== */
typedef struct {
    u32 magic;      /* Must be BOXART_CACHE_MAGIC */
    u16 width;      /* Must match BOXART_W (141) */
    u16 height;     /* Must match BOXART_H (141) */
    u32 png_size;   /* File size of source PNG when cached */
    u32 png_mtime;  /* Packed modification time of source PNG */
} BoxartCacheHeader;

/* ------------------------------------------------------------------ */
/*  Carousel RAM cache                                                */
/* ------------------------------------------------------------------ */

/* Fixed 5-slot cache for the visible carousel window.
 * Each slot holds a 141×141 buffer and the ROM name it belongs to.
 * When scrolling, only the newly visible edge slot needs loading.
 * This avoids disk I/O for the 4 slots that were already visible. */

typedef struct {
    u16 *buffer;                    /* 141×141 ABGR5551, or NULL if missing */
    char name[MAX_FILE];            /* ROM filename this buffer belongs to */
    u32 attempted;                  /* 1 = load was attempted (buffer may be NULL) */
    u32 is_folder;                  /* 0 = ROM, 1 = folder — prevents name collisions */
} CarouselSlot;

static CarouselSlot carousel_cache[CAROUSEL_SLOTS];

/* Scratch buffer for on-the-fly scaling + dimming.
 * Reused for every slot draw — max size = center slot (141×141). */
static u16 carousel_scratch[BOXART_W * BOXART_H];

/* ------------------------------------------------------------------ */
/*  Colour conversion                                                 */
/* ------------------------------------------------------------------ */

/* Convert 8-bit RGB components to PSP ABGR5551 format.
 * The PSP framebuffer uses 1-bit alpha | 5-bit blue | 5-bit green | 5-bit red.
 * We force alpha = 1 (opaque) since boxart has no transparency. */
static inline u16 rgba_to_psp5551(u8 r, u8 g, u8 b)
{
    u16 r5 = r >> 3;
    u16 g5 = g >> 3;
    u16 b5 = b >> 3;
    return 0x8000 | (b5 << 10) | (g5 << 5) | r5;
}

/* ------------------------------------------------------------------ */
/*  Image scaling                                                     */
/* ------------------------------------------------------------------ */

/* Nearest-neighbour downscale from source RGBA8 to 141×141 ABGR5551.
 *
 * src      : raw RGBA8 pixels (4 bytes per pixel)
 * dst      : output buffer, 141×141 ABGR5551 pixels
 * src_w/h  : dimensions of source image
 * src_pitch: bytes per row in source (src_w * 4)
 *
 * This is only called once per PNG — the first time it is encountered.
 * After that, the scaled result is cached to disk. */
static void scale_nearest(u8 *src, u16 *dst, u32 src_w, u32 src_h, u32 src_pitch)
{
    u32 x, y;
    for (y = 0; y < BOXART_H; y++)
    {
        u32 src_y = (y * src_h) / BOXART_H;
        for (x = 0; x < BOXART_W; x++)
        {
            u32 src_x = (x * src_w) / BOXART_W;
            u32 src_idx = (src_y * src_pitch) + (src_x * 4);
            u8 r = src[src_idx + 0];
            u8 g = src[src_idx + 1];
            u8 b = src[src_idx + 2];
            dst[y * BOXART_W + x] = rgba_to_psp5551(r, g, b);
        }
    }
}

/* ========================================================================
 * Minimal PNG decoder
 *
 * Replaces libpng to avoid setjmp/longjmp bloat and binary size issues.
 * Supports: non-interlaced RGB/RGBA/gray/palette PNGs, 8-bit only.
 * Max resolution: 1024×1024 (safety limit for PSP RAM).
 *
 * Returns a malloc'd RGBA8 buffer. Caller must free() it.
 * ======================================================================== */

#define PNG_MAKE_U32(a,b,c,d) (((u32)(a)<<24)|((u32)(b)<<16)|((u32)(c)<<8)|(d))
#define PNG_IHDR PNG_MAKE_U32('I','H','D','R')
#define PNG_IDAT PNG_MAKE_U32('I','D','A','T')
#define PNG_IEND PNG_MAKE_U32('I','E','N','D')
#define PNG_PLTE PNG_MAKE_U32('P','L','T','E')
#define PNG_tRNS PNG_MAKE_U32('t','R','N','S')

/* Read a big-endian 32-bit integer from a byte buffer. */
static u32 png_be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

/* Fast CRC-32 table (nibble-based) for PNG chunk validation.
 * We don't actually verify CRCs for speed — this is here for completeness. */
static u32 png_crc32(const u8 *buf, u32 len)
{
    static const u32 crc_table[16] = {
        0x00000000,0x1db71064,0x3b6e20c8,0x26d930ac,
        0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
        0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,
        0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c
    };
    u32 c = ~0U;
    u32 i;
    for (i = 0; i < len; i++) {
        c ^= buf[i];
        c = (c >> 4) ^ crc_table[c & 0xF];
        c = (c >> 4) ^ crc_table[c & 0xF];
    }
    return ~c;
}

/* zlib inflate wrapper.
 * Uses the project's existing zlib linkage — no extra dependencies. */
#include <zlib.h>

static int png_inflate(const u8 *src, u32 src_len, u8 *dst, u32 dst_len)
{
    z_stream zs;
    int ret;
    memset(&zs, 0, sizeof(zs));
    zs.next_in = (Bytef *)src;
    zs.avail_in = src_len;
    zs.next_out = dst;
    zs.avail_out = dst_len;
    ret = inflateInit(&zs);
    if (ret != Z_OK) return -1;
    ret = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (ret != Z_STREAM_END) return -1;
    return (int)zs.total_out;
}

/* PNG Paeth predictor — used by filter type 4. */
static u8 png_paeth(u8 a, u8 b, u8 c)
{
    int p = (int)a + (int)b - (int)c;
    int pa = p > (int)a ? p - (int)a : (int)a - p;
    int pb = p > (int)b ? p - (int)b : (int)b - p;
    int pc = p > (int)c ? p - (int)c : (int)c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/* Decode an in-memory PNG file into a raw RGBA8 pixel buffer.
 *
 * data      : pointer to the full PNG file bytes
 * data_size : length of the PNG file in bytes
 * out_w/out_h: receive the decoded image dimensions
 *
 * Returns: malloc'd RGBA8 buffer (4 bytes per pixel, row-major), or NULL.
 * Caller must free() the returned pointer. */
static u8 *png_decode_buffer(const u8 *data, u32 data_size, u32 *out_w, u32 *out_h)
{
    u32 pos = 0;
    u32 img_w = 0, img_h = 0, bpp = 4;
    u8 bit_depth = 8, color_type = 2;
    u8 have_trns = 0;
    u8 plte[256*3];
    u8 trns[256];
    u8 *idat_buf = NULL;
    u32 idat_size = 0;
    u8 *rgba = NULL;
    u32 y, x;

    /* Sanity checks */
    if (data_size < 33) return NULL;
    if (data[0] != 0x89 || data[1] != 'P' || data[2] != 'N' || data[3] != 'G')
        return NULL;
    pos = 8;

    /* --- Parse PNG chunks ------------------------------------------------ */
    while (pos + 12 <= data_size) {
        u32 chunk_len = png_be32(data + pos);
        u32 chunk_type = png_be32(data + pos + 4);
        const u8 *chunk_data = data + pos + 8;
        u32 chunk_crc = png_be32(data + pos + 8 + chunk_len);

        if (pos + 12 + chunk_len > data_size) break;
        (void)chunk_crc; /* Skip CRC verification for speed */

        if (chunk_type == PNG_IHDR) {
            if (chunk_len != 13) goto fail;
            img_w = png_be32(chunk_data);
            img_h = png_be32(chunk_data + 4);
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];
            if (chunk_data[12] != 0) goto fail; /* No interlace support */
            if (img_w > 1024 || img_h > 1024) goto fail;
            if (bit_depth != 8) goto fail;
            if (color_type == 0) bpp = 1;
            /* Determine decoded bytes-per-pixel based on colour type */
            if (color_type == 0) bpp = 1;       /* Grayscale */
            else if (color_type == 2) bpp = 3;  /* RGB */
            else if (color_type == 3) bpp = 1;  /* Palette */
            else if (color_type == 4) bpp = 2;  /* Grayscale + alpha */
            else if (color_type == 6) bpp = 4;  /* RGBA */
            else goto fail;
        }
        else if (chunk_type == PNG_PLTE) {
            if (chunk_len > 768) goto fail;
            memcpy(plte, chunk_data, chunk_len);
        }
        else if (chunk_type == PNG_tRNS) {
            if (chunk_len > 256) goto fail;
            memset(trns, 255, 256);
            memcpy(trns, chunk_data, chunk_len);
            have_trns = 1;
        }
        else if (chunk_type == PNG_IDAT) {
            /* Accumulate all IDAT chunks into one buffer for inflate */
            u8 *new_buf = (u8 *)realloc(idat_buf, idat_size + chunk_len);
            if (!new_buf) goto fail;
            idat_buf = new_buf;
            memcpy(idat_buf + idat_size, chunk_data, chunk_len);
            idat_size += chunk_len;
        }
        else if (chunk_type == PNG_IEND) {
            break;
        }
        pos += 12 + chunk_len;
    }

    if (!img_w || !img_h || !idat_buf) goto fail;

    /* --- Decompress image data ------------------------------------------ */
    u32 raw_stride = img_w * bpp + 1; /* +1 filter byte per row */
    u32 raw_size = raw_stride * img_h;
    u8 *raw = (u8 *)malloc(raw_size);
    if (!raw) goto fail;

    int inflated = png_inflate(idat_buf, idat_size, raw, raw_size);
    if (inflated != (int)raw_size) {
        free(raw);
        goto fail;
    }

    /* --- Convert filtered raw data to flat RGBA8 ------------------------ */
    rgba = (u8 *)malloc(img_w * img_h * 4);
    if (!rgba) {
        free(raw);
        goto fail;
    }

    for (y = 0; y < img_h; y++) {
        u8 filter = raw[y * raw_stride];
        const u8 *row = raw + y * raw_stride + 1;
        u8 *out_row = rgba + y * img_w * 4;

        for (x = 0; x < img_w; x++) {
            u8 r, g, b, a;
            u32 idx = x * bpp;

            /* Undo PNG filter per-pixel */
            u8 left[4] = {0,0,0,0};
            u8 above[4] = {0,0,0,0};
            u8 left_above[4] = {0,0,0,0};
            u8 raw_px[4];
            u8 out_px[4];
            int c;

            if (x > 0) {
                for (c = 0; c < (int)bpp; c++) raw_px[c] = row[idx + c];
                for (c = 0; c < 4; c++) left[c] = out_row[(x-1)*4 + c];
            } else {
                for (c = 0; c < (int)bpp; c++) raw_px[c] = row[idx + c];
            }
            if (y > 0) {
                u8 *prev_out = rgba + (y-1) * img_w * 4;
                for (c = 0; c < 4; c++) above[c] = prev_out[x*4 + c];
                if (x > 0) {
                    for (c = 0; c < 4; c++) left_above[c] = prev_out[(x-1)*4 + c];
                }
            }

            for (c = 0; c < (int)bpp; c++) {
                u8 v = raw_px[c];
                switch (filter) {
                    case 0: out_px[c] = v; break;
                    case 1: out_px[c] = v + left[c]; break;
                    case 2: out_px[c] = v + above[c]; break;
                    case 3: out_px[c] = v + (left[c] + above[c]) / 2; break;
                    case 4: out_px[c] = v + png_paeth(left[c], above[c], left_above[c]); break;
                    default: out_px[c] = v; break;
                }
            }

            /* Convert decoded pixel to RGBA8 */
            if (color_type == 0) { /* Gray */
                r = g = b = out_px[0];
                a = 255;
            }
            else if (color_type == 2) { /* RGB */
                r = out_px[0]; g = out_px[1]; b = out_px[2];
                a = 255;
            }
            else if (color_type == 3) { /* Palette */
                u32 pidx = out_px[0];
                r = plte[pidx*3+0];
                g = plte[pidx*3+1];
                b = plte[pidx*3+2];
                a = have_trns ? trns[pidx] : 255;
            }
            else if (color_type == 4) { /* Gray + alpha */
                r = g = b = out_px[0];
                a = out_px[1];
            }
            else { /* RGBA */
                r = out_px[0]; g = out_px[1]; b = out_px[2]; a = out_px[3];
            }

            out_row[x*4+0] = r;
            out_row[x*4+1] = g;
            out_row[x*4+2] = b;
            out_row[x*4+3] = a;
        }
    }

    free(raw);
    free(idat_buf);
    *out_w = img_w;
    *out_h = img_h;
    return rgba;

fail:
    if (idat_buf) free(idat_buf);
    return NULL;
}

/* ========================================================================
 * Raw cache helpers
 * ======================================================================== */

/* Pack a PSP date-time structure into a single 32-bit integer for the cache.
 *
 * Why this exists:
 *   If a user replaces a PNG with another file of the EXACT same byte size,
 *   the size check alone would NOT invalidate the cache. By also storing the
 *   modification time, ANY file replacement (even same-size) is detected.
 *
 * Layout: YYYY(12) | MM(4) | DD(5) | hh(5) | mm(6)
 * This gives minute-level precision — more than enough for boxart updates. */
static u32 pack_mtime(const ScePspDateTime *dt)
{
    return ((dt->year  & 0xFFF) << 20) |
           ((dt->month & 0xF)   << 16) |
           ((dt->day   & 0x1F)  << 11) |
           ((dt->hour  & 0x1F)  << 6)  |
           (dt->minute & 0x3F);
}

/* PSP newlib headers define st_atime, st_mtime, and st_ctime as macros
 * for POSIX struct-stat compatibility. These macros collide with the field
 * names in Sony's SceIoStat, making it impossible to use st->st_mtime
 * directly or to declare a mirror struct with those exact names.
 *
 * Workaround: define a layout-compatible struct with renamed fields, then
 * cast SceIoStat* to it. The binary layout is identical — only the field
 * names differ. */
typedef struct {
    SceMode st_mode;
    unsigned int st_attr;
    SceOff st_size;
    ScePspDateTime ctime;       /* was st_ctime — renamed to dodge macro */
    ScePspDateTime atime;       /* was st_atime — renamed to dodge macro */
    ScePspDateTime mtime;       /* was st_mtime — renamed to dodge macro */
    unsigned int st_blksize;
    SceULong64 st_blocks;
    unsigned int st_private[6];
} BoxartSceIoStat;

/* Attempt to load a pre-cached .raw file for a given PNG path.
 *
 * What it does:
 *   1. Builds the .raw filename from the .png path (foo.png → foo.raw)
 *   2. stat()s the PNG to get its current size and mtime
 *   3. Reads the cache header and validates every field
 *   4. If valid, reads the raw 141×141 ABGR5551 pixels into *out_buffer
 *
 * Returns: 0 on success (buffer allocated and filled), -1 on any failure.
 * On failure, the stale/invalid .raw file is deleted so the next call
 * will fall back to PNG decode. */
static int try_load_raw_cache(const char *png_path, u16 **out_buffer)
{
    char raw_path[MAX_PATH];
    SceUID fd = -1;
    SceIoStat stat_png;
    BoxartCacheHeader hdr;
    int ret = -1;

    /* Build raw cache path: replace .png extension with .raw */
    strncpy(raw_path, png_path, MAX_PATH - 1);
    raw_path[MAX_PATH - 1] = '\0';
    char *ext = strrchr(raw_path, '.');
    if (ext && strcasecmp(ext, ".png") == 0)
        strcpy(ext, ".raw");
    else
        strncat(raw_path, ".raw", MAX_PATH - strlen(raw_path) - 1);

    /* Get current PNG stats for validation */
    if (sceIoGetstat(png_path, &stat_png) < 0)
        return -1;

    fd = sceIoOpen(raw_path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return -1;

    if (sceIoRead(fd, &hdr, sizeof(hdr)) != sizeof(hdr))
        goto cleanup;

    /* Cast SceIoStat to our mirror struct to read mtime safely */
    const BoxartSceIoStat *bst = (const BoxartSceIoStat *)&stat_png;

    /* Validate header: magic, dimensions, file size, AND modification time */
    if (hdr.magic != BOXART_CACHE_MAGIC ||
        hdr.width != BOXART_W ||
        hdr.height != BOXART_H ||
        hdr.png_size != (u32)stat_png.st_size ||
        hdr.png_mtime != pack_mtime(&bst->mtime))
    {
        /* Stale cache — PNG was replaced or corrupted */
        goto cleanup;
    }

    *out_buffer = (u16 *)malloc(BOXART_W * BOXART_H * 2);
    if (!*out_buffer)
        goto cleanup;

    if (sceIoRead(fd, *out_buffer, BOXART_W * BOXART_H * 2) != (s32)(BOXART_W * BOXART_H * 2))
    {
        free(*out_buffer);
        *out_buffer = NULL;
        goto cleanup;
    }

    ret = 0; /* Success — raw pixels are ready to blit */

cleanup:
    if (fd >= 0) sceIoClose(fd);
    if (ret != 0)
        sceIoRemove(raw_path); /* Delete stale/invalid cache, force rebuild */
    return ret;
}

/* Write a freshly decoded boxart to the raw cache.
 *
 * Called once per PNG — the first time it is loaded. After this write,
 * every future scroll to this ROM will hit try_load_raw_cache() and
 * load instantly via fread().
 *
 * The header stores the PNG's current size and mtime so that any future
 * replacement is detected automatically. */
static void write_raw_cache(const char *png_path, const u16 *buffer)
{
    char raw_path[MAX_PATH];
    SceUID fd;
    SceIoStat stat_png;
    BoxartCacheHeader hdr;

    if (sceIoGetstat(png_path, &stat_png) < 0)
        return;

    /* Build .raw path */
    strncpy(raw_path, png_path, MAX_PATH - 1);
    raw_path[MAX_PATH - 1] = '\0';
    char *ext = strrchr(raw_path, '.');
    if (ext && strcasecmp(ext, ".png") == 0)
        strcpy(ext, ".raw");
    else
        strncat(raw_path, ".raw", MAX_PATH - strlen(raw_path) - 1);

    fd = sceIoOpen(raw_path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0)
        return;

    /* Cast to mirror struct to read mtime safely */
    const BoxartSceIoStat *bst = (const BoxartSceIoStat *)&stat_png;

    hdr.magic = BOXART_CACHE_MAGIC;
    hdr.width = BOXART_W;
    hdr.height = BOXART_H;
    hdr.png_size = (u32)stat_png.st_size;
    hdr.png_mtime = pack_mtime(&bst->mtime);

    sceIoWrite(fd, &hdr, sizeof(hdr));
    sceIoWrite(fd, buffer, BOXART_W * BOXART_H * 2);
    sceIoClose(fd);
}

/* ========================================================================
 * Carousel helpers
 * ======================================================================== */

/* Build the PNG path for a given name inside a specific base directory.
 * Writes into png_path buffer. Returns 0 on success, -1 on error. */
static int build_png_path_base(const char *base_dir, const char *name, char *png_path, u32 png_path_size)
{
    if (!name || !base_dir || !base_dir[0])
        return -1;

    png_path[0] = '\0';
    strncat(png_path, base_dir, png_path_size - 1);
    strncat(png_path, name, png_path_size - strlen(png_path) - 1);

    char *last_slash = strrchr(png_path, '/');
    char *last_dot = strrchr(png_path, '.');
    if (last_dot && (!last_slash || last_dot > last_slash))
    {
        if ((last_dot - png_path) + 5 < (s32)png_path_size)
            strcpy(last_dot, ".png");
        else
        {
            png_path[png_path_size - 5] = '\0';
            strcat(png_path, ".png");
        }
    }
    else
    {
        if (strlen(png_path) + 4 < png_path_size)
            strcat(png_path, ".png");
        else
            return -1;
    }
    return 0;
}

/* Load a 141×141 boxart from raw cache or PNG decode.
 * Returns malloc'd buffer, or NULL on failure.
 * This is the core loader used by both list view and carousel. */
static u16 *boxart_load_core(const char *base_dir, const char *name)
{
    char png_path[MAX_PATH];
    char raw_path[MAX_PATH];
    SceUID fd = -1;
    s32 file_size = 0;
    u8 *file_buffer = NULL;
    u8 *rgba = NULL;
    u32 img_w = 0, img_h = 0;
    u16 *result = NULL;
    SceIoStat stat_png;
    int png_exists;

    if (build_png_path_base(base_dir, name, png_path, sizeof(png_path)) != 0)
        return NULL;

    /* Build raw cache path from PNG path */
    strncpy(raw_path, png_path, MAX_PATH - 1);
    raw_path[MAX_PATH - 1] = '\0';
    {
        char *ext = strrchr(raw_path, '.');
        if (ext && strcasecmp(ext, ".png") == 0)
            strcpy(ext, ".raw");
        else
            strncat(raw_path, ".raw", MAX_PATH - strlen(raw_path) - 1);
    }

    /* Check if source PNG still exists */
    png_exists = (sceIoGetstat(png_path, &stat_png) >= 0);

    /* Case 1: PNG exists — try validated raw cache (checks size + mtime) */
    if (png_exists)
    {
        if (try_load_raw_cache(png_path, &result) == 0)
            return result;
    }
    /* Case 2: PNG deleted but raw cache still exists — load orphan cache */
    else
    {
        fd = sceIoOpen(raw_path, PSP_O_RDONLY, 0);
        if (fd >= 0)
        {
            BoxartCacheHeader hdr;
            if (sceIoRead(fd, &hdr, sizeof(hdr)) == sizeof(hdr) &&
                hdr.magic == BOXART_CACHE_MAGIC &&
                hdr.width == BOXART_W &&
                hdr.height == BOXART_H)
            {
                result = (u16 *)malloc(BOXART_W * BOXART_H * 2);
                if (result)
                {
                    if (sceIoRead(fd, result, BOXART_W * BOXART_H * 2) == (s32)(BOXART_W * BOXART_H * 2))
                    {
                        sceIoClose(fd);
                        return result;
                    }
                    free(result);
                    result = NULL;
                }
            }
            sceIoClose(fd);
            /* Corrupt orphan — delete it so we don't retry forever */
            sceIoRemove(raw_path);
        }
    }

    /* No valid cache and no PNG — nothing to display */
    if (!png_exists)
        return NULL;

    /* Decode PNG */
    fd = sceIoOpen(png_path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return NULL;

    file_size = sceIoLseek(fd, 0, SEEK_END);
    sceIoLseek(fd, 0, SEEK_SET);

    if (file_size <= 0 || file_size > (2 * 1024 * 1024))
        goto cleanup;

    file_buffer = (u8 *)malloc(file_size);
    if (!file_buffer)
        goto cleanup;

    s32 read_total = 0;
    while (read_total < file_size)
    {
        s32 n = sceIoRead(fd, file_buffer + read_total, file_size - read_total);
        if (n <= 0)
            goto cleanup;
        read_total += n;
    }
    sceIoClose(fd);
    fd = -1;

    /* Decode PNG into RGBA8 */
    rgba = png_decode_buffer(file_buffer, (u32)file_size, &img_w, &img_h);
    if (!rgba || img_w == 0 || img_h == 0)
        goto cleanup;

    /* Allocate final display buffer and downscale */
    result = (u16 *)malloc(BOXART_W * BOXART_H * 2);
    if (!result)
        goto cleanup;

    scale_nearest(rgba, result, img_w, img_h, img_w * 4);
    /* Write cache so next time is instant */
    write_raw_cache(png_path, result);

cleanup:
    if (rgba) free(rgba);
    if (file_buffer) free(file_buffer);
    if (fd >= 0) sceIoClose(fd);
    return result;
}

/* ========================================================================
 * Public API — List view
 * ======================================================================== */

void boxart_load(const char *rom_name)
{
    if (boxart_buffer)
    {
        free(boxart_buffer);
        boxart_buffer = NULL;
    }
    boxart_loaded = 0;

    boxart_buffer = boxart_load_core(dir_boxart, rom_name);
    if (boxart_buffer)
        boxart_loaded = 1;
}

/* Draw the currently loaded boxart to the screen.
 *
 * pos_x, pos_y : top-left corner on the PSP framebuffer
 * border_color : RGB565 colour for the 1-pixel border around the image
 *
 * Safe to call even if no boxart is loaded — does nothing in that case. */
void boxart_draw(u16 pos_x, u16 pos_y, u16 border_color)
{
    if (!boxart_loaded || !boxart_buffer)
        return;

    blit_to_screen(boxart_buffer, BOXART_W, BOXART_H, pos_x, pos_y);
    draw_box_line(pos_x - 1, pos_y - 1,
                  pos_x + BOXART_W, pos_y + BOXART_H,
                  border_color);
}

/* Free the current boxart buffer and mark as unloaded.
 * Call this when leaving the file browser or loading a new ROM. */
void boxart_free(void)
{
    if (boxart_buffer)
    {
        free(boxart_buffer);
        boxart_buffer = NULL;
    }
    boxart_loaded = 0;
}

/* Delete all .raw cache files in the boxart directory.
 *
 * Use case: User wants to force a rebuild (e.g., they bulk-replaced
 * boxart images and some happened to have the same size + mtime).
 * Wire this to a menu option in gui.c under Customization or Tools. */
void boxart_clear_cache(void)
{
    SceUID dir;
    SceIoDirent entry;

    if (!dir_boxart[0])
        return;

    dir = sceIoDopen(dir_boxart);
    if (dir < 0)
        return;

    memset(&entry, 0, sizeof(entry));

    while (sceIoDread(dir, &entry) > 0)
    {
        char *ext = strrchr(entry.d_name, '.');
        if (ext && strcasecmp(ext, ".raw") == 0)
        {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s%s", dir_boxart, entry.d_name);
            sceIoRemove(path);
        }
    }

    sceIoDclose(dir);
}

/* ========================================================================
 * Public API — Carousel
 * ======================================================================== */

/* Look up a name in the carousel RAM cache.
 * If found, return the existing buffer.
 * If not found, load it and store in the first empty slot.
 * If no empty slots, evict the oldest (slot 0) and shift. */
u16 *boxart_carousel_get(const char *name, u32 is_folder)
{
    u32 i;
    u32 empty_idx = (u32)-1;

    if (!name || !name[0])
        return NULL;

    /* ".." never has boxart — skip immediately */
    if (strcmp(name, "..") == 0)
        return NULL;

    /* Folders with no dedicated directory also skip */
    if (is_folder && (!dir_boxart_folders[0]))
        return NULL;

    /* Search for existing entry (match name AND is_folder) */
    for (i = 0; i < CAROUSEL_SLOTS; i++)
    {
        if (carousel_cache[i].attempted &&
            carousel_cache[i].is_folder == is_folder &&
            strcmp(carousel_cache[i].name, name) == 0)
        {
            /* Already tried this entry — return cached result (may be NULL) */
            return carousel_cache[i].buffer;
        }
        if (!carousel_cache[i].attempted && empty_idx == (u32)-1)
            empty_idx = i;
    }

    /* Not found — load from disk */
    const char *base_dir = is_folder ? dir_boxart_folders : dir_boxart;
    u16 *loaded = boxart_load_core(base_dir, name);

    /* Store result in cache (even if NULL, so we don't retry) */
    if (empty_idx != (u32)-1)
    {
        carousel_cache[empty_idx].buffer = loaded;
        carousel_cache[empty_idx].attempted = 1;
        carousel_cache[empty_idx].is_folder = is_folder;
        strncpy(carousel_cache[empty_idx].name, name, MAX_FILE - 1);
        carousel_cache[empty_idx].name[MAX_FILE - 1] = '\0';
        return loaded;
    }

    /* No empty slots — evict slot 0, shift everything down */
    if (carousel_cache[0].buffer)
    {
        free(carousel_cache[0].buffer);
    }
    for (i = 1; i < CAROUSEL_SLOTS; i++)
    {
        carousel_cache[i - 1] = carousel_cache[i];
    }
    carousel_cache[CAROUSEL_SLOTS - 1].buffer = loaded;
    carousel_cache[CAROUSEL_SLOTS - 1].attempted = 1;
    carousel_cache[CAROUSEL_SLOTS - 1].is_folder = is_folder;
    strncpy(carousel_cache[CAROUSEL_SLOTS - 1].name, name, MAX_FILE - 1);
    carousel_cache[CAROUSEL_SLOTS - 1].name[MAX_FILE - 1] = '\0';

    return loaded;
}

/* Draw a boxart buffer with on-the-fly nearest-neighbour downscale + dimming.
 *
 * The source is always 141×141. We scale to the requested w×h by sampling
 * the source at proportional coordinates, then dim each pixel.
 *
 * Brightness:
 *   100 = full brightness (no change)
 *   75  = 75% brightness (slight dim for inner slots)
 *   50  = 50% brightness (heavy dim for outer slots)
 *
 * The scratch buffer avoids a per-draw malloc. */
void boxart_carousel_draw(u16 *buffer, u16 x, u16 y, u16 w, u16 h, u8 brightness)
{
    u32 dx, dy;

    if (!buffer)
        return;

    /* Build the scaled + dimmed image in scratch buffer */
    for (dy = 0; dy < h; dy++)
    {
        u32 sy = (dy * BOXART_H) / h;
        for (dx = 0; dx < w; dx++)
        {
            u32 sx = (dx * BOXART_W) / w;
            u16 pixel = buffer[sy * BOXART_W + sx];

            if (brightness == 50)
            {
                /* 50% brightness: halve each ABGR5551 channel */
                u16 r = (pixel >> 0) & 0x1F;
                u16 g = (pixel >> 5) & 0x1F;
                u16 b = (pixel >> 10) & 0x1F;
                r >>= 1; g >>= 1; b >>= 1;
                pixel = 0x8000 | (b << 10) | (g << 5) | r;
            }
            else if (brightness == 75)
            {
                /* 75% brightness: 50% + 25% per channel */
                u16 r = (pixel >> 0) & 0x1F;
                u16 g = (pixel >> 5) & 0x1F;
                u16 b = (pixel >> 10) & 0x1F;
                r = (r >> 1) + (r >> 2);
                g = (g >> 1) + (g >> 2);
                b = (b >> 1) + (b >> 2);
                pixel = 0x8000 | (b << 10) | (g << 5) | r;
            }
            /* 100% = pass through unchanged */

            carousel_scratch[dy * w + dx] = pixel;
        }
    }

    /* Blit scratch buffer to screen using existing blit function */
    blit_to_screen(carousel_scratch, w, h, x, y);
}

/* Draw a lightweight folder frame (tab + body) for carousel slots.
 * Zero extra memory — uses existing primitive draw calls.
 *
 * The tab is a small rectangle on the top-left (1/3 width, 1/5 height)
 * sitting on top of the main body rectangle. */
void boxart_carousel_draw_folder_frame(u16 x, u16 y, u16 w, u16 h,
                                       u16 fill_color, u16 border_color)
{
    /* Folder occupies 75% of slot height, bottom-aligned for a natural look */
    u16 folder_h = (h * 3) / 4;
    u16 folder_y = y + h - folder_h;
    u16 tab_w = w / 3;
    u16 tab_h = folder_h / 10;          /* tab height (~25% of folder body) */

    /* Tab sits flush on top of the folder body */
    u16 tab_y = folder_y - tab_h;
    if (tab_y < y)
    {
        /* Slot too small — clamp tab to available space */
        tab_h = folder_y - y;
        tab_y = y;
    }

    /* Body fill */
    draw_box_fill(x, folder_y, x + w - 1, folder_y + folder_h - 1, fill_color);

    /* Tab fill (capped at body top so it never extends into the body) */
    draw_box_fill(x, tab_y, x + tab_w - 1, folder_y - 1, fill_color);

    /* Body border drawn in segments with a gap where the tab sits */
    /* Bottom */
    draw_hline(x - 1, x + w, folder_y + folder_h, border_color);
    /* Left side */
    draw_vline(x - 1, folder_y - 1, folder_y + folder_h, border_color);
    /* Right side */
    draw_vline(x + w, folder_y - 1, folder_y + folder_h, border_color);
    /* Top-left segment (before tab) */
    draw_hline(x - 1, x + tab_w, folder_y - 1, border_color);
    /* Top-right segment (after tab) */
    draw_hline(x + tab_w, x + w, folder_y - 1, border_color);

    /* Tab border — strictly within tab bounds, never extends into body */
    draw_hline(x - 1, x + tab_w, tab_y - 1, border_color);        /* tab top */
    draw_vline(x - 1, tab_y - 1, folder_y - 1, border_color);     /* tab left */
    draw_vline(x + tab_w, tab_y - 1, folder_y - 1, border_color); /* tab right */
    /* tab bottom is the body top — already drawn as body border segments */
}

/* Free all buffers in the carousel RAM cache.
 * Call when leaving the file browser to avoid leaking memory. */
void boxart_carousel_clear(void)
{
    u32 i;
    for (i = 0; i < CAROUSEL_SLOTS; i++)
    {
        if (carousel_cache[i].buffer)
        {
            free(carousel_cache[i].buffer);
        }
        carousel_cache[i].buffer = NULL;
        carousel_cache[i].attempted = 0;
        carousel_cache[i].is_folder = 0;
        carousel_cache[i].name[0] = '\0';
    }
}