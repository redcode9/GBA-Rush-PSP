/*
 * GBA Rush PSP
 *
 * Copyright (C) 2026 Piero Carrieri
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/*
 * media_engine.prx: the kernel module that hands the PSP's second
 * processor, the Media Engine, to the Exact renderer. Loaded next to the
 * EBOOT on custom firmware only (source/src/video_me.c), it is started with
 * one argument, the uncached address of the write log's mailbox.
 *
 * Starting the Media Engine takes the kernel: boot.S goes to its reset
 * address in the boot page at 0xBFC00040, then its reset line is pulsed
 * (bit 2 of 0xBC10004C, with its bus clock, bit 0 of 0xBC100050, on). That
 * ends Sony's Media Engine firmware, which GBA Rush does not use. The
 * firmware's driver still answers system events for it, though, and on a
 * suspend it would ask the gone firmware to save itself and wait forever;
 * so this module takes over that driver's event handler ("SceMeRpc") for
 * the rest of the session. On suspend it holds the Media Engine in reset
 * and stops its clock, as Sony's driver does; on resume it boots the
 * program again, which starts with empty memory and asks the emulator to
 * send everything again. Leaving the game reboots the kernel, which loads
 * Sony's firmware into the Media Engine afresh; before that reboot, and
 * when the module is stopped, the Media Engine is held in reset so it
 * writes nothing into memory that is no longer the emulator's.
 *
 * The emulator also calls media_engine_wake from user mode, through the
 * library this module exports (source/src/media_engine_stub.S), to end the
 * program's halt when it sleeps between batches of work, and
 * media_engine_publish to move on the copy of the log's write count the
 * program can poll in the boot page instead of the mailbox in main memory.
 * The boot page is the one memory both processors reach outside main
 * memory: the Media Engine's eDRAM has no address on the main processor,
 * and the boot page has none in user mode (it is in kseg1).
 *
 * What is known of this hardware comes from Sony's own driver as uOFW
 * describes it and from mcidclan's Media Engine research.
 */

#include <pspkernel.h>
#include <pspsdk.h>
#include <pspsysevent.h>

/* kernel | single load | single start */
PSP_MODULE_INFO("GBARushMediaEngine", 0x1006, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();

#define BOOT_ADDRESS      0xBFC00040u
#define SYSREG_RESET      (*(volatile u32 *)0xBC10004Cu)
#define SYSREG_RAM_CONFIG (*(volatile u32 *)0xBC100040u)
#define SYSREG_BUS_CLOCK  (*(volatile u32 *)0xBC100050u)
/* Written by one processor, interrupts the other one. */
#define SYSREG_INTERRUPT_OTHER (*(volatile u32 *)0xBC100044u)
#define RESET_MEDIA_ENGINE     0x04u
#define BUS_CLOCK_MEDIA_ENGINE 0x01u
/* Where renderer.ld places the program, in the Media Engine's eDRAM. */
#define PROGRAM_ADDRESS   0x80010000u

/* System events, as Sony's driver tells them apart. */
#define EVENT_SUSPEND 0x00004005
#define EVENT_RESUME  0x00010005

enum
{
  PARAMETER_RAM_CONFIG,
  PARAMETER_SOURCE,
  PARAMETER_DESTINATION,
  PARAMETER_WORDS,
  PARAMETER_ENTRY,
  PARAMETER_ARGUMENT,
  PARAMETER_WRITTEN
};

int module_start(SceSize args, void *argp);
int module_stop(SceSize args, void *argp);
int module_reboot_before(SceSize args, void *argp);
void media_engine_wake(void);
void media_engine_publish(u32 write);

/* boot.S, and the program built from renderer.c (program.S). */
extern const u32 media_engine_boot[];
extern const u32 media_engine_boot_end[];
extern const u32 media_engine_boot_parameters[];
extern const u8 media_engine_program[];
extern const u8 media_engine_program_end[];

/* What the boot page held where boot.S goes, put back on stop. */
static u32 saved_boot[64];
static u32 boot_bytes;
static u32 mailbox;
/* What media_engine_publish last put in the boot page, put back after a
   resume. */
static u32 written;
static PspSysEventHandler *driver;
static int running;

/* Writes the whole data cache back, by index: the program and the boot
   code must be in memory before the Media Engine reads them. */
static void write_back_data_cache(void)
{
  u32 index;

  for(index = 0; index < 0x4000; index += 64)
    __asm__ volatile("cache 0x14, 0(%0)" :: "r"(0x80000000u + index));
  __asm__ volatile("sync");
}

static void hold(void)
{
  int interrupts = pspSdkDisableInterrupts();

  SYSREG_RESET |= RESET_MEDIA_ENGINE;
  __asm__ volatile("sync");
  SYSREG_BUS_CLOCK &= ~BUS_CLOCK_MEDIA_ENGINE;
  __asm__ volatile("sync");
  pspSdkEnableInterrupts(interrupts);
}

static void boot(void)
{
  volatile u32 *page = (volatile u32 *)BOOT_ADDRESS;
  u32 parameter = media_engine_boot_parameters - media_engine_boot;
  u32 program_bytes = media_engine_program_end - media_engine_program;
  int interrupts;
  u32 i;

  hold();
  for(i = 0; i < boot_bytes / 4; i++)
    page[i] = media_engine_boot[i];
  page[parameter + PARAMETER_RAM_CONFIG] = SYSREG_RAM_CONFIG & 3;
  page[parameter + PARAMETER_SOURCE] = (u32)media_engine_program;
  page[parameter + PARAMETER_DESTINATION] = PROGRAM_ADDRESS;
  page[parameter + PARAMETER_WORDS] = (program_bytes + 3) / 4;
  page[parameter + PARAMETER_ENTRY] = PROGRAM_ADDRESS;
  page[parameter + PARAMETER_ARGUMENT] = mailbox;
  page[parameter + PARAMETER_WRITTEN] = written;
  write_back_data_cache();

  interrupts = pspSdkDisableInterrupts();
  SYSREG_BUS_CLOCK |= BUS_CLOCK_MEDIA_ENGINE;
  __asm__ volatile("sync");
  SYSREG_RESET &= ~RESET_MEDIA_ENGINE;
  __asm__ volatile("sync");
  pspSdkEnableInterrupts(interrupts);
}

/* Stands in for Sony's driver from module_start on. */
static int on_event(int id, char *name, void *param, int *result)
{
  (void)name;
  (void)param;
  (void)result;
  if(!running)
    return 0;
  if(id == EVENT_SUSPEND)
    hold();
  else if(id == EVENT_RESUME)
    boot();
  return 0;
}

/* A kernel address of main memory (32 MiB, or 64 on later models). An
   emulator that lacks sceKernelReferSysEventHandler returns an error code
   instead, such as PPSSPP's 0x8002013A for an import it cannot resolve
   (SCE_KERNEL_ERROR_LIBRARY_NOT_YET_LINKED): the chain is followed only
   through such addresses. */
static int in_kernel_memory(const void *address, u32 alignment)
{
  u32 value = (u32)address;

  return (value & 0x80000000u) && (value & 0x1FFFFFFFu) >= 0x08000000u &&
   (value & 0x1FFFFFFFu) < 0x0C000000u && (value & (alignment - 1)) == 0;
}

static int is_driver(const char *name)
{
  static const char wanted[] = "SceMeRpc";
  u32 i;

  if(!in_kernel_memory(name, 1))
    return 0;
  for(i = 0; i < sizeof(wanted); i++)
    if(name[i] != wanted[i])
      return 0;
  return 1;
}

/* argp: the mailbox's uncached address. Fails, touching nothing, where
   Sony's Media Engine driver is not found, as in an emulator. */
int module_start(SceSize args, void *argp)
{
  PspSysEventHandler *handler;
  int interrupts;
  u32 i;

  if(args < 4 || !argp || running)
    return -1;
  for(handler = sceKernelReferSysEventHandler(), i = 0;
   in_kernel_memory(handler, 4) && i < 256; handler = handler->next, i++)
    if(is_driver(handler->name))
      break;
  if(!in_kernel_memory(handler, 4) || i == 256)
    return -2;
  boot_bytes = (u32)(media_engine_boot_end - media_engine_boot) * 4;
  if(boot_bytes > sizeof(saved_boot))
    return -3;

  mailbox = *(u32 *)argp;
  driver = handler;
  for(i = 0; i < boot_bytes / 4; i++)
    saved_boot[i] = ((volatile u32 *)BOOT_ADDRESS)[i];
  interrupts = pspSdkDisableInterrupts();
  driver->handler = on_event;
  running = 1;
  pspSdkEnableInterrupts(interrupts);
  boot();
  return 0;
}

/* Holds the Media Engine in reset and puts the boot page back. The event
   handler stays, doing nothing: Sony's would wait for the firmware that
   is gone. */
int module_stop(SceSize args, void *argp)
{
  u32 i;

  (void)args;
  (void)argp;
  if(!running)
    return 0;
  running = 0;
  hold();
  for(i = 0; i < boot_bytes / 4; i++)
    ((volatile u32 *)BOOT_ADDRESS)[i] = saved_boot[i];
  return 0;
}

/* Interrupts the Media Engine, which ends its halt; nothing when it is
   not running. */
void media_engine_wake(void)
{
  if(!running)
    return;
  SYSREG_INTERRUPT_OTHER = 1;
  __asm__ volatile("sync");
}

/* The emulator published the log up to write: the copy in the boot page
   follows, once the mailbox's own count, stored before this call, is in
   memory. Nothing when the Media Engine is not running. */
void media_engine_publish(u32 write)
{
  volatile u32 *page = (volatile u32 *)BOOT_ADDRESS;

  if(!running)
    return;
  written = write;
  __asm__ volatile("sync");
  page[media_engine_boot_parameters - media_engine_boot +
   PARAMETER_WRITTEN] = write;
  __asm__ volatile("sync");
}

/* Leaving the game: the kernel reboots and loads Sony's firmware into the
   Media Engine again; until then it must not run. */
int module_reboot_before(SceSize args, void *argp)
{
  return module_stop(args, argp);
}
