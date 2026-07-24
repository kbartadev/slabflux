# SlabFlux Domain: Causal Backbone (`causal_backbone.hpp`)

## 1. Architectural Overview
In complex `deterministic_ai_core` topologies, inference networks are rarely flat. They contain interwoven layers, expert routing gates, and feedback loops. The `causal_backbone` defines the strict topological hierarchy of the neural mesh, ensuring that weight updates and propagation occur in a mathematically precise order.

## 2. Static Network Topology
The backbone maps the inference steps at compile-time:
- The layers are registered into a `slabflux::typelist`.
- The `pipeline` dispatcher utilizes this typelist to sequence the `moe_spark` (Mixture-of-Experts) activations.
- It mathematically enforces that Layer $N$ finishes its FMA operations and executes an `sfence` before Layer $N+1$ begins reading from the intermediate tensor slabs.

## 3. Sequence Propagation
Because neural matrices can suffer from gradient vanishing or historical drift, the `causal_backbone` forces the `LSN` (Logical Sequence Number) to propagate through every node in the graph. 
When the final output is generated, the payload carries the exact temporal signature of the incoming market tick, allowing the Aphasic Horizon to instantly decouple inference anomalies directly tied to the triggering network packet via Indexical Exhaustion.