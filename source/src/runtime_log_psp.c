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

#include "runtime_log_psp.h"

#include <stdio.h>
#include <string.h>

#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psppower.h>

#include "common.h"
#include "ku_bridge.h"
#include "runtime_log_store.h"

#define RUNTIME_LOG_SESSION_COUNT 4u

typedef struct
{
  RuntimeLogRing main_ring;
  RuntimeLogRing network_ring;
  RuntimeLogRing critical_ring;
  RuntimeLogMetadata metadata;
  char directory[MAX_PATH];
  char device[8];
  char slots[RUNTIME_LOG_SESSION_COUNT][MAX_PATH];
  char temporary[MAX_PATH];
  char backup[MAX_PATH];
  uint8_t encoded[RUNTIME_LOG_MAX_FILE_SIZE];
  int initialized;
  int storage_available;
} RuntimeLogPsp;

static RuntimeLogPsp runtime_log_psp;

static int runtime_log_psp_exists(void *context, const char *path)
{
  SceIoStat status;

  (void)context;
  return sceIoGetstat(path, &status) >= 0 && FIO_S_ISREG(status.st_mode);
}

static int runtime_log_psp_write(void *context, const char *path,
 const uint8_t *data, size_t size)
{
  SceUID file;
  size_t written = 0;
  int result;

  (void)context;
  file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0)
    return 0;
  while(written < size)
  {
    result = sceIoWrite(file, data + written,
     (SceSize)(size - written));
    if(result <= 0)
    {
      sceIoClose(file);
      return 0;
    }
    written += (size_t)result;
  }
  return sceIoClose(file) >= 0;
}

static int runtime_log_psp_remove(void *context, const char *path)
{
  (void)context;
  return sceIoRemove(path) >= 0;
}

static int runtime_log_psp_rename(void *context, const char *source,
 const char *target)
{
  (void)context;
  return sceIoRename(source, target) >= 0;
}

static RuntimeLogStoreIo runtime_log_psp_io(void)
{
  RuntimeLogStoreIo io;

  memset(&io, 0, sizeof(io));
  io.exists = runtime_log_psp_exists;
  io.write = runtime_log_psp_write;
  io.remove = runtime_log_psp_remove;
  io.rename = runtime_log_psp_rename;
  return io;
}

static int runtime_log_psp_paths(const char *base_path)
{
  const char *separator;
  size_t device_length;
  unsigned int index;
  int length;

  separator = strchr(base_path, ':');
  if(separator != NULL)
  {
    device_length = (size_t)(separator - base_path) + 1u;
    if(device_length < sizeof(runtime_log_psp.device))
    {
      memcpy(runtime_log_psp.device, base_path, device_length);
      runtime_log_psp.device[device_length] = '\0';
    }
  }

  length = snprintf(runtime_log_psp.directory,
   sizeof(runtime_log_psp.directory), "%slogs", base_path);
  if(length < 0 || (size_t)length >= sizeof(runtime_log_psp.directory))
    return 0;
  for(index = 0; index < RUNTIME_LOG_SESSION_COUNT; index++)
  {
    length = snprintf(runtime_log_psp.slots[index],
     sizeof(runtime_log_psp.slots[index]), "%s/session-%u.gbrlog",
     runtime_log_psp.directory, index + 1u);
    if(length < 0 || (size_t)length >=
       sizeof(runtime_log_psp.slots[index]))
      return 0;
  }
  length = snprintf(runtime_log_psp.temporary,
   sizeof(runtime_log_psp.temporary), "%s/session.tmp",
   runtime_log_psp.directory);
  if(length < 0 || (size_t)length >= sizeof(runtime_log_psp.temporary))
    return 0;
  length = snprintf(runtime_log_psp.backup,
   sizeof(runtime_log_psp.backup), "%s/session.bak",
   runtime_log_psp.directory);
  return length >= 0 && (size_t)length < sizeof(runtime_log_psp.backup);
}

int runtime_log_psp_init(const char *base_path)
{
  RuntimeLogStoreIo io;
  const char *slots[RUNTIME_LOG_SESSION_COUNT];
  SceIoStat status;
  uint64_t now;
  unsigned int index;
  int model;

  memset(&runtime_log_psp, 0, sizeof(runtime_log_psp));
  runtime_log_ring_init(&runtime_log_psp.main_ring,
   RUNTIME_LOG_SOURCE_MAIN);
  runtime_log_ring_init(&runtime_log_psp.network_ring,
   RUNTIME_LOG_SOURCE_NETWORK);
  runtime_log_ring_init(&runtime_log_psp.critical_ring,
   RUNTIME_LOG_SOURCE_CRITICAL);
  now = sceKernelGetSystemTimeWide();
  runtime_log_psp.metadata.session_id =
   (uint32_t)(now ^ (now >> 32));
  if(runtime_log_psp.metadata.session_id == 0)
    runtime_log_psp.metadata.session_id = 1;
  model = kuKernelGetModel();
  runtime_log_psp.metadata.psp_model = model < 0 ? UINT32_MAX :
   (uint32_t)model;
  runtime_log_psp.metadata.firmware = (uint32_t)sceKernelDevkitVersion();
  runtime_log_psp.metadata.cpu_clock_mhz =
   (uint32_t)scePowerGetCpuClockFrequencyInt();
  runtime_log_psp.metadata.started_us = now;
  runtime_log_psp.initialized = 1;

  runtime_log_psp_main(RUNTIME_LOG_EVENT_SESSION_START, 0, 0,
   runtime_log_psp.metadata.psp_model, runtime_log_psp.metadata.firmware,
   runtime_log_psp.metadata.cpu_clock_mhz);
  if(base_path == NULL || !runtime_log_psp_paths(base_path))
    return 1;

  if(sceIoGetstat(runtime_log_psp.directory, &status) < 0)
  {
    if(sceIoMkdir(runtime_log_psp.directory, 0777) < 0)
      return 1;
  }
  else if(!FIO_S_ISDIR(status.st_mode))
  {
    return 1;
  }

  io = runtime_log_psp_io();
  for(index = 0; index < RUNTIME_LOG_SESSION_COUNT; index++)
    slots[index] = runtime_log_psp.slots[index];
  if(runtime_log_store_recover(&io, runtime_log_psp.slots[0],
     runtime_log_psp.temporary, runtime_log_psp.backup))
    runtime_log_store_rotate(&io, slots, RUNTIME_LOG_SESSION_COUNT);
  runtime_log_psp.storage_available = 1;
  runtime_log_psp_flush(RUNTIME_LOG_FLUSH_START);
  return 1;
}

void runtime_log_psp_set_rom(const uint8_t *game_code, uint8_t revision,
 uint32_t rom_size, uint32_t emulated_cycle)
{
  uint32_t identity = 0;

  if(game_code != NULL)
  {
    identity = ((uint32_t)game_code[0] << 24) |
     ((uint32_t)game_code[1] << 16) | ((uint32_t)game_code[2] << 8) |
     game_code[3];
  }
  runtime_log_psp.metadata.rom_identity = identity;
  runtime_log_psp_main(RUNTIME_LOG_EVENT_ROM_OPEN, emulated_cycle,
   revision, identity, rom_size, 0);
}

void runtime_log_psp_main(RuntimeLogEventType type, uint32_t emulated_cycle,
 uint8_t flags, uint32_t value0, uint32_t value1, uint32_t value2)
{
  if(!runtime_log_psp.initialized)
    return;
  runtime_log_ring_emit(&runtime_log_psp.main_ring,
   sceKernelGetSystemTimeWide(), emulated_cycle, type, flags,
   value0, value1, value2);
}

void runtime_log_psp_network(RuntimeLogEventType type, uint8_t flags,
 uint32_t value0, uint32_t value1, uint32_t value2)
{
  if(!runtime_log_psp.initialized)
    return;
  runtime_log_ring_emit(&runtime_log_psp.network_ring,
   sceKernelGetSystemTimeWide(), 0, type, flags, value0, value1, value2);
}

void runtime_log_psp_critical(RuntimeLogEventType type,
 uint32_t emulated_cycle, uint8_t flags, uint32_t value0,
 uint32_t value1, uint32_t value2)
{
  if(!runtime_log_psp.initialized)
    return;
  runtime_log_ring_emit(&runtime_log_psp.critical_ring,
   sceKernelGetSystemTimeWide(), emulated_cycle, type, flags,
   value0, value1, value2);
}

void runtime_log_psp_get_drop_counts(uint32_t *main_dropped,
 uint32_t *network_dropped, uint32_t *critical_dropped)
{
  if(main_dropped != NULL)
    *main_dropped = runtime_log_ring_dropped(&runtime_log_psp.main_ring);
  if(network_dropped != NULL)
    *network_dropped = runtime_log_ring_dropped(
     &runtime_log_psp.network_ring);
  if(critical_dropped != NULL)
    *critical_dropped = runtime_log_ring_dropped(
     &runtime_log_psp.critical_ring);
}

int runtime_log_psp_flush(RuntimeLogFlushReason reason)
{
  RuntimeLogStoreIo io;
  size_t encoded_size = 0;
  int saved;

  if(!runtime_log_psp.initialized || !runtime_log_psp.storage_available)
    return 0;
  runtime_log_psp.metadata.flush_reason = (uint32_t)reason;
  runtime_log_psp.metadata.flushed_us = sceKernelGetSystemTimeWide();
  runtime_log_psp.metadata.cpu_clock_mhz =
   (uint32_t)scePowerGetCpuClockFrequencyInt();
  runtime_log_psp_main(RUNTIME_LOG_EVENT_LOG_FLUSH, 0, 0,
   reason, runtime_log_psp.main_ring.next_ordinal,
   runtime_log_psp.network_ring.next_ordinal);
  if(!runtime_log_encode(&runtime_log_psp.main_ring,
     &runtime_log_psp.network_ring, &runtime_log_psp.critical_ring,
     &runtime_log_psp.metadata, runtime_log_psp.encoded,
     sizeof(runtime_log_psp.encoded), &encoded_size))
    return 0;

  io = runtime_log_psp_io();
  saved = runtime_log_store_save(&io, runtime_log_psp.slots[0],
   runtime_log_psp.temporary, runtime_log_psp.backup,
   runtime_log_psp.encoded, encoded_size);
  if(!saved)
    return 0;
  return runtime_log_psp.device[0] == '\0' ||
   sceIoSync(runtime_log_psp.device, 0) >= 0;
}
