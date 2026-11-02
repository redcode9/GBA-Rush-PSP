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
 * PSP ad-hoc Wi-Fi for the link cable.
 *
 * A worker thread brings up the network, creates or joins the "GBARUSH"
 * group and owns the PDP socket. It only moves datagrams: the emulator
 * thread hands it outgoing datagrams and collects incoming ones through two
 * lock-free queues, so all link logic stays on the emulator thread and a
 * slow network call can never stall the game.
 */

#ifndef PSP_ADHOC_H
#define PSP_ADHOC_H

#include <stddef.h>
#include <stdint.h>

#define PSP_ADHOC_MAX_DATAGRAM 1024u

typedef enum
{
  PSP_ADHOC_STOPPED,
  PSP_ADHOC_STARTING,
  PSP_ADHOC_HOSTING,     /* group created, waiting for the other PSP */
  PSP_ADHOC_SEARCHING,   /* looking for a group to join */
  PSP_ADHOC_CONNECTING,
  PSP_ADHOC_CONNECTED,
  PSP_ADHOC_FAILED
} PspAdhocState;

/* The step that was running when the network failed, for diagnostics. */
typedef enum
{
  PSP_ADHOC_PHASE_NONE,
  PSP_ADHOC_PHASE_WLAN,
  PSP_ADHOC_PHASE_THREAD,
  PSP_ADHOC_PHASE_LOAD_MODULES,
  PSP_ADHOC_PHASE_NET_INIT,
  PSP_ADHOC_PHASE_ADHOC_INIT,
  PSP_ADHOC_PHASE_CONTROL_INIT,
  PSP_ADHOC_PHASE_CREATE_GROUP,
  PSP_ADHOC_PHASE_SCAN,
  PSP_ADHOC_PHASE_JOIN_GROUP,
  PSP_ADHOC_PHASE_GROUP_WAIT,
  PSP_ADHOC_PHASE_PEER_LIST,
  PSP_ADHOC_PHASE_SOCKET,
  PSP_ADHOC_PHASE_SEND,
  PSP_ADHOC_PHASE_RECEIVE,
  PSP_ADHOC_PHASE_GROUP_LOST
} PspAdhocPhase;

/* Starts the worker that creates (host) or joins the group; 0 if the
   WLAN switch is off or the worker cannot start. A worker left from an
   earlier link is stopped first; 0 too if it has not exited within
   PSP_ADHOC_STOP_WAIT_US (4 s). */
int psp_adhoc_start(int host);
/* Asks the worker to close the connection; returns at once. */
void psp_adhoc_stop(void);
/* Stops the worker and waits a short, bounded time for it. */
void psp_adhoc_stop_for_exit(void);

int psp_adhoc_wlan_enabled(void);
PspAdhocState psp_adhoc_state(void);
int psp_adhoc_last_error(void);
PspAdhocPhase psp_adhoc_failure_phase(void);
const char *psp_adhoc_phase_name(PspAdhocPhase phase);

/* Emulator thread side. Sending drops the datagram if the queue is full;
   the link protocol repeats anything that matters. */
int psp_adhoc_send(const uint8_t *data, size_t size);
size_t psp_adhoc_receive(uint8_t *data, size_t capacity);
/* Datagrams handed to the PSP network stack, those it refused, and
   incoming datagrams lost to a full queue. */
void psp_adhoc_counters(uint32_t *sends, uint32_t *send_errors,
 uint32_t *receive_drops);
/* Sleeps until a datagram arrives or the timeout expires. */
void psp_adhoc_wait(unsigned int timeout_us);

#endif /* PSP_ADHOC_H */
