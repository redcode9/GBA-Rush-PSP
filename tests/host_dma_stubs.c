// Host link stubs for tests/test_perf_dma_probe.c.
//
// memory.c is compiled on the host against the real PSP SDK headers (see
// the test_perf_dma_probe Makefile target). Everything memory.c references
// outside its own translation unit is stubbed here so the production
// dma_transfer() path can be exercised in a host test.
//
// Two stubs have behavior. The system clock: host_stub_set_time() arms two
// timestamps, so the first sceKernelGetSystemTimeLow() call of a transfer
// returns t0 and the second returns t1, giving a deterministic measured DMA
// duration of (t1 - t0) microseconds. And the ROM file reads used by the
// paged ROM test, controlled by host_stub_rom_io().
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* --- Controllable system clock ------------------------------------------ */

static uint64_t host_stub_t0 = 0;
static uint64_t host_stub_t1 = 0;
static int host_stub_time_used = 0;

void host_stub_set_time(uint64_t t0, uint64_t t1)
{
  host_stub_t0 = t0;
  host_stub_t1 = t1;
  host_stub_time_used = 0;
}

uint32_t sceKernelGetSystemTimeLow(void)
{
  if (host_stub_time_used)
    return host_stub_t1;
  host_stub_time_used = 1;
  return host_stub_t0;
}

uint64_t ticker(void)
{
  return host_stub_t1;
}

/* --- Data symbols -------------------------------------------------------- */

uint32_t reg[64] = { 0 };
int32_t idle_loop_targets = 0;
uint32_t idle_loop_target_pc[16] = { 0 };
int32_t smc_gate_targets = 0;
uint32_t iwram_stack_optimize = 0;
uint32_t enable_tilt_sensor = 0;
uint32_t tilt_sensorX = 0;
uint32_t tilt_sensorY = 0;
int32_t affine_reference_x[2] = { 0, 0 };
int32_t affine_reference_y[2] = { 0, 0 };
uint16_t color_active_item = 0;
uint32_t dma_cycle_count = 0;
uint8_t *memory_map_read[8 * 1024] = { 0 };
uint8_t *memory_map_write[8 * 1024] = { 0 };
uint32_t option_boot_mode = 0;
uint32_t option_language = 0;
uint32_t savestate_slot = 0;
char dir_save[512] = { 0 };
char dir_state[512] = { 0 };
char main_path[512] = { 0 };
unsigned char bios[0x20000] = { 0 };

/* --- File I/O (only the paged ROM reads are reached) --------------------- */

void *psp_fopen(const char *path, const char *mode)
{
  (void)path;
  (void)mode;
  return NULL;
}

int psp_fclose(void *file)
{
  (void)file;
  return 0;
}

/* Fake ROM file: every byte of 32 KiB page n reads as n + 1. A negative
   read limit fails every read; a smaller one makes reads short. */
static int64_t host_rom_offset;
static int host_rom_limit = -1;
static int host_rom_seek_fails;

void host_stub_rom_io(int read_limit, int seek_fails)
{
  host_rom_limit = read_limit;
  host_rom_seek_fails = seek_fails;
}

int64_t sceIoLseek(int fd, int64_t offset, int whence)
{
  (void)fd;
  (void)whence;
  if(host_rom_seek_fails)
    return -1;
  host_rom_offset = offset;
  return offset;
}

/* Fills buf with the fake ROM pattern at the current seek offset and
   advances it; returns -1 or a short count as host_stub_rom_io() set. */
int sceIoRead(int fd, void *buf, int size)
{
  (void)fd;
  if(host_rom_limit < 0)
    return -1;
  if(size > host_rom_limit)
    size = host_rom_limit;
  memset(buf, (unsigned char)((host_rom_offset >> 15) + 1), size);
  host_rom_offset += size;
  return size;
}

int sceIoWrite(int fd, const void *buf, int size)
{
  (void)fd;
  (void)buf;
  (void)size;
  return -1;
}

int file_length(const char *path)
{
  (void)path;
  return 0;
}

void *safe_malloc(size_t size)
{
  return malloc(size);
}

/* --- Power / RTC (never reached by the DMA test path) --------------------- */

int scePowerLock(int type)
{
  (void)type;
  return 0;
}

int scePowerUnlock(int type)
{
  (void)type;
  return 0;
}

int sceRtcGetCurrentClockLocalTime(void)
{
  return 0;
}

int sceRtcGetDayOfWeek(void)
{
  return 0;
}

/* --- GBA SIO (init path only; no-op keeps registers untouched) ------------ */

void gba_sio_init(void)
{
}

int gba_sio_load_snapshot(void)
{
  return 0;
}

int gba_sio_save_snapshot(void)
{
  return 0;
}

int gba_sio_write16(void)
{
  return 0;
}

int gba_sio_write32(void)
{
  return 0;
}

/* --- Emulation support (never reached by the DMA test path) --------------- */

void add_smc_gate(void) {}
void change_ext(void) {}
void clear_cheats(void) {}
void clear_metadata_area(void) {}
void cpu_read_savestate(void) {}
void cpu_write_mem_savestate(void) {}
void draw_popup_frame_auto(void) {}
void error_msg(void) {}
void flip_screen(void) {}
void gbc_sound_noise_control(void) {}
void gbc_sound_tone_control_high(void) {}
void gbc_sound_tone_control_high_wave(void) {}
void gbc_sound_tone_control_low(void) {}
void gbc_sound_tone_control_low_wave(void) {}
void gbc_sound_tone_control_sweep(void) {}
void gbc_sound_update(void) {}
void gbc_sound_wave_control(void) {}
void gbc_sound_wave_pattern_ram16(void) {}
void input_read_savestate(void) {}
void input_write_mem_savestate(void) {}
void load_file_zip(void) {}
void main_read_savestate(void) {}
void main_write_mem_savestate(void) {}
void load_game_config_file(void) {}
int main_link_active(void) { return 0; }
void message(void) {}
void partial_clear_metadata(void) {}
void print_string(void) {}
void print_string_gbk(void) {}
void quit(void) {}
void rebuild_fast_forward_button_mask(void) {}
void set_cpu_clock(void) {}
void sound_control_high(void) {}
void sound_control_low(void) {}
void sound_control_x(void) {}
void sound_pause(void) {}
void sound_read_savestate(void) {}
void sound_timer_queue(void) {}
void timer_control_high(void) {}
void timer_control_low(void) {}
void sound_write_mem_savestate(void) {}
void video_read_savestate(void) {}
void video_write_mem_savestate(void) {}
void yesno_dialog(void) {}
