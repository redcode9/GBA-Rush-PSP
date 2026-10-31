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

#include <string.h>

#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_adhoc.h>
#include <pspnet_adhocctl.h>
#include <psputility_netmodules.h>
#include <pspwlan.h>

#include "adhoc_peer.h"
#include "psp_adhoc.h"

#define PSP_ADHOC_HOST_PORT        31000u
#define PSP_ADHOC_JOIN_PORT        31001u
#define PSP_ADHOC_SOCKET_BUFFER    32768u
#define PSP_ADHOC_SCAN_CAPACITY       16u
#define PSP_ADHOC_PEER_CAPACITY        4u
#define PSP_ADHOC_RING                32u
#define PSP_ADHOC_RECEIVE_BATCH       32u
#define PSP_ADHOC_POLL_US           1000u
#define PSP_ADHOC_CONTROL_DELAY_US 10000u
#define PSP_ADHOC_CONTROL_TIMEOUT_US 10000000u
#define PSP_ADHOC_PEER_TIMEOUT_US    6000000u
#define PSP_ADHOC_BUSY_RETRY_US       750000u
#define PSP_ADHOC_SCAN_WAIT_US       1500000u
#define PSP_ADHOC_HEALTH_INTERVAL_US  250000u
#define PSP_ADHOC_HEALTH_GRACE_US    1500000u
#define PSP_ADHOC_STOP_WAIT_US       4000000u
#define PSP_ADHOC_EXIT_WAIT_US        250000u

/* The PSP scheduler is strictly preemptive and never time-slices threads
   of equal priority. The network stack and this worker must outrank the
   emulator thread (0x20), which runs flat out while it catches up, or
   reception stops during gameplay. Lower numbers run first. */
#define PSP_ADHOC_STACK_PRIORITY   0x1C
#define PSP_ADHOC_CONTROL_PRIORITY 0x1D
#define PSP_ADHOC_WORKER_PRIORITY  0x1E


#define SCE_NET_ERROR_NO_SPACE          ((int)0x80410001u)
#define SCE_ADHOC_ERROR_WOULD_BLOCK     ((int)0x80410709u)
#define SCE_ADHOC_ERROR_TIMEOUT         ((int)0x80410715u)
#define SCE_ADHOCCTL_ERROR_NO_SCAN_INFO ((int)0x80410b0au)
#define SCE_ADHOCCTL_ERROR_BUSY         ((int)0x80410b10u)

/* An adhocctl state of 1 means this PSP is in a group. */
#define PSP_ADHOCCTL_STATE_CONNECTED 1

static const char psp_adhoc_group[8] =
 { 'G', 'B', 'A', 'R', 'U', 'S', 'H', '\0' };

typedef struct
{
  uint16_t size;
  uint8_t data[PSP_ADHOC_MAX_DATAGRAM];
} PspAdhocDatagram;

/* Single producer, single consumer queue between the two threads. */
typedef struct
{
  PspAdhocDatagram slots[PSP_ADHOC_RING];
  volatile uint32_t read;
  volatile uint32_t write;
} PspAdhocRing;

typedef struct
{
  volatile PspAdhocState state;
  volatile int stop_requested;
  volatile int last_error;
  volatile PspAdhocPhase phase;
  volatile PspAdhocPhase failure_phase;
  int host;
  SceUID thread_id;
  /* Wakes the worker for outgoing data, and the emulator for incoming. */
  SceUID wake_send;
  SceUID wake_receive;
  int module_common;
  int module_adhoc;
  int net_initialized;
  int adhoc_initialized;
  int control_initialized;
  int group_joined;
  int socket_id;
  unsigned char local_mac[ADHOC_PEER_MAC_SIZE];
  unsigned char peer_mac[ADHOC_PEER_MAC_SIZE];
  unsigned short peer_port;
  int peer_valid;
  PspAdhocRing outgoing;
  PspAdhocRing incoming;
  volatile uint32_t sends;
  volatile uint32_t send_errors;
  volatile uint32_t receive_drops;
} PspAdhocContext;

static PspAdhocContext psp_adhoc =
{
  .state = PSP_ADHOC_STOPPED,
  .thread_id = -1,
  .wake_send = -1,
  .wake_receive = -1,
  .socket_id = -1
};

/* Producer side: 0 for an empty or oversized datagram or a full ring. The
   barrier makes the slot visible before the new write index. */
static int ring_push(PspAdhocRing *ring, const uint8_t *data, size_t size)
{
  uint32_t write = ring->write;
  PspAdhocDatagram *slot;

  if(size == 0 || size > PSP_ADHOC_MAX_DATAGRAM ||
     write - ring->read >= PSP_ADHOC_RING)
    return 0;
  slot = &ring->slots[write % PSP_ADHOC_RING];
  memcpy(slot->data, data, size);
  slot->size = (uint16_t)size;
  __sync_synchronize();
  ring->write = write + 1;
  return 1;
}

/* A datagram larger than capacity is discarded and returns 0, the same
   as an empty ring. */
static size_t ring_pop(PspAdhocRing *ring, uint8_t *data, size_t capacity)
{
  uint32_t read = ring->read;
  const PspAdhocDatagram *slot;
  size_t size;

  if(read == ring->write)
    return 0;
  __sync_synchronize();
  slot = &ring->slots[read % PSP_ADHOC_RING];
  size = slot->size <= capacity ? slot->size : 0;
  if(size != 0)
    memcpy(data, slot->data, size);
  __sync_synchronize();
  ring->read = read + 1;
  return size;
}

static void psp_adhoc_wake(SceUID flag)
{
  if(flag >= 0)
    sceKernelSetEventFlag(flag, 1);
}

/* Waits up to timeout_us for psp_adhoc_wake() on flag; a plain delay when
   the flag was never created. */
static void psp_adhoc_sleep(SceUID flag, unsigned int timeout_us)
{
  SceUInt timeout = timeout_us;
  u32 matched = 0;

  if(flag < 0)
  {
    sceKernelDelayThread(timeout_us);
    return;
  }
  sceKernelWaitEventFlag(flag, 1, PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR,
   &matched, &timeout);
}

/* Deletes both wake flags and marks them -1, which turns later wakes into
   no-ops and sleeps into plain delays. */
static void psp_adhoc_delete_flags(PspAdhocContext *context)
{
  if(context->wake_send >= 0)
    sceKernelDeleteEventFlag(context->wake_send);
  if(context->wake_receive >= 0)
    sceKernelDeleteEventFlag(context->wake_receive);
  context->wake_send = -1;
  context->wake_receive = -1;
}

/* Enters FAILED, keeping the error (-1 when the call returned 0) and the
   phase it failed in for the menu and log; returns -1. */
static int psp_adhoc_fail(PspAdhocContext *context, int error)
{
  context->last_error = error != 0 ? error : -1;
  context->failure_phase = context->phase;
  context->state = PSP_ADHOC_FAILED;
  return -1;
}

static int psp_adhoc_stopping(const PspAdhocContext *context)
{
  return context->stop_requested;
}

/* ---- Bring-up --------------------------------------------------------- */

static int psp_adhoc_network_init(PspAdhocContext *context)
{
  struct productStruct product;
  int result;

  context->phase = PSP_ADHOC_PHASE_LOAD_MODULES;
  result = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
  if(result < 0)
    return psp_adhoc_fail(context, result);
  context->module_common = 1;
  result = sceUtilityLoadNetModule(PSP_NET_MODULE_ADHOC);
  if(result < 0)
    return psp_adhoc_fail(context, result);
  context->module_adhoc = 1;

  /* 128 KB pool; callout and interrupt threads with 4 KB stacks. */
  context->phase = PSP_ADHOC_PHASE_NET_INIT;
  result = sceNetInit(128 * 1024, PSP_ADHOC_STACK_PRIORITY, 4 * 1024,
   PSP_ADHOC_STACK_PRIORITY, 4 * 1024);
  if(result < 0)
    return psp_adhoc_fail(context, result);
  context->net_initialized = 1;

  context->phase = PSP_ADHOC_PHASE_ADHOC_INIT;
  result = sceNetAdhocInit();
  if(result < 0)
    return psp_adhoc_fail(context, result);
  context->adhoc_initialized = 1;

  memset(&product, 0, sizeof(product));
  memcpy(product.product, "GBARUSH01", sizeof(product.product));
  context->phase = PSP_ADHOC_PHASE_CONTROL_INIT;
  result = sceNetAdhocctlInit(32 * 1024, PSP_ADHOC_CONTROL_PRIORITY,
   &product);
  if(result < 0)
    return psp_adhoc_fail(context, result);
  context->control_initialized = 1;
  return 0;
}

/* Undoes psp_adhoc_network_init() in reverse, socket and group first;
   only the steps that succeeded are undone, so a partial bring-up is safe. */
static void psp_adhoc_network_term(PspAdhocContext *context)
{
  if(context->socket_id >= 0)
  {
    sceNetAdhocPdpDelete(context->socket_id, 0);
    context->socket_id = -1;
  }
  if(context->group_joined)
  {
    sceNetAdhocctlDisconnect();
    context->group_joined = 0;
  }
  if(context->control_initialized)
    sceNetAdhocctlTerm();
  if(context->adhoc_initialized)
    sceNetAdhocTerm();
  if(context->net_initialized)
    sceNetTerm();
  if(context->module_adhoc)
    sceUtilityUnloadNetModule(PSP_NET_MODULE_ADHOC);
  if(context->module_common)
    sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
  context->control_initialized = 0;
  context->adhoc_initialized = 0;
  context->net_initialized = 0;
  context->module_adhoc = 0;
  context->module_common = 0;
}

/* Waits until this PSP is in the group. 1 = ready, 0 = stopped, -1 = error. */
static int psp_adhoc_wait_group(PspAdhocContext *context)
{
  uint64_t started = sceKernelGetSystemTimeWide();
  int state;
  int result;

  context->phase = PSP_ADHOC_PHASE_GROUP_WAIT;
  while(sceKernelGetSystemTimeWide() - started < PSP_ADHOC_CONTROL_TIMEOUT_US)
  {
    if(psp_adhoc_stopping(context))
      return 0;
    if(!sceWlanGetSwitchState())
    {
      context->phase = PSP_ADHOC_PHASE_WLAN;
      return psp_adhoc_fail(context, -1);
    }
    state = 0;
    result = sceNetAdhocctlGetState(&state);
    if(result < 0)
      return psp_adhoc_fail(context, result);
    if(state == PSP_ADHOCCTL_STATE_CONNECTED)
      return 1;
    sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY_US);
  }
  return psp_adhoc_fail(context, SCE_ADHOC_ERROR_TIMEOUT);
}

/* Opens the PDP (datagram) socket on this PSP's MAC and the given port;
   1 on success, -1 with the failure recorded. */
static int psp_adhoc_open_socket(PspAdhocContext *context,
 unsigned short port)
{
  int result;

  context->phase = PSP_ADHOC_PHASE_SOCKET;
  result = sceWlanGetEtherAddr(context->local_mac);
  if(result < 0)
    return psp_adhoc_fail(context, result);
  context->socket_id = sceNetAdhocPdpCreate(context->local_mac, port,
   PSP_ADHOC_SOCKET_BUFFER, 0);
  if(context->socket_id < 0)
  {
    result = context->socket_id;
    context->socket_id = -1;
    return psp_adhoc_fail(context, result);
  }
  return 1;
}

/* Retries an adhocctl call while the library reports it is busy. */
#define PSP_ADHOC_RETRY_BUSY(context, call, result) \
  do \
  { \
    uint64_t retry_started = sceKernelGetSystemTimeWide(); \
    for(;;) \
    { \
      (result) = (call); \
      if((result) != SCE_ADHOCCTL_ERROR_BUSY || \
         psp_adhoc_stopping(context) || \
         sceKernelGetSystemTimeWide() - retry_started >= \
          PSP_ADHOC_BUSY_RETRY_US) \
        break; \
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY_US); \
    } \
  } while(0)

/* Creates the group and opens the host socket. 1 = ready, 0 = stopped,
   -1 = error. */
static int psp_adhoc_host(PspAdhocContext *context)
{
  int result;

  context->state = PSP_ADHOC_HOSTING;
  context->phase = PSP_ADHOC_PHASE_CREATE_GROUP;
  PSP_ADHOC_RETRY_BUSY(context, sceNetAdhocctlCreate(psp_adhoc_group),
   result);
  if(result < 0)
    return psp_adhoc_fail(context, result);
  context->group_joined = 1;

  result = psp_adhoc_wait_group(context);
  if(result <= 0)
    return result;
  /* The joiner's address is learned from its first datagram. */
  return psp_adhoc_open_socket(context, PSP_ADHOC_HOST_PORT);
}

/* Finds the GBARUSH group. 1 = found, 2 = not yet, 0 = stopped, -1 = error. */
static int psp_adhoc_scan(PspAdhocContext *context,
 struct SceNetAdhocctlScanInfo *selected)
{
  struct SceNetAdhocctlScanInfo scans[PSP_ADHOC_SCAN_CAPACITY];
  uint64_t started;
  int length;
  int count;
  int state;
  int result;
  int index;

  context->phase = PSP_ADHOC_PHASE_SCAN;
  PSP_ADHOC_RETRY_BUSY(context, sceNetAdhocctlScan(), result);
  if(result < 0)
    return psp_adhoc_fail(context, result);

  /* A scan reports state 2 until it finishes; wait at least 250 ms. */
  started = sceKernelGetSystemTimeWide();
  while(sceKernelGetSystemTimeWide() - started < PSP_ADHOC_SCAN_WAIT_US)
  {
    if(psp_adhoc_stopping(context))
      return 0;
    state = 0;
    result = sceNetAdhocctlGetState(&state);
    if(result < 0)
      return psp_adhoc_fail(context, result);
    if(state != 2 && sceKernelGetSystemTimeWide() - started >= 250000u)
      break;
    sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY_US);
  }

  memset(scans, 0, sizeof(scans));
  length = sizeof(scans);
  result = sceNetAdhocctlGetScanInfo(&length, scans);
  if(result == SCE_ADHOCCTL_ERROR_BUSY ||
     result == SCE_ADHOCCTL_ERROR_NO_SCAN_INFO)
    return 2;
  if(result < 0)
    return psp_adhoc_fail(context, result);

  count = length / (int)sizeof(scans[0]);
  if(count > (int)PSP_ADHOC_SCAN_CAPACITY)
    count = PSP_ADHOC_SCAN_CAPACITY;
  for(index = 0; index < count; index++)
  {
    if(memcmp(scans[index].name, psp_adhoc_group,
       sizeof(scans[index].name)) == 0)
    {
      *selected = scans[index];
      selected->next = NULL;
      return 1;
    }
  }
  return 2;
}

/* Picks the host among the group's members, preferring the group owner. */
static int psp_adhoc_find_host(PspAdhocContext *context,
 const unsigned char *preferred_mac)
{
  struct SceNetAdhocctlPeerInfo peers[PSP_ADHOC_PEER_CAPACITY];
  uint8_t peer_macs[PSP_ADHOC_PEER_CAPACITY][ADHOC_PEER_MAC_SIZE];
  uint64_t started = sceKernelGetSystemTimeWide();
  int length;
  int count;
  int result;
  int index;

  context->phase = PSP_ADHOC_PHASE_PEER_LIST;
  while(sceKernelGetSystemTimeWide() - started < PSP_ADHOC_PEER_TIMEOUT_US)
  {
    if(psp_adhoc_stopping(context))
      return 0;
    memset(peers, 0, sizeof(peers));
    length = sizeof(peers);
    result = sceNetAdhocctlGetPeerList(&length, peers);
    if(result < 0 && result != SCE_ADHOCCTL_ERROR_BUSY)
      return psp_adhoc_fail(context, result);
    if(result >= 0)
    {
      count = length / (int)sizeof(peers[0]);
      if(count > (int)PSP_ADHOC_PEER_CAPACITY)
        count = PSP_ADHOC_PEER_CAPACITY;
      for(index = 0; index < count; index++)
        memcpy(peer_macs[index], peers[index].mac, ADHOC_PEER_MAC_SIZE);
      if(adhoc_peer_select(context->local_mac, preferred_mac,
         &peer_macs[0][0], (size_t)count, context->peer_mac))
        return 1;
    }
    sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY_US);
  }
  return psp_adhoc_fail(context, SCE_ADHOC_ERROR_TIMEOUT);
}

/* Finds and joins the group, then the host's address. 1 = ready,
   0 = stopped, -1 = error. */
static int psp_adhoc_join(PspAdhocContext *context)
{
  struct SceNetAdhocctlScanInfo selected;
  int result;

  context->state = PSP_ADHOC_SEARCHING;
  do
  {
    result = psp_adhoc_scan(context, &selected);
    if(result <= 0)
      return result;
    if(result == 2)
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY_US);
  }
  while(result != 1);

  context->state = PSP_ADHOC_CONNECTING;
  context->phase = PSP_ADHOC_PHASE_JOIN_GROUP;
  PSP_ADHOC_RETRY_BUSY(context, sceNetAdhocctlJoin(&selected), result);
  if(result < 0)
    return psp_adhoc_fail(context, result);
  context->group_joined = 1;

  result = psp_adhoc_wait_group(context);
  if(result <= 0)
    return result;
  result = psp_adhoc_open_socket(context, PSP_ADHOC_JOIN_PORT);
  if(result <= 0)
    return result;
  result = psp_adhoc_find_host(context, selected.bssid);
  if(result <= 0)
    return result;
  context->peer_port = PSP_ADHOC_HOST_PORT;
  context->peer_valid = 1;
  return 1;
}

/* ---- Connected -------------------------------------------------------- */

static int psp_adhoc_receive_all(PspAdhocContext *context)
{
  uint8_t data[PSP_ADHOC_MAX_DATAGRAM];
  unsigned char source_mac[ADHOC_PEER_MAC_SIZE];
  unsigned short source_port;
  unsigned int batch;
  int delivered = 0;

  context->phase = PSP_ADHOC_PHASE_RECEIVE;
  for(batch = 0; batch < PSP_ADHOC_RECEIVE_BATCH; batch++)
  {
    int length = sizeof(data);
    int result;

    source_port = 0;
    result = sceNetAdhocPdpRecv(context->socket_id, source_mac,
     &source_port, data, &length, 0, 1);
    if(result == SCE_ADHOC_ERROR_WOULD_BLOCK ||
       result == SCE_ADHOC_ERROR_TIMEOUT || result == SCE_NET_ERROR_NO_SPACE)
      break;
    if(result < 0)
      return psp_adhoc_fail(context, result);
    /* Some firmware reports the size in the return value instead. */
    if(result > 0 && (length == (int)sizeof(data) || length == 0))
      length = result;
    if(length <= 0 || length > (int)sizeof(data))
      continue;

    if(!context->peer_valid)
    {
      /* The host pairs with the first PSP that speaks the link protocol,
         whose datagrams start with "GR". */
      if(length < 2 || data[0] != 'G' || data[1] != 'R' ||
         memcmp(source_mac, context->local_mac, ADHOC_PEER_MAC_SIZE) == 0)
        continue;
      memcpy(context->peer_mac, source_mac, ADHOC_PEER_MAC_SIZE);
      context->peer_port = source_port;
      context->peer_valid = 1;
      context->state = PSP_ADHOC_CONNECTED;
    }
    else if(memcmp(source_mac, context->peer_mac, ADHOC_PEER_MAC_SIZE) != 0 ||
            source_port != context->peer_port)
      continue;

    if(ring_push(&context->incoming, data, (size_t)length))
      delivered = 1;
    else
      context->receive_drops++;
  }
  if(delivered)
    psp_adhoc_wake(context->wake_receive);
  return 1;
}

/* Sends everything the emulator queued. Until the host has heard from a
   joiner there is no peer, and the datagrams are discarded. */
static int psp_adhoc_send_all(PspAdhocContext *context)
{
  uint8_t data[PSP_ADHOC_MAX_DATAGRAM];
  size_t size;

  context->phase = PSP_ADHOC_PHASE_SEND;
  while((size = ring_pop(&context->outgoing, data, sizeof(data))) != 0)
  {
    int result;

    if(!context->peer_valid)
      continue;
    context->sends++;
    result = sceNetAdhocPdpSend(context->socket_id, context->peer_mac,
     context->peer_port, data, (unsigned int)size, 0, 1);
    /* A busy stack drops this datagram; the next one repeats its messages.
       Never retry at once: on a PSP, sending again every millisecond made
       the stack hold datagrams for seconds and the games never linked. */
    if(result == SCE_NET_ERROR_NO_SPACE ||
       result == SCE_ADHOC_ERROR_WOULD_BLOCK ||
       result == SCE_ADHOC_ERROR_TIMEOUT)
      context->send_errors++;
    else if(result < 0)
      return psp_adhoc_fail(context, result);
  }
  return 1;
}

/* Fails at once when WLAN is switched off, or once the group has been
   lost for PSP_ADHOC_HEALTH_GRACE_US. */
static int psp_adhoc_check_health(PspAdhocContext *context,
 uint64_t *unhealthy_since)
{
  uint64_t now = sceKernelGetSystemTimeWide();
  int state = 0;
  int result;

  if(!sceWlanGetSwitchState())
  {
    context->phase = PSP_ADHOC_PHASE_WLAN;
    return psp_adhoc_fail(context, -1);
  }
  result = sceNetAdhocctlGetState(&state);
  if(result >= 0 && state == PSP_ADHOCCTL_STATE_CONNECTED)
  {
    *unhealthy_since = 0;
    return 1;
  }
  if(*unhealthy_since == 0)
    *unhealthy_since = now;
  else if(now - *unhealthy_since >= PSP_ADHOC_HEALTH_GRACE_US)
  {
    context->phase = PSP_ADHOC_PHASE_GROUP_LOST;
    return psp_adhoc_fail(context, result < 0 ? result : -1);
  }
  return 1;
}

/* The worker's session loop: moves datagrams and checks health until the
   emulator stops it or the network fails. */
static void psp_adhoc_connected(PspAdhocContext *context)
{
  uint64_t next_health = sceKernelGetSystemTimeWide();
  uint64_t unhealthy_since = 0;

  if(context->peer_valid)
    context->state = PSP_ADHOC_CONNECTED;

  while(!psp_adhoc_stopping(context))
  {
    uint64_t now;

    if(psp_adhoc_receive_all(context) < 0 ||
       psp_adhoc_send_all(context) < 0)
      return;

    now = sceKernelGetSystemTimeWide();
    if(now >= next_health)
    {
      if(psp_adhoc_check_health(context, &unhealthy_since) < 0)
        return;
      next_health = now + PSP_ADHOC_HEALTH_INTERVAL_US;
    }

    /* Wake at once for outgoing data, otherwise poll every millisecond. */
    psp_adhoc_sleep(context->wake_send, PSP_ADHOC_POLL_US);
  }

  /* Deliver a final goodbye, if the emulator queued one. */
  psp_adhoc_send_all(context);
}

/* The worker thread: brings the network up, hosts or joins, runs the
   session, then tears everything down and leaves STOPPED or FAILED. */
static int psp_adhoc_thread(SceSize args, void *argp)
{
  PspAdhocContext *context = &psp_adhoc;
  int result;

  (void)args;
  (void)argp;

  if(psp_adhoc_network_init(context) == 0)
  {
    result = context->host ? psp_adhoc_host(context) :
     psp_adhoc_join(context);
    if(result > 0)
      psp_adhoc_connected(context);
  }

  psp_adhoc_network_term(context);
  if(context->state != PSP_ADHOC_FAILED)
    context->state = PSP_ADHOC_STOPPED;
  /* Release an emulator thread that may be waiting for data. */
  psp_adhoc_wake(context->wake_receive);
  return 0;
}

/* ---- Emulator thread API ---------------------------------------------- */

/* Joins the worker for at most timeout_us; 1 once it is gone. */
static int psp_adhoc_reap(SceUInt timeout_us)
{
  SceUInt timeout = timeout_us;

  if(psp_adhoc.thread_id < 0)
    return 1;
  if(sceKernelWaitThreadEnd(psp_adhoc.thread_id, &timeout) < 0)
    return 0;
  sceKernelDeleteThread(psp_adhoc.thread_id);
  psp_adhoc.thread_id = -1;
  psp_adhoc_delete_flags(&psp_adhoc);
  return 1;
}

int psp_adhoc_start(int host)
{
  SceUID thread;

  if(psp_adhoc.thread_id >= 0)
  {
    psp_adhoc_stop();
    if(!psp_adhoc_reap(PSP_ADHOC_STOP_WAIT_US))
      return 0;
  }

  memset(&psp_adhoc, 0, sizeof(psp_adhoc));
  psp_adhoc.thread_id = -1;
  psp_adhoc.wake_send = -1;
  psp_adhoc.wake_receive = -1;
  psp_adhoc.socket_id = -1;
  psp_adhoc.host = host != 0;
  psp_adhoc.state = PSP_ADHOC_STARTING;

  psp_adhoc.phase = PSP_ADHOC_PHASE_WLAN;
  if(!sceWlanGetSwitchState())
  {
    psp_adhoc_fail(&psp_adhoc, -1);
    return 0;
  }

  psp_adhoc.phase = PSP_ADHOC_PHASE_THREAD;
  psp_adhoc.wake_send = sceKernelCreateEventFlag("GBA link send",
   PSP_EVENT_WAITSINGLE, 0, NULL);
  psp_adhoc.wake_receive = sceKernelCreateEventFlag("GBA link receive",
   PSP_EVENT_WAITSINGLE, 0, NULL);
  thread = -1;
  if(psp_adhoc.wake_send >= 0 && psp_adhoc.wake_receive >= 0)
    thread = sceKernelCreateThread("GBA link network", psp_adhoc_thread,
     PSP_ADHOC_WORKER_PRIORITY, 0x10000, PSP_THREAD_ATTR_USER, NULL);
  if(thread < 0 || sceKernelStartThread(thread, 0, NULL) < 0)
  {
    if(thread >= 0)
      sceKernelDeleteThread(thread);
    psp_adhoc_fail(&psp_adhoc, thread < 0 ? thread : -1);
    psp_adhoc_delete_flags(&psp_adhoc);
    return 0;
  }
  psp_adhoc.thread_id = thread;
  return 1;
}

void psp_adhoc_stop(void)
{
  if(psp_adhoc.thread_id < 0)
    return;
  psp_adhoc.stop_requested = 1;
  psp_adhoc_wake(psp_adhoc.wake_send);
}

void psp_adhoc_stop_for_exit(void)
{
  psp_adhoc_stop();
  if(!psp_adhoc_reap(PSP_ADHOC_EXIT_WAIT_US) && psp_adhoc.thread_id >= 0)
  {
    /* The game is closing; do not hang on a stuck network call. */
    sceKernelTerminateDeleteThread(psp_adhoc.thread_id);
    psp_adhoc.thread_id = -1;
    psp_adhoc_delete_flags(&psp_adhoc);
  }
}

int psp_adhoc_wlan_enabled(void)
{
  return sceWlanGetSwitchState() != 0;
}

PspAdhocState psp_adhoc_state(void)
{
  return psp_adhoc.state;
}

int psp_adhoc_last_error(void)
{
  return psp_adhoc.last_error;
}

PspAdhocPhase psp_adhoc_failure_phase(void)
{
  return psp_adhoc.failure_phase;
}

/* Short phase label for the link error line in the menu. */
const char *psp_adhoc_phase_name(PspAdhocPhase phase)
{
  switch(phase)
  {
    case PSP_ADHOC_PHASE_WLAN:         return "WLAN";
    case PSP_ADHOC_PHASE_THREAD:       return "THREAD";
    case PSP_ADHOC_PHASE_LOAD_MODULES: return "MODULES";
    case PSP_ADHOC_PHASE_NET_INIT:     return "NET INIT";
    case PSP_ADHOC_PHASE_ADHOC_INIT:   return "ADHOC INIT";
    case PSP_ADHOC_PHASE_CONTROL_INIT: return "CTL INIT";
    case PSP_ADHOC_PHASE_CREATE_GROUP: return "CREATE";
    case PSP_ADHOC_PHASE_SCAN:         return "SCAN";
    case PSP_ADHOC_PHASE_JOIN_GROUP:   return "JOIN";
    case PSP_ADHOC_PHASE_GROUP_WAIT:   return "GROUP";
    case PSP_ADHOC_PHASE_PEER_LIST:    return "PEERS";
    case PSP_ADHOC_PHASE_SOCKET:       return "SOCKET";
    case PSP_ADHOC_PHASE_SEND:         return "SEND";
    case PSP_ADHOC_PHASE_RECEIVE:      return "RECEIVE";
    case PSP_ADHOC_PHASE_GROUP_LOST:   return "GROUP LOST";
    case PSP_ADHOC_PHASE_NONE:
    default:
      return "NETWORK";
  }
}

int psp_adhoc_send(const uint8_t *data, size_t size)
{
  if(psp_adhoc.thread_id < 0 || !ring_push(&psp_adhoc.outgoing, data, size))
    return 0;
  psp_adhoc_wake(psp_adhoc.wake_send);
  return 1;
}

size_t psp_adhoc_receive(uint8_t *data, size_t capacity)
{
  return ring_pop(&psp_adhoc.incoming, data, capacity);
}

void psp_adhoc_counters(uint32_t *sends, uint32_t *send_errors,
 uint32_t *receive_drops)
{
  *sends = psp_adhoc.sends;
  *send_errors = psp_adhoc.send_errors;
  *receive_drops = psp_adhoc.receive_drops;
}

void psp_adhoc_wait(unsigned int timeout_us)
{
  if(psp_adhoc.incoming.read != psp_adhoc.incoming.write ||
     psp_adhoc.thread_id < 0)
    return;
  psp_adhoc_sleep(psp_adhoc.wake_receive, timeout_us);
}
