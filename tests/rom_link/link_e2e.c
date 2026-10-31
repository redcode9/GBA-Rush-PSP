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
 *   link                                start the cable (console 0 hosts)
 *   net <latency_us> <jitter_us> <loss%> <dup%>
 *   speed <side> <factor>               guest speed relative to real time
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
#define SERVICE_CYCLES 4096

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
  uint32_t keys;
  uint64_t now;
  uint64_t clock;
  uint32_t clock_last;
} Side;

static Side sides[2];

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
static uint32_t rng_state = 12345;
static uint64_t sim_now;

static uint32_t rng(void)
{
  rng_state = rng_state * 1664525u + 1013904223u;
  return rng_state >> 8;
}

static void network_send(int to, const uint8_t *data, size_t size)
{
  unsigned copies = 1;

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

static void network_deliver(void)
{
  unsigned i = 0;

  while(i < flight_count)
  {
    if(flights[i].arrival <= sim_now)
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

static void side_sync_registers(Side *side)
{
  side->gba->sio.siocnt = side->registers[GBA_SIO_REG_CONTROL >> 1];
  side->gba->sio.rcnt = side->registers[GBA_SIO_REG_RCNT >> 1];
}

static void side_schedule_finish(Side *side)
{
  if(side->sio.active && !mTimingIsScheduled(&side->gba->timing,
     &side->finish_event))
    mTimingSchedule(&side->gba->timing, &side->finish_event,
     (int32_t)side->sio.cycles_remaining);
}

static int trace_transfers;

static void side_deliver_irq(Side *side, uint32_t late)
{
  int i;

  if(trace_transfers)
    fprintf(stderr, "t=%llu side %d out %04X in %04X %04X %04X %04X cnt %04X local %d peer %d off %u\n",
     (unsigned long long)sim_now, side->index, side->sio.outgoing & 0xFFFF,
     side->registers[0x120 >> 1], side->registers[0x122 >> 1],
     side->registers[0x124 >> 1], side->registers[0x126 >> 1],
     side->registers[0x128 >> 1], side->cable.pokemon.local,
     side->cable.pokemon.peer, side->cable.pokemon.offset);

  for(i = 0; i < 4; i++)
    side->gba->memory.io[(GBA_SIO_REG_MULTI0 >> 1) + i] =
     side->registers[(GBA_SIO_REG_MULTI0 >> 1) + i];
  side_sync_registers(side);
  GBARaiseIRQ(side->gba, GBA_IRQ_SIO, late);
}

static void side_service(Side *side)
{
  GbaSioEvent event;

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
  event = link_cable_service(&side->cable, side_cycle(side), side->now);
  if(event & GBA_SIO_EVENT_SERIAL_IRQ)
    side_deliver_irq(side, 0);
  side_schedule_finish(side);
  side_sync_registers(side);
}

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
    side_sync_registers(side);
}

static void service_callback(struct mTiming *timing, void *context,
 uint32_t late)
{
  Side *side = context;
  uint32_t next = SERVICE_CYCLES;
  uint32_t until;

  (void)late;
  side_service(side);
  until = link_cable_cycles_until_service(&side->cable, side_cycle(side));
  if(until < next)
    next = until;
  mTimingSchedule(timing, &side->service_event, (int32_t)next);
}

static uint16_t driver_write(struct GBASIODriver *driver, uint32_t address,
 uint16_t value)
{
  Side *side = (Side *)driver;
  GbaSioEvent event;

  side->registers[GBA_SIO_REG_SEND >> 1] =
   side->gba->memory.io[REG_SIOMLT_SEND >> 1];
  event = gba_sio_write16(&side->sio, address, value);
  if(event & GBA_SIO_EVENT_START)
    side_service(side);
  if(address == REG_SIOCNT)
    return side->registers[GBA_SIO_REG_CONTROL >> 1];
  if(address == REG_RCNT)
    return side->registers[GBA_SIO_REG_RCNT >> 1];
  return value;
}

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

static void side_link(Side *side)
{
  char code[16] = { 0 };

  /* mGBA reports "AGB-BPRI"; the cable wants the header's four letters. */
  side->core->getGameCode(side->core, code);
  link_cable_start(&side->cable, &side->sio, side->index == 0, code + 4,
   0x100u + (uint32_t)side->index, sim_now);
  side->linked = 1;
  mTimingSchedule(&side->gba->timing, &side->service_event, SERVICE_CYCLES);
}

static void run_tick(void)
{
  int s;

  sim_now += TICK_US;
  network_deliver();
  for(s = 0; s < 2; s++)
  {
    Side *side = &sides[s];
    uint8_t datagram[LINK_NET_MAX_DATAGRAM];
    size_t size;

    side->now = sim_now;
    side->core->setKeys(side->core, side->keys);
    if(side->linked)
    {
      side_service(side);
      if(link_cable_blocked(&side->cable, side_cycle(side)))
      {
        side->blocked_us += TICK_US;
        side->target = side_clock(side);
      }
      else
      {
        side->budget += side->speed * TICK_US * CYCLES_PER_US;
        side->target += (uint64_t)side->budget;
        side->budget -= (uint64_t)side->budget;
      }
    }
    else
    {
      side->budget += side->speed * TICK_US * CYCLES_PER_US;
      side->target += (uint64_t)side->budget;
      side->budget -= (uint64_t)side->budget;
    }
    while(side_clock(side) < side->target)
    {
      if(side->linked && link_cable_blocked(&side->cable, side_cycle(side)))
        break;
      side->core->runLoop(side->core);
    }
    if(side->linked)
    {
      size = link_cable_build(&side->cable, datagram, sizeof(datagram),
       side_cycle(side), sim_now);
      if(size)
        network_send(1 - s, datagram, size);
    }
  }
}

/* Runs until both consoles have shown `frames` more frames, so a script
   stays aligned with each game even when one console is slower. */
static void run_frames(unsigned frames)
{
  uint32_t end0 = sides[0].core->frameCounter(sides[0].core) + frames;
  uint32_t end1 = sides[1].core->frameCounter(sides[1].core) + frames;
  uint64_t limit = sim_now + (uint64_t)frames * 16743u * 4u + 2000000u;
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

static void print_status(void)
{
  int s;

  for(s = 0; s < 2; s++)
  {
    Side *side = &sides[s];
    printf("side %d: status %d mode %d local %d peer %d frames tx %u rx %u "
     "rtt %uus blocked %.1fs frame %u\n", s, side->cable.status,
     side->cable.mode, side->cable.pokemon.local, side->cable.pokemon.peer,
     side->cable.pokemon.frames_sent, side->cable.pokemon.frames_received,
     side->cable.net.rtt_us, (double)side->blocked_us / 1e6,
     side->core->frameCounter(side->core));
  }
  fflush(stdout);
}

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
      side_link(&sides[0]);
      side_link(&sides[1]);
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
    else if(strcmp(cmd, "speed") == 0)
      sides[atoi(a)].speed = atof(b);
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
  return 0;
}
