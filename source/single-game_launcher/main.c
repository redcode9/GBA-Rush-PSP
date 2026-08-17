/* TempGBA-mod Single-Game Launcher
 *
 * Reads rom_path.txt        (required)  -> ROM path to auto-load
 * Reads emulator_path.txt   (optional)  -> override emulator directory
 *                                         or full path to boot .PBP file
 *
 * CFW path:  sctrlKernelLoadExecVSHWithApitype (real PSP)
 * Fallback:  sceKernelLoadExec with argv[0] + argv[1] (PPSSPP / non-CFW)
 */

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <psploadexec.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "systemctrl.h"

PSP_MODULE_INFO("TempGBA-mod Single-Game Launcher", PSP_MODULE_USER, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define MAX_PATH 512
#define ROM_TXT  "rom_path.txt"
#define EMU_TXT  "emulator_path.txt"

/* Default emulator folder and boot file names.
 * Change these if your emulator uses different naming. */
#define DEFAULT_EMU_FOLDER   "tempgba4psp-mod"
#define DEFAULT_BOOT_FILE    "EBOOT.PBP"

/* Show a simple error screen and wait for any button press.
 * line1 = short error title, line2 = optional detail, path = optional path */
static void error_screen(const char *line1, const char *line2, const char *path)
{
    pspDebugScreenInit();
    pspDebugScreenSetBackColor(0xFF000080);   /* red */
    pspDebugScreenSetTextColor(0xFFFFFFFF);   /* white */

    pspDebugScreenClear();
    pspDebugScreenPrintf("\n\n");
    pspDebugScreenPrintf("  TempGBA-mod Single-Game Launcher\n");
    pspDebugScreenPrintf("  =============================\n\n");
    pspDebugScreenPrintf("  ERROR: %s\n\n", line1);

    if (line2)
        pspDebugScreenPrintf("  %s\n\n", line2);

    if (path && path[0] != '\0')
    {
        /* Wrap long paths across multiple lines (~50 chars each) */
        const char *p = path;
        size_t len = strlen(path);
        size_t chunk = 50;

        pspDebugScreenPrintf("  ");
        while (len > 0)
        {
            size_t take = (len > chunk) ? chunk : len;
            /* Try to break at '/' if possible */
            if (len > chunk)
            {
                size_t i;
                for (i = take; i > 0; i--)
                {
                    if (p[i] == '/')
                    {
                        take = i + 1;
                        break;
                    }
                }
            }
            pspDebugScreenPrintf("%.*s", (int)take, p);
            p += take;
            len -= take;
            if (len > 0)
                pspDebugScreenPrintf("\n  ");
        }
        pspDebugScreenPrintf("\n\n");
    }

    pspDebugScreenPrintf("  Press any button to exit.\n");

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);

    SceCtrlData pad;
    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);
        if (pad.Buttons & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE |
                           PSP_CTRL_SQUARE | PSP_CTRL_START | PSP_CTRL_SELECT))
            break;
        sceKernelDelayThread(50000);
    }
}

/* Extract directory from argv[0] */
static void find_my_dir(char *out, const char *argv0)
{
    if (!argv0 || !argv0[0]) {
        out[0] = '\0';
        return;
    }
    strncpy(out, argv0, MAX_PATH - 1);
    out[MAX_PATH - 1] = '\0';
    char *slash = strrchr(out, '/');
    if (slash)
        *(slash + 1) = '\0';
    else
        out[0] = '\0';
}

/* Go one folder up from my_dir, append DEFAULT_EMU_FOLDER + "/"
 *
 * Example: ms0:/PSP/GAME/tempgba4psp-mod_single-game/
 *      ->  ms0:/PSP/GAME/tempgba4psp-mod/
 */
static void derive_emulator_dir(char *out, const char *my_dir)
{
    char temp[MAX_PATH];
    strncpy(temp, my_dir, MAX_PATH - 1);
    temp[MAX_PATH - 1] = '\0';

    size_t len = strlen(temp);
    if (len < 2) {
        out[0] = '\0';
        return;
    }

    /* Walk back from the trailing slash to the previous slash */
    char *p = temp + len - 2;
    while (p > temp && *p != '/') p--;

    if (p > temp) {
        *(p + 1) = '\0';
        snprintf(out, MAX_PATH, "%s%s/", temp, DEFAULT_EMU_FOLDER);
    } else {
        out[0] = '\0';
    }
}

/* Read first non-comment, non-empty line from a text file.
 * Skips lines starting with '#' or '//' and blank lines.
 * Strips leading/trailing whitespace from the value line.
 * Returns length (>0) on success, <=0 on failure. */
static int read_txt_line(const char *path, char *out, int out_size)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return fd;

    char buf[MAX_PATH];
    int total = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (total <= 0) return total;

    buf[total] = '\0';

    char *line = buf;
    while (*line) {
        /* Find end of current line */
        char *end = line;
        while (*end && *end != '\n' && *end != '\r') end++;

        /* Temporarily terminate for processing */
        char saved = *end;
        *end = '\0';

        /* Strip leading whitespace */
        char *val = line;
        while (*val == ' ' || *val == '\t') val++;

        /* Skip comments and blank lines */
        int is_comment = (val[0] == '#') ||
                         (val[0] == '/' && val[1] == '/');
        if (val[0] != '\0' && !is_comment) {
            /* Strip trailing whitespace */
            char *tail = end - 1;
            while (tail >= val && (*tail == ' ' || *tail == '\t' ||
                                   *tail == '\n' || *tail == '\r'))
                *tail-- = '\0';

            strncpy(out, val, out_size - 1);
            out[out_size - 1] = '\0';
            *end = saved;
            return strlen(out);
        }

        /* Restore and advance to next line */
        *end = saved;
        if (*end == '\r') end++;
        if (*end == '\n') end++;
        line = end;
    }

    return 0;
}

static int is_cfw(void)
{
    return (sctrlHENGetVersion() >= 0);
}

/* Check if path exists and is a directory.
 * NOTE: sceIoGetstat on PSP often fails with trailing slash on dirs.
 * We strip the trailing slash before stat'ing. */
static int path_is_dir(const char *path)
{
    SceIoStat stat;
    char tmp[MAX_PATH];
    size_t len;

    if (!path || !path[0])
        return 0;

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    len = strlen(tmp);
    /* Strip trailing slash for stat (but keep root "ms0:/") */
    if (len > 1 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
        len--;
    }

    if (sceIoGetstat(tmp, &stat) < 0)
        return 0;

    return (stat.st_mode & FIO_S_IFDIR) != 0;
}

/* Check if path exists and is a regular file */
static int path_is_file(const char *path)
{
    SceIoStat stat;
    if (!path || !path[0])
        return 0;
    if (sceIoGetstat(path, &stat) < 0)
        return 0;
    return (stat.st_mode & FIO_S_IFREG) != 0;
}

/* Extract parent directory from a full file path into `out`.
 * out_size must be >= MAX_PATH. Returns 0 on success, -1 if no slash found. */
static int get_parent_dir(const char *path, char *out, size_t out_size)
{
    if (!path || !path[0]) return -1;

    strncpy(out, path, out_size - 1);
    out[out_size - 1] = '\0';

    char *slash = strrchr(out, '/');
    if (!slash) return -1;

    /* Keep the trailing slash */
    *(slash + 1) = '\0';
    return 0;
}

/* Check if string ends with .pbp (case-insensitive) */
static int is_pbp_path(const char *path)
{
    size_t len = strlen(path);
    if (len < 4) return 0;
    const char *ext = path + len - 4;
    return (strcasecmp(ext, ".pbp") == 0);
}

/* Ensure path ends with '/'. If it doesn't end with '/' and doesn't look
 * like a file path (no .pbp), auto-append '/'. */
static void ensure_trailing_slash(char *path, size_t size)
{
    size_t len = strlen(path);
    if (len > 0 && path[len - 1] != '/' && !is_pbp_path(path)) {
        if (len + 1 < size) {
            path[len] = '/';
            path[len + 1] = '\0';
        }
    }
}

int main(int argc, char *argv[])
{
    char my_dir[MAX_PATH];
    char emu_dir[MAX_PATH];
    char emu_boot_file[MAX_PATH];
    char rom_path[MAX_PATH];
    int ret;
    int is_dir_format = 0;
    int emu_path_from_txt = 0;  /* 1 = read from emulator_path.txt, 0 = auto-derived */

    find_my_dir(my_dir, (argc > 0) ? argv[0] : NULL);
    if (my_dir[0] == '\0') {
        error_screen("Cannot determine launcher directory.",
                     "argv[0] is missing or invalid.", NULL);
        sceKernelExitGame();
        return 0;
    }

    /* --- Emulator path resolution ---
     *
     * emulator_path.txt can contain:
     *   - Directory path ending with '/'  -> we append DEFAULT_BOOT_FILE
     *   - Full file path (no trailing '/', ends with .pbp) -> we use it as the boot file
     *
     * If the file doesn't exist or is empty, we auto-derive the directory.
     */
    {
        char emu_txt_path[MAX_PATH];
        char emu_raw[MAX_PATH];
        snprintf(emu_txt_path, sizeof(emu_txt_path), "%s%s", my_dir, EMU_TXT);

        if (read_txt_line(emu_txt_path, emu_raw, sizeof(emu_raw)) > 0) {
            emu_path_from_txt = 1;
            size_t len = strlen(emu_raw);

            /* Auto-append trailing slash if needed */
            ensure_trailing_slash(emu_raw, sizeof(emu_raw));
            len = strlen(emu_raw);

            if (len > 0 && emu_raw[len - 1] == '/') {
                /* Directory format */
                is_dir_format = 1;
                strncpy(emu_dir, emu_raw, sizeof(emu_dir) - 1);
                emu_dir[sizeof(emu_dir) - 1] = '\0';
                snprintf(emu_boot_file, sizeof(emu_boot_file), "%s%s", emu_dir, DEFAULT_BOOT_FILE);
            } else {
                /* Full file path format */
                is_dir_format = 0;
                strncpy(emu_boot_file, emu_raw, sizeof(emu_boot_file) - 1);
                emu_boot_file[sizeof(emu_boot_file) - 1] = '\0';

                /* Derive directory from boot file for validation */
                if (get_parent_dir(emu_boot_file, emu_dir, sizeof(emu_dir)) < 0) {
                    emu_dir[0] = '\0';
                }
            }
        } else {
            /* Auto-derive: file missing or empty */
            emu_path_from_txt = 0;
            derive_emulator_dir(emu_dir, my_dir);
            if (emu_dir[0] != '\0') {
                is_dir_format = 1;
                snprintf(emu_boot_file, sizeof(emu_boot_file), "%s%s", emu_dir, DEFAULT_BOOT_FILE);
            }
        }
    }

    /* --- Validate emulator directory --- */
    if (emu_dir[0] == '\0') {
        error_screen("Cannot determine emulator directory.",
                     "Create emulator_path.txt or use standard layout.", NULL);
        sceKernelExitGame();
        return 0;
    }

    if (!path_is_dir(emu_dir)) {
        if (emu_path_from_txt) {
            error_screen("Emulator directory not found.",
                         "Check the path in emulator_path.txt.", emu_dir);
        } else {
            char detail[256];
            snprintf(detail, sizeof(detail),
                     "Default naming failed.\n"
                     "  Ensure emulator is in the same parent folder\n"
                     "  or create emulator_path.txt.\n"
                     "  Expected folder name: %s",
                     DEFAULT_EMU_FOLDER);
            error_screen("Emulator directory not found.", detail, emu_dir);
        }
        sceKernelExitGame();
        return 0;
    }

    /* --- Validate emulator boot file --- */
    if (!path_is_file(emu_boot_file)) {
        if (emu_path_from_txt) {
            if (is_dir_format) {
                error_screen("Emulator boot file not found.",
                             "Expected " DEFAULT_BOOT_FILE " in emulator directory.\n"
                             "  If you renamed the boot file\n"
                             "  use emulator_path.txt to specify the full path.",
                             emu_boot_file);
            } else {
                error_screen("Emulator boot file not found.",
                             "Check the boot file path in emulator_path.txt.", emu_boot_file);
            }
        } else {
            /* Auto-derived: folder exists but EBOOT.PBP missing */
            char detail[256];
            snprintf(detail, sizeof(detail),
                     DEFAULT_BOOT_FILE " not found in " DEFAULT_EMU_FOLDER "/.\n"
                     "  If you renamed the boot file\n"
                     "  use emulator_path.txt to specify the full path.");
            error_screen("Emulator boot file not found.", detail, emu_boot_file);
        }
        sceKernelExitGame();
        return 0;
    }

    /* --- ROM path --- */
    {
        char rom_txt_path[MAX_PATH];
        snprintf(rom_txt_path, sizeof(rom_txt_path), "%s%s", my_dir, ROM_TXT);
        if (read_txt_line(rom_txt_path, rom_path, sizeof(rom_path)) <= 0) {
            error_screen("Cannot read rom_path.txt.",
                         "Make sure the file exists in the launcher folder.", NULL);
            sceKernelExitGame();
            return 0;
        }
    }

    /* --- Validate ROM file --- */
    if (!path_is_file(rom_path)) {
        error_screen("ROM file not found.",
                     "Check the path in rom_path.txt.", rom_path);
        sceKernelExitGame();
        return 0;
    }

    /* --- Build argument block: eboot_path\0rom_path\0 --- */
    char arg_buf[MAX_PATH * 2];
    int len0 = snprintf(arg_buf, sizeof(arg_buf), "%s", emu_boot_file);
    arg_buf[len0] = '\0';
    int len1 = snprintf(arg_buf + len0 + 1, sizeof(arg_buf) - len0 - 1, "%s", rom_path);
    arg_buf[len0 + 1 + len1] = '\0';

    /* --- Launch --- */
    if (is_cfw()) {
        struct SceKernelLoadExecVSHParam param;
        memset(&param, 0, sizeof(param));
        param.size = sizeof(param);
        param.key  = "game";
        param.args = (len0 + 1) + (len1 + 1);
        param.argp = arg_buf;

        ret = sctrlKernelLoadExecVSHWithApitype(
                  PSP_INIT_APITYPE_MS2,
                  emu_boot_file,
                  &param);
    } else {
        struct SceKernelLoadExecParam param;
        memset(&param, 0, sizeof(param));
        param.size = sizeof(param);
        param.args = (len0 + 1) + (len1 + 1);
        param.argp = arg_buf;

        ret = sceKernelLoadExec(emu_boot_file, &param);
    }

    /* If we reach here, the launch failed */
    {
        char err[128];
        snprintf(err, sizeof(err), "Launch failed (0x%08X).", (unsigned)ret);
        error_screen(err, "Check that the emulator boot file is valid.", emu_boot_file);
    }

    sceKernelExitGame();
    return 0;
}
