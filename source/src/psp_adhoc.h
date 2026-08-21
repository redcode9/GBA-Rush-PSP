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

#ifndef PSP_ADHOC_H
#define PSP_ADHOC_H

#include "link_transport.h"

typedef enum
{
  PSP_ADHOC_STOPPED,
  PSP_ADHOC_STARTING,
  PSP_ADHOC_READY,
  PSP_ADHOC_HOSTING,
  PSP_ADHOC_SEARCHING,
  PSP_ADHOC_CONNECTING,
  PSP_ADHOC_CONNECTED,
  PSP_ADHOC_FAILED
} PspAdhocState;

typedef enum
{
  PSP_ADHOC_ERROR_NONE = 0,
  PSP_ADHOC_ERROR_WLAN_OFF = -1,
  PSP_ADHOC_ERROR_THREAD = -2,
  PSP_ADHOC_ERROR_COMMAND_QUEUE = -3,
  PSP_ADHOC_ERROR_EVENT_QUEUE = -4,
  PSP_ADHOC_ERROR_PROTOCOL = -5,
  PSP_ADHOC_ERROR_SHUTDOWN = -6
} PspAdhocError;

int psp_adhoc_start(void);
void psp_adhoc_stop(void);
LinkTransport psp_adhoc_transport(void);
int psp_adhoc_wlan_enabled(void);
PspAdhocState psp_adhoc_state(void);
int psp_adhoc_last_error(void);

#endif /* PSP_ADHOC_H */
