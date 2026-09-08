# M8 RC3 Bidirectional Liveness Design

Date: 2026-09-08

Status: approved in conversation; written review pending

Scope: protocol-v3 idle liveness and directional diagnostics only

## 1. Evidence and problem statement

The RC2 EBOOT, SHA-256
`dcdf2bf1d77677cd14ee2fc12638d6448f94556f15d87ecb4c63fa741a3a17f8`,
was byte-identical on the PSP-1004 and PSP-2004. RC2 corrected the earlier
cycle-rate heartbeat flood: the new logs contain no PDP backpressure and no
PSP API retries.

The retained evidence is under
`build/hardware-logs/20260907-m8-v3-rc2-second-hardware-stall/`. Its new
session-1 logs have these hashes:

- PSP-1004 joiner:
  `3021d29b4980b6c6756fff0378b59d82adb9c31c1b5d3108cb05ab56b49895c1`;
- PSP-2004 host:
  `f043c90b253241d59fcfc7c928682e14ca57b15c4d8ce4ec418eba650b5d07f4`.

Both FireRed attempts reached protocol-v3 `CONNECTED`, but neither began a
GBA transfer: SIO starts, commits, aborts and serial IRQs remained zero. In the
second attempt the host published 41 heartbeat anchors and the joiner received
only one. The host received all five anchors published by the joiner. After
five seconds without inbound progress the joiner requested recovery. The host
accepted that request, but the recovery rendezvous could not restore the
direction from host to joiner.

The proven software defect is therefore not FireRed or its mandatory save. A
token-zero stream heartbeat is replaceable and unacknowledged. Reverse traffic
keeps the host satisfied without causing it to answer, while independent host
anchors can be lost repeatedly in the other direction. The session then treats
the missing optional anchors as a dead peer and enters a recovery path that
depends on the same impaired direction.

WLAN power saving, antenna placement or interference may increase physical
loss, but RC3 must not depend on guessing or changing the user's global PSP
settings.

## 2. Goals and non-goals

RC3 must:

1. turn idle liveness into a correlated bidirectional exchange;
2. preserve the one-second wall-clock request cadence introduced by RC2;
3. let either role answer a valid request immediately;
4. prevent reply echo, duplicate amplification and cycle-rate publication;
5. retain host clock authority and stream exact-once semantics;
6. attribute requests, replies, duplicates and unmatched replies by direction;
7. pass all existing safety, recovery, build and artifact gates before hardware
   installation.

RC3 does not change the GBA transfer record format, retransmission policy,
journal, recovery reconciliation, PDP socket topology, ROM data or save data.
It does not silently alter the PSP WLAN power-save system parameter. PTP,
GameMode, KCP and ENet remain outside M8.

## 3. Approaches considered

### Selected: tokenized request/reply heartbeats

Each scheduled idle anchor carries a non-zero request token. A peer of either
role returns one immediate heartbeat reply containing the same token. Replies
never request another reply. A later request uses the next token, so a lost
reply is retried without reusing stale state.

This reuses the existing heartbeat flags and stream heartbeat payload, adds no
socket and keeps traffic bounded. It directly repairs the asymmetry present in
the hardware trace.

### Rejected: increase unacknowledged heartbeat frequency

Sending 20--50 independent anchors per second would improve probability but
would not prove receipt. It also moves back toward the RC1 failure mode and
would require hardware-derived tuning before it could be trusted.

### Rejected: force WLAN power saving off or replace PDP

Changing a global console setting is surprising and does not repair the
protocol. Replacing PDP would broaden M8 into a transport rewrite and discard
the verified lane, journal and recovery work.

## 4. Protocol behavior

The existing flag layout remains wire-compatible within protocol v3:

- bits 1--6: non-zero request token;
- bit 7 clear: request;
- bit 7 set: reply;
- sequence remains zero;
- the stream heartbeat payload continues to carry generation, clock cycle and
  cumulative acknowledgement.

When a connected peer schedules an idle clock anchor, it allocates the next
non-zero token and sends a request. Scheduled requests remain limited to one
per `LINK_SESSION_HEARTBEAT_US` by monotonic wall time. GBA-cycle changes never
authorize another request.

On receipt of a valid request, host and joiner apply its heartbeat payload and
send one immediate reply with the same token. A reply is terminal and cannot
be echoed. A duplicate request with the same token is observed but does not
create another reply. New request tokens cannot be generated faster than the
one-second request cadence; replies are therefore bounded by valid requests
and cannot recreate the RC1 flood.

Receiving any valid request or reply remains peer activity. A matching reply
clears the local outstanding liveness request. An unmatched or stale reply is
safe activity but does not acknowledge the current request. If a reply is
lost, the next scheduled interval sends a new token. Token wrap skips zero and
uses modular equality only; it does not affect stream generation or transfer
sequence numbers.

Heartbeat replies do not change host authority. The host's reply carries the
current authoritative clock anchor. A joiner's reply can inform host liveness
and cumulative state but cannot advance host-controlled transfer order.

## 5. State and component boundaries

`LinkSession` owns the heartbeat request token, the outstanding token and all
request/reply counters. `link_session_send_heartbeat` remains the only encoder
and sender; small wrappers distinguish scheduled requests from replies.

`LinkClock` continues to own periodic wall-clock pacing. Only scheduled
requests consume its one-second publication budget. An immediate reply is a
correlated control response, not permission for a new periodic anchor.

The PSP transport scheduler continues to classify heartbeats as replaceable
state. No reliable critical slot is consumed, so a liveness exchange cannot
block `START`, `REPLY` or recovery records.

The runtime logger and decoder expose, at minimum:

- heartbeat requests transmitted and received;
- heartbeat replies transmitted and received;
- duplicate requests suppressed;
- unmatched replies received;
- the outstanding token at failure.

These values are appended through a new `link-stream-metrics` flag so existing
version-3 log records and earlier snapshots remain decodable.

## 6. Error handling

An invalid token layout, malformed payload or session mismatch retains the
existing protocol-error behavior. Queue publication failure retains the
existing queue-full behavior.

A duplicate request is not an error. An unmatched reply is counted and
ignored after contributing transport activity. Missing replies continue to be
bounded by the existing five-second session silence threshold and coordinated
recovery; RC3 does not hide a permanent partition or extend recovery without a
limit.

No heartbeat response may mutate the transfer journal, latch a GBA word, write
GBA result registers or raise a serial IRQ.

## 7. Verification

Unit and integration tests must first reproduce the RC2 weakness and then
prove:

1. a joiner request elicits an immediate host reply with the same token;
2. a host request elicits an immediate joiner reply;
3. replies have no echo path;
4. a duplicate request produces at most one reply;
5. a lost reply causes a new token at the next one-second interval;
6. one-way loss of independent anchors cannot suppress the correlated reply
   path when the reverse request is delivered;
7. prolonged bidirectional loss still reaches bounded recovery or close;
8. rapid GBA-cycle churn cannot exceed the periodic request rate;
9. transfer exact-once, journal and recovery invariants are unchanged;
10. new diagnostics survive encode, retention, decode and older-log parsing.

The native suite, ASan/UBSan suite, CBMC harnesses, TLC safety/liveness models,
message/layout/lifecycle/tree checks, clean PSP build, embedded-PRX comparison
and release artifact smoke test must all pass. Only then may RC3 be installed
byte-identically on both SD cards.

Physical acceptance starts with the ten-minute no-ROM benchmark in both role
assignments. The PSPs should be separated rather than stacked and WLAN power
saving should be set to Off on both for the controlled baseline. FireRed is
tested only with the exact benchmark-passing EBOOT, following the full timing
and role matrix already defined by M8.

## 8. Completion criterion

RC3 is a candidate after software and artifact gates pass. The liveness defect
is closed only after paired hardware logs show matching request/reply progress,
zero cyclic recovery, a passing benchmark in both role assignments and the
subsequent FireRed trade matrix completes without abnormal text pacing or
connection loss.
