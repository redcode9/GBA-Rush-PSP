#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "adhoc_datagram.h"
#include "adhoc_resync.h"

static int failures;

#define CHECK(condition) \
  do \
  { \
    if(!(condition)) \
    { \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
       #condition); \
      failures++; \
    } \
  } while(0)

#define RESYNC_WIRE_CAPACITY 48u
#define RESYNC_KIND_COUNT 11u
#define RESYNC_MODEL_CAPACITY 4096u
#define RECOVERY_ID 0x52454331u
#define DATA_WIRE_CAPACITY 64u

typedef struct
{
  AdhocRendezvousKind kind;
  uint32_t epoch;
  uint32_t recovery_id;
  unsigned int deliver_at;
  uint8_t destination;
  uint8_t valid;
} ResyncWirePacket;

typedef struct
{
  ResyncWirePacket packets[RESYNC_WIRE_CAPACITY];
  unsigned int sent[RESYNC_KIND_COUNT];
  unsigned int drop[RESYNC_KIND_COUNT];
} ResyncWire;

typedef struct
{
  AdhocResync peer[2];
  uint16_t emitted;
  uint8_t reset_count[2];
} ResyncModel;

typedef struct
{
  AdhocDatagramPacket packet;
  uint64_t deliver_at_us;
  uint8_t destination;
  uint8_t valid;
} DataWirePacket;

static ResyncModel resync_models[RESYNC_MODEL_CAPACITY];

static int resync_model_equal(const ResyncModel *left,
 const ResyncModel *right)
{
  unsigned int endpoint;

  if(left->emitted != right->emitted ||
     memcmp(left->reset_count, right->reset_count,
      sizeof(left->reset_count)) != 0)
    return 0;
  for(endpoint = 0; endpoint < 2u; endpoint++)
  {
    const AdhocResync *a = &left->peer[endpoint];
    const AdhocResync *b = &right->peer[endpoint];

    if(a->role != b->role || a->state != b->state ||
       a->current_epoch != b->current_epoch ||
       a->target_epoch != b->target_epoch ||
       a->recovery_id != b->recovery_id ||
       a->local_data_ready != b->local_data_ready ||
       a->peer_data_ready != b->peer_data_ready)
      return 0;
  }
  return 1;
}

static int resync_model_add(const ResyncModel *model,
 unsigned int *count)
{
  unsigned int index;

  for(index = 0; index < *count; index++)
  {
    if(resync_model_equal(&resync_models[index], model))
      return 1;
  }
  if(*count >= RESYNC_MODEL_CAPACITY)
    return 0;
  resync_models[*count] = *model;
  (*count)++;
  return 1;
}

static uint16_t resync_model_kind_bit(AdhocRendezvousKind kind)
{
  return (uint16_t)(1u << (unsigned int)kind);
}

static int resync_model_emit(ResyncModel *model, unsigned int endpoint)
{
  AdhocRendezvousKind kind;
  uint32_t epoch;
  uint32_t recovery_id;
  uint16_t bit;

  if(!adhoc_resync_outbound(&model->peer[endpoint], &kind, &epoch,
      &recovery_id))
    return 0;
  CHECK((unsigned int)kind < RESYNC_KIND_COUNT);
  CHECK(epoch != 0u);
  CHECK(recovery_id != 0u);
  bit = resync_model_kind_bit(kind);
  if((model->emitted & bit) != 0u)
    return 0;
  model->emitted |= bit;
  return 1;
}

static unsigned int resync_model_receive(ResyncModel *model,
 unsigned int destination, AdhocRendezvousKind kind,
 uint32_t epoch, uint32_t recovery_id)
{
  unsigned int actions = adhoc_resync_receive(&model->peer[destination],
   kind, epoch, recovery_id);

  if((actions & ADHOC_RESYNC_ACTION_RESET_EPOCH) != 0)
  {
    model->reset_count[destination]++;
    CHECK(model->reset_count[destination] == 1u);
  }
  if((actions & ADHOC_RESYNC_ACTION_FINAL_ACK) != 0)
    model->emitted |= resync_model_kind_bit(
     ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK);
  return actions;
}

static int resync_model_deliver(ResyncModel *model,
 AdhocRendezvousKind kind, uint32_t joiner_id)
{
  unsigned int destination;
  uint32_t epoch;
  uint32_t recovery_id;

  if((model->emitted & resync_model_kind_bit(kind)) == 0u)
    return 0;
  switch(kind)
  {
    case ADHOC_RENDEZVOUS_RESYNC_REQUEST:
      destination = 0u;
      epoch = 1u;
      recovery_id = joiner_id;
      break;

    case ADHOC_RENDEZVOUS_RESYNC_BEGIN:
    case ADHOC_RENDEZVOUS_RESYNC_COMMIT:
    case ADHOC_RENDEZVOUS_RESYNC_COMPLETE:
      destination = 1u;
      epoch = 2u;
      recovery_id = RECOVERY_ID;
      break;

    case ADHOC_RENDEZVOUS_RESYNC_READY:
    case ADHOC_RENDEZVOUS_RESYNC_COMMIT_ACK:
    case ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK:
    case ADHOC_RENDEZVOUS_RESYNC_DATA_READY:
      destination = 0u;
      epoch = 2u;
      recovery_id = RECOVERY_ID;
      break;

    default:
      return 0;
  }
  resync_model_receive(model, destination, kind, epoch, recovery_id);
  return 1;
}

static int resync_model_safe(const ResyncModel *model)
{
  const AdhocResync *host = &model->peer[0];
  const AdhocResync *joiner = &model->peer[1];

  if(host->role != ADHOC_RESYNC_HOST ||
     joiner->role != ADHOC_RESYNC_JOINER ||
     host->target_epoch != 2u ||
     joiner->target_epoch != (joiner->state == ADHOC_RESYNC_WAIT_BEGIN ?
      1u : 2u) ||
     (host->current_epoch != 1u && host->current_epoch != 2u) ||
     (joiner->current_epoch != 1u && joiner->current_epoch != 2u) ||
     host->recovery_id != RECOVERY_ID ||
     model->reset_count[0] > 1u || model->reset_count[1] > 1u)
    return 0;
  if(host->state >= ADHOC_RESYNC_WAIT_COMMIT_ACK &&
     host->current_epoch != 2u)
    return 0;
  if(joiner->state >= ADHOC_RESYNC_WAIT_COMMIT &&
     joiner->recovery_id != RECOVERY_ID)
    return 0;
  if(joiner->state >= ADHOC_RESYNC_WAIT_COMPLETE &&
     joiner->current_epoch != 2u)
    return 0;
  if(host->state == ADHOC_RESYNC_COMPLETE &&
     joiner->state != ADHOC_RESYNC_COMPLETE)
    return 0;
  return 1;
}

static int resync_model_fair_complete(const ResyncModel *input)
{
  ResyncModel model = *input;
  unsigned int round;

  for(round = 0; round < 16u; round++)
  {
    unsigned int endpoint;

    if(model.peer[0].current_epoch == model.peer[0].target_epoch &&
       model.peer[1].current_epoch == model.peer[1].target_epoch)
    {
      for(endpoint = 0; endpoint < 2u; endpoint++)
      {
        if(adhoc_resync_data_phase(&model.peer[endpoint]))
          (void)adhoc_resync_mark_data_ready(&model.peer[endpoint]);
      }
    }

    for(endpoint = 0; endpoint < 2u; endpoint++)
    {
      AdhocRendezvousKind kind;
      uint32_t epoch;
      uint32_t recovery_id;
      unsigned int actions;

      if(!adhoc_resync_outbound(&model.peer[endpoint], &kind, &epoch,
          &recovery_id))
        continue;
      actions = resync_model_receive(&model, endpoint ^ 1u, kind, epoch,
       recovery_id);
      if((actions & ADHOC_RESYNC_ACTION_FINAL_ACK) != 0)
      {
        resync_model_receive(&model, 0u,
         ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK, epoch, recovery_id);
      }
    }
    if(!resync_model_safe(&model))
      return 0;
    if(adhoc_resync_complete(&model.peer[0]) &&
       adhoc_resync_complete(&model.peer[1]))
      return 1;
  }
  return 0;
}

static void test_arbitrary_finite_fault_closure(void)
{
  static const uint32_t joiner_ids[] =
   { RECOVERY_ID, RECOVERY_ID + 1u };
  unsigned int scenario;

  for(scenario = 0; scenario < sizeof(joiner_ids) /
      sizeof(joiner_ids[0]); scenario++)
  {
    unsigned int count = 1u;
    unsigned int cursor;

    memset(resync_models, 0, sizeof(resync_models));
    adhoc_resync_init(&resync_models[0].peer[0], ADHOC_RESYNC_HOST,
     1u, RECOVERY_ID);
    adhoc_resync_init(&resync_models[0].peer[1], ADHOC_RESYNC_JOINER,
     1u, joiner_ids[scenario]);
    for(cursor = 0; cursor < count; cursor++)
    {
      ResyncModel base = resync_models[cursor];
      unsigned int endpoint;
      unsigned int kind;

      CHECK(resync_model_safe(&base));
      if(base.peer[0].current_epoch == base.peer[0].target_epoch &&
         base.peer[1].current_epoch == base.peer[1].target_epoch)
      {
        for(endpoint = 0; endpoint < 2u; endpoint++)
        {
          ResyncModel next = base;

          if(!adhoc_resync_data_phase(&next.peer[endpoint]) ||
             next.peer[endpoint].local_data_ready)
            continue;
          (void)adhoc_resync_mark_data_ready(&next.peer[endpoint]);
          CHECK(resync_model_add(&next, &count));
        }
      }
      for(endpoint = 0; endpoint < 2u; endpoint++)
      {
        ResyncModel next = base;

        if(resync_model_emit(&next, endpoint))
          CHECK(resync_model_add(&next, &count));
      }
      for(kind = ADHOC_RENDEZVOUS_RESYNC_REQUEST;
          kind <= ADHOC_RENDEZVOUS_RESYNC_DATA_READY; kind++)
      {
        ResyncModel next = base;
        AdhocResyncState old_host = next.peer[0].state;
        AdhocResyncState old_joiner = next.peer[1].state;

        if(!resync_model_deliver(&next, (AdhocRendezvousKind)kind,
            joiner_ids[scenario]))
          continue;
        CHECK(next.peer[0].state >= old_host);
        CHECK(next.peer[1].state >= old_joiner);
        CHECK(resync_model_safe(&next));
        CHECK(resync_model_add(&next, &count));
      }
    }
    CHECK(count < RESYNC_MODEL_CAPACITY);
    for(cursor = 0; cursor < count; cursor++)
      CHECK(resync_model_fair_complete(&resync_models[cursor]));
    printf("Resync closure %u explored %u reachable states\n",
     scenario, count);
  }
}

static void resync_wire_send(ResyncWire *wire,
 AdhocRendezvousKind kind, uint32_t epoch, uint32_t recovery_id,
 uint8_t destination, unsigned int now)
{
  unsigned int index;
  unsigned int delay;

  CHECK((unsigned int)kind < RESYNC_KIND_COUNT);
  if((unsigned int)kind >= RESYNC_KIND_COUNT)
    return;
  wire->sent[kind]++;
  if(wire->drop[kind] != 0)
  {
    wire->drop[kind]--;
    return;
  }
  delay = ((unsigned int)kind * 3u + wire->sent[kind]) % 5u;
  for(index = 0; index < RESYNC_WIRE_CAPACITY; index++)
  {
    if(wire->packets[index].valid)
      continue;
    wire->packets[index].kind = kind;
    wire->packets[index].epoch = epoch;
    wire->packets[index].recovery_id = recovery_id;
    wire->packets[index].deliver_at = now + delay;
    wire->packets[index].destination = destination;
    wire->packets[index].valid = 1;
    break;
  }
  CHECK(index != RESYNC_WIRE_CAPACITY);

  if(wire->sent[kind] == 2u)
  {
    for(index = 0; index < RESYNC_WIRE_CAPACITY; index++)
    {
      if(wire->packets[index].valid)
        continue;
      wire->packets[index].kind = kind;
      wire->packets[index].epoch = epoch;
      wire->packets[index].recovery_id = recovery_id;
      wire->packets[index].deliver_at = now + delay + 2u;
      wire->packets[index].destination = destination;
      wire->packets[index].valid = 1;
      break;
    }
    CHECK(index != RESYNC_WIRE_CAPACITY);
  }
}

static void test_faulted_completion(void)
{
  AdhocResync peer[2];
  ResyncWire wire;
  unsigned int reset_count[2] = { 0, 0 };
  unsigned int tick;
  unsigned int kind;

  memset(&wire, 0, sizeof(wire));
  for(kind = ADHOC_RENDEZVOUS_RESYNC_REQUEST;
      kind <= ADHOC_RENDEZVOUS_RESYNC_DATA_READY; kind++)
    wire.drop[kind] = 1;
  wire.drop[ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK] = 4;

  adhoc_resync_init(&peer[0], ADHOC_RESYNC_HOST, 1, RECOVERY_ID);
  adhoc_resync_init(&peer[1], ADHOC_RESYNC_JOINER, 1, RECOVERY_ID);
  for(tick = 0; tick < 500u; tick++)
  {
    unsigned int endpoint;
    unsigned int index;

    if(peer[0].current_epoch == peer[0].target_epoch &&
       peer[1].current_epoch == peer[1].target_epoch)
    {
      for(endpoint = 0; endpoint < 2u; endpoint++)
      {
        if(adhoc_resync_data_phase(&peer[endpoint]))
          (void)adhoc_resync_mark_data_ready(&peer[endpoint]);
      }
    }

    if(tick % 3u == 0)
    {
      for(endpoint = 0; endpoint < 2; endpoint++)
      {
        AdhocRendezvousKind outbound_kind;
        uint32_t epoch;
        uint32_t recovery_id;

        if(adhoc_resync_outbound(&peer[endpoint], &outbound_kind,
           &epoch, &recovery_id))
        {
          resync_wire_send(&wire, outbound_kind, epoch, recovery_id,
           (uint8_t)(endpoint ^ 1u), tick);
        }
      }
    }

    for(index = RESYNC_WIRE_CAPACITY; index-- != 0;)
    {
      ResyncWirePacket packet;
      unsigned int actions;

      if(!wire.packets[index].valid ||
         wire.packets[index].deliver_at > tick)
        continue;
      packet = wire.packets[index];
      wire.packets[index].valid = 0;
      actions = adhoc_resync_receive(&peer[packet.destination],
       packet.kind, packet.epoch, packet.recovery_id);
      if((actions & ADHOC_RESYNC_ACTION_RESET_EPOCH) != 0)
        reset_count[packet.destination]++;
      if((actions & ADHOC_RESYNC_ACTION_FINAL_ACK) != 0)
      {
        resync_wire_send(&wire,
         ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK, packet.epoch,
         packet.recovery_id,
         (uint8_t)(packet.destination ^ 1u), tick);
      }
    }

    if(adhoc_resync_complete(&peer[0]) &&
       adhoc_resync_complete(&peer[1]))
      break;
  }

  CHECK(tick < 500u);
  CHECK(peer[0].current_epoch == 2);
  CHECK(peer[1].current_epoch == 2);
  CHECK(reset_count[0] == 1);
  CHECK(reset_count[1] == 1);
  CHECK(wire.sent[ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK] > 4u);
}

static void test_stale_epoch_is_ignored(void)
{
  AdhocResync host;
  AdhocRendezvousKind kind;
  uint32_t epoch = 0;

  adhoc_resync_init(&host, ADHOC_RESYNC_HOST, 9, RECOVERY_ID);
  CHECK(adhoc_resync_receive(&host, ADHOC_RENDEZVOUS_RESYNC_READY,
   9, RECOVERY_ID) == ADHOC_RESYNC_ACTION_NONE);
  CHECK(host.state == ADHOC_RESYNC_WAIT_READY);
  CHECK(adhoc_resync_outbound(&host, &kind, &epoch, &(uint32_t){ 0 }));
  CHECK(kind == ADHOC_RENDEZVOUS_RESYNC_BEGIN);
  CHECK(epoch == 10);
}

static void test_delayed_request_does_not_restart_epoch(void)
{
  AdhocResync host;
  AdhocRendezvousKind kind;
  uint32_t epoch = 0;
  unsigned int replay;
  unsigned int actions;

  adhoc_resync_init(&host, ADHOC_RESYNC_HOST, 9, RECOVERY_ID);
  actions = adhoc_resync_receive(&host,
   ADHOC_RENDEZVOUS_RESYNC_READY, 10, RECOVERY_ID);
  CHECK((actions & ADHOC_RESYNC_ACTION_RESET_EPOCH) != 0);
  CHECK(host.current_epoch == 10);
  CHECK(host.state == ADHOC_RESYNC_WAIT_COMMIT_ACK);

  for(replay = 0; replay < 180u; replay++)
  {
    actions = adhoc_resync_receive(&host,
     ADHOC_RENDEZVOUS_RESYNC_REQUEST, 9, RECOVERY_ID);
    CHECK(actions == ADHOC_RESYNC_ACTION_NONE);
    CHECK(host.state == ADHOC_RESYNC_WAIT_COMMIT_ACK);
    CHECK(host.current_epoch == 10);
  }
  CHECK(adhoc_resync_outbound(&host, &kind, &epoch, &(uint32_t){ 0 }));
  CHECK(kind == ADHOC_RENDEZVOUS_RESYNC_COMMIT);
  CHECK(epoch == 10);

  actions = adhoc_resync_receive(&host,
   ADHOC_RENDEZVOUS_RESYNC_REQUEST, 10, RECOVERY_ID);
  CHECK(actions == ADHOC_RESYNC_ACTION_SEND_NOW);
  CHECK(host.state == ADHOC_RESYNC_WAIT_COMMIT_ACK);
}

static void test_same_epoch_replay_is_idempotent(void)
{
  AdhocResync joiner;
  AdhocRendezvousKind kind;
  uint32_t epoch = 0;
  unsigned int actions;

  adhoc_resync_init(&joiner, ADHOC_RESYNC_JOINER, 2, RECOVERY_ID);
  actions = adhoc_resync_receive(&joiner,
   ADHOC_RENDEZVOUS_RESYNC_BEGIN, 2, RECOVERY_ID);
  CHECK((actions & ADHOC_RESYNC_ACTION_RESET_EPOCH) == 0);
  CHECK((actions & ADHOC_RESYNC_ACTION_SEND_NOW) != 0);
  CHECK(adhoc_resync_outbound(&joiner, &kind, &epoch,
   &(uint32_t){ 0 }));
  CHECK(kind == ADHOC_RENDEZVOUS_RESYNC_READY && epoch == 2);

  actions = adhoc_resync_receive(&joiner,
   ADHOC_RENDEZVOUS_RESYNC_COMMIT, 2, RECOVERY_ID);
  CHECK((actions & ADHOC_RESYNC_ACTION_SEND_NOW) != 0);
  CHECK(adhoc_resync_outbound(&joiner, &kind, &epoch,
   &(uint32_t){ 0 }));
  CHECK(kind == ADHOC_RENDEZVOUS_RESYNC_COMMIT_ACK);

  CHECK(adhoc_resync_data_phase(&joiner));
  CHECK((adhoc_resync_mark_data_ready(&joiner) &
   ADHOC_RESYNC_ACTION_SEND_NOW) != 0);
  CHECK(adhoc_resync_outbound(&joiner, &kind, &epoch,
   &(uint32_t){ 0 }));
  CHECK(kind == ADHOC_RENDEZVOUS_RESYNC_DATA_READY);

  actions = adhoc_resync_receive(&joiner,
   ADHOC_RENDEZVOUS_RESYNC_COMPLETE, 2, RECOVERY_ID);
  CHECK((actions & ADHOC_RESYNC_ACTION_COMPLETE) != 0);
  CHECK((actions & ADHOC_RESYNC_ACTION_FINAL_ACK) != 0);
  actions = adhoc_resync_receive(&joiner,
   ADHOC_RENDEZVOUS_RESYNC_COMPLETE, 2, RECOVERY_ID);
  CHECK((actions & ADHOC_RESYNC_ACTION_FINAL_ACK) != 0);
}

static void test_epoch_wrap(void)
{
  AdhocResync host;
  AdhocRendezvousKind kind;
  uint32_t epoch = 0;

  adhoc_resync_init(&host, ADHOC_RESYNC_HOST, UINT32_MAX, RECOVERY_ID);
  CHECK(adhoc_resync_outbound(&host, &kind, &epoch,
   &(uint32_t){ 0 }));
  CHECK(kind == ADHOC_RENDEZVOUS_RESYNC_BEGIN);
  CHECK(epoch == 1);
}

static void test_data_probe_binds_role_epoch_and_recovery(void)
{
  AdhocResync host;
  AdhocResync joiner;
  AdhocRendezvousKind kind;
  uint8_t host_probe[ADHOC_RESYNC_PROBE_SIZE];
  uint8_t joiner_probe[ADHOC_RESYNC_PROBE_SIZE];
  uint32_t epoch;
  uint32_t recovery_id;

  adhoc_resync_init(&host, ADHOC_RESYNC_HOST, 1u, RECOVERY_ID);
  adhoc_resync_init(&joiner, ADHOC_RESYNC_JOINER, 1u, RECOVERY_ID);
  CHECK(adhoc_resync_outbound(&host, &kind, &epoch, &recovery_id));
  (void)adhoc_resync_receive(&joiner, kind, epoch, recovery_id);
  CHECK(adhoc_resync_outbound(&joiner, &kind, &epoch, &recovery_id));
  (void)adhoc_resync_receive(&host, kind, epoch, recovery_id);
  CHECK(adhoc_resync_outbound(&host, &kind, &epoch, &recovery_id));
  (void)adhoc_resync_receive(&joiner, kind, epoch, recovery_id);

  CHECK(adhoc_resync_data_phase(&host));
  CHECK(adhoc_resync_data_phase(&joiner));
  CHECK(adhoc_resync_encode_probe(&host, host_probe,
   sizeof(host_probe)));
  CHECK(adhoc_resync_encode_probe(&joiner, joiner_probe,
   sizeof(joiner_probe)));
  CHECK(adhoc_resync_accept_probe(&host, joiner_probe,
   sizeof(joiner_probe)));
  CHECK(adhoc_resync_accept_probe(&joiner, host_probe,
   sizeof(host_probe)));
  CHECK(!adhoc_resync_accept_probe(&host, host_probe,
   sizeof(host_probe)));
  joiner_probe[15] ^= 1u;
  CHECK(!adhoc_resync_accept_probe(&host, joiner_probe,
   sizeof(joiner_probe)));
}

static void test_data_barrier_survives_bidirectional_loss(void)
{
  AdhocResync resync[2];
  AdhocDatagram datagram[2];
  DataWirePacket wire[DATA_WIRE_CAPACITY];
  AdhocRendezvousKind kind;
  uint8_t probe[2][ADHOC_RESYNC_PROBE_SIZE];
  uint8_t payload[ADHOC_DATAGRAM_MAX_PAYLOAD];
  uint32_t epoch;
  uint32_t recovery_id;
  uint64_t now_us;
  unsigned int actions;
  unsigned int sent[2] = { 0u, 0u };
  unsigned int side;
  int peer_probe[2] = { 0, 0 };

  memset(wire, 0, sizeof(wire));
  adhoc_resync_init(&resync[0], ADHOC_RESYNC_HOST, 1u, RECOVERY_ID);
  adhoc_resync_init(&resync[1], ADHOC_RESYNC_JOINER, 1u, RECOVERY_ID);

  CHECK(adhoc_resync_outbound(&resync[0], &kind, &epoch, &recovery_id));
  (void)adhoc_resync_receive(&resync[1], kind, epoch, recovery_id);
  CHECK(adhoc_resync_outbound(&resync[1], &kind, &epoch, &recovery_id));
  actions = adhoc_resync_receive(&resync[0], kind, epoch, recovery_id);
  CHECK((actions & ADHOC_RESYNC_ACTION_RESET_EPOCH) != 0);
  adhoc_datagram_init(&datagram[0], 0x44544131u,
   resync[0].current_epoch, 0u);
  adhoc_datagram_set_role(&datagram[0], 1);

  CHECK(adhoc_resync_outbound(&resync[0], &kind, &epoch, &recovery_id));
  actions = adhoc_resync_receive(&resync[1], kind, epoch, recovery_id);
  CHECK((actions & ADHOC_RESYNC_ACTION_RESET_EPOCH) != 0);
  adhoc_datagram_init(&datagram[1], 0x44544131u,
   resync[1].current_epoch, 0u);
  adhoc_datagram_set_role(&datagram[1], 0);
  CHECK(adhoc_resync_outbound(&resync[1], &kind, &epoch, &recovery_id));
  (void)adhoc_resync_receive(&resync[0], kind, epoch, recovery_id);

  for(side = 0; side < 2u; side++)
  {
    CHECK(adhoc_resync_encode_probe(&resync[side], probe[side],
     sizeof(probe[side])));
    CHECK(adhoc_datagram_queue(&datagram[side], probe[side],
     sizeof(probe[side])));
  }

  for(now_us = 0u; now_us < 5000000u; now_us += 1000u)
  {
    unsigned int index;

    for(side = 0; side < 2u; side++)
    {
      AdhocDatagramPacket packet;

      if(!adhoc_datagram_prepare(&datagram[side], now_us, &packet))
        continue;
      CHECK(adhoc_datagram_note_sent(&datagram[side], &packet, now_us));
      sent[side]++;
      if(sent[side] <= 2u)
        continue;
      for(index = 0; index < DATA_WIRE_CAPACITY; index++)
      {
        if(wire[index].valid)
          continue;
        wire[index].packet = packet;
        wire[index].deliver_at_us = now_us +
         ((sent[side] * 3u + side * 5u) % 7u) * 1000u;
        wire[index].destination = (uint8_t)(side ^ 1u);
        wire[index].valid = 1;
        break;
      }
      CHECK(index != DATA_WIRE_CAPACITY);
    }

    for(side = DATA_WIRE_CAPACITY; side-- != 0u;)
    {
      DataWirePacket queued;
      AdhocDatagramReceiveResult received;
      size_t payload_size;
      unsigned int destination;

      if(!wire[side].valid || wire[side].deliver_at_us > now_us)
        continue;
      queued = wire[side];
      wire[side].valid = 0;
      destination = queued.destination;
      received = adhoc_datagram_receive(&datagram[destination],
       queued.packet.data, queued.packet.size, now_us);
      CHECK(received == ADHOC_DATAGRAM_ACCEPTED ||
       received == ADHOC_DATAGRAM_DUPLICATE);
      while(adhoc_datagram_pop(&datagram[destination], payload,
            sizeof(payload), &payload_size))
      {
        if(adhoc_resync_accept_probe(&resync[destination], payload,
           payload_size))
          peer_probe[destination] = 1;
      }
    }

    for(side = 0; side < 2u; side++)
    {
      if(!resync[side].local_data_ready && peer_probe[side] &&
         adhoc_datagram_outstanding(&datagram[side]) == 0u)
        (void)adhoc_resync_mark_data_ready(&resync[side]);
    }
    if(resync[0].local_data_ready && resync[1].local_data_ready)
      break;
  }

  CHECK(now_us < 5000000u);
  CHECK(datagram[0].retransmissions != 0u);
  CHECK(datagram[1].retransmissions != 0u);
  CHECK(adhoc_resync_outbound(&resync[1], &kind, &epoch, &recovery_id));
  CHECK(kind == ADHOC_RENDEZVOUS_RESYNC_DATA_READY);
  CHECK((adhoc_resync_receive(&resync[0], kind, epoch, recovery_id) &
   ADHOC_RESYNC_ACTION_SEND_NOW) != 0);
  CHECK(adhoc_resync_outbound(&resync[0], &kind, &epoch, &recovery_id));
  CHECK(kind == ADHOC_RENDEZVOUS_RESYNC_COMPLETE);
  actions = adhoc_resync_receive(&resync[1], kind, epoch, recovery_id);
  CHECK((actions & ADHOC_RESYNC_ACTION_FINAL_ACK) != 0);
  CHECK((actions & ADHOC_RESYNC_ACTION_COMPLETE) != 0);
  CHECK((adhoc_resync_receive(&resync[0],
   ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK, epoch, recovery_id) &
   ADHOC_RESYNC_ACTION_COMPLETE) != 0);
}

static void test_request_gate_rejects_single_and_link_duplicates(void)
{
  AdhocResyncRequestGate gate;

  adhoc_resync_request_gate_init(&gate);
  CHECK(adhoc_resync_request_gate_note(&gate, 4, 4, RECOVERY_ID, 1000) ==
   ADHOC_RESYNC_REQUEST_PENDING);
  CHECK(adhoc_resync_request_gate_note(&gate, 4, 4, RECOVERY_ID, 2000) ==
   ADHOC_RESYNC_REQUEST_IGNORED);
  CHECK(gate.confirmations == 1);
  CHECK(adhoc_resync_request_gate_note(&gate, 4, 3, RECOVERY_ID,
   100000) == ADHOC_RESYNC_REQUEST_IGNORED);
  CHECK(gate.confirmations == 1);
}

static void test_request_gate_accepts_real_retry(void)
{
  AdhocResyncRequestGate gate;

  adhoc_resync_request_gate_init(&gate);
  CHECK(adhoc_resync_request_gate_note(&gate, 7, 7, RECOVERY_ID, 1000) ==
   ADHOC_RESYNC_REQUEST_PENDING);
  CHECK(adhoc_resync_request_gate_note(&gate, 7, 7, RECOVERY_ID, 101000) ==
   ADHOC_RESYNC_REQUEST_ACCEPTED);
  CHECK(gate.confirmations == 0);

  CHECK(adhoc_resync_request_gate_note(&gate, 7, 7, RECOVERY_ID, 700000) ==
   ADHOC_RESYNC_REQUEST_PENDING);
  CHECK(adhoc_resync_request_gate_note(&gate, 7, 7,
   RECOVERY_ID + 1u, 800000) == ADHOC_RESYNC_REQUEST_PENDING);
  CHECK(gate.recovery_id == RECOVERY_ID + 1u);
}

static void test_request_gate_settling_rejects_stale_retry(void)
{
  AdhocResyncRequestGate gate;

  adhoc_resync_request_gate_settle(&gate, 1000000u, 750000u);
  CHECK(adhoc_resync_request_gate_note(&gate, 8, 8, RECOVERY_ID,
   1100000u) == ADHOC_RESYNC_REQUEST_IGNORED);
  CHECK(adhoc_resync_request_gate_note(&gate, 8, 8, RECOVERY_ID,
   1749999u) == ADHOC_RESYNC_REQUEST_IGNORED);
  CHECK(adhoc_resync_request_gate_note(&gate, 8, 8, RECOVERY_ID,
   1750000u) == ADHOC_RESYNC_REQUEST_PENDING);
}

static void test_transaction_id_isolates_stale_control(void)
{
  AdhocResync host;
  AdhocResync joiner;
  unsigned int actions;

  adhoc_resync_init(&host, ADHOC_RESYNC_HOST, 3, RECOVERY_ID);
  actions = adhoc_resync_receive(&host, ADHOC_RENDEZVOUS_RESYNC_READY,
   4, RECOVERY_ID + 1u);
  CHECK(actions == ADHOC_RESYNC_ACTION_NONE);
  CHECK(host.state == ADHOC_RESYNC_WAIT_READY);

  adhoc_resync_init(&joiner, ADHOC_RESYNC_JOINER, 3,
   RECOVERY_ID + 1u);
  actions = adhoc_resync_receive(&joiner,
   ADHOC_RENDEZVOUS_RESYNC_BEGIN, 4, RECOVERY_ID);
  CHECK((actions & ADHOC_RESYNC_ACTION_SEND_NOW) != 0);
  CHECK(joiner.recovery_id == RECOVERY_ID);
  actions = adhoc_resync_receive(&joiner,
   ADHOC_RENDEZVOUS_RESYNC_COMMIT, 4, RECOVERY_ID + 1u);
  CHECK(actions == ADHOC_RESYNC_ACTION_NONE);
  CHECK(joiner.state == ADHOC_RESYNC_WAIT_COMMIT);
}

static void test_host_transaction_wins_simultaneous_recovery(void)
{
  const uint32_t host_id = RECOVERY_ID;
  const uint32_t joiner_id = RECOVERY_ID + 1u;
  AdhocResync host;
  AdhocResync joiner;
  AdhocRendezvousKind kind;
  uint32_t epoch;
  uint32_t recovery_id;
  unsigned int actions;

  adhoc_resync_init(&host, ADHOC_RESYNC_HOST, 11, host_id);
  adhoc_resync_init(&joiner, ADHOC_RESYNC_JOINER, 11, joiner_id);
  CHECK(adhoc_resync_outbound(&joiner, &kind, &epoch, &recovery_id));
  CHECK(kind == ADHOC_RENDEZVOUS_RESYNC_REQUEST);
  CHECK(recovery_id == joiner_id);
  CHECK(adhoc_resync_receive(&host, kind, epoch, recovery_id) ==
   ADHOC_RESYNC_ACTION_NONE);

  CHECK(adhoc_resync_outbound(&host, &kind, &epoch, &recovery_id));
  CHECK(kind == ADHOC_RENDEZVOUS_RESYNC_BEGIN);
  actions = adhoc_resync_receive(&joiner, kind, epoch, recovery_id);
  CHECK((actions & ADHOC_RESYNC_ACTION_RESET_EPOCH) == 0);
  CHECK(joiner.recovery_id == host_id);

  CHECK(adhoc_resync_outbound(&joiner, &kind, &epoch, &recovery_id));
  actions = adhoc_resync_receive(&host, kind, epoch, recovery_id);
  CHECK((actions & ADHOC_RESYNC_ACTION_RESET_EPOCH) != 0);
  CHECK(adhoc_resync_outbound(&host, &kind, &epoch, &recovery_id));
  CHECK((adhoc_resync_receive(&joiner, kind, epoch, recovery_id) &
   (ADHOC_RESYNC_ACTION_SEND_NOW |
    ADHOC_RESYNC_ACTION_RESET_EPOCH)) ==
   (ADHOC_RESYNC_ACTION_SEND_NOW |
    ADHOC_RESYNC_ACTION_RESET_EPOCH));
  CHECK(adhoc_resync_outbound(&joiner, &kind, &epoch, &recovery_id));
  CHECK(kind == ADHOC_RENDEZVOUS_RESYNC_COMMIT_ACK);
  (void)adhoc_resync_receive(&host, kind, epoch, recovery_id);

  CHECK(adhoc_resync_data_phase(&host));
  CHECK(adhoc_resync_data_phase(&joiner));
  (void)adhoc_resync_mark_data_ready(&host);
  CHECK((adhoc_resync_mark_data_ready(&joiner) &
   ADHOC_RESYNC_ACTION_SEND_NOW) != 0);
  CHECK(adhoc_resync_outbound(&joiner, &kind, &epoch, &recovery_id));
  CHECK(kind == ADHOC_RENDEZVOUS_RESYNC_DATA_READY);
  CHECK((adhoc_resync_receive(&host, kind, epoch, recovery_id) &
   ADHOC_RESYNC_ACTION_SEND_NOW) != 0);
  CHECK(adhoc_resync_outbound(&host, &kind, &epoch, &recovery_id));
  CHECK(kind == ADHOC_RENDEZVOUS_RESYNC_COMPLETE);
  actions = adhoc_resync_receive(&joiner, kind, epoch, recovery_id);
  CHECK((actions & ADHOC_RESYNC_ACTION_COMPLETE) != 0);
  CHECK((actions & ADHOC_RESYNC_ACTION_FINAL_ACK) != 0);
  CHECK((adhoc_resync_receive(&host,
   ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK, epoch, recovery_id) &
   ADHOC_RESYNC_ACTION_COMPLETE) != 0);
  CHECK(adhoc_resync_complete(&host));
  CHECK(adhoc_resync_complete(&joiner));
}

static void test_retry_schedule_breaks_symmetric_lockstep(void)
{
  uint64_t host = adhoc_resync_send_delay(ADHOC_RESYNC_HOST, 1);
  uint64_t joiner = adhoc_resync_send_delay(ADHOC_RESYNC_JOINER, 1);
  uint64_t host_retry = adhoc_resync_send_delay(ADHOC_RESYNC_HOST, 0);
  uint64_t joiner_retry = adhoc_resync_send_delay(ADHOC_RESYNC_JOINER, 0);
  unsigned int host_solo = 0u;
  unsigned int joiner_solo = 0u;
  unsigned int step;

  CHECK(host == 0u);
  CHECK(joiner != 0u);
  CHECK(host_retry != 0u);
  CHECK(joiner_retry != 0u);
  CHECK(host_retry != joiner_retry);

  for(step = 0u; step < 128u && host < 8000000u &&
      joiner < 8000000u; step++)
  {
    uint64_t difference = host > joiner ? host - joiner : joiner - host;

    if(difference <= 1000u)
    {
      host += host_retry;
      joiner += joiner_retry;
    }
    else if(host < joiner)
    {
      host_solo++;
      host += host_retry;
    }
    else
    {
      joiner_solo++;
      joiner += joiner_retry;
    }
  }
  CHECK(host_solo >= 20u);
  CHECK(joiner_solo >= 20u);
}

int main(void)
{
  test_arbitrary_finite_fault_closure();
  test_faulted_completion();
  test_stale_epoch_is_ignored();
  test_delayed_request_does_not_restart_epoch();
  test_same_epoch_replay_is_idempotent();
  test_epoch_wrap();
  test_data_probe_binds_role_epoch_and_recovery();
  test_data_barrier_survives_bidirectional_loss();
  test_request_gate_rejects_single_and_link_duplicates();
  test_request_gate_accepts_real_retry();
  test_request_gate_settling_rejects_stale_retry();
  test_transaction_id_isolates_stale_control();
  test_host_transaction_wins_simultaneous_recovery();
  test_retry_schedule_breaks_symmetric_lockstep();

  if(failures != 0)
  {
    fprintf(stderr, "%d ad-hoc resync test(s) failed\n", failures);
    return 1;
  }
  puts("ad-hoc resync tests passed");
  return 0;
}
