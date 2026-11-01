# Source provenance

Extracted on 2026-10-29 from the accepted ZeroSlack phase B working-tree candidate,
version 0.33.0, repository HEAD `5e218f6dce0d9ad3a156d9368275361458566ad5`.
The HEAD alone is not the source revision: accepted A/B changes were uncommitted.

- Candidate SHA-256: `51cf7a662ed92cbd2def305cab88cafc4f661c5fc744c80ea9c99d3ad3a0d581`.
- Frozen source manifest SHA-256: `4e0bb31514b3b407bcb5b420c29720b991de67a9bd07187cdf2af6fa2e20d74f`.
- Input: `build/stage-b-20261029/src` and phase B implementation manifests.

The sources are the neutral portions of `src/insights/insightgraphcore.cpp`,
`fsmgraphlayout.cpp`, `signaltracemodels.cpp`, `signaltracepanel.cpp` and
`graphexportservice.cpp`, plus the existing CodeLink value definition.
FSM layout is copied with only include/input-type/namespace substitutions.
Qt object capture, drawing delegates, semantic services and lifecycle management
remain in the host. Old algorithm bodies are replaced by host adapters, so only
one implementation is linked by production.

No third-party algorithm, library, license or external protocol was introduced.
This component remains first-party ZeroSlack code under the repository terms.
