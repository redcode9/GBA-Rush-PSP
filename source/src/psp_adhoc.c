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

#include <stdint.h>
#include <string.h>

#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_adhoc.h>
#include <pspnet_adhocctl.h>
#include <psputility_netmodules.h>
#include <pspwlan.h>

#include "link_protocol.h"
#include "psp_adhoc.h"

#define PSP_ADHOC_COMMAND_CAPACITY 32u
#define PSP_ADHOC_EVENT_CAPACITY   64u
#define PSP_ADHOC_SCAN_CAPACITY    16u
#define PSP_ADHOC_SOCKET_BUFFER    8192u
#define PSP_ADHOC_PORT             31000u
#define PSP_ADHOC_CLIENT_PORT      31001u
#define PSP_ADHOC_CONTROL_TIMEOUT  5000000u
#define PSP_ADHOC_CONNECT_TIMEOUT  5000000u
#define PSP_ADHOC_SEND_TIMEOUT     500000u
#define PSP_ADHOC_POLL_DELAY       1000u
#define PSP_ADHOC_CONTROL_DELAY    10000u
#define PSP_ADHOC_SHUTDOWN_TIMEOUT 1000000u
#define PSP_ADHOC_CLOSE_TIMEOUT    250000u

#define SCE_ADHOC_ERROR_SOCKET_DELETED     ((int)0x80410707u)
#define SCE_ADHOC_ERROR_WOULD_BLOCK        ((int)0x80410709u)
#define SCE_ADHOC_ERROR_DISCONNECTED       ((int)0x8041070cu)
#define SCE_ADHOC_ERROR_TIMEOUT            ((int)0x80410715u)
#define SCE_ADHOC_ERROR_CONNECTION_REFUSED ((int)0x80410718u)

static const char psp_adhoc_group[8] =
 { 'G', 'B', 'A', 'R', 'U', 'S', 'H', '\0' };

typedef enum
{
  PSP_ADHOC_COMMAND_HOST,
  PSP_ADHOC_COMMAND_JOIN,
  PSP_ADHOC_COMMAND_SEND,
  PSP_ADHOC_COMMAND_DISCONNECT,
  PSP_ADHOC_COMMAND_STOP
} PspAdhocCommandType;

typedef struct
{
  PspAdhocCommandType type;
  uint16_t size;
  uint8_t data[LINK_PROTOCOL_MAX_PACKET];
} PspAdhocCommand;

typedef struct
{
  PspAdhocCommand commands[PSP_ADHOC_COMMAND_CAPACITY];
  LinkTransportEvent events[PSP_ADHOC_EVENT_CAPACITY];
  volatile uint32_t command_read;
  volatile uint32_t command_write;
  volatile uint32_t event_read;
  volatile uint32_t event_write;
  volatile int stop_requested;
  volatile int fallback_error;
  volatile int fallback_pending;
  volatile PspAdhocState state;
  volatile int last_error;
  SceUID thread_id;
  int module_common;
  int module_adhoc;
  int net_initialized;
  int adhoc_initialized;
  int control_initialized;
  volatile int control_connected;
  volatile int listener_id;
  volatile int socket_id;
  PspAdhocCommandType deferred_role;
  int deferred_role_valid;
  LinkProtocolParser parser;
} PspAdhocContext;

static PspAdhocContext psp_adhoc =
{
  .state = PSP_ADHOC_STOPPED,
  .thread_id = -1,
  .listener_id = -1,
  .socket_id = -1
};

static void psp_adhoc_memory_barrier(void)
{
  __builtin_allegrex_sync();
}

static int psp_adhoc_command_push(PspAdhocContext *context,
 const PspAdhocCommand *command)
{
  uint32_t write = context->command_write;
  uint32_t next = (write + 1u) % PSP_ADHOC_COMMAND_CAPACITY;

  if(next == context->command_read)
    return 0;

  psp_adhoc_memory_barrier();
  context->commands[write] = *command;
  psp_adhoc_memory_barrier();
  context->command_write = next;
  return 1;
}

static int psp_adhoc_command_pop(PspAdhocContext *context,
 PspAdhocCommand *command)
{
  uint32_t read = context->command_read;

  if(read == context->command_write)
    return 0;

  psp_adhoc_memory_barrier();
  *command = context->commands[read];
  psp_adhoc_memory_barrier();
  context->command_read = (read + 1u) % PSP_ADHOC_COMMAND_CAPACITY;
  return 1;
}

static int psp_adhoc_event_push(PspAdhocContext *context,
 const LinkTransportEvent *event)
{
  uint32_t write = context->event_write;
  uint32_t next = (write + 1u) % PSP_ADHOC_EVENT_CAPACITY;

  if(next == context->event_read)
  {
    context->last_error = PSP_ADHOC_ERROR_EVENT_QUEUE;
    context->fallback_error = PSP_ADHOC_ERROR_EVENT_QUEUE;
    psp_adhoc_memory_barrier();
    context->fallback_pending = 1;
    return 0;
  }

  psp_adhoc_memory_barrier();
  context->events[write] = *event;
  psp_adhoc_memory_barrier();
  context->event_write = next;
  return 1;
}

static int psp_adhoc_event_pop(PspAdhocContext *context,
 LinkTransportEvent *event)
{
  uint32_t read = context->event_read;

  if(read != context->event_write)
  {
    psp_adhoc_memory_barrier();
    *event = context->events[read];
    psp_adhoc_memory_barrier();
    context->event_read = (read + 1u) % PSP_ADHOC_EVENT_CAPACITY;
    return 1;
  }

  if(context->fallback_pending)
  {
    psp_adhoc_memory_barrier();
    memset(event, 0, sizeof(*event));
    event->type = LINK_TRANSPORT_EVENT_ERROR;
    event->error = context->fallback_error;
    psp_adhoc_memory_barrier();
    context->fallback_pending = 0;
    return 1;
  }

  return 0;
}

static int psp_adhoc_emit(PspAdhocContext *context,
 LinkTransportEventType type, int error, const uint8_t *data, size_t size)
{
  LinkTransportEvent event;

  if(data != NULL && size > sizeof(event.data))
  {
    context->last_error = PSP_ADHOC_ERROR_PROTOCOL;
    return 0;
  }

  memset(&event, 0, sizeof(event));
  event.type = type;
  event.error = error;
  if(data != NULL)
  {
    event.size = (uint16_t)size;
    memcpy(event.data, data, size);
  }
  return psp_adhoc_event_push(context, &event);
}

static void psp_adhoc_set_error(PspAdhocContext *context, int error)
{
  context->last_error = error;
  context->state = PSP_ADHOC_FAILED;
  psp_adhoc_emit(context, LINK_TRANSPORT_EVENT_ERROR, error, NULL, 0);
}

static int psp_adhoc_is_temporary_error(int error)
{
  return error == SCE_ADHOC_ERROR_WOULD_BLOCK ||
   error == SCE_ADHOC_ERROR_TIMEOUT;
}

static int psp_adhoc_is_closed_error(int error)
{
  return error == SCE_ADHOC_ERROR_DISCONNECTED ||
   error == SCE_ADHOC_ERROR_SOCKET_DELETED ||
   error == SCE_ADHOC_ERROR_CONNECTION_REFUSED;
}

static void psp_adhoc_close_connection(PspAdhocContext *context)
{
  int socket_id = context->socket_id;
  int listener_id = context->listener_id;
  int control_connected = context->control_connected;

  context->socket_id = -1;
  context->listener_id = -1;
  context->control_connected = 0;
  psp_adhoc_memory_barrier();

  if(socket_id >= 0)
    sceNetAdhocPtpClose(socket_id, 0);

  if(listener_id >= 0)
    sceNetAdhocPtpClose(listener_id, 0);

  if(control_connected)
    sceNetAdhocctlDisconnect();

  link_protocol_parser_init(&context->parser);
}

static void psp_adhoc_network_term(PspAdhocContext *context)
{
  psp_adhoc_close_connection(context);

  if(context->control_initialized)
  {
    sceNetAdhocctlTerm();
    context->control_initialized = 0;
  }
  if(context->adhoc_initialized)
  {
    sceNetAdhocTerm();
    context->adhoc_initialized = 0;
  }
  if(context->net_initialized)
  {
    sceNetTerm();
    context->net_initialized = 0;
  }
  if(context->module_adhoc)
  {
    sceUtilityUnloadNetModule(PSP_NET_MODULE_ADHOC);
    context->module_adhoc = 0;
  }
  if(context->module_common)
  {
    sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
    context->module_common = 0;
  }
}

static int psp_adhoc_network_init(PspAdhocContext *context)
{
  struct productStruct product;
  int result;

  result = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
  if(result < 0)
    return result;
  context->module_common = 1;

  result = sceUtilityLoadNetModule(PSP_NET_MODULE_ADHOC);
  if(result < 0)
    return result;
  context->module_adhoc = 1;

  result = sceNetInit(128 * 1024, 42, 4 * 1024, 42, 4 * 1024);
  if(result < 0)
    return result;
  context->net_initialized = 1;

  result = sceNetAdhocInit();
  if(result < 0)
    return result;
  context->adhoc_initialized = 1;

  memset(&product, 0, sizeof(product));
  memcpy(product.product, "GBARUSH01", sizeof(product.product));
  result = sceNetAdhocctlInit(32 * 1024, 0x20, &product);
  if(result < 0)
    return result;
  context->control_initialized = 1;
  return 0;
}

static int psp_adhoc_control_command(PspAdhocContext *context)
{
  PspAdhocCommand command;

  while(psp_adhoc_command_pop(context, &command))
  {
    switch(command.type)
    {
      case PSP_ADHOC_COMMAND_STOP:
        context->stop_requested = 1;
        return 1;

      case PSP_ADHOC_COMMAND_DISCONNECT:
        return 1;

      case PSP_ADHOC_COMMAND_HOST:
      case PSP_ADHOC_COMMAND_JOIN:
        context->deferred_role = command.type;
        context->deferred_role_valid = 1;
        return 1;

      case PSP_ADHOC_COMMAND_SEND:
        break;
    }
  }

  return context->stop_requested != 0;
}

static int psp_adhoc_wait_control_state(PspAdhocContext *context,
 unsigned int timeout)
{
  uint64_t started = sceKernelGetSystemTimeWide();
  int state;
  int result;

  while(sceKernelGetSystemTimeWide() - started < timeout)
  {
    if(psp_adhoc_control_command(context))
      return 0;

    if(!sceWlanGetSwitchState())
    {
      context->last_error = PSP_ADHOC_ERROR_WLAN_OFF;
      return -1;
    }

    state = 0;
    result = sceNetAdhocctlGetState(&state);
    if(result < 0)
    {
      context->last_error = result;
      return -1;
    }
    if(state == 1)
      return 1;

    sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
  }

  context->last_error = SCE_ADHOC_ERROR_TIMEOUT;
  return -1;
}

static int psp_adhoc_host_connection(PspAdhocContext *context)
{
  unsigned char local_mac[6];
  unsigned char peer_mac[6];
  unsigned short peer_port = 0;
  int result;

  psp_adhoc_close_connection(context);
  context->state = PSP_ADHOC_HOSTING;
  context->last_error = PSP_ADHOC_ERROR_NONE;

  result = sceNetAdhocctlCreate(psp_adhoc_group);
  if(result < 0)
  {
    context->last_error = result;
    return -1;
  }
  context->control_connected = 1;

  result = psp_adhoc_wait_control_state(context, PSP_ADHOC_CONTROL_TIMEOUT);
  if(result <= 0)
    return result;

  result = sceWlanGetEtherAddr(local_mac);
  if(result < 0)
  {
    context->last_error = result;
    return -1;
  }

  context->listener_id = sceNetAdhocPtpListen(local_mac, PSP_ADHOC_PORT,
   PSP_ADHOC_SOCKET_BUFFER, PSP_ADHOC_CONTROL_DELAY, 50, 1, 0);
  if(context->listener_id < 0)
  {
    context->last_error = context->listener_id;
    return -1;
  }

  while(!context->stop_requested)
  {
    if(psp_adhoc_control_command(context))
      return 0;
    if(!sceWlanGetSwitchState())
    {
      context->last_error = PSP_ADHOC_ERROR_WLAN_OFF;
      return -1;
    }

    result = sceNetAdhocPtpAccept(context->listener_id, peer_mac,
     &peer_port, 0, 1);
    if(result > 0)
    {
      context->socket_id = result;
      sceNetAdhocPtpClose(context->listener_id, 0);
      context->listener_id = -1;
      return 1;
    }
    if(result < 0 && !psp_adhoc_is_temporary_error(result))
    {
      context->last_error = result;
      return -1;
    }

    sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
  }

  return 0;
}

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

  result = sceNetAdhocctlScan();
  if(result < 0)
  {
    context->last_error = result;
    return -1;
  }

  started = sceKernelGetSystemTimeWide();
  while(sceKernelGetSystemTimeWide() - started < 1500000u)
  {
    if(psp_adhoc_control_command(context))
      return 0;

    state = 0;
    result = sceNetAdhocctlGetState(&state);
    if(result < 0)
    {
      context->last_error = result;
      return -1;
    }
    if(state != 2 && sceKernelGetSystemTimeWide() - started >= 250000u)
      break;
    sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
  }

  memset(scans, 0, sizeof(scans));
  length = sizeof(scans);
  result = sceNetAdhocctlGetScanInfo(&length, scans);
  if(result < 0)
  {
    context->last_error = result;
    return -1;
  }

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

static int psp_adhoc_join_connection(PspAdhocContext *context)
{
  struct SceNetAdhocctlScanInfo selected;
  unsigned char local_mac[6];
  uint64_t started;
  int result;

  psp_adhoc_close_connection(context);
  context->state = PSP_ADHOC_SEARCHING;
  context->last_error = PSP_ADHOC_ERROR_NONE;

  while(!context->stop_requested)
  {
    result = psp_adhoc_scan(context, &selected);
    if(result <= 0)
      return result;
    if(result == 2)
    {
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
      continue;
    }

    result = sceNetAdhocctlJoin(&selected);
    if(result < 0)
    {
      context->last_error = result;
      return -1;
    }
    context->control_connected = 1;
    context->state = PSP_ADHOC_CONNECTING;

    result = psp_adhoc_wait_control_state(context,
     PSP_ADHOC_CONTROL_TIMEOUT);
    if(result <= 0)
      return result;

    result = sceWlanGetEtherAddr(local_mac);
    if(result < 0)
    {
      context->last_error = result;
      return -1;
    }

    started = sceKernelGetSystemTimeWide();
    while(sceKernelGetSystemTimeWide() - started < PSP_ADHOC_CONNECT_TIMEOUT)
    {
      if(psp_adhoc_control_command(context))
        return 0;

      if(context->socket_id < 0)
      {
        context->socket_id = sceNetAdhocPtpOpen(local_mac,
         PSP_ADHOC_CLIENT_PORT, selected.bssid, PSP_ADHOC_PORT,
         PSP_ADHOC_SOCKET_BUFFER, PSP_ADHOC_CONTROL_DELAY, 50, 0);
        if(context->socket_id < 0)
        {
          if(psp_adhoc_is_temporary_error(context->socket_id) ||
             context->socket_id == SCE_ADHOC_ERROR_CONNECTION_REFUSED)
          {
            context->socket_id = -1;
            sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
            continue;
          }
          context->last_error = context->socket_id;
          return -1;
        }
      }

      result = sceNetAdhocPtpConnect(context->socket_id, 0, 1);
      if(result == 0)
        return 1;
      if(!psp_adhoc_is_temporary_error(result) &&
         result != SCE_ADHOC_ERROR_CONNECTION_REFUSED)
      {
        context->last_error = result;
        return -1;
      }

      if(result == SCE_ADHOC_ERROR_CONNECTION_REFUSED)
      {
        sceNetAdhocPtpClose(context->socket_id, 0);
        context->socket_id = -1;
      }
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
    }

    psp_adhoc_close_connection(context);
    context->state = PSP_ADHOC_SEARCHING;
  }

  return 0;
}

static int psp_adhoc_send_frame(PspAdhocContext *context,
 const uint8_t *data, size_t size)
{
  uint64_t started = sceKernelGetSystemTimeWide();
  size_t sent = 0;
  int amount;
  int result;

  while(sent < size &&
        sceKernelGetSystemTimeWide() - started < PSP_ADHOC_SEND_TIMEOUT)
  {
    amount = (int)(size - sent);
    result = sceNetAdhocPtpSend(context->socket_id, (void *)(data + sent),
     &amount, PSP_ADHOC_CONTROL_DELAY, 0);
    if(result == 0 && amount > 0)
    {
      sent += (size_t)amount;
      continue;
    }
    if(result < 0 && !psp_adhoc_is_temporary_error(result))
    {
      context->last_error = result;
      return 0;
    }
  }

  if(sent != size)
  {
    context->last_error = SCE_ADHOC_ERROR_TIMEOUT;
    return 0;
  }
  return 1;
}

static int psp_adhoc_receive(PspAdhocContext *context)
{
  uint8_t input[512];
  uint8_t encoded[LINK_PROTOCOL_MAX_PACKET];
  LinkPacket packet;
  size_t position;
  size_t consumed;
  size_t encoded_size;
  LinkParseResult parse_result;
  int input_size = sizeof(input);
  int result;

  result = sceNetAdhocPtpRecv(context->socket_id, input, &input_size, 0, 1);
  if(result < 0)
  {
    if(psp_adhoc_is_temporary_error(result))
      return 1;
    context->last_error = result;
    return psp_adhoc_is_closed_error(result) ? -1 : 0;
  }
  if(input_size <= 0)
  {
    context->last_error = SCE_ADHOC_ERROR_DISCONNECTED;
    return -1;
  }

  position = 0;
  while(position < (size_t)input_size)
  {
    consumed = 0;
    parse_result = link_protocol_parser_feed(&context->parser,
     input + position, (size_t)input_size - position, &consumed, &packet);
    position += consumed;

    if(parse_result == LINK_PARSE_ERROR)
    {
      context->last_error = PSP_ADHOC_ERROR_PROTOCOL;
      return 0;
    }
    if(parse_result == LINK_PARSE_PACKET)
    {
      if(link_protocol_encode(&packet, encoded, sizeof(encoded),
         &encoded_size) != LINK_PROTOCOL_OK)
      {
        context->last_error = PSP_ADHOC_ERROR_PROTOCOL;
        return 0;
      }
      if(!psp_adhoc_emit(context, LINK_TRANSPORT_EVENT_PACKET, 0, encoded,
         encoded_size))
        return 0;
    }
  }

  return 1;
}

static int psp_adhoc_connected_service(PspAdhocContext *context)
{
  PspAdhocCommand command;
  uint64_t health_check = sceKernelGetSystemTimeWide();
  int state;
  int result;
  int command_count;

  while(!context->stop_requested)
  {
    command_count = 0;
    while(command_count < 8 && psp_adhoc_command_pop(context, &command))
    {
      command_count++;
      switch(command.type)
      {
        case PSP_ADHOC_COMMAND_SEND:
          if(!psp_adhoc_send_frame(context, command.data, command.size))
            return 0;
          break;

        case PSP_ADHOC_COMMAND_DISCONNECT:
          return 2;

        case PSP_ADHOC_COMMAND_STOP:
          context->stop_requested = 1;
          return 2;

        case PSP_ADHOC_COMMAND_HOST:
        case PSP_ADHOC_COMMAND_JOIN:
          context->deferred_role = command.type;
          context->deferred_role_valid = 1;
          return 2;
      }
    }

    result = psp_adhoc_receive(context);
    if(result <= 0)
      return result;

    if(sceKernelGetSystemTimeWide() - health_check >= 250000u)
    {
      if(!sceWlanGetSwitchState())
      {
        context->last_error = PSP_ADHOC_ERROR_WLAN_OFF;
        return 0;
      }
      state = 0;
      result = sceNetAdhocctlGetState(&state);
      if(result < 0 || state != 1)
      {
        context->last_error = result < 0 ? result :
         SCE_ADHOC_ERROR_DISCONNECTED;
        return -1;
      }
      health_check = sceKernelGetSystemTimeWide();
    }

    sceKernelDelayThread(PSP_ADHOC_POLL_DELAY);
  }

  return 2;
}

static int psp_adhoc_thread(SceSize args, void *argp)
{
  PspAdhocContext *context = &psp_adhoc;
  PspAdhocCommand command;
  int result;

  (void)args;
  (void)argp;

  result = psp_adhoc_network_init(context);
  if(result < 0)
  {
    psp_adhoc_set_error(context, result);
    psp_adhoc_network_term(context);
    return 0;
  }

  context->state = PSP_ADHOC_READY;
  while(!context->stop_requested)
  {
    if(context->deferred_role_valid)
    {
      memset(&command, 0, sizeof(command));
      command.type = context->deferred_role;
      context->deferred_role_valid = 0;
    }
    else if(!psp_adhoc_command_pop(context, &command))
    {
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
      continue;
    }

    if(command.type == PSP_ADHOC_COMMAND_STOP)
    {
      context->stop_requested = 1;
      break;
    }
    if(command.type == PSP_ADHOC_COMMAND_DISCONNECT ||
       command.type == PSP_ADHOC_COMMAND_SEND)
      continue;

    if(command.type == PSP_ADHOC_COMMAND_HOST)
      result = psp_adhoc_host_connection(context);
    else
      result = psp_adhoc_join_connection(context);

    if(result == 1)
    {
      link_protocol_parser_init(&context->parser);
      context->state = PSP_ADHOC_CONNECTED;
      psp_adhoc_emit(context, LINK_TRANSPORT_EVENT_CONNECTED, 0, NULL, 0);
      result = psp_adhoc_connected_service(context);
      if(result == -1)
        psp_adhoc_emit(context, LINK_TRANSPORT_EVENT_CLOSED,
         context->last_error, NULL, 0);
      else if(result == 0)
        psp_adhoc_set_error(context, context->last_error);
    }
    else if(result < 0)
    {
      psp_adhoc_set_error(context, context->last_error);
    }

    psp_adhoc_close_connection(context);
    if(!context->stop_requested && !context->deferred_role_valid &&
       context->state != PSP_ADHOC_FAILED)
      context->state = PSP_ADHOC_READY;
  }

  psp_adhoc_network_term(context);
  context->state = PSP_ADHOC_STOPPED;
  return 0;
}

static int psp_adhoc_transport_host(void *opaque)
{
  PspAdhocContext *context = (PspAdhocContext *)opaque;
  PspAdhocCommand command;

  memset(&command, 0, sizeof(command));
  command.type = PSP_ADHOC_COMMAND_HOST;
  if(!psp_adhoc_command_push(context, &command))
  {
    context->last_error = PSP_ADHOC_ERROR_COMMAND_QUEUE;
    return 0;
  }
  return 1;
}

static int psp_adhoc_transport_join(void *opaque)
{
  PspAdhocContext *context = (PspAdhocContext *)opaque;
  PspAdhocCommand command;

  memset(&command, 0, sizeof(command));
  command.type = PSP_ADHOC_COMMAND_JOIN;
  if(!psp_adhoc_command_push(context, &command))
  {
    context->last_error = PSP_ADHOC_ERROR_COMMAND_QUEUE;
    return 0;
  }
  return 1;
}

static int psp_adhoc_transport_send(void *opaque, const uint8_t *data,
 size_t size)
{
  PspAdhocContext *context = (PspAdhocContext *)opaque;
  PspAdhocCommand command;

  if(data == NULL || size == 0 || size > sizeof(command.data))
    return 0;

  memset(&command, 0, sizeof(command));
  command.type = PSP_ADHOC_COMMAND_SEND;
  command.size = (uint16_t)size;
  memcpy(command.data, data, size);
  if(!psp_adhoc_command_push(context, &command))
  {
    context->last_error = PSP_ADHOC_ERROR_COMMAND_QUEUE;
    return 0;
  }
  return 1;
}

static int psp_adhoc_transport_poll(void *opaque, LinkTransportEvent *event)
{
  PspAdhocContext *context = (PspAdhocContext *)opaque;

  if(event == NULL)
    return 0;
  return psp_adhoc_event_pop(context, event);
}

static void psp_adhoc_transport_disconnect(void *opaque)
{
  PspAdhocContext *context = (PspAdhocContext *)opaque;
  PspAdhocCommand command;

  memset(&command, 0, sizeof(command));
  command.type = PSP_ADHOC_COMMAND_DISCONNECT;
  if(!psp_adhoc_command_push(context, &command))
    context->last_error = PSP_ADHOC_ERROR_COMMAND_QUEUE;
}

static uint64_t psp_adhoc_transport_now(void *opaque)
{
  (void)opaque;
  return sceKernelGetSystemTimeWide();
}

static const LinkTransportOps psp_adhoc_transport_ops =
{
  psp_adhoc_transport_host,
  psp_adhoc_transport_join,
  psp_adhoc_transport_send,
  psp_adhoc_transport_poll,
  psp_adhoc_transport_disconnect,
  psp_adhoc_transport_now
};

int psp_adhoc_start(void)
{
  SceUID thread;

  if(psp_adhoc.thread_id >= 0)
    return 1;
  if(!sceWlanGetSwitchState())
  {
    psp_adhoc.last_error = PSP_ADHOC_ERROR_WLAN_OFF;
    psp_adhoc.state = PSP_ADHOC_FAILED;
    return 0;
  }

  memset(&psp_adhoc, 0, sizeof(psp_adhoc));
  psp_adhoc.thread_id = -1;
  psp_adhoc.listener_id = -1;
  psp_adhoc.socket_id = -1;
  psp_adhoc.state = PSP_ADHOC_STARTING;

  thread = sceKernelCreateThread("GBA link network", psp_adhoc_thread,
   0x18, 0x10000, PSP_THREAD_ATTR_USER, NULL);
  if(thread < 0)
  {
    psp_adhoc.last_error = thread;
    psp_adhoc.state = PSP_ADHOC_FAILED;
    return 0;
  }

  psp_adhoc.thread_id = thread;
  if(sceKernelStartThread(thread, 0, NULL) < 0)
  {
    sceKernelDeleteThread(thread);
    psp_adhoc.thread_id = -1;
    psp_adhoc.last_error = PSP_ADHOC_ERROR_THREAD;
    psp_adhoc.state = PSP_ADHOC_FAILED;
    return 0;
  }
  return 1;
}

void psp_adhoc_stop(void)
{
  PspAdhocCommand command;
  SceUInt timeout;
  int result;
  int terminated = 0;

  if(psp_adhoc.thread_id < 0)
    return;

  memset(&command, 0, sizeof(command));
  command.type = PSP_ADHOC_COMMAND_STOP;
  if(!psp_adhoc_command_push(&psp_adhoc, &command))
  {
    psp_adhoc.stop_requested = 1;
    psp_adhoc_memory_barrier();
  }

  timeout = PSP_ADHOC_SHUTDOWN_TIMEOUT;
  result = sceKernelWaitThreadEnd(psp_adhoc.thread_id, &timeout);
  if(result < 0)
  {
    psp_adhoc.stop_requested = 1;
    psp_adhoc_memory_barrier();
    psp_adhoc_close_connection(&psp_adhoc);

    timeout = PSP_ADHOC_CLOSE_TIMEOUT;
    result = sceKernelWaitThreadEnd(psp_adhoc.thread_id, &timeout);
  }

  if(result < 0)
  {
    sceKernelTerminateDeleteThread(psp_adhoc.thread_id);
    terminated = 1;
    psp_adhoc_network_term(&psp_adhoc);
    psp_adhoc.last_error = PSP_ADHOC_ERROR_SHUTDOWN;
    psp_adhoc.state = PSP_ADHOC_FAILED;
  }
  else
  {
    sceKernelDeleteThread(psp_adhoc.thread_id);
  }

  psp_adhoc.thread_id = -1;
  if(!terminated)
    psp_adhoc.state = PSP_ADHOC_STOPPED;
}

LinkTransport psp_adhoc_transport(void)
{
  LinkTransport transport;

  transport.ops = &psp_adhoc_transport_ops;
  transport.context = &psp_adhoc;
  return transport;
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
