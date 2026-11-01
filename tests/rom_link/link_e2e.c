/*
 * End-to-end link test with real games: two mGBA cores, each with a serial
 * driver that forwards the SIO registers to GBA Rush PSP's gba_sio and
 * link_cable code, joined by a simulated Wi-Fi network with delay, jitter,
 * loss and duplicates. A script drives the buttons on both consoles.
 *
 * Usage: link_e2e ROM0 SAVE0 ROM1 SAVE1 BIOS SCRIPT
 *
 * Script lines (frames count on each console, so a slower console stays
 * in step with the script):
 *   run <frames> [<keys0>] [<keys1>]   hold keys (A+B+START... or -)
 *   tap <side> <keys> [frames]          press keys on one side, then release
 *   link [lockstep]                     start the cable (console 0 hosts);
 *                                       lockstep forces the generic mode
 *   net <latency_us> <jitter_us> <loss%> <dup%>
 *   outage <period_ms> <length_ms>      drop every datagram for the last
 *                                       length_ms of each period
 *   seed <n>                            reseed the network's randomness
 *   speed <side> <factor>               guest speed relative to real time
 *   psp <side> <burst_ms>               run like the PSP: each frame is
 *                                       emulated in one burst of burst_ms,
 *                                       and the link is not serviced while
 *                                       the console waits for VBlank
 *   hiccup <side> <period_ms> <length_ms>  freeze the console, link
 *                                       included, once per period
 *   stack <side> <send_yield_us>        model the PSP's network stack, part
 *                                       of which runs below the emulator:
 *                                       datagrams move only while the
 *                                       console sleeps. It sleeps 2 ms after
 *                                       each late frame, and send_yield_us
 *                                       after each datagram it sends while
 *                                       emulating, in every mode; a PSP
 *                                       does so only in lockstep mode
 *   linked <transfers>                  fail unless both cables are connected
 *                                       and this many transfers went through
 *   same <percent> [x y w h]            fail unless at least this share of
 *                                       the two screens' pixels match, or of
 *                                       the given area, such as a race clock
 *   minfps <fps>                        the speed `pace end` must reach
 *   pace <start|end>                    measure frames per simulated second
 *                                       between the two lines and fail
 *                                       below minfps on either console
 *   ram16 <address> <tolerance> <min>   fail unless the 16-bit value at
 *                                       this work RAM address is at least
 *                                       min on both consoles and differs by
 *                                       at most tolerance, such as a race
 *                                       clock both games keep
 *   shot <name>                         write <name>-0.bmp and <name>-1.bmp
 *   dump <name>                         write both saves to <name>-N.sav
 *   state <save|load> <name>            savestates, before linking only
 *   trace <0|1>                         print every serial transfer
 *   status                              print link counters
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GBA Rush's gba_sio.h and mGBA's sio.h share an include guard. */
#include "gba_sio.h"
#include "link_cable.h"
#undef GBA_SIO_H

#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/config.h>
#include <mgba/core/timing.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/sio.h>
#include <mgba-util/vfs.h>

#define CYCLES_PER_US 16.777216
#define TICK_US 100u
#define FRAME_CYCLES 280896u
#define FRAME_US 16743u
/* Guest cycles between the run loop's link services (service_callback);
   main.c's idle interval, MAIN_LINK_SERVICE_CYCLES, is 16384. */
#define SERVICE_CYCLES 4096
/* main.c MAIN_LINK_YIELD_US: a linked PSP sleeps this long after each late
   frame. */
#define LATE_FRAME_SLEEP_US 2000u

typedef struct
{
  struct GBASIODriver d;   /* first, so the driver pointer is the side */
  int index;
  struct mCore *core;
  struct GBA *gba;
  uint16_t registers[GBA_SIO_REGISTER_BYTES / 2];
  GbaSio sio;
  LinkCable cable;
  int linked;
  struct mTimingEvent service_event;
  struct mTimingEvent finish_event;
  color_t video[256 * 160];
  double speed;
  double budget;
  uint64_t target;
  uint64_t blocked_us;
  uint32_t burst_cycles;    /* per tick in PSP mode, 0 for smooth running */
  uint64_t hiccup_period_us;
  uint64_t hiccup_us;
  int stack;                /* datagrams wait while the console emulates */
  uint32_t send_yield_us;   /* sleep after each datagram sent while running,
                               in every mode (a PSP: lockstep only) */
  uint64_t sleep_until;     /* asleep, letting the stack work, until then */
  int ran;                  /* emulated during the last tick */
  uint32_t frame_seen;      /* frame counter after the last tick */
  /* Datagrams sent while emulating, which leave when the console sleeps. */
  struct
  {
    uint8_t data[LINK_NET_MAX_DATAGRAM];
    size_t size;
  } outbox[256];
  unsigned outbox_count;
  uint32_t max_backlog;
  int rehalt;               /* woken only to stop the run loop, see below */
  uint64_t backlog_sum;     /* per tick while the games are linked */
  uint64_t linked_ticks;
  uint32_t keys;
  uint64_t now;
  uint64_t clock;
  uint32_t clock_last;
} Side;

static Side sides[2];

/* Keeps mGBA's own log quiet so only the test's output is printed. */
static void silent_log(struct mLogger *logger, int category,
 enum mLogLevel level, const char *format, va_list args)
{
  (void)logger; (void)category; (void)level; (void)format; (void)args;
}

static struct mLogger silent_logger = { .log = silent_log };

/* ---- Network ------------------------------------------------------------ */

typedef struct
{
  uint8_t data[LINK_NET_MAX_DATAGRAM];
  size_t size;
  uint64_t arrival;
  int to;
} Flight;

static Flight flights[4096];
static unsigned flight_count;
static uint32_t net_latency = 3000, net_jitter = 4000, net_loss = 3,
 net_dup = 1;
static uint64_t outage_period_us, outage_us;
static uint32_t rng_state = 12345;
static uint64_t sim_now;

/* Linear congruential generator; the low bits are weak, so drop them. */
static uint32_t rng(void)
{
  rng_state = rng_state * 1664525u + 1013904223u;
  return rng_state >> 8;
}

/* Puts a datagram in flight to console `to` with the scripted outage,
   loss, duplication, latency and jitter (which also reorders). */
static void network_send(int to, const uint8_t *data, size_t size)
{
  unsigned copies = 1;

  if(outage_period_us != 0 &&
     sim_now % outage_period_us >= outage_period_us - outage_us)
    return;
  if(rng() % 100u < net_loss)
    return;
  if(rng() % 100u < net_dup)
    copies = 2;
  while(copies-- && flight_count < 4096)
  {
    Flight *f = &flights[flight_count++];
    memcpy(f->data, data, size);
    f->size = size;
    f->to = to;
    f->arrival = sim_now + net_latency + (net_jitter ? rng() % net_jitter : 0);
  }
}

/* Hands every datagram whose arrival time has come to its console. */
static void network_deliver(void)
{
  unsigned i = 0;

  while(i < flight_count)
  {
    /* A PSP's stack hands datagrams over only while the console sleeps. */
    if(flights[i].arrival <= sim_now &&
       !(sides[flights[i].to].stack && sides[flights[i].to].ran))
    {
      link_cable_receive(&sides[flights[i].to].cable, flights[i].data,
       flights[i].size, sim_now);
      flights[i] = flights[--flight_count];
    }
    else
      i++;
  }
}

/* ---- Serial driver bridging mGBA to gba_sio ------------------------------ */

static uint32_t side_cycle(Side *side)
{
  return (uint32_t)mTimingCurrentTime(&side->gba->timing);
}

/* mGBA keeps a 64-bit clock only in debugger builds; extend the 32-bit one. */
static uint64_t side_clock(Side *side)
{
  uint32_t now = side_cycle(side);

  side->clock += (uint32_t)(now - side->clock_last);
  side->clock_last = now;
  return side->clock;
}

/* Copies SIOCNT and RCNT, as gba_sio left them, back into mGBA. */
static void side_sync_registers(Side *side)
{
  side->gba->sio.siocnt = side->registers[GBA_SIO_REG_CONTROL >> 1];
  side->gba->sio.rcnt = side->registers[GBA_SIO_REG_RCNT >> 1];
}

/* Schedules finish_callback for when the running transfer completes. */
static void side_schedule_finish(Side *side)
{
  if(side->sio.active && !mTimingIsScheduled(&side->gba->timing,
     &side->finish_event))
    mTimingSchedule(&side->gba->timing, &side->finish_event,
     (int32_t)side->sio.cycles_remaining);
}

static int trace_transfers;
/* A linked, same, ram16 or pace check failed; the exit status. */
static int check_failed;
/* The pace check: minfps, and the time and frame counters it started at. */
static double min_fps;
static uint64_t pace_since;
static uint32_t pace_frames[2];

/* Copies the words a finished transfer received into the guest's memory.
   Games that poll the busy bit instead of taking the serial interrupt
   read them from there too. */
static void side_copy_received(Side *side)
{
  int i;

  for(i = 0; i < 4; i++)
    side->gba->memory.io[(GBA_SIO_REG_MULTI0 >> 1) + i] =
     side->registers[(GBA_SIO_REG_MULTI0 >> 1) + i];
  if(side->sio.transfer_mode == GBA_SIO_MODE_NORMAL_8)
    side->gba->memory.io[REG_SIODATA8 >> 1] =
     side->registers[GBA_SIO_REG_SEND >> 1];
}

static int side_busy(const Side *side)
{
  return side->sio.active || side->sio.waiting || side->sio.pending_start;
}

/* Shows a finished transfer's words to the guest and raises the serial
   interrupt, `late` cycles after it was due. */
static void side_deliver_irq(Side *side, uint32_t late)
{
  if(trace_transfers)
    fprintf(stderr, "t=%llu side %d out %04X in %04X %04X %04X %04X cnt %04X local %d peer %d off %u\n",
     (unsigned long long)sim_now, side->index, side->sio.outgoing & 0xFFFF,
     side->registers[GBA_SIO_REG_MULTI0 >> 1],
     side->registers[GBA_SIO_REG_MULTI1 >> 1],
     side->registers[GBA_SIO_REG_MULTI2 >> 1],
     side->registers[GBA_SIO_REG_MULTI3 >> 1],
     side->registers[GBA_SIO_REG_CONTROL >> 1], side->cable.pokemon.local,
     side->cable.pokemon.peer, side->cable.pokemon.offset);

  side_copy_received(side);
  side_sync_registers(side);
  GBARaiseIRQ(side->gba, GBA_IRQ_SIO, late);
}

/* Games that poll the busy bit instead of taking the serial interrupt:
   print their received words whenever a transfer has completed. */
static void side_trace_polled(Side *side)
{
  static uint32_t last[2];
  uint32_t transfers = side->cable.lockstep.transfers;

  if(transfers == last[side->index] || side->sio.waiting || side->sio.active)
    return;
  last[side->index] = transfers;
  fprintf(stderr, "t=%llu side %d cycle %u polled in %04X %04X %04X %04X "
   "cnt %04X transfers %u\n", (unsigned long long)sim_now, side->index,
   side_cycle(side),
   side->registers[GBA_SIO_REG_MULTI0 >> 1],
   side->registers[GBA_SIO_REG_MULTI1 >> 1],
   side->registers[GBA_SIO_REG_MULTI2 >> 1],
   side->registers[GBA_SIO_REG_MULTI3 >> 1],
   side->registers[GBA_SIO_REG_CONTROL >> 1], transfers);
}

/* Runs one console's cable and applies the events it returns. A game that
   polls instead of taking the interrupt gets the received words once the
   transfer is over. */
static void side_service(Side *side)
{
  GbaSioEvent event;
  int was_busy;

  if(!side->linked)
    return;
  /* The guest's send registers live in mGBA; mirror them before use. */
  side->registers[GBA_SIO_REG_SEND >> 1] =
   side->gba->memory.io[REG_SIOMLT_SEND >> 1];
  if(!side->sio.active)
  {
    side->registers[GBA_SIO_REG_DATA32_L >> 1] =
     side->gba->memory.io[REG_SIODATA32_LO >> 1];
    side->registers[GBA_SIO_REG_DATA32_H >> 1] =
     side->gba->memory.io[REG_SIODATA32_HI >> 1];
  }
  was_busy = side_busy(side);
  event = link_cable_service(&side->cable, side_cycle(side), side->now);
  if(event & GBA_SIO_EVENT_SERIAL_IRQ)
    side_deliver_irq(side, 0);
  else
  {
    if(was_busy && !side_busy(side))
      side_copy_received(side);
    if(trace_transfers)
      side_trace_polled(side);
  }
  side_schedule_finish(side);
  side_sync_registers(side);
}

/* mGBA timing event: the local transfer has clocked its last bit. */
static void finish_callback(struct mTiming *timing, void *context,
 uint32_t late)
{
  Side *side = context;
  GbaSioEvent event;

  (void)timing;
  event = gba_sio_advance(&side->sio, side->sio.cycles_remaining);
  if(event & GBA_SIO_EVENT_SERIAL_IRQ)
    side_deliver_irq(side, late);
  else
  {
    if(!side_busy(side))
      side_copy_received(side);
    side_sync_registers(side);
  }
}

/* mGBA timing event: services the cable every SERVICE_CYCLES, or sooner
   when the cable asks for it. */
static void service_callback(struct mTiming *timing, void *context,
 uint32_t late)
{
  Side *side = context;
  uint32_t next = SERVICE_CYCLES;
  uint32_t until;

  (void)late;
  side_service(side);
  /* While the guest is halted, mGBA runs event after event inside one run
     loop until an interrupt wakes it, so a guest that must wait for the
     other console would sail past the point it has to stop at. gpSP checks
     the link at every event; here the halt is lifted to end the run loop
     and restored before the guest runs again. */
  if(side->gba->cpu->halted &&
     link_cable_blocked(&side->cable, side_cycle(side)))
  {
    side->gba->cpu->halted = 0;
    side->gba->cpu->nextEvent = side->gba->cpu->cycles;
    side->rehalt = 1;
  }
  until = link_cable_cycles_until_service(&side->cable, side_cycle(side));
  if(until < next)
    next = until;
  mTimingSchedule(timing, &side->service_event, (int32_t)next);
}

/* mGBA's serial register write hook: gba_sio takes the write, and a write
   that starts a transfer services the cable at once. */
static uint16_t driver_write(struct GBASIODriver *driver, uint32_t address,
 uint16_t value)
{
  Side *side = (Side *)driver;
  GbaSioEvent event;

  side->registers[GBA_SIO_REG_SEND >> 1] =
   side->gba->memory.io[REG_SIOMLT_SEND >> 1];
  if(trace_transfers && (address == REG_SIOMLT_SEND || address == REG_SIOCNT))
    fprintf(stderr, "t=%llu side %d frame %u cycle %u writes %s %04X\n",
     (unsigned long long)sim_now, side->index,
     side->core->frameCounter(side->core), side_cycle(side),
     address == REG_SIOCNT ? "SIOCNT" : "SEND", value);
  event = gba_sio_write16(&side->sio, address, value);
  if(event & GBA_SIO_EVENT_START)
    side_service(side);
  if(address == REG_SIOCNT)
    return side->registers[GBA_SIO_REG_CONTROL >> 1];
  if(address == REG_RCNT)
    return side->registers[GBA_SIO_REG_RCNT >> 1];
  return value;
}

/* Driver callbacks the bridge needs no work for. */
static bool driver_true(struct GBASIODriver *driver)
{
  (void)driver;
  return true;
}

static void driver_none(struct GBASIODriver *driver)
{
  (void)driver;
}

/* ---- Consoles ----------------------------------------------------------- */

/* Reads a whole file, or exits: the test cannot run without it. */
static void *read_file(const char *path, size_t *size)
{
  FILE *f = fopen(path, "rb");
  void *data;

  if(!f)
  {
    fprintf(stderr, "cannot open %s\n", path);
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  *size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  data = malloc(*size);
  if(fread(data, 1, *size, f) != *size)
    exit(1);
  fclose(f);
  return data;
}

/* Boots one mGBA core with the ROM, BIOS and save, and puts our serial
   driver and link timing events in place of mGBA's own. */
static void side_init(Side *side, int index, const char *rom,
 const char *bios, const char *save)
{
  size_t size;
  void *data;

  memset(side, 0, sizeof(*side));
  side->index = index;
  side->speed = 1.0;
  side->core = GBACoreCreate();
  side->core->init(side->core);
  mCoreInitConfig(side->core, NULL);
  mCoreConfigSetValue(&side->core->config, "skipBios", "1");
  side->core->loadConfig(side->core, &side->core->config);
  side->core->setVideoBuffer(side->core, side->video, 256);
  if(!side->core->loadROM(side->core, VFileOpen(rom, O_RDONLY)))
    exit(1);
  side->core->loadBIOS(side->core, VFileOpen(bios, O_RDONLY), 0);
  data = read_file(save, &size);
  side->core->loadSave(side->core, VFileMemChunk(data, size));
  side->core->reset(side->core);
  side->gba = side->core->board;

  gba_sio_init(&side->sio, side->registers);
  side->d.init = driver_true;
  side->d.deinit = driver_none;
  side->d.load = driver_true;
  side->d.unload = driver_true;
  side->d.writeRegister = driver_write;
  GBASIOSetDriver(&side->gba->sio, &side->d, SIO_MULTI);
  GBASIOSetDriver(&side->gba->sio, &side->d, SIO_NORMAL_8);

  side->service_event.context = side;
  side->service_event.callback = service_callback;
  side->service_event.name = "Link service";
  side->service_event.priority = 0x80;
  side->finish_event.context = side;
  side->finish_event.callback = finish_callback;
  side->finish_event.name = "Link transfer";
  side->finish_event.priority = 0x81;
  side->target = side_clock(side);
}

/* `generic` hides the game code so the cable uses the lockstep mode it
   uses for every non-Pokemon game. */
static void side_link(Side *side, int generic)
{
  char code[16] = { 0 };

  /* mGBA reports "AGB-BPRI"; the cable wants the header's four letters. */
  side->core->getGameCode(side->core, code);
  if(generic)
    memcpy(code + 4, "TEST", 4);
  link_cable_start(&side->cable, &side->sio, side->index == 0, code + 4,
   0x100u + (uint32_t)side->index, sim_now);
  side->linked = 1;
  mTimingSchedule(&side->gba->timing, &side->service_event, SERVICE_CYCLES);
}

/* Datagrams a PSP queued while emulating leave once it sleeps. */
static void side_flush_outbox(Side *side)
{
  unsigned i;

  for(i = 0; i < side->outbox_count; i++)
    network_send(1 - side->index, side->outbox[i].data, side->outbox[i].size);
  side->outbox_count = 0;
}

/* Moves one console's emulation target on by a tick: smoothly at its
   speed, or a whole frame per VBlank in PSP burst mode. */
static void side_advance(Side *side)
{
  if(side->burst_cycles == 0)
  {
    side->budget += side->speed * TICK_US * CYCLES_PER_US;
    side->target += (uint64_t)side->budget;
    side->budget -= (uint64_t)side->budget;
  }
  else if(sim_now % FRAME_US < TICK_US)
  {
    /* PSP mode: the next frame becomes due at each VBlank. A console that
       fell behind catches up at burst speed, as frameskip lets it. */
    side->target += FRAME_CYCLES;
  }
}

/* Advances the simulation by TICK_US: delivers due datagrams, then runs
   each console towards its target unless frozen, asleep (until VBlank, or
   for its network stack) or blocked by the cable, and sends the cable's
   next datagram, which a console with a stack holds until it sleeps. */
static void run_tick(void)
{
  int s;

  sim_now += TICK_US;
  network_deliver();
  for(s = 0; s < 2; s++)
  {
    Side *side = &sides[s];
    uint8_t datagram[LINK_NET_MAX_DATAGRAM];
    uint64_t limit;
    uint64_t before;
    uint32_t frame;
    size_t size;

    side->now = sim_now;
    side->core->setKeys(side->core, side->keys);
    side->ran = 0;
    /* Asleep for the stack: time passes and held datagrams leave. */
    if(sim_now < side->sleep_until)
    {
      side_advance(side);
      side_flush_outbox(side);
      continue;
    }
    if(side->hiccup_period_us != 0 &&
       sim_now % side->hiccup_period_us < side->hiccup_us)
    {
      /* Frozen, like a PSP writing to the memory stick: nothing runs and
         the lost time is never made up. */
      side->target = side_clock(side);
      side_flush_outbox(side);
      continue;
    }
    /* A PSP that has finished its frame sleeps until VBlank without
       touching the link. */
    if(side->burst_cycles != 0 && side_clock(side) >= side->target)
    {
      side_advance(side);
      if(side_clock(side) >= side->target)
      {
        side_flush_outbox(side);
        continue;
      }
    }
    else
      side_advance(side);
    if(side->linked)
    {
      side_service(side);
      if(link_cable_blocked(&side->cable, side_cycle(side)))
      {
        side->blocked_us += TICK_US;
        if(side->burst_cycles == 0)
          side->target = side_clock(side);
      }
    }
    limit = side->burst_cycles != 0 ?
     side_clock(side) + side->burst_cycles : side->target;
    if(limit > side->target)
      limit = side->target;
    before = side_clock(side);
    while(side_clock(side) < limit)
    {
      if(side->linked && link_cable_blocked(&side->cable, side_cycle(side)))
        break;
      if(side->rehalt)
      {
        side->rehalt = 0;
        side->gba->cpu->halted = 1;
        side->gba->cpu->nextEvent = side->gba->cpu->cycles;
      }
      side->core->runLoop(side->core);
    }
    side->ran = side_clock(side) != before;
    /* A new frame while still behind the target is a late frame. */
    frame = side->core->frameCounter(side->core);
    if(side->stack && side->linked && frame != side->frame_seen &&
       side_clock(side) < side->target)
      side->sleep_until = sim_now + LATE_FRAME_SLEEP_US;
    side->frame_seen = frame;
    if(side->linked)
    {
      uint32_t backlog = link_net_backlog(&side->cable.net);

      if(backlog > side->max_backlog)
        side->max_backlog = backlog;
      if(side->cable.pokemon.local == LINK_POKEMON_CONNECTED)
      {
        side->backlog_sum += backlog;
        side->linked_ticks++;
      }
      size = link_cable_build(&side->cable, datagram, sizeof(datagram),
       side_cycle(side), sim_now);
      if(size && side->stack && side->ran &&
         side->outbox_count < sizeof(side->outbox) / sizeof(side->outbox[0]))
      {
        memcpy(side->outbox[side->outbox_count].data, datagram, size);
        side->outbox[side->outbox_count++].size = size;
        if(side->send_yield_us != 0 && sim_now + side->send_yield_us >
           side->sleep_until)
          side->sleep_until = sim_now + side->send_yield_us;
      }
      else if(size)
      {
        side_flush_outbox(side);
        network_send(1 - s, datagram, size);
      }
    }
    if(!side->ran)
      side_flush_outbox(side);
  }
}

/* Runs until both consoles have shown `frames` more frames, so a script
   stays aligned with each game even when one console is slower. */
static void run_frames(unsigned frames)
{
  uint32_t end0 = sides[0].core->frameCounter(sides[0].core) + frames;
  uint32_t end1 = sides[1].core->frameCounter(sides[1].core) + frames;
  /* Generous: in lockstep mode a game sending many words per frame runs
     several times slower than real time. */
  uint64_t limit = sim_now + (uint64_t)frames * 16743u * 20u + 5000000u;
  uint32_t keys0 = sides[0].keys;
  uint32_t keys1 = sides[1].keys;

  while(sim_now < limit)
  {
    int done0 = (int32_t)(sides[0].core->frameCounter(sides[0].core) - end0) >= 0;
    int done1 = (int32_t)(sides[1].core->frameCounter(sides[1].core) - end1) >= 0;

    if(done0 && done1)
      break;
    /* A console that has finished its frames keeps running, but releases
       the keys it was told to hold. */
    sides[0].keys = done0 ? 0 : keys0;
    sides[1].keys = done1 ? 0 : keys1;
    run_tick();
  }
  sides[0].keys = keys0;
  sides[1].keys = keys1;
}

/* "A+B+START" to KEYINPUT bits (bit 0 A ... bit 9 L); "-" means none. */
static uint32_t parse_keys(const char *text)
{
  static const char *names[] = { "A", "B", "SEL", "START", "RIGHT", "LEFT",
   "UP", "DOWN", "R", "L" };
  uint32_t keys = 0;
  char buffer[128];
  char *token;
  unsigned i;

  if(!text || strcmp(text, "-") == 0)
    return 0;
  snprintf(buffer, sizeof(buffer), "%s", text);
  for(token = strtok(buffer, "+"); token; token = strtok(NULL, "+"))
  {
    for(i = 0; i < 10; i++)
      if(strcmp(token, names[i]) == 0)
        keys |= 1u << i;
  }
  return keys;
}

/* Saves the console's last frame as a 240x160 24-bit BMP: a 54-byte
   header, then bottom-up BGR rows. */
static void write_bmp(Side *side, const char *name)
{
  char path[512];
  FILE *f;
  uint8_t header[54] = { 'B', 'M' };
  int x, y;
  uint32_t size = 54 + 240 * 160 * 3;

  snprintf(path, sizeof(path), "%s-%d.bmp", name, side->index);
  f = fopen(path, "wb");
  header[2] = (uint8_t)size; header[3] = (uint8_t)(size >> 8);
  header[4] = (uint8_t)(size >> 16);
  header[10] = 54; header[14] = 40;
  header[18] = 240; header[22] = 160; header[26] = 1; header[28] = 24;
  fwrite(header, 1, 54, f);
  for(y = 159; y >= 0; y--)
    for(x = 0; x < 240; x++)
    {
      color_t c = side->video[y * 256 + x];
      uint8_t bgr[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
      fwrite(bgr, 1, 3, f);
    }
  fclose(f);
}

/* The status command, also run when the script ends: cable counters for
   each console, plus the lockstep state while in lockstep mode. */
static void print_status(void)
{
  int s;

  printf("time %.1fs\n", (double)sim_now / 1e6);

  for(s = 0; s < 2; s++)
  {
    Side *side = &sides[s];
    printf("side %d: %.4s with %.4s status %d mode %d local %d peer %d "
     "frames tx %u rx %u rtt %uus blocked %.1fs frame %u max-backlog %u "
     "avg-backlog %.1f\n", s, side->cable.local_game, side->cable.peer_game,
     side->cable.status, side->cable.mode, side->cable.pokemon.local,
     side->cable.pokemon.peer, side->cable.pokemon.frames_sent,
     side->cable.pokemon.frames_received, side->cable.net.rtt_us,
     (double)side->blocked_us / 1e6, side->core->frameCounter(side->core),
     side->max_backlog, side->linked_ticks ?
      (double)side->backlog_sum / (double)side->linked_ticks : 0.0);
    if(side->cable.mode == LINK_CABLE_MODE_LOCKSTEP)
      printf("  lockstep waiting %d deferred %d transfers %u cycle %u "
       "finish %u deferred-until %u peer-clock %u pacing %u/%u sio %d%d%d "
       "control %04X inbox %u early %u mispredicted %u streak %u/%u\n",
       side->cable.lockstep.waiting,
       side->cable.lockstep.deferred, side->cable.lockstep.transfers,
       side_cycle(side), side->cable.lockstep.finish_cycle,
       side->cable.lockstep.deferred_until, side->cable.net.peer_clock,
       side->cable.pacing_local, side->cable.pacing_peer,
       side->sio.pending_start, side->sio.waiting, side->sio.active,
       side->registers[GBA_SIO_REG_CONTROL >> 1],
       side->cable.net.in_write - side->cable.net.in_read,
       side->cable.lockstep.answered_early, side->cable.lockstep.mispredicted,
       side->cable.lockstep.streak, side->cable.lockstep.trust_after);
  }
  fflush(stdout);
}

/* Boots both consoles and executes the script ("-" reads stdin) line by
   line; exits 1 on too few arguments, a ROM that does not load, an
   unreadable save or savestate, or a failed check. */
int main(int argc, char **argv)
{
  char line[512];
  FILE *script;

  if(argc < 7)
  {
    fprintf(stderr, "usage: %s rom0 save0 rom1 save1 bios script\n", argv[0]);
    return 1;
  }
  mLogSetDefaultLogger(&silent_logger);
  side_init(&sides[0], 0, argv[1], argv[5], argv[2]);
  side_init(&sides[1], 1, argv[3], argv[5], argv[4]);
  script = strcmp(argv[6], "-") == 0 ? stdin : fopen(argv[6], "r");

  while(fgets(line, sizeof(line), script))
  {
    char *cmd = strtok(line, " \t\n");
    char *a = strtok(NULL, " \t\n");
    char *b = strtok(NULL, " \t\n");
    char *c = strtok(NULL, " \t\n");
    char *d = strtok(NULL, " \t\n");

    if(!cmd || cmd[0] == '#')
      continue;
    if(strcmp(cmd, "run") == 0)
    {
      sides[0].keys = parse_keys(b);
      sides[1].keys = parse_keys(c);
      run_frames((unsigned)atoi(a));
      sides[0].keys = sides[1].keys = 0;
    }
    else if(strcmp(cmd, "tap") == 0)
    {
      int s = atoi(a);
      sides[s].keys = parse_keys(b);
      run_frames(c ? (unsigned)atoi(c) : 6u);
      sides[s].keys = 0;
      run_frames(10);
    }
    else if(strcmp(cmd, "link") == 0)
    {
      int generic = a != NULL && strcmp(a, "lockstep") == 0;

      side_link(&sides[0], generic);
      side_link(&sides[1], generic);
    }
    else if(strcmp(cmd, "shot") == 0)
    {
      write_bmp(&sides[0], a);
      write_bmp(&sides[1], a);
    }
    else if(strcmp(cmd, "state") == 0)
    {
      int s;
      for(s = 0; s < 2; s++)
      {
        char path[512];
        size_t size = sides[s].core->stateSize(sides[s].core);
        void *state = malloc(size);
        FILE *f;

        snprintf(path, sizeof(path), "%s-%d.state", b, s);
        if(strcmp(a, "save") == 0)
        {
          sides[s].core->saveState(sides[s].core, state);
          f = fopen(path, "wb");
          fwrite(state, 1, size, f);
          fclose(f);
        }
        else
        {
          f = fopen(path, "rb");
          if(!f || fread(state, 1, size, f) != size)
            return 1;
          fclose(f);
          sides[s].core->loadState(sides[s].core, state);
          sides[s].target = side_clock(&sides[s]);
        }
        free(state);
      }
    }
    else if(strcmp(cmd, "net") == 0)
    {
      net_latency = (uint32_t)atoi(a);
      net_jitter = (uint32_t)atoi(b);
      net_loss = (uint32_t)atoi(c);
      net_dup = (uint32_t)atoi(d);
    }
    else if(strcmp(cmd, "outage") == 0)
    {
      outage_period_us = (uint64_t)atoi(a) * 1000u;
      outage_us = (uint64_t)atoi(b) * 1000u;
    }
    else if(strcmp(cmd, "seed") == 0)
      rng_state = (uint32_t)strtoul(a, NULL, 0);
    else if(strcmp(cmd, "speed") == 0)
      sides[atoi(a)].speed = atof(b);
    else if(strcmp(cmd, "psp") == 0)
      sides[atoi(a)].burst_cycles = (uint32_t)((double)FRAME_CYCLES *
       TICK_US / (atof(b) * 1000.0));
    else if(strcmp(cmd, "linked") == 0)
    {
      int s;

      for(s = 0; s < 2; s++)
      {
        uint32_t transfers = sides[s].cable.lockstep.transfers +
         sides[s].cable.pokemon.frames_received;

        printf("side %d: %u transfers\n", s, transfers);
        if(sides[s].cable.status != LINK_CABLE_CONNECTED ||
           transfers < (uint32_t)atoi(a))
          check_failed = 1;
      }
    }
    else if(strcmp(cmd, "same") == 0)
    {
      /* The whole 240x160 screen by default; video rows are 256 wide. */
      int left = b ? atoi(b) : 0;
      int top = c ? atoi(c) : 0;
      int width = d ? atoi(d) : 240;
      char *e = strtok(NULL, " \t\n");
      int height = e ? atoi(e) : 160;
      unsigned matching = 0;
      int x, y;

      for(y = top; y < top + height; y++)
        for(x = left; x < left + width; x++)
          matching += sides[0].video[y * 256 + x] ==
           sides[1].video[y * 256 + x];
      printf("screens match: %.1f%%\n",
       matching * 100.0 / (width * height));
      if(matching * 100.0 / (width * height) < atof(a))
        check_failed = 1;
    }
    else if(strcmp(cmd, "ram16") == 0)
    {
      uint32_t address = (uint32_t)strtoul(a, NULL, 0);
      uint16_t value[2];
      int s;

      for(s = 0; s < 2; s++)
        value[s] = (uint16_t)sides[s].core->rawRead16(sides[s].core,
         address, -1);
      printf("ram %08X: %u / %u\n", address, value[0], value[1]);
      if(abs((int)value[0] - (int)value[1]) > atoi(b) ||
         value[0] < atoi(c) || value[1] < atoi(c))
        check_failed = 1;
    }
    else if(strcmp(cmd, "hiccup") == 0)
    {
      sides[atoi(a)].hiccup_period_us = (uint64_t)atoi(b) * 1000u;
      sides[atoi(a)].hiccup_us = (uint64_t)atoi(c) * 1000u;
    }
    else if(strcmp(cmd, "minfps") == 0)
      min_fps = atof(a);
    else if(strcmp(cmd, "pace") == 0)
    {
      int s;

      for(s = 0; s < 2; s++)
      {
        uint32_t frames = sides[s].core->frameCounter(sides[s].core);
        double fps = sim_now > pace_since ? (frames - pace_frames[s]) *
         1e6 / (double)(sim_now - pace_since) : 0.0;

        if(strcmp(a, "end") == 0)
        {
          printf("side %d: %.1f fps\n", s, fps);
          if(fps < min_fps)
            check_failed = 1;
        }
        pace_frames[s] = frames;
      }
      pace_since = sim_now;
    }
    else if(strcmp(cmd, "stack") == 0)
    {
      sides[atoi(a)].stack = 1;
      sides[atoi(a)].send_yield_us = (uint32_t)atoi(b);
    }
    else if(strcmp(cmd, "status") == 0)
      print_status();
    else if(strcmp(cmd, "trace") == 0)
      trace_transfers = atoi(a);
    else if(strcmp(cmd, "dump") == 0)
    {
      int s;
      for(s = 0; s < 2; s++)
      {
        char path[512];
        void *sram = NULL;
        size_t size = sides[s].core->savedataClone(sides[s].core, &sram);
        FILE *f;

        snprintf(path, sizeof(path), "%s-%d.sav", a, s);
        f = fopen(path, "wb");
        fwrite(sram, 1, size, f);
        fclose(f);
        free(sram);
      }
    }
  }
  print_status();
  return check_failed;
}
