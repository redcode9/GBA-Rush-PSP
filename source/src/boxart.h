#ifndef BOXA_H
#define BOXA_H

/* ========================================================================
 * Boxart API
 * ======================================================================== */

/* Standard boxart (141×141) for list view */
void boxart_load(const char *rom_name);
void boxart_draw(u16 pos_x, u16 pos_y, u16 border_color);
void boxart_free(void);
void boxart_clear_cache(void);

/* ========================================================================
 * Standard boxart dimensions (shared with savestate preview scaler)
 * ======================================================================== */

#define BOXART_W  141
#define BOXART_H  141

/* ========================================================================
 * Carousel API
 * ======================================================================== */

#define CAROUSEL_SLOTS      5   /* Total visible slots */
#define CAROUSEL_CENTER     2   /* Index of the center/selected slot */

/* Slot dimensions */
#define CAROUSEL_W_OUTER    70
#define CAROUSEL_H_OUTER    70
#define CAROUSEL_W_INNER    100
#define CAROUSEL_H_INNER    100
#define CAROUSEL_W_CENTER   141
#define CAROUSEL_H_CENTER   141

/* Screen positions for each slot */
#define CAROUSEL_X_OUTER_L  0
#define CAROUSEL_Y_OUTER_L  101
#define CAROUSEL_X_INNER_L  55
#define CAROUSEL_Y_INNER_L  86
#define CAROUSEL_X_CENTER   170
#define CAROUSEL_Y_CENTER   66
#define CAROUSEL_X_INNER_R  325
#define CAROUSEL_Y_INNER_R  86
#define CAROUSEL_X_OUTER_R  410
#define CAROUSEL_Y_OUTER_R  101

/* Brightness levels (percentage of original) */
#define CAROUSEL_BRIGHT_OUTER  50
#define CAROUSEL_BRIGHT_INNER  75
#define CAROUSEL_BRIGHT_CENTER 100

/* Load a boxart into the carousel RAM cache.
 * is_folder: 0 = ROM boxart from dir_boxart, 1 = folder boxart from dir_boxart_folders
 * Returns a pointer to the 141×141 ABGR5551 buffer, or NULL.
 * Uses the raw disk cache for fast loading after first decode. */
u16 *boxart_carousel_get(const char *rom_name, u32 is_folder);

/* Draw a boxart buffer to the screen with on-the-fly scaling and dimming.
 *
 * buffer    : 141×141 ABGR5551 source (from boxart_carousel_get)
 * x, y      : top-left destination position
 * w, h      : destination size (will nearest-neighbour downscale from 141×141)
 * brightness: 50, 75, or 100 (percent)
 *
 * Uses an internal static scratch buffer — safe to call multiple times
 * per frame for all 5 carousel slots. */
void boxart_carousel_draw(u16 *buffer, u16 x, u16 y, u16 w, u16 h, u8 brightness);

/* Draw a lightweight folder frame (tab + body) for carousel slots.
 * Zero extra memory — uses existing primitive draw calls. */
void boxart_carousel_draw_folder_frame(u16 x, u16 y, u16 w, u16 h,
                                       u16 fill_color, u16 border_color);

/* Free all buffers in the carousel RAM cache.
 * Call when leaving the file browser. */
void boxart_carousel_clear(void);

#endif