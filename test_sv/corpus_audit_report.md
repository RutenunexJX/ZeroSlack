# ZeroSlack Functional Corpus Audit

- Generated: 2026-06-29T17:47:45Z UTC
- Roots: test_sv/new, test_sv/huge_prj
- Files: 454
- Modules: 424
- Always/process records: 0
- Signal/port candidates: 49345
- Semantic records: 113127
- Relationships: 82636
- Diagnostics: 704
- Elapsed: 741364 ms

## Feature Summary

| Feature | Pass | Fail | Skipped | Timeout | Empty-but-valid |
| --- | ---: | ---: | ---: | ---: | ---: |
| `state_transition_graph` | 77 | 66 | 384 | 0 | 0 |
| `signal_kernel_graph` | 388 | 0 | 1592 | 0 | 12 |
| `module_block_diagram` | 208 | 0 | 0 | 0 | 216 |
| `wave_preview` | 408 | 0 | 16 | 0 | 0 |
| `semantic_baseline` | 874 | 3 | 0 | 0 | 2 |

## Key Failures

- `state_transition_graph` test_sv/huge_prj/Adm/xadm_out_formation.sv:267 module `xadm_out_formation` symbol `next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Axi/vendor_ip_axi_gm_core.sv:600 module `vendor_ip_axi_gm_core` symbol `ncbe_next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Axi/vendor_ip_axi_gm_core.sv:694 module `vendor_ip_axi_gm_core` symbol `next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Axi/vendor_ip_axi_gm_core.sv:3039 module `vendor_ip_axi_gm_core` symbol `NCBE_NEXT_STATE`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Axi/vendor_ip_axi_gs_sm.sv:157 module `vendor_ip_axi_gs_sm` symbol `next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Axi/axi_bridge.sv:2291 module `axi_bridge` symbol `sif_np_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Axi/axi_bridge.sv:2311 module `axi_bridge` symbol `sif_p_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Axi/axi_bridge.sv:2349 module `axi_bridge` symbol `nprspf_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/inbound/vendor_ip_bridge_ib_rd_dcmp_inq.sv:247 module `vendor_ip_bridge_ib_rd_dcmp_inq` symbol `next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/outbound/vendor_ip_axi_cpl_comp.sv:124 module `vendor_ip_axi_cpl_comp` symbol `cmp_cpl_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/outbound/vendor_ip_axi_ob.sv:149 module `vendor_ip_axi_ob` symbol `sif_np_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/outbound/vendor_ip_axi_ob.sv:191 module `vendor_ip_axi_ob` symbol `nprspf_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/outbound/vendor_ip_axi_ob.sv:215 module `vendor_ip_axi_ob` symbol `sif_p_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/outbound/vendor_ip_axi_ob.sv:366 module `vendor_ip_axi_ob` symbol `cmp_cpl_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/outbound/vendor_ip_axi_ob_np_formation.sv:118 module `vendor_ip_axi_ob_np_formation` symbol `sif_np_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/outbound/vendor_ip_axi_ob_p_formation.sv:80 module `vendor_ip_axi_ob_p_formation` symbol `sif_p_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/outbound/vendor_ip_axi_ob_p_formation.sv:176 module `vendor_ip_axi_ob_p_formation` symbol `prf_tlp_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/outbound/vendor_ip_axi_ob_rsp_formation.sv:53 module `vendor_ip_axi_ob_rsp_formation` symbol `cmp_cpl_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/outbound/vendor_ip_axi_ob_rsp_formation.sv:67 module `vendor_ip_axi_ob_rsp_formation` symbol `nprspf_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/outbound/vendor_ip_bridge_ob_np_dcmp_inq.sv:292 module `vendor_ip_bridge_ob_np_dcmp_inq` symbol `next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/outbound/vendor_ip_bridge_ob_p_dcmp_inq.sv:294 module `vendor_ip_bridge_ob_p_dcmp_inq` symbol `next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/outbound/vendor_ip_bridge_ob_p_dcmp_outq.sv:244 module `vendor_ip_bridge_ob_p_dcmp_outq` symbol `next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv.sv:89 module `vendor_ip_axi_slv` symbol `armisc_info_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv.sv:124 module `vendor_ip_axi_slv` symbol `awmisc_info_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv.sv:171 module `vendor_ip_axi_slv` symbol `sif_np_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv.sv:184 module `vendor_ip_axi_slv` symbol `nprspf_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv.sv:210 module `vendor_ip_axi_slv` symbol `sif_p_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv.sv:306 module `vendor_ip_axi_slv` symbol `s_nprsp_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv_rreq.sv:71 module `vendor_ip_axi_slv_rreq` symbol `armisc_info_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv_rreq.sv:113 module `vendor_ip_axi_slv_rreq` symbol `srrq_np_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv_rreq.sv:206 module `vendor_ip_axi_slv_rreq` symbol `pdly1_np_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv_rsp.sv:67 module `vendor_ip_axi_slv_rsp` symbol `nprspf_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv_rsp.sv:244 module `vendor_ip_axi_slv_rsp` symbol `r_nprspf_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv_wreq.sv:68 module `vendor_ip_axi_slv_wreq` symbol `awmisc_info_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv_wreq.sv:109 module `vendor_ip_axi_slv_wreq` symbol `swrq_p_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv_wreq.sv:255 module `vendor_ip_axi_slv_wreq` symbol `pdly1_awmisc_info_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv_wreq.sv:320 module `vendor_ip_axi_slv_wreq` symbol `lnull_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv_wreq.sv:320 module `vendor_ip_axi_slv_wreq` symbol `s_lnull_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv_wreq.sv:321 module `vendor_ip_axi_slv_wreq` symbol `r_lnull_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Bridge/slave/vendor_ip_axi_slv_wreq_if.sv:59 module `vendor_ip_axi_slv_wreq_if` symbol `awmisc_info_ns`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Cdm/cdm_reg_chk.sv:139 module `cdm_reg_chk` symbol `next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Cdm/error_inj_ctrl.sv:65 module `error_inj_ctrl` symbol `next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Edma/vendor_ip_edma_arb.sv:104 module `vendor_ip_edma_arb` symbol `next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Edma/vendor_ip_edma_cdm_crgb.sv:106 module `vendor_ip_edma_cdm_crgb` symbol `next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Edma/vendor_ip_edma_cdm_crgb.sv:274 module `vendor_ip_edma_cdm_crgb` symbol `NEXT_STATE`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Edma/vendor_ip_edma_rdbuff_ctrl.sv:221 module `vendor_ip_edma_rdbuff_ctrl` symbol `wr_next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Edma/vendor_ip_edma_rdbuff_ctrl.sv:268 module `vendor_ip_edma_rdbuff_ctrl` symbol `rd_next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Edma/vendor_ip_edma_rdbuff_dword_align.sv:122 module `vendor_ip_edma_rdbuff_dword_align` symbol `next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Layer1/rmlh_token_finder4.sv:112 module `rmlh_token_finder4` symbol `int_first_t_next_state`: no FSM graph
- `state_transition_graph` test_sv/huge_prj/Layer1/rmlh_token_finder4.sv:113 module `rmlh_token_finder4` symbol `int_first_t_cnt_next_state`: no FSM graph

## Notes

- Real corpus files were opened read-only; reports are written under `test_sv`.
- `empty-but-valid` means the service returned a coherent empty/root-only result, not a full feature pass.
- `skipped` means the corpus item did not contain the required trigger shape, such as no next-state signal or no always block.

## Known Issues And Residual Risk

- State Transition Graph has 66 real next-state candidates that pass the trigger
  filter but return `no FSM graph`.
- Signal Kernel Graph uses a bounded deep-call budget: 4 signal graph attempts
  per module and 400 total attempts. Skipped candidates are counted explicitly.
- Wave Preview used module-scope fallback because this workspace symbol pass did
  not expose always/process records; `cpld_preproc.sv` still passed with 220
  lanes and 590 assignments.
- Three source files produced empty outline results:
  `test_sv/huge_prj/vendor_ip_ctl-undef.v`,
  `test_sv/huge_prj/Layer1/scramble3_skip_align.sv`, and
  `test_sv/huge_prj/common/tb_lane_flip_mux.sv`.
