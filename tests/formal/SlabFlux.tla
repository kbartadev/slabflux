--------------------------- MODULE SlabFlux ---------------------------
EXTENDS Integers, Sequences, FiniteSets

CONSTANTS Nodes, MaxLSN

VARIABLES 
    sequencer_lsn,   \* The global counter (Master Sequencer)
    node_states,     \* Current LSN of each simulation node
    global_log       \* The set of all "blessed" LSNs in the journal

Init == 
    /\ sequencer_lsn = 0
    /\ node_states = [n \in Nodes |-> 0]
    /\ global_log = {}

\* The Master Sequencer assigns a new LSN to an input
SequencerStep ==
    /\ sequencer_lsn < MaxLSN
    /\ sequencer_lsn' = sequencer_lsn + 1
    /\ global_log' = global_log \cup {sequencer_lsn'}
    /\ UNCHANGED <<node_states>>

\* A Simulation Node processes the next LSN in order
NodeProcess ==
    \exists n \in Nodes :
        LET next_lsn == node_states[n] + 1
        IN 
            /\ next_lsn \in global_log
            /\ node_states' = [node_states EXCEPT ![n] = next_lsn]
            /\ UNCHANGED <<sequencer_lsn, global_log>>

Next == SequencerStep \/ NodeProcess

\* SAFETY PROPERTY: No node can ever be ahead of the sequencer
Safety_NoTimeTravel == \forall n \in Nodes : node_states[n] <= sequencer_lsn

\* LIVENESS PROPERTY: Every LSN in the log eventually gets processed by all nodes
Liveness_TotalConvergence == 
    \forall n \in Nodes : \forall l \in global_log : 
        WF_node_states(NodeProcess) => <>(node_states[n] >= l)

=============================================================================