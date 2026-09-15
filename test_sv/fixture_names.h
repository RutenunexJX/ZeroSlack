#ifndef ZEROSLACK_TEST_FIXTURE_NAMES_H
#define ZEROSLACK_TEST_FIXTURE_NAMES_H

// Module, signal and file names inside the real-workspace fixture
// test_sv/huge_prj. That workspace is third-party RTL and is not part of this
// repository, so the names below are neutral placeholders: they keep the tests
// readable and compilable without publishing the vendor's identifiers.
//
// To run these tests against the real workspace, place the actual names in
// test_sv/local_fixture_names.h, which is excluded from version control:
//
//     #define ZS_FIXTURE_TOP_CTL "<real module name>"
//     ...
//
// Any macro that file defines wins; the rest fall back to the placeholders.

#if defined(__has_include)
#  if __has_include("local_fixture_names.h")
#    include "local_fixture_names.h"
#  endif
#endif

#ifndef ZS_FIXTURE_TOP_CTL
#  define ZS_FIXTURE_TOP_CTL "vendor_ip_ctl"
#endif

#ifndef ZS_FIXTURE_TOP_CTL_FILE
#  define ZS_FIXTURE_TOP_CTL_FILE "vendor_ip_ctl.sv"
#endif

#ifndef ZS_FIXTURE_GPHY
#  define ZS_FIXTURE_GPHY "vendor_ip_gphy"
#endif

#ifndef ZS_FIXTURE_AXI_GM
#  define ZS_FIXTURE_AXI_GM "vendor_ip_axi_gm"
#endif

#ifndef ZS_FIXTURE_AXI_GM_FILE
#  define ZS_FIXTURE_AXI_GM_FILE "Axi/vendor_ip_axi_gm.sv"
#endif

#ifndef ZS_FIXTURE_BRIDGE_IB_FILE
#  define ZS_FIXTURE_BRIDGE_IB_FILE "Bridge/inbound/vendor_ip_bridge_ib.sv"
#endif

#ifndef ZS_FIXTURE_CTX_FSM
#  define ZS_FIXTURE_CTX_FSM "vendor_ip_edma_ctx_fsm"
#endif

#ifndef ZS_FIXTURE_CTX_FSM_FILE
#  define ZS_FIXTURE_CTX_FSM_FILE "Edma/vendor_ip_edma_ctx_fsm.sv"
#endif

#ifndef ZS_FIXTURE_CTX_SIGNAL
#  define ZS_FIXTURE_CTX_SIGNAL "vendor_ip_edma_ctx"
#endif

#ifndef ZS_FIXTURE_CTX_FSM_SNAPSHOT
#  define ZS_FIXTURE_CTX_FSM_SNAPSHOT "fsm_vendor_ip_edma_ctx.png"
#endif

#endif // ZEROSLACK_TEST_FIXTURE_NAMES_H
