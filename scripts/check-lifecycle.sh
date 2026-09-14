#!/bin/sh
set -eu

# Supplement the runtime SIO tests with a guard on the PSP-only menu wiring.
# This is source-order validation, not a simulation of the real PSP lifecycle.
link_start_body=$(sed -n '/^static int main_link_start(int host)/,/^}/p' \
  source/src/main.c)
if ! printf '%s\n' "$link_start_body" | awk '
  /if\(!gba_sio_prepare\(&gba_sio, io_registers\)\)/ { prepare = 1; next }
  prepare && /return 0;/ { checked = 1 }
  /main_link_disconnect\(|link_session_init\(|psp_adhoc_start\(/ {
    if (!checked) bad = 1
    consumers++
  }
  END { exit !(checked && consumers >= 3 && !bad) }
'
then
  echo "Link startup uses SIO before guarded register preparation" >&2
  exit 1
fi

callback_body=$(sed -n \
  '/^static int power_callback/,/^static int callback_thread/p' \
  source/src/main.c)

if printf '%s\n' "$callback_body" | rg -q \
  'psp_sound_frequency|auto_savestate_sleep|main_link_disconnect|psp_adhoc_stop|sceKernel|sceIo|sceNet|FILE_|error_msg|quit\('
then
  echo "Power callback contains a blocking lifecycle operation" >&2
  exit 1
fi

if ! printf '%s\n' "$callback_body" | rg -q 'sound_power_resume'
then
  echo "Power callback does not use the suspend-safe audio resume path" >&2
  exit 1
fi

exit_callback_body=$(sed -n \
  '/^static int exit_callback/,/^static int power_callback/p' \
  source/src/main.c)
if printf '%s\n' "$exit_callback_body" | rg -q '^[[:space:]]*quit\(\)' || \
   ! printf '%s\n' "$exit_callback_body" | rg -q 'exit_requested = 1' || \
   ! printf '%s\n' "$exit_callback_body" | \
    rg -q 'MAIN_EXIT_CALLBACK_TIMEOUT_US'
then
  echo "Exit callback bypasses the bounded main-thread shutdown path" >&2
  exit 1
fi

if ! rg -q 'sceKernelTerminateDeleteThread' source/src/psp_adhoc.c || \
   ! rg -q 'psp_adhoc_stop_for_exit' source/src/main.c
then
  echo "Exit path cannot bound a stalled ad-hoc worker" >&2
  exit 1
fi

if rg -q 'sceKernelWaitThreadEnd\([^,]+, NULL\)' \
  source/src/psp_adhoc.c source/src/sound.c
then
  echo "Lifecycle worker uses an unlimited thread join" >&2
  exit 1
fi

if rg -q 'sceKernelWaitSema\([^,]+,[^,]+,[[:space:]]*(0|NULL)\)' \
  source/src/sound.c
then
  echo "Sound lifecycle uses an unlimited semaphore wait" >&2
  exit 1
fi

if rg -q 'sceAudioOutput2' source/src/sound.c
then
  echo "SRC audio lifecycle calls the Output2 API family" >&2
  exit 1
fi

audio_power_body=$(sed -n \
  '/^void sound_power_suspend/,/^void sound_power_resume_finish/p' \
  source/src/sound.c)
if ! printf '%s\n' "$audio_power_body" | rg -q 'SOUND_OPERATION_TIMEOUT' || \
   ! printf '%s\n' "$audio_power_body" | rg -q 'sceKernelWaitSema'
then
  echo "Audio suspend and resume are not serialized with a deadline" >&2
  exit 1
fi

if ! sed -n '/^void synchronize_sound/,/^void update_gbc_sound/p' \
  source/src/sound.c | rg -q 'SOUND_OPERATION_TIMEOUT'
then
  echo "Sound synchronization has no recovery deadline" >&2
  exit 1
fi

require_queue_barriers()
{
  function_name=$1
  minimum=$2
  function_body=$(sed -n "/^static int $function_name/,/^}/p" \
    source/src/psp_adhoc.c)
  barrier_count=$(printf '%s\n' "$function_body" | \
    rg -c 'psp_adhoc_memory_barrier' || true)

  if [ "$barrier_count" -lt "$minimum" ]
  then
    echo "Ad-hoc queue function lacks publication barriers: $function_name" >&2
    exit 1
  fi
}

require_queue_barriers psp_adhoc_command_push 2
require_queue_barriers psp_adhoc_command_pop 2
require_queue_barriers psp_adhoc_event_push 3
require_queue_barriers psp_adhoc_event_pop 4

link_start_body=$(sed -n \
  '/^static int main_link_start/,/^int main_link_host/p' source/src/main.c)
clock_line=$(printf '%s\n' "$link_start_body" | \
  rg -n 'set_cpu_clock\(PSP_CLOCK_333\)' | head -n 1 | cut -d: -f1 || true)
adhoc_line=$(printf '%s\n' "$link_start_body" | \
  rg -n 'psp_adhoc_start\(' | head -n 1 | cut -d: -f1 || true)
if [ -z "$clock_line" ] || [ -z "$adhoc_line" ] || \
   [ "$clock_line" -ge "$adhoc_line" ]
then
  echo "Link clock is not pinned before ad-hoc startup" >&2
  exit 1
fi

link_close_body=$(sed -n \
  '/^static void main_link_handle_closed/,/^}/p' source/src/main.c)
if ! printf '%s\n' "$link_close_body" | \
  rg -q 'set_cpu_clock\(option_clock_speed\)'
then
  echo "Link clock is not restored after shutdown" >&2
  exit 1
fi

menu_body=$(sed -n '/^u32 menu(void)/,/^static void update_status_string/p' \
  source/src/gui.c)
menu_clock_guards=$(printf '%s\n' "$menu_body" | \
  rg -c 'if\(!main_link_active\(\)\)' || true)
if [ "$menu_clock_guards" -lt 2 ]
then
  echo "Menu clock transitions are not guarded by link state" >&2
  exit 1
fi

failure_body=$(sed -n \
  '/^static void psp_adhoc_capture_failure/,/^}/p' source/src/psp_adhoc.c)
if ! printf '%s\n' "$failure_body" | rg -q 'psp_adhoc_capture_snapshot'
then
  echo "Ad-hoc first failure does not capture a network snapshot" >&2
  exit 1
fi

temporary_error_body=$(sed -n \
  '/^static int psp_adhoc_is_temporary_error/,/^}/p' source/src/psp_adhoc.c)
for required in SCE_NET_ERROR_NO_SPACE SCE_ADHOC_ERROR_WOULD_BLOCK \
  SCE_ADHOC_ERROR_TIMEOUT
do
  if ! printf '%s\n' "$temporary_error_body" | rg -q "$required"
  then
    echo "PDP temporary error policy lacks $required" >&2
    exit 1
  fi
done

recoverable_error_body=$(sed -n \
  '/^static int psp_adhoc_is_recoverable_socket_error/,/^}/p' \
  source/src/psp_adhoc.c)
for required in SCE_ADHOC_ERROR_SOCKET_DELETED \
  SCE_ADHOC_ERROR_DISCONNECTED
do
  if ! printf '%s\n' "$recoverable_error_body" | rg -q "$required"
  then
    echo "PDP recovery policy lacks $required" >&2
    exit 1
  fi
done

if rg -q 'sceNetAdhocPtp|adhoc_stream|PSP_ADHOC_PTP' \
  source/src tests Makefile source/Makefile
then
  echo "Obsolete PTP transport is still present" >&2
  exit 1
fi

for required in sceNetAdhocPdpCreate sceNetAdhocPdpSend \
  sceNetAdhocPdpRecv sceNetAdhocPdpDelete adhoc_datagram_prepare \
  adhoc_datagram_receive adhoc_datagram_note_sent
do
  if ! rg -q "$required" source/src/psp_adhoc.c
  then
    echo "Reliable PDP transport lacks $required" >&2
    exit 1
  fi
done

connected_body=$(sed -n \
  '/^static .*psp_adhoc_connected_service/,/^}/p' source/src/psp_adhoc.c)
recovery_line=$(printf '%s\n' "$connected_body" | \
  rg -n 'psp_adhoc_recovery_step\(' | head -n 1 | cut -d: -f1 || true)
receive_line=$(printf '%s\n' "$connected_body" | \
  rg -n 'psp_adhoc_receive\(' | head -n 1 | cut -d: -f1 || true)
command_line=$(printf '%s\n' "$connected_body" | \
  rg -n 'psp_adhoc_command_pop\(' | head -n 1 | cut -d: -f1 || true)
send_line=$(printf '%s\n' "$connected_body" | \
  rg -n 'psp_adhoc_send_drain\(' | head -n 1 | cut -d: -f1 || true)
close_line=$(printf '%s\n' "$connected_body" | \
  rg -n 'if\(context->close_requested\)' | head -n 1 | cut -d: -f1 || true)
if [ -z "$recovery_line" ] || [ -z "$receive_line" ] || \
   [ -z "$command_line" ] || [ -z "$send_line" ] || \
   [ "$recovery_line" -ge "$receive_line" ] || \
   [ "$receive_line" -ge "$command_line" ] || \
   [ "$command_line" -ge "$send_line" ] || \
   [ -z "$close_line" ] || [ "$close_line" -ge "$receive_line" ]
then
  echo "Connected PDP service ordering is unsafe" >&2
  exit 1
fi

if ! rg -q '^#define PSP_ADHOC_RECOVERY_IDLE[[:space:]]+0u$' \
  source/src/psp_adhoc.c || \
   ! rg -q '^#define PSP_ADHOC_PROBE_TIMEOUT[[:space:]]+6000000u$' \
  source/src/psp_adhoc.c || \
   ! rg -q '^#define PSP_ADHOC_ACK_HARD_TIMEOUT[[:space:]]+6000000u$' \
  source/src/psp_adhoc.c || \
   ! rg -q '^#define PSP_ADHOC_TRANSPORT_RECOVERY[[:space:]]+8000000u$' \
  source/src/psp_adhoc.c || \
   ! rg -q '^#define PSP_ADHOC_GROUP_RECOVERY[[:space:]]+15000000u$' \
  source/src/psp_adhoc.c || \
   ! rg -q '^#define PSP_ADHOC_STACK_RECOVERY[[:space:]]+12000000u$' \
  source/src/psp_adhoc.c
then
  echo "Hierarchical ad-hoc recovery policy changed unexpectedly" >&2
  exit 1
fi

probe_timeout=$(sed -n \
  's/^#define PSP_ADHOC_PROBE_TIMEOUT[[:space:]]*\([0-9][0-9]*\)u$/\1/p' \
  source/src/psp_adhoc.c)
transport_timeout=$(sed -n \
  's/^#define PSP_ADHOC_TRANSPORT_RECOVERY[[:space:]]*\([0-9][0-9]*\)u$/\1/p' \
  source/src/psp_adhoc.c)
group_timeout=$(sed -n \
  's/^#define PSP_ADHOC_GROUP_RECOVERY[[:space:]]*\([0-9][0-9]*\)u$/\1/p' \
  source/src/psp_adhoc.c)
stack_timeout=$(sed -n \
  's/^#define PSP_ADHOC_STACK_RECOVERY[[:space:]]*\([0-9][0-9]*\)u$/\1/p' \
  source/src/psp_adhoc.c)
liveness_timeout=$(sed -n \
  's/^#define LINK_SESSION_LIVENESS_TIMEOUT_US[[:space:]]*\([0-9][0-9]*\)u$/\1/p' \
  source/src/link_session.h)
if [ -z "$probe_timeout" ] || \
   [ -z "$transport_timeout" ] || [ -z "$group_timeout" ] || \
   [ -z "$stack_timeout" ] || [ -z "$liveness_timeout" ] || \
   [ "$liveness_timeout" -le \
    "$((probe_timeout + transport_timeout + group_timeout + stack_timeout))" ]
then
  echo "Session liveness can expire before hierarchical recovery" >&2
  exit 1
fi

recovery_step_body=$(sed -n \
  '/^static .*psp_adhoc_recovery_step/,/^}/p' source/src/psp_adhoc.c)
for required in ADHOC_RECOVERY_ACTION_RESYNC_TRANSPORT \
  ADHOC_RECOVERY_ACTION_REBUILD_GROUP ADHOC_RECOVERY_ACTION_RESTART_STACK \
  psp_adhoc_resync_transport psp_adhoc_rebuild_group psp_adhoc_restart_stack \
  LINK_TRANSPORT_EVENT_RECOVERING
do
  if ! printf '%s\n' "$recovery_step_body" | rg -q "$required"
  then
    echo "Hierarchical ad-hoc recovery lacks $required" >&2
    exit 1
  fi
done
if ! rg -q 'LINK_TRANSPORT_EVENT_RECOVERED' source/src/psp_adhoc.c
then
  echo "Successful PDP recovery is not reported" >&2
  exit 1
fi

if rg -q 'gamepak_rom[[:space:]]*\+[[:space:]]*0x(A0|AC)|gamepak_rom\[0xBC\]' \
  source/src/main.c || \
   ! rg -q 'gamepak_header\[GAMEPAK_HEADER_SIZE\]' source/src/memory.c || \
   ! rg -q 'memcpy\(gamepak_header, gamepak_rom' source/src/memory.c
then
  echo "Paged ROM metadata is not backed by a stable header copy" >&2
  exit 1
fi

if ! printf '%s\n' "$connected_body" | \
   rg -q 'recovery.state < ADHOC_RECOVERY_TRANSPORT_RESYNC' || \
   printf '%s\n' "$connected_body" | \
   rg -q '!adhoc_recovery_active.*psp_adhoc_command_pop'
then
  echo "Passive probing can stall the transmit queue" >&2
  exit 1
fi

for required in link_transport_scheduler_queue \
  link_transport_scheduler_prepare link_transport_scheduler_note_sent \
  link_transport_scheduler_note_blocked link_transport_scheduler_receive \
  LINK_LANE_CRITICAL LINK_LANE_REPLACEABLE LINK_STATE_SIO \
  LINK_STATE_HEARTBEAT LINK_STATE_CLOCK __ATOMIC_ACQUIRE __ATOMIC_RELEASE
do
  if ! rg -q "$required" source/src/psp_adhoc.c \
    source/src/link_transport_scheduler.c \
    source/src/link_transport_scheduler.h \
    source/src/link_transport_lanes.c source/src/link_transport_lanes.h \
    source/src/link_spsc_ring.c
  then
    echo "Production transport scheduler lacks $required" >&2
    exit 1
  fi
done
if ! rg -q 'test_link_spsc_ring' Makefile || \
   ! rg -q 'test_link_transport_lanes' Makefile || \
   ! rg -q 'test_link_transport_scheduler' Makefile || \
   ! rg -q 'test_link_clock' Makefile
then
  echo "Link scheduler, lane and clock harnesses are not part of the test gate" >&2
  exit 1
fi

for required in LINK_MESSAGE_TRANSFER_COMMIT_ACK LINK_MESSAGE_CLOCK_SYNC \
  LINK_MESSAGE_RESUME LINK_MESSAGE_RESUME_ACK LINK_MESSAGE_RESUME_DONE \
  LINK_CAPABILITY_CLOCK_SYNC LINK_CAPABILITY_RECOVERY
do
  if ! rg -q "$required" source/src/link_protocol.h source/src/link_session.c
  then
    echo "Recoverable link protocol lacks $required" >&2
    exit 1
  fi
done
for required in link_session_pause_reason link_session_cycles_until_pause \
  link_session_publish_clock
do
  if ! rg -q "$required" source/src/main.c
  then
    echo "Emulation loop lacks $required" >&2
    exit 1
  fi
done

for required in 'uint32_t generation' 'LinkTransportHealth' \
  'LINK_TRANSPORT_HEALTH_PRESSURED' 'LINK_TRANSPORT_HEALTH_RECOVERING'
do
  if ! rg -q "$required" source/src/link_transport.h
  then
    echo "Transport events lack exclusive generation-safe health: $required" >&2
    exit 1
  fi
done
if ! rg -q 'event\.generation = psp_adhoc_event_generation' \
  source/src/psp_adhoc.c || \
   ! rg -q 'event->generation =.*health_epoch' source/src/psp_adhoc.c || \
   ! rg -q 'event->generation = context->activity_epoch' source/src/psp_adhoc.c
then
  echo "PSP transport does not stamp every event source with an epoch" >&2
  exit 1
fi
if rg -q 'transport_degraded|transport_recovering|uint8_t recovery_requested' \
  source/src/link_session.h || \
   ! rg -q 'LinkTransportHealth transport_health' source/src/link_session.h || \
   ! rg -q 'stale_transport_events' source/src/link_session.c \
    source/src/link_session.h || \
   ! rg -q 'test_stale_health_cannot_overlap_recovery' \
    tests/test_link_session.c
then
  echo "Session transport health can overlap or accept stale epochs" >&2
  exit 1
fi

group_recovery_body=$(sed -n \
  '/^static int psp_adhoc_rebuild_group/,/^}/p' source/src/psp_adhoc.c)
for required in psp_adhoc_recovery_disconnect_group sceNetAdhocctlCreate \
  sceNetAdhocctlJoin psp_adhoc_open_pdp psp_adhoc_resync_transport
do
  if ! printf '%s\n' "$group_recovery_body" | rg -q "$required"
  then
    echo "Ad-hoc group rebuild lacks $required" >&2
    exit 1
  fi
done

stack_recovery_body=$(sed -n \
  '/^static int psp_adhoc_restart_stack/,/^}/p' source/src/psp_adhoc.c)
for required in psp_adhoc_network_deinit psp_adhoc_network_init \
  psp_adhoc_rebuild_group
do
  if ! printf '%s\n' "$stack_recovery_body" | rg -q "$required"
  then
    echo "Ad-hoc stack restart lacks $required" >&2
    exit 1
  fi
done

send_step_body=$(sed -n \
  '/^static .*psp_adhoc_send_step/,/^}/p' source/src/psp_adhoc.c)
for required in link_transport_scheduler_prepare psp_adhoc_send_raw \
  link_transport_scheduler_note_blocked \
  link_transport_scheduler_note_sent PSP_ADHOC_PHASE_PDP_SEND
do
  if ! printf '%s\n' "$send_step_body" | rg -q "$required"
  then
    echo "Lane-scheduled PDP send path lacks $required" >&2
    exit 1
  fi
done

send_drain_body=$(sed -n \
  '/^static .*psp_adhoc_send_drain/,/^}/p' source/src/psp_adhoc.c)
for required in PSP_ADHOC_HOT_DRAIN_MAX PSP_ADHOC_HOT_DRAIN_BUDGET_US \
  psp_adhoc_send_step PSP_ADHOC_SEND_SENT hot_drain_count \
  hot_drain_high_water
do
  if ! printf '%s\n' "$send_drain_body" | rg -q "$required"
  then
    echo "Bounded hot PDP drain lacks $required" >&2
    exit 1
  fi
done

for required in sceKernelCreateEventFlag sceKernelSetEventFlag \
  sceKernelPollEventFlag sceKernelWaitEventFlag sceKernelDeleteEventFlag \
  PSP_EVENT_WAITCLEAR psp_adhoc_tx_wake_create \
  psp_adhoc_tx_wake_reset psp_adhoc_tx_wake_cancel \
  psp_adhoc_tx_wake_delete
do
  if ! rg -q "$required" source/src/psp_adhoc.c
  then
    echo "PSP worker wake lifecycle lacks $required" >&2
    exit 1
  fi
done

transport_reset_body=$(sed -n \
  '/^static void psp_adhoc_transport_reset/,/^}/p' source/src/psp_adhoc.c)
thread_body=$(sed -n \
  '/^static int psp_adhoc_thread/,/^}/p' source/src/psp_adhoc.c)
start_body=$(sed -n \
  '/^int psp_adhoc_start/,/^}/p' source/src/psp_adhoc.c)
shutdown_service_body=$(sed -n \
  '/^int psp_adhoc_service_shutdown/,/^}/p' source/src/psp_adhoc.c)
if ! printf '%s\n' "$transport_reset_body" | \
   rg -q 'psp_adhoc_tx_wake_reset' || \
   ! printf '%s\n' "$thread_body" | \
    rg -q 'psp_adhoc_tx_wake_cancel' || \
   ! printf '%s\n' "$start_body" | \
    rg -q 'psp_adhoc_tx_wake_create' || \
   ! printf '%s\n' "$start_body" | \
    rg -q 'psp_adhoc_tx_wake_delete' || \
   ! printf '%s\n' "$shutdown_service_body" | \
    rg -q 'psp_adhoc_tx_wake_delete'
then
  echo "PSP worker wake is not owned across reset, failure, and restart" >&2
  exit 1
fi

tx_handoff_body=$(sed -n \
  '/^static int psp_adhoc_tx_handoff_push/,/^}/p' source/src/psp_adhoc.c)
if ! printf '%s\n' "$tx_handoff_body" | \
   rg -q 'lane == LINK_LANE_CRITICAL' || \
   ! printf '%s\n' "$tx_handoff_body" | \
    rg -q 'psp_adhoc_publish_hot_wake'
then
  echo "Critical main-to-network handoff does not wake the PSP worker" >&2
  exit 1
fi

if printf '%s\n' "$connected_body" | \
   rg -q 'sceKernelDelayThread\(PSP_ADHOC_POLL_DELAY\)' || \
   ! printf '%s\n' "$connected_body" | rg -q 'psp_adhoc_hot_active' || \
   ! printf '%s\n' "$connected_body" | \
    rg -q 'psp_adhoc_tx_wake_wait.*PSP_ADHOC_POLL_DELAY' || \
   ! printf '%s\n' "$connected_body" | \
    rg -q 'sceKernelDelayThread\(PSP_ADHOC_HOT_YIELD_US\)'
then
  echo "Connected PSP worker does not separate bounded hot yield from cold wait" >&2
  exit 1
fi

receive_body=$(sed -n \
  '/^static .*psp_adhoc_receive(/,/^}/p' source/src/psp_adhoc.c)
for required in psp_adhoc_receive_raw adhoc_rendezvous_decode \
  link_transport_scheduler_receive link_protocol_decode \
  psp_adhoc_note_datagram_activity psp_adhoc_emit
do
  if ! printf '%s\n' "$receive_body" | rg -q "$required"
  then
    echo "Lane-scheduled PDP receive path lacks $required" >&2
    exit 1
  fi
done

resync_body=$(sed -n \
  '/^static int psp_adhoc_resync_transport/,/^static int psp_adhoc_wait_control_value/p' \
  source/src/psp_adhoc.c)
for required in adhoc_resync_init adhoc_resync_outbound \
  adhoc_resync_receive ADHOC_RESYNC_ACTION_RESET_EPOCH \
  ADHOC_RESYNC_ACTION_FINAL_ACK ADHOC_RESYNC_ACTION_COMPLETE \
  ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK adhoc_datagram_init \
  psp_adhoc_transport_reset
do
  if ! printf '%s\n' "$resync_body" | rg -q "$required"
  then
    echo "PDP epoch resync lacks $required" >&2
    exit 1
  fi
done

rendezvous_body=$(sed -n \
  '/^static int psp_adhoc_send_rendezvous/,/^}/p' source/src/psp_adhoc.c)
if ! printf '%s\n' "$rendezvous_body" | rg -q 'adhoc_rendezvous_encode' || \
   ! printf '%s\n' "$rendezvous_body" | rg -q 'psp_adhoc_send_raw'
then
  echo "Initial PDP rendezvous is incomplete" >&2
  exit 1
fi

if rg -q 'adhoc_reliable|ADHOC_RELIABLE' source/src tests Makefile \
  source/Makefile
then
  echo "Obsolete reliable-over-PDP layer is still present" >&2
  exit 1
fi

if ! rg -q 'test_adhoc_datagram' Makefile || \
   ! rg -q 'test_seeded_fault_network' tests/test_adhoc_datagram.c || \
   ! rg -q 'test_adhoc_resync' Makefile || \
   ! rg -q 'test_faulted_completion' tests/test_adhoc_resync.c || \
   ! rg -q 'test_arbitrary_finite_fault_closure' \
     tests/test_adhoc_resync.c
then
  echo "Reliable PDP fault harness is not part of the test gate" >&2
  exit 1
fi

for required in test_resume_handshake_waits_for_transport_error \
  test_nested_transport_recovery_reuses_coordinator \
  test_joiner_recovery_waits_for_transport_error \
  test_late_attach_delivers_abort_irq_once \
  test_duplicate_resume_ack_replays_resume_done \
  test_resume_ack_retry_replays_lost_resume_done \
  test_resume_retry_survives_long_finite_loss
do
  if ! rg -q "$required" tests/test_link_session.c
  then
    echo "Session recovery harness lacks $required" >&2
    exit 1
  fi
done
if ! rg -q 'resume_done_valid' source/src/link_session.c \
  source/src/link_session.h
then
  echo "Resume completion is not replayable" >&2
  exit 1
fi
if ! rg -q 'test_delayed_request_does_not_restart_epoch' \
  tests/test_adhoc_resync.c || \
   ! rg -q 'adhoc_resync_request_matches_epoch' source/src/adhoc_resync.c \
  source/src/psp_adhoc.c
then
  echo "Stale resync requests are not gated by transport epoch" >&2
  exit 1
fi

if ! rg -q '^#define LINK_SESSION_TRANSFER_TIMEOUT_US[[:space:]]+12000000u$' \
  source/src/link_session.h || \
   ! rg -q 'session->barrier_active.*&&' source/src/link_session.c
then
  echo "GBA transfer timeout is not bounded independently" >&2
  exit 1
fi

drop_pdp_body=$(sed -n \
  '/^static void psp_adhoc_drop_pdp/,/^}/p' source/src/psp_adhoc.c)
if ! printf '%s\n' "$drop_pdp_body" | rg -q 'sceNetAdhocPdpDelete'
then
  echo "PDP sockets are not closed centrally" >&2
  exit 1
fi

transport_poll_body=$(sed -n \
  '/^static int psp_adhoc_transport_poll/,/^}/p' source/src/psp_adhoc.c)
if ! printf '%s\n' "$transport_poll_body" | \
   rg -q 'LINK_TRANSPORT_EVENT_ACTIVITY' || \
   ! rg -q 'case LINK_TRANSPORT_EVENT_ACTIVITY:' source/src/link_session.c
then
  echo "Transport activity is not connected to session liveness" >&2
  exit 1
fi
stop_body=$(sed -n \
  '/^static void psp_adhoc_stop_internal/,/^}/p' source/src/psp_adhoc.c)
if ! printf '%s\n' "$stop_body" | rg -q 'PSP_ADHOC_COMMAND_STOP' || \
   ! printf '%s\n' "$stop_body" | rg -q 'psp_adhoc_command_push' || \
   ! printf '%s\n' "$stop_body" | rg -q 'lifecycle_teardown_begin' || \
   ! printf '%s\n' "$stop_body" | \
    rg -q 'lifecycle_teardown_remaining_us'
then
  echo "Ad-hoc stop bypasses the bounded cancellation contract" >&2
  exit 1
fi
stop_wait_count=$(printf '%s\n' "$stop_body" | \
  rg -c 'sceKernelWaitThreadEnd' || true)
if [ "$stop_wait_count" -ne 1 ] || \
   rg -q 'PSP_ADHOC_CLOSE_TIMEOUT' source/src/psp_adhoc.c || \
   ! printf '%s\n' "$stop_body" | rg -q 'if\(!exiting\)'
then
  echo "Ad-hoc stop does not use one fixed monotonic deadline" >&2
  exit 1
fi
nonexit_line=$(printf '%s\n' "$stop_body" | \
  rg -n 'if\(!exiting\)' | head -n 1 | cut -d: -f1 || true)
wait_line=$(printf '%s\n' "$stop_body" | \
  rg -n 'sceKernelWaitThreadEnd' | head -n 1 | cut -d: -f1 || true)
if [ -z "$nonexit_line" ] || [ -z "$wait_line" ] || \
   [ "$nonexit_line" -ge "$wait_line" ] || \
   rg -q 'sceKernelWaitThreadEnd' source/src/main.c
then
  echo "Emulation thread can wait for worker destruction" >&2
  exit 1
fi
if ! rg -q 'sceKernelReferThreadStatus' source/src/psp_adhoc.c || \
   ! rg -q 'lifecycle_teardown_poll' source/src/psp_adhoc.c \
    tests/test_lifecycle_teardown.c
then
  echo "Normal teardown is not serviced nonblockingly" >&2
  exit 1
fi
if printf '%s\n' "$stop_body" | rg -q 'psp_adhoc_close_connection'
then
  echo "Ad-hoc stop closes worker-owned resources from the main thread" >&2
  exit 1
fi

for required in LIFECYCLE_TEARDOWN_ADMISSION_STOPPED \
  LIFECYCLE_TEARDOWN_WORKER_SIGNALED LIFECYCLE_TEARDOWN_WAITING \
  LIFECYCLE_TEARDOWN_WORKER_QUIESCED \
  LIFECYCLE_TEARDOWN_RESOURCES_CLOSED \
  LIFECYCLE_TEARDOWN_MODULES_UNLOADED \
  LIFECYCLE_TEARDOWN_WORKER_STOPPED \
  LIFECYCLE_TEARDOWN_DEADLINE_EXPIRED
do
  if ! rg -q "$required" source/src/lifecycle_teardown.h \
    source/src/psp_adhoc.c tests/test_lifecycle_teardown.c
  then
    echo "Lifecycle teardown contract lacks $required" >&2
    exit 1
  fi
done
if ! rg -q 'test_lifecycle_teardown' Makefile || \
   ! rg -q 'test_fault_at_each_externally_visible_phase' \
    tests/test_lifecycle_teardown.c
then
  echo "Lifecycle fault injection is not part of the test gate" >&2
  exit 1
fi

for function_name in psp_adhoc_transport_host psp_adhoc_transport_join \
  psp_adhoc_transport_send
do
  function_body=$(sed -n "/^static int $function_name/,/^}/p" \
    source/src/psp_adhoc.c)
  if ! printf '%s\n' "$function_body" | rg -q 'psp_adhoc_cancelled'
  then
    echo "Shutdown does not stop admission in $function_name" >&2
    exit 1
  fi
done

if ! rg -q 'link_audio_wait_begin' source/src/sound.c || \
   ! rg -q 'link_audio_wait_end' source/src/sound.c || \
   ! rg -q 'test_link_audio_wait' Makefile || \
   ! sed -n '/^static int sound_update_thread/,/^}/p' source/src/sound.c | \
    rg -q 'memset\(psp_sound_buffer\[buf_idx\], 0'
then
  echo "Link audio wait policy is not idempotently testable" >&2
  exit 1
fi

for required in queue_sojourn_high_water_us rtt_estimate_us shutdown_phase \
  cancellation_token runtime_log_psp_get_drop_counts sio_revision \
  clock.local_cycle clock.peer_cycle journal.epoch backpressure_age_ms \
  backpressure_episode_count clock_backoff_us clock_deferred health_causes \
  session_recovery_requests api_error api_phase error_domain \
  internal_error_code internal_error_phase timeout_source hot_wake_count \
  hot_wake_latency_max_us hot_drain_packets hot_yields hot_time_ms cold_time_ms
do
  if ! rg -q "$required" source/src/psp_adhoc.h \
    source/src/psp_adhoc.c source/src/main.c source/src/runtime_log_psp.c
  then
    echo "Critical failure snapshot lacks $required" >&2
    exit 1
  fi
done

for required in LINK_TIMEOUT_RECEIVE_SILENCE LINK_TIMEOUT_ACTIVE_TRANSFER \
  LINK_TIMEOUT_AHEAD_PACING LINK_TIMEOUT_PRESSURE \
  LINK_TIMEOUT_RECOVERY_RENDEZVOUS
do
  if ! rg -q "$required" source/src/link_transport.h source/src/link_session.c \
    source/src/main.c tools/decode_runtime_log.py tests/test_link_session.c
  then
    echo "Timeout attribution lacks $required" >&2
    exit 1
  fi
done

session_recovery_body=$(sed -n \
  '/^static void psp_adhoc_consume_session_recovery/,/^}/p' \
  source/src/psp_adhoc.c)
internal_attribution_body=$(sed -n \
  '/^static void psp_adhoc_record_internal_error/,/^}/p' \
  source/src/psp_adhoc.c)
if ! printf '%s\n' "$session_recovery_body" | \
   rg -q 'PSP_ADHOC_ERROR_SESSION_RECOVERY' || \
   ! printf '%s\n' "$session_recovery_body" | rg -q 'timeout_source' || \
   ! printf '%s\n' "$internal_attribution_body" | \
    rg -q 'domain == PSP_ADHOC_ERROR_DOMAIN_SESSION' || \
   ! printf '%s\n' "$internal_attribution_body" | \
    rg -q 'PSP_ADHOC_PHASE_NONE' || \
   printf '%s\n' "$failure_body" | rg -q 'recovery_trigger_phase'
then
  echo "Session recovery errors can be mislabeled as a PDP receive failure" >&2
  exit 1
fi
if rg -q 'last_error[[:space:]]*=[[:space:]]*SCE_ADHOC_ERROR_TIMEOUT' \
  source/src/psp_adhoc.c || \
   ! rg -q 'D%u I%d/0x%08X A%s:%d/0x%08X TO%u' source/src/gui.c || \
   ! rg -q 'RUNTIME_LOG_EVENT_LINK_FAILURE' source/src/main.c \
    source/src/runtime_log.h tools/decode_runtime_log.py || \
   ! rg -q 'RUNTIME_LOG_EVENT_LINK_STREAM_METRICS' source/src/main.c \
    source/src/runtime_log.h tools/decode_runtime_log.py
then
  echo "Structured signed/hex failure attribution is incomplete" >&2
  exit 1
fi
if rg -q 'DATAGRAM_STALL|adhoc_datagram_stalled' source/src tests
then
  echo "Reliable datagrams still trigger an independent recovery timer" >&2
  exit 1
fi
quit_body=$(sed -n '/^void quit(void)/,/^}/p' source/src/main.c)
if ! printf '%s\n' "$quit_body" | rg -q '__sync_bool_compare_and_swap'
then
  echo "Emulator shutdown is not guarded against concurrent callbacks" >&2
  exit 1
fi

runtime_emit_body=$(sed -n \
  '/^void runtime_log_ring_emit/,/^size_t runtime_log_ring_snapshot/p' \
  source/src/runtime_log.c)
if printf '%s\n' "$runtime_emit_body" | rg -q \
  'sceIo|FILE_|printf|malloc|calloc|realloc|sceKernelWait|sceKernelDelay'
then
  echo "Runtime event recording performs a blocking operation" >&2
  exit 1
fi

main_link_service_body=$(sed -n \
  '/^static void main_link_service/,/^u32 update_gba/p' source/src/main.c)
if printf '%s\n' "$main_link_service_body" | \
   rg -q 'runtime_log_psp_flush'
then
  echo "Link wait path writes diagnostics to storage" >&2
  exit 1
fi

if ! rg -q 'test_runtime_log' Makefile || \
   ! rg -q 'test_decode_runtime_log' Makefile || \
   ! rg -q 'RUNTIME_LOG_RING_CAPACITY' source/src/runtime_log.c || \
   ! rg -q 'runtime_log_store_rotate' source/src/runtime_log_psp.c
then
  echo "Runtime diagnostics are not covered by the validation gate" >&2
  exit 1
fi

echo "Runtime lifecycle checks passed"
