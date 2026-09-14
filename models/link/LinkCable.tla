------------------------------ MODULE LinkCable ------------------------------
\* Finite executable model for the two-peer link journal and its recovery.
\* The generated module below is derived from the same transition table exposed
\* to production C.  This model intentionally abstracts PSP calls and GBA
\* register details; their later milestones refine these actions without
\* changing journal semantics.

EXTENDS Integers, Sequences, FiniteSets, TLC
INSTANCE LinkJournalTransitions

CONSTANTS
  MaxTransfers,
  InitialTransfer,
  SequenceModulus,
  EpochModulus,
  PacketModulus,
  MaxRecoveries,
  MaxWire,
  MaxFaults,
  ShutdownSteps

ASSUME /\ InitialTransfer >= 0
       /\ MaxTransfers > InitialTransfer
       /\ SequenceModulus >= 3
       /\ MaxTransfers >= SequenceModulus
       /\ InitialTransfer = SequenceModulus - 1
       /\ EpochModulus >= 3
       /\ PacketModulus >= 3
       /\ MaxRecoveries >= 1
       /\ MaxWire >= 2
       /\ MaxFaults >= 1
       /\ ShutdownSteps >= 1

Host == "HOST"
Joiner == "JOINER"
Peers == {Host, Joiner}

Other(p) == IF p = Host THEN Joiner ELSE Host
RoleOf(p) == p

Empty == "EMPTY"
Armed == "ARMED"
Decided == "DECIDED"
Released == "RELEASED"

Running == "RUNNING"
Closing == "CLOSING"
Closed == "CLOSED"
LifeStates == {Running, Closing, Closed}

Normal == "NORMAL"
Quiescent == "QUIESCENT"
RecoveryStates == {Normal, Quiescent}

Healthy == "HEALTHY"
Degraded == "DEGRADED"
HealthStates == {Healthy, Degraded}

NoHash == "NO_HASH"
Hashes == {"HASH_A", "HASH_B"}

NoneMessageKind == "NONE"
HostArmMessage == "HOST_ARM"
JoinerArmMessage == "JOINER_ARM"
DecisionMessage == "DECISION"
DecisionAckMessage == "DECISION_ACK"
ReleaseMessage == "RELEASE"
ReleaseAckMessage == "RELEASE_ACK"
RecoveryMessage == "RECOVERY"
StateMessage == "STATE"
CloseMessage == "CLOSE"

NormalMessageKinds ==
  {HostArmMessage, JoinerArmMessage, DecisionMessage, DecisionAckMessage,
   ReleaseMessage, ReleaseAckMessage}
MessageKinds ==
  NormalMessageKinds \cup
    {NoneMessageKind, RecoveryMessage, StateMessage, CloseMessage}

TransferIds == 0..MaxTransfers
RecoveryRounds == 0..MaxRecoveries
Epochs == 0..(EpochModulus - 1)
Packets == 0..(PacketModulus - 1)
Cycles == 0..MaxTransfers

SeqOf(id) == id % SequenceModulus
NextEpoch(value) == (value + 1) % EpochModulus
NextPacket(value) == (value + 1) % PacketModulus
HashOf(id) == IF id % 2 = 0 THEN "HASH_A" ELSE "HASH_B"
InitialHash == IF InitialTransfer = 0 THEN NoHash ELSE HashOf(InitialTransfer)

LedgerEntry(id, hash) == [transfer |-> id, hash |-> hash]

MessageType ==
  [kind: MessageKinds,
   from: Peers,
   to: Peers,
   nonce: 0..1,
   epoch: Epochs,
   recovery: RecoveryRounds,
   packet: Packets,
   logical: TransferIds \X MessageKinds \X RecoveryRounds,
   transfer: TransferIds,
   sequence: 0..(SequenceModulus - 1),
   hash: Hashes \cup {NoHash},
   stage: JournalStages,
   checkpoint: TransferIds,
   checkpointHash: Hashes \cup {NoHash},
   startCycle: Cycles,
   finishCycle: Cycles]

WireSet(s) == {s[index] : index \in 1..Len(s)}

RemoveAt(s, index) ==
  IF index = 1
  THEN Tail(s)
  ELSE IF index = Len(s)
       THEN SubSeq(s, 1, Len(s) - 1)
       ELSE SubSeq(s, 1, index - 1) \o SubSeq(s, index + 1, Len(s))

VARIABLES peer, issued, wire, faultsLeft, hot

vars == <<peer, issued, wire, faultsLeft, hot>>

MakeMessage(kind, sender, nonceValue, epochValue, recoveryValue,
            transferValue, hashValue, stageValue, checkpointValue,
            checkpointHashValue, startValue, finishValue) ==
  [kind |-> kind,
   from |-> sender,
   to |-> Other(sender),
   nonce |-> nonceValue,
   epoch |-> epochValue,
   recovery |-> recoveryValue,
   packet |-> 0,
   logical |-> <<transferValue, kind, recoveryValue>>,
   transfer |-> transferValue,
   sequence |-> SeqOf(transferValue),
   hash |-> hashValue,
   stage |-> stageValue,
   checkpoint |-> checkpointValue,
   checkpointHash |-> checkpointHashValue,
   startCycle |-> startValue,
   finishCycle |-> finishValue]

NoMessage ==
  MakeMessage(NoneMessageKind, Host, 0, 0, 0, 0, NoHash, Empty, 0,
              NoHash, 0, 0)
NoEvidence == NoMessage

ProtocolMessage(kind, sender, transferValue, hashValue) ==
  MakeMessage(kind, sender, peer[sender].nonce, peer[sender].epoch,
              peer[sender].recoveryRound, transferValue, hashValue,
              peer[sender].stage, peer[sender].checkpoint,
              peer[sender].checkpointHash, peer[sender].startCycle,
              peer[sender].finishCycle)

RecoverySnapshot(sender, epochValue, recoveryValue) ==
  MakeMessage(RecoveryMessage, sender, peer[sender].nonce, epochValue,
              recoveryValue, peer[sender].active, peer[sender].hash,
              peer[sender].stage, peer[sender].checkpoint,
              peer[sender].checkpointHash, peer[sender].startCycle,
              peer[sender].finishCycle)

ReplaceableState(sender) ==
  MakeMessage(StateMessage, sender, peer[sender].nonce, peer[sender].epoch,
              peer[sender].recoveryRound, 0, NoHash, peer[sender].stage,
              peer[sender].checkpoint, peer[sender].checkpointHash,
              peer[sender].cycle, peer[sender].horizon)

PeerInit(p) ==
  [role |-> RoleOf(p),
   stage |-> Empty,
   active |-> 0,
   hash |-> NoHash,
   checkpoint |-> InitialTransfer,
   checkpointHash |-> InitialHash,
   seen |-> InitialTransfer,
   ledger |-> {},
   applied |-> {},
   irq |-> {},
   applyCount |-> 0,
   irqCount |-> 0,
   critical |-> NoMessage,
   replaceable |-> FALSE,
   nextPacket |-> 0,
   nonce |-> 1,
   epoch |-> 0,
   recoveryRound |-> 0,
   recovery |-> Normal,
   health |-> Healthy,
   recoveryRequested |-> FALSE,
   localCancelCount |-> 0,
   evidence |-> NoEvidence,
   recoveryFloor |-> InitialTransfer,
   life |-> Running,
   shutdown |-> 0,
   ready |-> TRUE,
   compatible |-> TRUE,
   releaseShared |-> FALSE,
   cycle |-> InitialTransfer,
   horizon |-> InitialTransfer,
   startCycle |-> InitialTransfer,
   finishCycle |-> InitialTransfer,
   backpressure |-> 0,
   closeReason |-> "NONE",
   lastMutationNonce |-> 1,
   lastMutationEpoch |-> 0]

Init ==
  /\ peer = [p \in Peers |-> PeerInit(p)]
  /\ issued = InitialTransfer
  /\ wire = <<>>
  /\ faultsLeft = MaxFaults
  /\ hot = TRUE

Fresh(message) ==
  /\ message.to \in Peers
  /\ message.nonce = peer[message.to].nonce
  /\ message.epoch = peer[message.to].epoch

CanHandleNormal(message) ==
  /\ message.kind \in NormalMessageKinds
  /\ Fresh(message)
  /\ peer[message.to].life = Running
  /\ peer[message.to].recovery = Normal
  /\ peer[message.to].health = Healthy
  /\ ~peer[message.to].recoveryRequested

CriticalInFlight(p) ==
  \E message \in WireSet(wire):
    /\ message.from = p
    /\ message.logical = peer[p].critical.logical

DecisionRecorded(p, transferValue) ==
  \E entry \in peer[p].ledger: entry.transfer = transferValue

SetCritical(state, p, message) ==
  [state EXCEPT ![p].critical = message]

CloseOne(state, p, reason) ==
  [state EXCEPT
    ![p].life = Closing,
    ![p].shutdown = ShutdownSteps,
    ![p].ready = FALSE,
    ![p].recovery = Quiescent,
    ![p].health = Healthy,
    ![p].recoveryRequested = FALSE,
    ![p].critical = NoMessage,
    ![p].replaceable = FALSE,
    ![p].closeReason = reason,
    ![p].horizon = state[p].cycle]

CloseBoth(state, reason) ==
  [p \in Peers |->
    [state[p] EXCEPT
      !.life = Closing,
      !.shutdown = ShutdownSteps,
      !.ready = FALSE,
      !.recovery = Quiescent,
      !.health = Healthy,
      !.recoveryRequested = FALSE,
      !.critical = NoMessage,
      !.replaceable = FALSE,
      !.closeReason = reason,
      !.horizon = state[p].cycle]]

\* The host admits a new transfer only at a checkpoint shared by both peers.
HostArm ==
  /\ issued < MaxTransfers
  /\ \A p \in Peers:
       /\ peer[p].life = Running
       /\ peer[p].recovery = Normal
       /\ peer[p].health = Healthy
       /\ ~peer[p].recoveryRequested
       /\ peer[p].stage = Empty
       /\ peer[p].ready
       /\ peer[p].compatible
  /\ peer[Host].checkpoint = peer[Joiner].checkpoint
  /\ peer[Host].checkpointHash = peer[Joiner].checkpointHash
  /\ peer[Host].cycle = peer[Joiner].cycle
  /\ peer[Host].cycle < MaxTransfers
  /\ IsLegalJournalTransition("arm", Host, peer[Host].stage, Armed)
  /\ LET id == issued + 1
         start == peer[Host].cycle
         finish == start + 1
         arm == MakeMessage(HostArmMessage, Host, peer[Host].nonce,
                            peer[Host].epoch, peer[Host].recoveryRound,
                            id, NoHash, Armed, peer[Host].checkpoint,
                            peer[Host].checkpointHash, start, finish)
     IN /\ issued' = id
        /\ peer' =
             [peer EXCEPT
               ![Host].stage = Armed,
               ![Host].active = id,
               ![Host].hash = NoHash,
               ![Host].seen = id,
               ![Host].critical = arm,
               ![Host].releaseShared = FALSE,
               ![Host].startCycle = start,
               ![Host].finishCycle = finish,
               ![Host].horizon = start,
               ![Host].lastMutationNonce = peer[Host].nonce,
               ![Host].lastMutationEpoch = peer[Host].epoch]
  /\ UNCHANGED <<wire, faultsLeft, hot>>

SendCritical(p) ==
  /\ peer[p].life = Running
  /\ peer[p].critical # NoMessage
  /\ ~CriticalInFlight(p)
  /\ Len(wire) < MaxWire
  /\ LET packet == [peer[p].critical EXCEPT !.packet = peer[p].nextPacket]
     IN wire' = Append(wire, packet)
  /\ peer' =
       [peer EXCEPT ![p].nextPacket = NextPacket(peer[p].nextPacket)]
  /\ UNCHANGED <<issued, faultsLeft, hot>>

SendReplaceable(p) ==
  /\ peer[p].life = Running
  /\ peer[p].replaceable
  /\ Len(wire) < MaxWire
  /\ LET packet ==
       [ReplaceableState(p) EXCEPT !.packet = peer[p].nextPacket]
     IN wire' = Append(wire, packet)
  /\ peer' =
       [peer EXCEPT
         ![p].replaceable = FALSE,
         ![p].nextPacket = NextPacket(peer[p].nextPacket)]
  /\ UNCHANGED <<issued, faultsLeft, hot>>

\* Loss and duplication consume a finite fault budget in the liveness model.
Lose(index) ==
  /\ faultsLeft > 0
  /\ index \in 1..Len(wire)
  /\ wire' = RemoveAt(wire, index)
  /\ faultsLeft' = faultsLeft - 1
  /\ UNCHANGED <<peer, issued, hot>>

Duplicate(index) ==
  /\ faultsLeft > 0
  /\ index \in 1..Len(wire)
  /\ Len(wire) < MaxWire
  /\ wire' = Append(wire, wire[index])
  /\ faultsLeft' = faultsLeft - 1
  /\ UNCHANGED <<peer, issued, hot>>

Backpressure(p) ==
  /\ faultsLeft > 0
  /\ peer[p].life = Running
  /\ \/ /\ peer[p].critical # NoMessage
         /\ ~CriticalInFlight(p)
     \/ peer[p].replaceable
  /\ peer' =
       [peer EXCEPT
         ![p].backpressure =
           IF @ < MaxFaults THEN @ + 1 ELSE @]
  /\ faultsLeft' = faultsLeft - 1
  /\ UNCHANGED <<issued, wire, hot>>

\* Receive silence is an independent recovery cause.  In particular, a peer
\* with no critical record cannot wait for an ACK-progress stall that can
\* never exist.
SilenceRecovery(p) ==
  /\ faultsLeft > 0
  /\ peer[p].life = Running
  /\ peer[p].recovery = Normal
  /\ peer[p].health = Healthy
  /\ ~peer[p].recoveryRequested
  /\ peer[p].recoveryRound < MaxRecoveries
  /\ peer' =
       [peer EXCEPT
         ![p].recoveryRequested = TRUE,
         ![p].replaceable = FALSE,
         ![p].horizon = peer[p].cycle]
  /\ faultsLeft' = faultsLeft - 1
  /\ UNCHANGED <<issued, wire, hot>>

\* A valid-looking conflicting decision is never adopted; it fails closed.
InjectConflictingDecision ==
  /\ faultsLeft > 0
  /\ peer[Host].stage \in {Decided, Released}
  /\ Len(wire) < MaxWire
  /\ LET wrong == IF peer[Host].hash = "HASH_A" THEN "HASH_B" ELSE "HASH_A"
         message ==
           MakeMessage(DecisionMessage, Host, peer[Host].nonce,
                       peer[Host].epoch, peer[Host].recoveryRound,
                       peer[Host].active, wrong, Decided,
                       peer[Host].checkpoint, peer[Host].checkpointHash,
                       peer[Host].startCycle, peer[Host].finishCycle)
     IN wire' = Append(wire, message)
  /\ faultsLeft' = faultsLeft - 1
  /\ UNCHANGED <<peer, issued, hot>>

\* Stale identity traffic exercises nonce/epoch rejection without mutation.
InjectStaleIdentity ==
  /\ faultsLeft > 0
  /\ Len(wire) < MaxWire
  /\ LET staleNonce == IF peer[Host].nonce = 0 THEN 1 ELSE 0
         message ==
           MakeMessage(DecisionMessage, Host, staleNonce,
                       peer[Host].epoch, peer[Host].recoveryRound,
                       peer[Host].active, HashOf(peer[Host].active), Decided,
                       peer[Host].checkpoint, peer[Host].checkpointHash,
                       peer[Host].startCycle, peer[Host].finishCycle)
     IN wire' = Append(wire, message)
  /\ faultsLeft' = faultsLeft - 1
  /\ UNCHANGED <<peer, issued, hot>>

DeliverHostArm(index) ==
  /\ index \in 1..Len(wire)
  /\ LET message == wire[index]
         q == message.to
     IN /\ message.kind = HostArmMessage
        /\ q = Joiner
        /\ CanHandleNormal(message)
        /\ peer[q].stage = Empty
        /\ message.transfer > peer[q].seen
        /\ message.sequence = SeqOf(message.transfer)
        /\ peer[q].cycle <= message.startCycle
        /\ IsLegalJournalTransition("arm", Joiner, peer[q].stage, Armed)
        /\ peer' =
             [peer EXCEPT
               ![q].stage = Armed,
               ![q].active = message.transfer,
               ![q].hash = NoHash,
               ![q].seen = message.transfer,
               ![q].critical =
                 MakeMessage(JoinerArmMessage, q, peer[q].nonce, peer[q].epoch,
                             peer[q].recoveryRound, message.transfer, NoHash,
                             Armed, peer[q].checkpoint, peer[q].checkpointHash,
                             message.startCycle, message.finishCycle),
               ![q].releaseShared = FALSE,
               ![q].startCycle = message.startCycle,
               ![q].finishCycle = message.finishCycle,
               ![q].horizon = message.startCycle,
               ![q].lastMutationNonce = message.nonce,
               ![q].lastMutationEpoch = message.epoch]
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, faultsLeft, hot>>

DeliverJoinerArm(index) ==
  /\ index \in 1..Len(wire)
  /\ LET message == wire[index]
         q == message.to
         id == message.transfer
         decisionHash == HashOf(id)
     IN /\ message.kind = JoinerArmMessage
        /\ q = Host
        /\ CanHandleNormal(message)
        /\ peer[q].stage = Armed
        /\ peer[q].active = id
        /\ peer[q].startCycle = message.startCycle
        /\ peer[q].finishCycle = message.finishCycle
        /\ IsLegalJournalTransition("issue_decision", Host,
                                    peer[q].stage, Decided)
        /\ peer' =
             [peer EXCEPT
               ![q].stage = Decided,
               ![q].hash = decisionHash,
               ![q].ledger =
                 @ \cup {LedgerEntry(id, decisionHash)},
               ![q].critical =
                 ProtocolMessage(DecisionMessage, q, id, decisionHash),
               ![q].lastMutationNonce = message.nonce,
               ![q].lastMutationEpoch = message.epoch]
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, faultsLeft, hot>>

DeliverDecision(index) ==
  /\ index \in 1..Len(wire)
  /\ LET message == wire[index]
         q == message.to
         id == message.transfer
     IN /\ message.kind = DecisionMessage
        /\ q = Joiner
        /\ CanHandleNormal(message)
        /\ peer[q].stage = Armed
        /\ peer[q].active = id
        /\ message.hash = HashOf(id)
        /\ message.startCycle = peer[q].startCycle
        /\ message.finishCycle = peer[q].finishCycle
        /\ IsLegalJournalTransition("record_decision", Joiner,
                                    peer[q].stage, Decided)
        /\ peer' =
             [peer EXCEPT
               ![q].stage = Decided,
               ![q].hash = message.hash,
               ![q].ledger = @ \cup {LedgerEntry(id, message.hash)},
               ![q].critical =
                 ProtocolMessage(DecisionAckMessage, q, id, message.hash),
               ![q].lastMutationNonce = message.nonce,
               ![q].lastMutationEpoch = message.epoch]
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, faultsLeft, hot>>

DeliverDecisionAck(index) ==
  /\ index \in 1..Len(wire)
  /\ LET message == wire[index]
         q == message.to
         id == message.transfer
     IN /\ message.kind = DecisionAckMessage
        /\ q = Host
        /\ CanHandleNormal(message)
        /\ peer[q].stage = Decided
        /\ peer[q].active = id
        /\ peer[q].hash = message.hash
        /\ IsLegalJournalTransition("mark_released", Host,
                                    peer[q].stage, Released)
        /\ peer' =
             [peer EXCEPT
               ![q].stage = Released,
               ![q].critical =
                 ProtocolMessage(ReleaseMessage, q, id, message.hash),
               ![q].releaseShared = FALSE,
               ![q].horizon = peer[q].finishCycle,
               ![q].lastMutationNonce = message.nonce,
               ![q].lastMutationEpoch = message.epoch]
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, faultsLeft, hot>>

DeliverRelease(index) ==
  /\ index \in 1..Len(wire)
  /\ LET message == wire[index]
         q == message.to
         id == message.transfer
     IN /\ message.kind = ReleaseMessage
        /\ q = Joiner
        /\ CanHandleNormal(message)
        /\ peer[q].stage = Decided
        /\ peer[q].active = id
        /\ peer[q].hash = message.hash
        /\ IsLegalJournalTransition("record_release", Joiner,
                                    peer[q].stage, Released)
        /\ peer' =
             [peer EXCEPT
               ![q].stage = Released,
               ![q].critical =
                 ProtocolMessage(ReleaseAckMessage, q, id, message.hash),
               ![q].releaseShared = TRUE,
               ![q].horizon = peer[q].finishCycle,
               ![q].lastMutationNonce = message.nonce,
               ![q].lastMutationEpoch = message.epoch]
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, faultsLeft, hot>>

DeliverReleaseAck(index) ==
  /\ index \in 1..Len(wire)
  /\ LET message == wire[index]
         q == message.to
         id == message.transfer
     IN /\ message.kind = ReleaseAckMessage
        /\ q = Host
        /\ CanHandleNormal(message)
        /\ peer[q].stage = Released
        /\ peer[q].active = id
        /\ peer[q].hash = message.hash
        /\ IsLegalJournalTransition("replay_release", Host,
                                    peer[q].stage, Released)
        /\ peer' =
             [peer EXCEPT
               ![q].critical = NoMessage,
               ![q].releaseShared = TRUE,
               ![q].lastMutationNonce = message.nonce,
               ![q].lastMutationEpoch = message.epoch]
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, faultsLeft, hot>>

ConflictingDecision(index) ==
  /\ index \in 1..Len(wire)
  /\ LET message == wire[index]
         q == message.to
     IN /\ message.kind \in {DecisionMessage, ReleaseMessage}
        /\ Fresh(message)
        /\ peer[q].life = Running
        /\ peer[q].active = message.transfer
        /\ \/ /\ message.kind = DecisionMessage
              /\ peer[q].stage = Armed
              /\ message.hash # HashOf(message.transfer)
           \/ /\ peer[q].stage \in {Decided, Released}
              /\ peer[q].hash # message.hash
        /\ peer' = CloseOne(peer, q, "HASH_CONFLICT")
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, faultsLeft, hot>>

\* Exact duplicates only repeat an acknowledgement; they never repeat effects.
DeliverDuplicate(index) ==
  /\ index \in 1..Len(wire)
  /\ LET message == wire[index]
         q == message.to
     IN /\ CanHandleNormal(message)
        /\ message.transfer = peer[q].active
        /\ message.hash \in {NoHash, peer[q].hash}
        /\ CASE message.kind = HostArmMessage ->
                  /\ q = Joiner
                  /\ peer[q].stage \in {Armed, Decided, Released}
               [] message.kind = JoinerArmMessage ->
                  /\ q = Host
                  /\ peer[q].stage \in {Decided, Released}
               [] message.kind = DecisionMessage ->
                  /\ q = Joiner
                  /\ peer[q].stage \in {Decided, Released}
                  /\ message.hash = peer[q].hash
               [] message.kind = DecisionAckMessage ->
                  /\ q = Host
                  /\ peer[q].stage = Released
                  /\ message.hash = peer[q].hash
               [] message.kind = ReleaseMessage ->
                  /\ q = Joiner
                  /\ peer[q].stage = Released
                  /\ message.hash = peer[q].hash
               [] OTHER -> FALSE
        /\ peer' =
             IF message.kind = ReleaseMessage
             THEN SetCritical(peer, q,
                    ProtocolMessage(ReleaseAckMessage, q, message.transfer,
                                    message.hash))
             ELSE peer
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, faultsLeft, hot>>

\* Old normal traffic is drained only after it is unambiguously behind the
\* active journal or belongs to an obsolete identity.
DropObsolete(index) ==
  /\ index \in 1..Len(wire)
  /\ LET message == wire[index]
         q == message.to
     IN /\ message.kind \in NormalMessageKinds
        /\ peer[q].life \in LifeStates
        /\ \/ ~Fresh(message)
           \/ peer[q].life # Running
           \/ peer[q].recovery # Normal
           \/ /\ peer[q].stage = Empty
              /\ message.transfer <= peer[q].seen
        /\ peer' =
             IF /\ Fresh(message)
                /\ peer[q].life = Running
                /\ peer[q].stage = Empty
                /\ message.kind = ReleaseMessage
                /\ message.transfer = peer[q].checkpoint
                /\ message.hash = peer[q].checkpointHash
             THEN SetCritical(peer, q,
                    ProtocolMessage(ReleaseAckMessage, q, message.transfer,
                                    message.hash))
             ELSE peer
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, faultsLeft, hot>>

DropState(index) ==
  /\ index \in 1..Len(wire)
  /\ wire[index].kind = StateMessage
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<peer, issued, faultsLeft, hot>>

AdvanceReleased(p) ==
  /\ peer[p].life = Running
  /\ peer[p].recovery = Normal
  /\ peer[p].health = Healthy
  /\ ~peer[p].recoveryRequested
  /\ peer[p].stage = Released
  /\ peer[p].cycle < peer[p].finishCycle
  /\ peer' = [peer EXCEPT ![p].cycle = @ + 1]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

ApplyReleased(p) ==
  /\ peer[p].life = Running
  /\ peer[p].recovery = Normal
  /\ peer[p].health = Healthy
  /\ ~peer[p].recoveryRequested
  /\ peer[p].stage = Released
  /\ peer[p].cycle = peer[p].finishCycle
  /\ peer[p].active \notin peer[p].applied
  /\ peer' =
       [peer EXCEPT
         ![p].applied = @ \cup {peer[p].active},
         ![p].irq = @ \cup {peer[p].active},
         ![p].applyCount = @ + 1,
         ![p].irqCount = @ + 1]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

FinishReleased(p) ==
  /\ peer[p].life = Running
  /\ peer[p].recovery = Normal
  /\ peer[p].health = Healthy
  /\ ~peer[p].recoveryRequested
  /\ peer[p].stage = Released
  /\ peer[p].releaseShared
  /\ peer[p].active \in peer[p].applied
  /\ peer[p].active \in peer[p].irq
  /\ IsLegalJournalTransition("clear_released", RoleOf(p),
                              peer[p].stage, Empty)
  /\ peer' =
       [peer EXCEPT
         ![p].stage = Empty,
         ![p].checkpoint = peer[p].active,
         ![p].checkpointHash = peer[p].hash,
         ![p].active = 0,
         ![p].hash = NoHash,
         ![p].releaseShared = FALSE,
         ![p].startCycle = peer[p].cycle,
         ![p].finishCycle = peer[p].cycle,
         ![p].horizon = peer[p].cycle,
         ![p].lastMutationNonce = peer[p].nonce,
         ![p].lastMutationEpoch = peer[p].epoch]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

\* Degradation pauses normal decisions without changing durable evidence.
Degrade(p) ==
  /\ faultsLeft > 0
  /\ peer[p].life = Running
  /\ peer[p].recovery = Normal
  /\ peer[p].health = Healthy
  /\ ~peer[p].recoveryRequested
  /\ peer' =
       [peer EXCEPT
         ![p].health = Degraded,
         ![p].replaceable = FALSE]
  /\ faultsLeft' = faultsLeft - 1
  /\ UNCHANGED <<issued, wire, hot>>

RestoreHealth(p) ==
  /\ peer[p].life = Running
  /\ peer[p].recovery = Normal
  /\ peer[p].health = Degraded
  /\ ~peer[p].recoveryRequested
  /\ peer' =
       [peer EXCEPT
         ![p].health = Healthy,
         ![p].replaceable = TRUE]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

RequestRecovery(p) ==
  /\ peer[p].life = Running
  /\ peer[p].recovery = Normal
  /\ peer[p].health = Degraded
  /\ ~peer[p].recoveryRequested
  /\ peer[p].recoveryRound < MaxRecoveries
  /\ peer' = [peer EXCEPT ![p].recoveryRequested = TRUE]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

\* Only the coordinator can locally cancel an armed, still-undecided record.
\* There is deliberately no corresponding participant action.
LocalCancelHost ==
  /\ peer[Host].life = Running
  /\ peer[Host].recovery = Normal
  /\ peer[Host].health = Healthy
  /\ ~peer[Host].recoveryRequested
  /\ peer[Host].stage = Armed
  /\ peer[Joiner].stage = Empty
  /\ ~CriticalInFlight(Host)
  /\ ~DecisionRecorded(Host, peer[Host].active)
  /\ IsLegalJournalTransition("cancel_arm", Host, Armed, Empty)
  /\ peer' =
       [peer EXCEPT
         ![Host].stage = Empty,
         ![Host].active = 0,
         ![Host].hash = NoHash,
         ![Host].critical = NoMessage,
         ![Host].releaseShared = FALSE,
         ![Host].startCycle = peer[Host].cycle,
         ![Host].finishCycle = peer[Host].cycle,
         ![Host].horizon = peer[Host].cycle,
         ![Host].localCancelCount = @ + 1]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

BeginRecovery(p) ==
  /\ peer[p].life = Running
  /\ peer[p].recovery = Normal
  /\ peer[p].recoveryRequested
  /\ peer[p].recoveryRound < MaxRecoveries
  /\ LET round == peer[p].recoveryRound + 1
         newEpoch == NextEpoch(peer[p].epoch)
         snapshot == RecoverySnapshot(p, newEpoch, round)
     IN peer' =
          [peer EXCEPT
            ![p].epoch = newEpoch,
            ![p].recoveryRound = round,
            ![p].recovery = Quiescent,
            ![p].health = Healthy,
            ![p].recoveryRequested = FALSE,
            ![p].evidence = NoEvidence,
            ![p].recoveryFloor = peer[p].checkpoint,
            ![p].critical = snapshot,
            ![p].replaceable = FALSE,
            ![p].horizon = peer[p].cycle,
            ![p].lastMutationNonce = peer[p].nonce,
            ![p].lastMutationEpoch = newEpoch]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

DeliverRecoveryStart(index) ==
  /\ index \in 1..Len(wire)
  /\ LET message == wire[index]
         q == message.to
     IN /\ message.kind = RecoveryMessage
        /\ peer[q].life = Running
        /\ message.nonce = peer[q].nonce
        /\ message.recovery = peer[q].recoveryRound + 1
        /\ message.recovery <= MaxRecoveries
        /\ message.epoch = NextEpoch(peer[q].epoch)
        /\ peer[q].recovery = Normal
        /\ peer' =
             [peer EXCEPT
               ![q].epoch = message.epoch,
               ![q].recoveryRound = message.recovery,
               ![q].recovery = Quiescent,
               ![q].health = Healthy,
               ![q].recoveryRequested = FALSE,
               ![q].evidence = message,
               ![q].recoveryFloor = peer[q].checkpoint,
               ![q].critical =
                 RecoverySnapshot(q, message.epoch, message.recovery),
               ![q].replaceable = FALSE,
               ![q].horizon = peer[q].cycle,
               ![q].lastMutationNonce = peer[q].nonce,
               ![q].lastMutationEpoch = message.epoch]
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, faultsLeft, hot>>

DeliverRecoveryPeer(index) ==
  /\ index \in 1..Len(wire)
  /\ LET message == wire[index]
         q == message.to
     IN /\ message.kind = RecoveryMessage
        /\ peer[q].life = Running
        /\ peer[q].recovery = Quiescent
        /\ message.nonce = peer[q].nonce
        /\ message.epoch = peer[q].epoch
        /\ message.recovery = peer[q].recoveryRound
        /\ peer' = [peer EXCEPT ![q].evidence = message]
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, faultsLeft, hot>>

RecoveryReady ==
  /\ \A p \in Peers:
       /\ peer[p].life = Running
       /\ peer[p].recovery = Quiescent
       /\ peer[p].evidence # NoEvidence
       /\ peer[p].evidence.from = Other(p)
       /\ peer[p].evidence.nonce = peer[p].nonce
       /\ peer[p].evidence.epoch = peer[p].epoch
       /\ peer[p].evidence.recovery = peer[p].recoveryRound
  /\ peer[Host].epoch = peer[Joiner].epoch
  /\ peer[Host].recoveryRound = peer[Joiner].recoveryRound

RecoveryNoDecision ==
  /\ RecoveryReady
  /\ peer[Host].stage \in {Empty, Armed}
  /\ peer[Joiner].stage \in {Empty, Armed}
  /\ peer[Host].checkpoint = peer[Joiner].checkpoint
  /\ peer[Host].checkpointHash = peer[Joiner].checkpointHash

ReconcileNoDecision ==
  /\ RecoveryNoDecision
  /\ \A p \in Peers:
       IF peer[p].stage = Armed
       THEN IsLegalJournalTransition("recover_cancel_arm", RoleOf(p),
                                     Armed, Empty)
       ELSE IsLegalJournalTransition("recover_empty", RoleOf(p), Empty, Empty)
  /\ peer' =
       [p \in Peers |->
         [peer[p] EXCEPT
           !.stage = Empty,
           !.active = 0,
           !.hash = NoHash,
           !.critical = NoMessage,
           !.recovery = Normal,
           !.evidence = NoEvidence,
           !.replaceable = TRUE,
           !.releaseShared = FALSE,
           !.startCycle = peer[p].cycle,
           !.finishCycle = peer[p].cycle,
           !.horizon = peer[p].cycle,
           !.lastMutationNonce = peer[p].nonce,
           !.lastMutationEpoch = peer[p].epoch]]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

ReconcileHostDecision ==
  /\ RecoveryReady
  /\ peer[Host].stage = Decided
  /\ peer[Joiner].stage = Armed
  /\ peer[Host].active = peer[Joiner].active
  /\ peer[Host].hash = HashOf(peer[Host].active)
  /\ IsLegalJournalTransition("recover_decision", Joiner,
                              peer[Joiner].stage, Decided)
  /\ LET id == peer[Host].active
         hash == peer[Host].hash
     IN peer' =
          [peer EXCEPT
            ![Host].recovery = Normal,
            ![Host].evidence = NoEvidence,
            ![Host].critical =
              ProtocolMessage(DecisionMessage, Host, id, hash),
            ![Host].replaceable = TRUE,
            ![Host].horizon = peer[Host].startCycle,
            ![Joiner].stage = Decided,
            ![Joiner].hash = hash,
            ![Joiner].ledger = @ \cup {LedgerEntry(id, hash)},
            ![Joiner].recovery = Normal,
            ![Joiner].evidence = NoEvidence,
            ![Joiner].critical =
              ProtocolMessage(DecisionAckMessage, Joiner, id, hash),
            ![Joiner].replaceable = TRUE,
            ![Joiner].horizon = peer[Joiner].startCycle,
            ![Joiner].lastMutationNonce = peer[Joiner].nonce,
            ![Joiner].lastMutationEpoch = peer[Joiner].epoch]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

SameDecision ==
  /\ peer[Host].active = peer[Joiner].active
  /\ peer[Host].active # 0
  /\ peer[Host].hash = peer[Joiner].hash
  /\ peer[Host].hash = HashOf(peer[Host].active)

ReconcileBothDecided ==
  /\ RecoveryReady
  /\ peer[Host].stage = Decided
  /\ peer[Joiner].stage = Decided
  /\ SameDecision
  /\ IsLegalJournalTransition("recover_release", Host, Decided, Released)
  /\ LET id == peer[Host].active
         hash == peer[Host].hash
     IN peer' =
          [peer EXCEPT
            ![Host].stage = Released,
            ![Host].recovery = Normal,
            ![Host].evidence = NoEvidence,
            ![Host].critical =
              ProtocolMessage(ReleaseMessage, Host, id, hash),
            ![Host].replaceable = TRUE,
            ![Host].horizon = peer[Host].finishCycle,
            ![Host].releaseShared = FALSE,
            ![Joiner].recovery = Normal,
            ![Joiner].evidence = NoEvidence,
            ![Joiner].critical =
              ProtocolMessage(DecisionAckMessage, Joiner, id, hash),
            ![Joiner].replaceable = TRUE,
            ![Joiner].horizon = peer[Joiner].startCycle]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

ReconcileHostReleased ==
  /\ RecoveryReady
  /\ peer[Host].stage = Released
  /\ peer[Joiner].stage = Decided
  /\ SameDecision
  /\ LET id == peer[Host].active
         hash == peer[Host].hash
     IN peer' =
          [peer EXCEPT
            ![Host].recovery = Normal,
            ![Host].evidence = NoEvidence,
            ![Host].critical =
              ProtocolMessage(ReleaseMessage, Host, id, hash),
            ![Host].replaceable = TRUE,
            ![Host].horizon = peer[Host].finishCycle,
            ![Joiner].recovery = Normal,
            ![Joiner].evidence = NoEvidence,
            ![Joiner].critical =
              ProtocolMessage(DecisionAckMessage, Joiner, id, hash),
            ![Joiner].replaceable = TRUE,
            ![Joiner].horizon = peer[Joiner].startCycle]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

ReconcileBothReleased ==
  /\ RecoveryReady
  /\ peer[Host].stage = Released
  /\ peer[Joiner].stage = Released
  /\ SameDecision
  /\ LET id == peer[Host].active
         hash == peer[Host].hash
     IN peer' =
          [peer EXCEPT
            ![Host].recovery = Normal,
            ![Host].evidence = NoEvidence,
            ![Host].critical = NoMessage,
            ![Host].replaceable = TRUE,
            ![Host].releaseShared = TRUE,
            ![Host].horizon = peer[Host].finishCycle,
            ![Joiner].recovery = Normal,
            ![Joiner].evidence = NoEvidence,
            ![Joiner].critical =
              ProtocolMessage(ReleaseAckMessage, Joiner, id, hash),
            ![Joiner].replaceable = TRUE,
            ![Joiner].releaseShared = TRUE,
            ![Joiner].horizon = peer[Joiner].finishCycle]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

ReconcileReleasedCheckpoint ==
  /\ RecoveryReady
  /\ \/ /\ peer[Host].stage = Released
        /\ peer[Joiner].stage = Empty
        /\ peer[Host].active = peer[Joiner].checkpoint
        /\ peer[Host].hash = peer[Joiner].checkpointHash
     \/ /\ peer[Host].stage = Empty
        /\ peer[Joiner].stage = Released
        /\ peer[Joiner].active = peer[Host].checkpoint
        /\ peer[Joiner].hash = peer[Host].checkpointHash
  /\ peer' =
       [p \in Peers |->
         [peer[p] EXCEPT
           !.recovery = Normal,
           !.evidence = NoEvidence,
           !.critical = NoMessage,
           !.replaceable = TRUE,
           !.releaseShared =
             IF peer[p].stage = Released
             THEN TRUE
             ELSE peer[p].releaseShared,
           !.horizon =
             IF peer[p].stage = Released
             THEN peer[p].finishCycle
             ELSE peer[p].horizon]]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

RecoveryConflict ==
  /\ RecoveryReady
  /\ ~RecoveryNoDecision
  /\ ~(/\ peer[Host].stage = Decided
       /\ peer[Joiner].stage = Armed
       /\ peer[Host].active = peer[Joiner].active
       /\ peer[Host].hash = HashOf(peer[Host].active))
  /\ ~(/\ peer[Host].stage = Decided
       /\ peer[Joiner].stage = Decided
       /\ SameDecision)
  /\ ~(/\ peer[Host].stage = Released
       /\ peer[Joiner].stage = Decided
       /\ SameDecision)
  /\ ~(/\ peer[Host].stage = Released
       /\ peer[Joiner].stage = Released
       /\ SameDecision)
  /\ ~(/\ peer[Host].stage = Released
       /\ peer[Joiner].stage = Empty
       /\ peer[Host].active = peer[Joiner].checkpoint
       /\ peer[Host].hash = peer[Joiner].checkpointHash)
  /\ ~(/\ peer[Host].stage = Empty
       /\ peer[Joiner].stage = Released
       /\ peer[Joiner].active = peer[Host].checkpoint
       /\ peer[Joiner].hash = peer[Host].checkpointHash)
  /\ peer' = CloseBoth(peer, "RECOVERY_CONFLICT")
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

DropStaleRecovery(index) ==
  /\ index \in 1..Len(wire)
  /\ LET message == wire[index]
         q == message.to
     IN /\ message.kind = RecoveryMessage
        /\ peer[q].life \in LifeStates
        /\ \/ message.nonce # peer[q].nonce
           \/ message.recovery < peer[q].recoveryRound
           \/ message.recovery > peer[q].recoveryRound + 1
           \/ /\ peer[q].life = Running
              /\ peer[q].recovery = Normal
              /\ message.epoch = peer[q].epoch
              /\ message.recovery = peer[q].recoveryRound
           \/ peer[q].life # Running
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<peer, issued, faultsLeft, hot>>

Suspend(p) ==
  /\ peer[p].life = Running
  /\ peer' = CloseOne(peer, p, "SUSPEND")
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

PeerExit(p) ==
  /\ peer[p].life = Running
  /\ peer' = CloseOne(peer, p, "PEER_EXIT")
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

ShutdownTick(p) ==
  /\ peer[p].life = Closing
  /\ peer[p].shutdown > 0
  /\ peer' =
       IF peer[p].shutdown = 1
       THEN [peer EXCEPT
              ![p].shutdown = 0,
              ![p].life = Closed,
              ![p].critical = NoMessage,
              ![p].replaceable = FALSE]
       ELSE [peer EXCEPT ![p].shutdown = @ - 1]
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

NoticePeerClosed(p) ==
  /\ peer[p].life = Running
  /\ peer[Other(p)].life = Closed
  /\ peer' = CloseOne(peer, p, "PEER_CLOSED")
  /\ UNCHANGED <<issued, wire, faultsLeft, hot>>

DeliverClose(index) ==
  /\ index \in 1..Len(wire)
  /\ LET message == wire[index]
         q == message.to
     IN /\ message.kind = CloseMessage
        /\ peer[q].life = Running
        /\ message.nonce = peer[q].nonce
        /\ peer' = CloseOne(peer, q, "PEER_CLOSED")
  /\ wire' = RemoveAt(wire, index)
  /\ UNCHANGED <<issued, faultsLeft, hot>>

Handle(index) ==
    \/ DeliverHostArm(index)
    \/ DeliverJoinerArm(index)
    \/ DeliverDecision(index)
    \/ DeliverDecisionAck(index)
    \/ DeliverRelease(index)
    \/ DeliverReleaseAck(index)
    \/ ConflictingDecision(index)
    \/ DeliverDuplicate(index)
    \/ DropObsolete(index)
    \/ DropState(index)
    \/ DeliverRecoveryStart(index)
    \/ DeliverRecoveryPeer(index)
    \/ DropStaleRecovery(index)
    \/ DeliverClose(index)

Deliver ==
  \E index \in 1..Len(wire): Handle(index)

DeliverFrom(p) ==
  \E index \in 1..Len(wire):
    /\ wire[index].from = p
    /\ Handle(index)

Progress ==
  \/ HostArm
  \/ \E p \in Peers: SendCritical(p)
  \/ \E p \in Peers: SendReplaceable(p)
  \/ Deliver
  \/ \E p \in Peers: AdvanceReleased(p)
  \/ \E p \in Peers: ApplyReleased(p)
  \/ \E p \in Peers: FinishReleased(p)
  \/ \E p \in Peers: RestoreHealth(p)
  \/ \E p \in Peers: RequestRecovery(p)
  \/ LocalCancelHost
  \/ \E p \in Peers: BeginRecovery(p)
  \/ ReconcileNoDecision
  \/ ReconcileHostDecision
  \/ ReconcileBothDecided
  \/ ReconcileHostReleased
  \/ ReconcileBothReleased
  \/ ReconcileReleasedCheckpoint
  \/ RecoveryConflict
  \/ \E p \in Peers: ShutdownTick(p)
  \/ \E p \in Peers: NoticePeerClosed(p)

Fault ==
  \/ \E p \in Peers: Degrade(p)
  \/ \E index \in 1..Len(wire): Lose(index)
  \/ \E index \in 1..Len(wire): Duplicate(index)
  \/ \E p \in Peers: Backpressure(p)
  \/ \E p \in Peers: SilenceRecovery(p)
  \/ InjectConflictingDecision
  \/ InjectStaleIdentity
  \/ \E p \in Peers: Suspend(p)
  \/ \E p \in Peers: PeerExit(p)

Next == Progress \/ Fault

TypeOK ==
  /\ issued \in TransferIds
  /\ faultsLeft \in 0..MaxFaults
  /\ hot \in BOOLEAN
  /\ Len(wire) <= MaxWire
  /\ WireSet(wire) \subseteq MessageType
  /\ peer \in
       [Peers ->
         [role: Peers,
          stage: JournalStages,
          active: TransferIds,
          hash: Hashes \cup {NoHash},
          checkpoint: TransferIds,
          checkpointHash: Hashes \cup {NoHash},
          seen: TransferIds,
          ledger: SUBSET [transfer: TransferIds, hash: Hashes],
          applied: SUBSET TransferIds,
          irq: SUBSET TransferIds,
          applyCount: 0..MaxTransfers,
          irqCount: 0..MaxTransfers,
          critical: MessageType \cup {NoMessage},
          replaceable: BOOLEAN,
          nextPacket: Packets,
          nonce: 0..1,
          epoch: Epochs,
          recoveryRound: RecoveryRounds,
          recovery: RecoveryStates,
          health: HealthStates,
          recoveryRequested: BOOLEAN,
          localCancelCount: 0..MaxTransfers,
          evidence: MessageType \cup {NoEvidence},
          recoveryFloor: TransferIds,
          life: LifeStates,
          shutdown: 0..ShutdownSteps,
          ready: BOOLEAN,
          compatible: BOOLEAN,
          releaseShared: BOOLEAN,
          cycle: Cycles,
          horizon: Cycles,
          startCycle: Cycles,
          finishCycle: Cycles,
          backpressure: 0..MaxFaults,
          closeReason: STRING,
          lastMutationNonce: 0..1,
          lastMutationEpoch: Epochs]]

SequenceMonotonic ==
  \A p \in Peers:
    /\ peer[p].seen <= issued
    /\ peer[p].checkpoint <= peer[p].seen
    /\ peer[p].active <= peer[p].seen
    /\ (peer[p].stage = Empty) = (peer[p].active = 0)
    /\ peer[p].stage # Empty => peer[p].active = peer[p].seen
    /\ peer[p].active # 0 => SeqOf(peer[p].active) \in 0..(SequenceModulus - 1)

AtMostOneTransfer ==
  /\ \A p \in Peers: peer[p].stage \in JournalStages
  /\ (peer[Host].active # 0 /\ peer[Joiner].active # 0) =>
       peer[Host].active = peer[Joiner].active

CanonicalHash ==
  \A p \in Peers:
    /\ \A entry \in peer[p].ledger:
         entry.hash = HashOf(entry.transfer)
    /\ peer[p].stage \in {Decided, Released} =>
         /\ peer[p].hash = HashOf(peer[p].active)
         /\ LedgerEntry(peer[p].active, peer[p].hash) \in peer[p].ledger
    /\ peer[p].stage \in {Empty, Armed} => peer[p].hash = NoHash

DecisionNeverRegresses ==
  \A p \in Peers:
    peer[p].stage = Armed => ~DecisionRecorded(p, peer[p].active)

ApplyAndIrqAtMostOnce ==
  \A p \in Peers:
    /\ peer[p].applied = peer[p].irq
    /\ peer[p].applyCount = Cardinality(peer[p].applied)
    /\ peer[p].irqCount = Cardinality(peer[p].irq)

SharedReleaseBeforeAdmission ==
  peer[Host].stage = Armed =>
    /\ peer[Host].checkpoint = peer[Joiner].checkpoint
    /\ peer[Host].checkpointHash = peer[Joiner].checkpointHash
    /\ \/ peer[Joiner].stage = Empty
       \/ /\ peer[Joiner].stage = Armed
          /\ peer[Joiner].active = peer[Host].active

CurrentIdentityOnly ==
  \A p \in Peers:
    peer[p].stage # Empty =>
      /\ peer[p].lastMutationNonce = peer[p].nonce
      /\ peer[p].lastMutationEpoch = peer[p].epoch

RecoveryPreservesCheckpoint ==
  \A p \in Peers:
    peer[p].life # Closed => peer[p].checkpoint >= peer[p].recoveryFloor

QueuesBounded ==
  /\ Len(wire) <= MaxWire
  /\ \A p \in Peers:
       /\ peer[p].critical \in MessageType \cup {NoMessage}
       /\ peer[p].replaceable \in BOOLEAN

BackpressureNoDuplicateWork ==
  \A p \in Peers:
    peer[p].critical # NoMessage =>
      peer[p].critical.logical =
        <<peer[p].critical.transfer, peer[p].critical.kind,
          peer[p].critical.recovery>>

HotHorizonSafe ==
  hot => \A p \in Peers: peer[p].cycle <= peer[p].horizon

ShutdownBounded ==
  \A p \in Peers:
    /\ peer[p].life = Closing => peer[p].shutdown \in 1..ShutdownSteps
    /\ peer[p].life = Closed => peer[p].shutdown = 0

CompatibleBeforeReady ==
  \A p \in Peers: peer[p].ready => peer[p].compatible

PreparedJoinerNeverCancelsLocally ==
  peer[Joiner].localCancelCount = 0

RecoveryRequestPreservesUncertainty ==
  \A p \in Peers:
    peer[p].recoveryRequested /\ peer[p].stage # Empty =>
      /\ peer[p].active # 0
      /\ peer[p].recovery = Normal

Safety ==
  /\ TypeOK
  /\ SequenceMonotonic
  /\ AtMostOneTransfer
  /\ CanonicalHash
  /\ DecisionNeverRegresses
  /\ ApplyAndIrqAtMostOnce
  /\ SharedReleaseBeforeAdmission
  /\ CurrentIdentityOnly
  /\ RecoveryPreservesCheckpoint
  /\ QueuesBounded
  /\ BackpressureNoDuplicateWork
  /\ HotHorizonSafe
  /\ ShutdownBounded
  /\ CompatibleBeforeReady
  /\ PreparedJoinerNeverCancelsLocally
  /\ RecoveryRequestPreservesUncertainty

\* Packet numbers deliberately do not participate in session decisions.  TLC
\* still explores a fresh number on every retransmission, while this view
\* collapses states that differ only in those transport-level labels.
CanonicalPeerState(p) ==
  [peer[p] EXCEPT
    !.nextPacket = 0,
    !.critical.packet = 0,
    !.evidence.packet = 0]

CanonicalWire ==
  [index \in 1..Len(wire) |->
    [wire[index] EXCEPT !.packet = 0]]

ModelView ==
  <<[p \in Peers |-> CanonicalPeerState(p)],
    issued, CanonicalWire, faultsLeft, hot>>

Pending ==
  \/ \E p \in Peers:
       /\ peer[p].life = Running
       /\ peer[p].stage # Empty
  \/ \E p \in Peers: peer[p].recovery = Quiescent
  \/ \E p \in Peers: peer[p].health = Degraded
  \/ \E p \in Peers: peer[p].recoveryRequested
  \/ \E p \in Peers: peer[p].life = Closing

Settled ==
  \/ /\ \A p \in Peers:
          /\ peer[p].life = Running
          /\ peer[p].stage = Empty
          /\ peer[p].recovery = Normal
          /\ peer[p].health = Healthy
          /\ ~peer[p].recoveryRequested
       /\ peer[Host].checkpoint = peer[Joiner].checkpoint
       /\ peer[Host].checkpointHash = peer[Joiner].checkpointHash
  \/ \A p \in Peers: peer[p].life = Closed

SafetySpec == Init /\ [][Next]_vars

Fairness ==
  /\ \A p \in Peers: SF_vars(SendCritical(p))
  /\ \A p \in Peers: SF_vars(SendReplaceable(p))
  /\ \A p \in Peers: SF_vars(DeliverFrom(p))
  /\ \A p \in Peers: WF_vars(AdvanceReleased(p))
  /\ \A p \in Peers: WF_vars(ApplyReleased(p))
  /\ \A p \in Peers: WF_vars(FinishReleased(p))
  /\ \A p \in Peers: WF_vars(RestoreHealth(p))
  /\ \A p \in Peers: WF_vars(RequestRecovery(p))
  /\ \A p \in Peers: WF_vars(BeginRecovery(p))
  /\ WF_vars(ReconcileNoDecision)
  /\ WF_vars(ReconcileHostDecision)
  /\ WF_vars(ReconcileBothDecided)
  /\ WF_vars(ReconcileHostReleased)
  /\ WF_vars(ReconcileBothReleased)
  /\ WF_vars(ReconcileReleasedCheckpoint)
  /\ WF_vars(RecoveryConflict)
  /\ \A p \in Peers: WF_vars(ShutdownTick(p))
  /\ \A p \in Peers: WF_vars(NoticePeerClosed(p))

FairSpec == SafetySpec /\ Fairness

PendingConverges == Pending ~> Settled
ClosingConverges ==
  (\E p \in Peers: peer[p].life = Closing) ~>
    (\A p \in Peers: peer[p].life = Closed)

=============================================================================
