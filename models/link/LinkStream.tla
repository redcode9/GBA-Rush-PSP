------------------------------ MODULE LinkStream -----------------------------
\* Finite executable model of the v3 alternating START/REPLY stream.  Packet
\* labels are transport details; transfer identities, generation and the
\* single cached reply are the semantic state used for exact-once recovery.

EXTENDS Integers, Sequences, FiniteSets, TLC

CONSTANTS MaxTransfers, MaxGeneration, MaxWire, MaxFaults

ASSUME /\ MaxTransfers >= 2
       /\ MaxGeneration >= 2
       /\ MaxWire >= 2
       /\ MaxFaults >= 1

Host == "HOST"
Joiner == "JOINER"
Peers == {Host, Joiner}

Ready == "READY"
WaitReply == "WAIT_REPLY"
HostStates == {Ready, WaitReply}

Healthy == "HEALTHY"
Degraded == "DEGRADED"
HealthStates == {Healthy, Degraded}

StartKind == "START"
ReplyKind == "REPLY"
MessageKinds == {StartKind, ReplyKind}

Transfers == 1..MaxTransfers
TransfersOrNone == 0..MaxTransfers
Generations == 1..MaxGeneration

MessageType ==
  [kind: MessageKinds,
   transfer: Transfers,
   generation: Generations,
   ack: TransfersOrNone]

MakeStart(id, generationValue, ackValue) ==
  [kind |-> StartKind,
   transfer |-> id,
   generation |-> generationValue,
   ack |-> ackValue]

MakeReply(id, generationValue) ==
  [kind |-> ReplyKind,
   transfer |-> id,
   generation |-> generationValue,
   ack |-> id]

RemoveAt(s, index) ==
  IF index = 1
  THEN Tail(s)
  ELSE IF index = Len(s)
       THEN SubSeq(s, 1, Len(s) - 1)
       ELSE SubSeq(s, 1, index - 1) \o SubSeq(s, index + 1, Len(s))

WireSet(s) == {s[index] : index \in 1..Len(s)}

VARIABLES
  issued,
  hostState,
  hostPending,
  hostLast,
  joinLast,
  joinCache,
  hostApplied,
  joinApplied,
  hostIrq,
  joinIrq,
  hostApplyCount,
  joinApplyCount,
  retiredCaches,
  generation,
  wire,
  faultsLeft,
  health,
  pressure

vars ==
  <<issued, hostState, hostPending, hostLast, joinLast, joinCache,
    hostApplied, joinApplied, hostIrq, joinIrq, hostApplyCount,
    joinApplyCount, retiredCaches, generation, wire, faultsLeft, health,
    pressure>>

Init ==
  /\ issued = 0
  /\ hostState = Ready
  /\ hostPending = 0
  /\ hostLast = 0
  /\ joinLast = 0
  /\ joinCache = 0
  /\ hostApplied = {}
  /\ joinApplied = {}
  /\ hostIrq = {}
  /\ joinIrq = {}
  /\ hostApplyCount = 0
  /\ joinApplyCount = 0
  /\ retiredCaches = {}
  /\ generation = 1
  /\ wire = <<>>
  /\ faultsLeft = MaxFaults
  /\ health = [p \in Peers |-> Healthy]
  /\ pressure = [p \in Peers |-> 0]

\* At most one causal transfer may wait for its reply.
Start ==
  /\ hostState = Ready
  /\ issued < MaxTransfers
  /\ Len(wire) < MaxWire
  /\ \A p \in Peers: health[p] = Healthy
  /\ LET id == issued + 1
     IN /\ wire' = Append(wire, MakeStart(id, generation, hostLast))
        /\ issued' = id
        /\ hostState' = WaitReply
        /\ hostPending' = id
  /\ UNCHANGED <<hostLast, joinLast, joinCache, hostApplied, joinApplied,
                  hostIrq, joinIrq, hostApplyCount, joinApplyCount,
                  retiredCaches, generation, faultsLeft, health, pressure>>

FreshStart(message) ==
  /\ message.kind = StartKind
  /\ message.generation = generation
  /\ message.transfer = joinLast + 1
  /\ \/ joinCache = 0
     \/ message.ack = joinCache

CachedStart(message) ==
  /\ message.kind = StartKind
  /\ message.generation = generation
  /\ joinCache # 0
  /\ message.transfer = joinCache

DeliverFreshStart(index) ==
  /\ index \in 1..Len(wire)
  /\ health[Joiner] = Healthy
  /\ LET message == wire[index]
         without == RemoveAt(wire, index)
     IN /\ FreshStart(message)
        /\ wire' = Append(without,
             MakeReply(message.transfer, generation))
        /\ joinLast' = message.transfer
        /\ joinCache' = message.transfer
        /\ joinApplied' = joinApplied \cup {message.transfer}
        /\ joinIrq' = joinIrq \cup {message.transfer}
        /\ joinApplyCount' = joinApplyCount + 1
        /\ retiredCaches' =
             IF joinCache = 0
             THEN retiredCaches
             ELSE retiredCaches \cup {joinCache}
  /\ UNCHANGED <<issued, hostState, hostPending, hostLast, hostApplied,
                  hostIrq, hostApplyCount, generation, faultsLeft, health,
                  pressure>>

\* A duplicate START replays the one cached semantic reply without applying
\* the transfer or delivering another IRQ.
ReplayCachedStart(index) ==
  /\ index \in 1..Len(wire)
  /\ health[Joiner] = Healthy
  /\ LET message == wire[index]
         without == RemoveAt(wire, index)
     IN /\ CachedStart(message)
        /\ wire' = Append(without,
             MakeReply(message.transfer, generation))
  /\ UNCHANGED <<issued, hostState, hostPending, hostLast, joinLast,
                  joinCache, hostApplied, joinApplied, hostIrq, joinIrq,
                  hostApplyCount, joinApplyCount, retiredCaches, generation,
                  faultsLeft, health, pressure>>

DropInvalidStart(index) ==
  /\ index \in 1..Len(wire)
  /\ wire[index].kind = StartKind
  /\ ~FreshStart(wire[index])
  /\ ~CachedStart(wire[index])
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, hostState, hostPending, hostLast, joinLast,
                  joinCache, hostApplied, joinApplied, hostIrq, joinIrq,
                  hostApplyCount, joinApplyCount, retiredCaches, generation,
                  faultsLeft, health, pressure>>

MatchingReply(message) ==
  /\ message.kind = ReplyKind
  /\ message.generation = generation
  /\ hostState = WaitReply
  /\ message.transfer = hostPending
  /\ message.ack = hostPending
  /\ joinLast = hostPending

DeliverMatchingReply(index) ==
  /\ index \in 1..Len(wire)
  /\ health[Host] = Healthy
  /\ LET message == wire[index]
     IN /\ MatchingReply(message)
        /\ wire' = RemoveAt(wire, index)
        /\ hostState' = Ready
        /\ hostLast' = message.transfer
        /\ hostPending' = 0
        /\ hostApplied' = hostApplied \cup {message.transfer}
        /\ hostIrq' = hostIrq \cup {message.transfer}
        /\ hostApplyCount' = hostApplyCount + 1
  /\ UNCHANGED <<issued, joinLast, joinCache, joinApplied, joinIrq,
                  joinApplyCount, retiredCaches, generation, faultsLeft,
                  health, pressure>>

DropInvalidReply(index) ==
  /\ index \in 1..Len(wire)
  /\ wire[index].kind = ReplyKind
  /\ ~MatchingReply(wire[index])
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, hostState, hostPending, hostLast, joinLast,
                  joinCache, hostApplied, joinApplied, hostIrq, joinIrq,
                  hostApplyCount, joinApplyCount, retiredCaches, generation,
                  faultsLeft, health, pressure>>

Deliver ==
  \E index \in 1..Len(wire):
    \/ DeliverFreshStart(index)
    \/ ReplayCachedStart(index)
    \/ DropInvalidStart(index)
    \/ DeliverMatchingReply(index)
    \/ DropInvalidReply(index)

HasCurrentCausalPacket ==
  \E message \in WireSet(wire):
    /\ message.transfer = hostPending
    /\ message.generation = generation

\* Reliable critical traffic eventually retries either the missing START or,
\* after the joiner applied, the START that deterministically replays REPLY.
RetransmitStart ==
  /\ hostState = WaitReply
  /\ hostPending # 0
  /\ health[Host] = Healthy
  /\ Len(wire) < MaxWire
  /\ ~HasCurrentCausalPacket
  /\ wire' = Append(wire,
       MakeStart(hostPending, generation, hostLast))
  /\ UNCHANGED <<issued, hostState, hostPending, hostLast, joinLast,
                  joinCache, hostApplied, joinApplied, hostIrq, joinIrq,
                  hostApplyCount, joinApplyCount, retiredCaches, generation,
                  faultsLeft, health, pressure>>

\* This is the valid asymmetric recovery point: joiner has applied and cached
\* the reply, while the host still waits.  Rebase identity only; never apply.
RecoverAsymmetric ==
  /\ hostState = WaitReply
  /\ hostPending = joinLast
  /\ joinCache = hostPending
  /\ generation < MaxGeneration
  /\ Len(wire) < MaxWire
  /\ \A p \in Peers: health[p] = Healthy
  /\ generation' = generation + 1
  /\ wire' = Append(wire,
       MakeStart(hostPending, generation + 1, hostLast))
  /\ UNCHANGED <<issued, hostState, hostPending, hostLast, joinLast,
                  joinCache, hostApplied, joinApplied, hostIrq, joinIrq,
                  hostApplyCount, joinApplyCount, retiredCaches, faultsLeft,
                  health, pressure>>

Lose(index) ==
  /\ faultsLeft > 0
  /\ index \in 1..Len(wire)
  /\ wire' = RemoveAt(wire, index)
  /\ faultsLeft' = faultsLeft - 1
  /\ UNCHANGED <<issued, hostState, hostPending, hostLast, joinLast,
                  joinCache, hostApplied, joinApplied, hostIrq, joinIrq,
                  hostApplyCount, joinApplyCount, retiredCaches, generation,
                  health, pressure>>

Duplicate(index) ==
  /\ faultsLeft > 0
  /\ index \in 1..Len(wire)
  /\ Len(wire) < MaxWire
  /\ wire' = Append(wire, wire[index])
  /\ faultsLeft' = faultsLeft - 1
  /\ UNCHANGED <<issued, hostState, hostPending, hostLast, joinLast,
                  joinCache, hostApplied, joinApplied, hostIrq, joinIrq,
                  hostApplyCount, joinApplyCount, retiredCaches, generation,
                  health, pressure>>

Degrade(p) ==
  /\ faultsLeft > 0
  /\ health[p] = Healthy
  /\ health' = [health EXCEPT ![p] = Degraded]
  /\ faultsLeft' = faultsLeft - 1
  /\ UNCHANGED <<issued, hostState, hostPending, hostLast, joinLast,
                  joinCache, hostApplied, joinApplied, hostIrq, joinIrq,
                  hostApplyCount, joinApplyCount, retiredCaches, generation,
                  wire, pressure>>

Restore(p) ==
  /\ health[p] = Degraded
  /\ health' = [health EXCEPT ![p] = Healthy]
  /\ UNCHANGED <<issued, hostState, hostPending, hostLast, joinLast,
                  joinCache, hostApplied, joinApplied, hostIrq, joinIrq,
                  hostApplyCount, joinApplyCount, retiredCaches, generation,
                  wire, faultsLeft, pressure>>

Backpressure(p) ==
  /\ faultsLeft > 0
  /\ hostState = WaitReply
  /\ pressure' = [pressure EXCEPT ![p] = @ + 1]
  /\ faultsLeft' = faultsLeft - 1
  /\ UNCHANGED <<issued, hostState, hostPending, hostLast, joinLast,
                  joinCache, hostApplied, joinApplied, hostIrq, joinIrq,
                  hostApplyCount, joinApplyCount, retiredCaches, generation,
                  wire, health>>

Progress ==
  \/ Start
  \/ Deliver
  \/ RetransmitStart
  \/ RecoverAsymmetric
  \/ \E p \in Peers: Restore(p)

Fault ==
  \/ \E index \in 1..Len(wire): Lose(index)
  \/ \E index \in 1..Len(wire): Duplicate(index)
  \/ \E p \in Peers: Degrade(p)
  \/ \E p \in Peers: Backpressure(p)

Next == Progress \/ Fault

AppliedPrefix(values, last) == values = 1..last

TypeOK ==
  /\ issued \in TransfersOrNone
  /\ hostState \in HostStates
  /\ hostPending \in TransfersOrNone
  /\ hostLast \in TransfersOrNone
  /\ joinLast \in TransfersOrNone
  /\ joinCache \in TransfersOrNone
  /\ hostApplied \subseteq Transfers
  /\ joinApplied \subseteq Transfers
  /\ hostIrq \subseteq Transfers
  /\ joinIrq \subseteq Transfers
  /\ hostApplyCount \in TransfersOrNone
  /\ joinApplyCount \in TransfersOrNone
  /\ retiredCaches \subseteq Transfers
  /\ generation \in Generations
  /\ wire \in Seq(MessageType)
  /\ Len(wire) <= MaxWire
  /\ faultsLeft \in 0..MaxFaults
  /\ health \in [Peers -> HealthStates]
  /\ pressure \in [Peers -> 0..MaxFaults]

OneCausalTransfer ==
  /\ issued >= hostLast
  /\ issued >= joinLast
  /\ joinLast >= hostLast
  /\ joinLast <= hostLast + 1
  /\ IF hostState = Ready
     THEN /\ hostPending = 0
          /\ hostLast = joinLast
     ELSE /\ hostPending = hostLast + 1
          /\ issued = hostPending
          /\ joinLast \in {hostLast, hostPending}

ExactOnceApplyAndIrq ==
  /\ AppliedPrefix(hostApplied, hostLast)
  /\ AppliedPrefix(joinApplied, joinLast)
  /\ hostApplied = hostIrq
  /\ joinApplied = joinIrq
  /\ hostApplyCount = Cardinality(hostApplied)
  /\ joinApplyCount = Cardinality(joinApplied)

ReplyCacheBoundedAndCausal ==
  /\ joinCache \in {0, joinLast}
  /\ joinCache # 0 => joinCache \in joinApplied
  /\ retiredCaches \subseteq hostApplied

CurrentGenerationOnly ==
  \A message \in WireSet(wire): message.generation <= generation

HealthExclusive == \A p \in Peers: health[p] \in HealthStates

BoundedResources ==
  /\ Len(wire) <= MaxWire
  /\ hostApplyCount <= MaxTransfers
  /\ joinApplyCount <= MaxTransfers
  /\ \A p \in Peers: pressure[p] <= MaxFaults

Safety ==
  /\ TypeOK
  /\ OneCausalTransfer
  /\ ExactOnceApplyAndIrq
  /\ ReplyCacheBoundedAndCausal
  /\ CurrentGenerationOnly
  /\ HealthExclusive
  /\ BoundedResources

Pending ==
  \/ hostState = WaitReply
  \/ Len(wire) # 0
  \/ \E p \in Peers: health[p] = Degraded

Settled ==
  /\ hostState = Ready
  /\ hostLast = joinLast
  /\ Len(wire) = 0
  /\ \A p \in Peers: health[p] = Healthy

SafetySpec == Init /\ [][Next]_vars

Fairness ==
  /\ SF_vars(Deliver)
  /\ SF_vars(RetransmitStart)
  /\ WF_vars(RecoverAsymmetric)
  /\ \A p \in Peers: WF_vars(Restore(p))

FairSpec == SafetySpec /\ Fairness

PendingConverges == Pending ~> Settled
AllIssuedTransfersComplete ==
  issued = MaxTransfers ~>
    /\ hostLast = MaxTransfers
    /\ joinLast = MaxTransfers

=============================================================================
