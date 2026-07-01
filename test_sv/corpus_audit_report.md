# ZeroSlack Functional Corpus Audit

- Generated: 2026-07-01T07:25:07Z UTC
- Roots: test_sv/new, test_sv/huge_prj
- Files: 454
- Modules: 424
- Always/process records: 3942
- Signal/port candidates: 49345
- Semantic records: 117069
- Relationships: 82636
- Diagnostics: 704
- Elapsed: 1198391 ms

## Feature Summary

| Feature | Pass | Fail | Skipped | Timeout | Empty-but-valid |
| --- | ---: | ---: | ---: | ---: | ---: |
| `state_transition_graph` | 90 | 0 | 400 | 0 | 0 |
| `signal_kernel_graph` | 388 | 0 | 1592 | 0 | 12 |
| `module_block_diagram` | 208 | 0 | 0 | 0 | 216 |
| `wave_preview` | 3963 | 29 | 16 | 0 | 0 |
| `semantic_baseline` | 874 | 0 | 0 | 0 | 5 |

## Key Failures

- `wave_preview` test_sv/huge_prj/Adm/radm_filter_ep.sv:2161 module `radm_filter_ep` symbol `always@2161`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Bridge/outbound/vendor_ip_axi_cpl_comp_ctl.sv:1018 module `vendor_ip_axi_cpl_comp_ctl` symbol `always@1018`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Cdm/cdm_pl_reg.sv:1599 module `cdm_pl_reg` symbol `always@1599`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Cdm/cdm_rasdes_sd_reg.sv:510 module `cdm_rasdes_sd_reg` symbol `always@510`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Edma/vendor_ip_edma_cpld2mwr.sv:638 module `vendor_ip_edma_cpld2mwr` symbol `always@638`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Edma/vendor_ip_edma_cpld2mwr.sv:672 module `vendor_ip_edma_cpld2mwr` symbol `always@672`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Edma/vendor_ip_edma_rdbuff_ctrl.sv:787 module `vendor_ip_edma_rdbuff_ctrl` symbol `always@787`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Layer1/rmlh_token_finder3.sv:278 module `rmlh_token_finder3` symbol `always@278`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Layer1/rmlh_token_finder4.sv:269 module `rmlh_token_finder4` symbol `always@269`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Layer1/smlh_link.sv:526 module `smlh_link` symbol `always@526`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Layer1/smlh_link.sv:759 module `smlh_link` symbol `always@759`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Layer1/smlh_link.sv:768 module `smlh_link` symbol `always@768`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Layer1/smlh_ltssm.sv:3437 module `smlh_ltssm` symbol `always@3437`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Layer1/xmlh_byte_xmt.sv:3291 module `xmlh_byte_xmt` symbol `always@3291`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Layer3/xtlh_ctrl.sv:564 module `xtlh_ctrl` symbol `always@564`: preview returned no lane and no warning
- `wave_preview` test_sv/huge_prj/Layer3/xtlh_merge_128b.sv:841 module `xtlh_merge_128b` symbol `always@841`: preview returned no lane and no warning
- `wave_preview` test_sv/new/elec_phy_import/elec/low_speed_DAC_ctrl.v:200 module `low_speed_DAC_ctrl` symbol `always@200`: preview returned no lane and no warning
- `wave_preview` test_sv/new/elec_phy_import/lite/lite_debug_area.sv:277 module `lite_debug_area` symbol `always_ff@277`: preview returned no lane and no warning
- `wave_preview` test_sv/new/elec_phy_import/lite/lite_debug_area.sv:286 module `lite_debug_area` symbol `always_ff@286`: preview returned no lane and no warning
- `wave_preview` test_sv/new/elec_phy_import/lite/lite_fault_inj_area.sv:287 module `lite_fault_inj_area` symbol `always_ff@287`: preview returned no lane and no warning
- `wave_preview` test_sv/new/elec_phy_import/lite/lite_fault_inj_area.sv:296 module `lite_fault_inj_area` symbol `always_ff@296`: preview returned no lane and no warning
- `wave_preview` test_sv/new/elec_phy_import/lite/lite_generic_area.sv:291 module `lite_generic_area` symbol `always_ff@291`: preview returned no lane and no warning
- `wave_preview` test_sv/new/elec_phy_import/lite/lite_generic_area.sv:300 module `lite_generic_area` symbol `always_ff@300`: preview returned no lane and no warning
- `wave_preview` test_sv/new/elec_phy_import/phy/fpga2cpld_tx.v:200 module `fpga2cpld_tx` symbol `always@200`: preview returned no lane and no warning
- `wave_preview` test_sv/new/elec_phy_import/phy/tca9535_i2c_ctrl.v:445 module `tca9535_i2c_ctrl` symbol `always@445`: preview returned no lane and no warning
- `wave_preview` test_sv/new/elec_phy_import/phy/tca9535_i2c_ctrl.v:489 module `tca9535_i2c_ctrl` symbol `always@489`: preview returned no lane and no warning
- `wave_preview` test_sv/new/elec_phy_import/prot/prot_inj.sv:177 module `prot_inj` symbol `always_ff@177`: preview returned no lane and no warning
- `wave_preview` test_sv/new/elec_phy_import/prot/prot_inj.sv:186 module `prot_inj` symbol `always_ff@186`: preview returned no lane and no warning
- `wave_preview` test_sv/new/elec_phy_import/prot/prot_inj.sv:195 module `prot_inj` symbol `always_ff@195`: preview returned no lane and no warning

## Notes

- Real corpus files were opened read-only; reports are written under `test_sv`.
- `empty-but-valid` means the service returned a coherent empty/root-only result, not a full feature pass.
- `skipped` means the corpus item did not contain the required structural trigger shape, such as no clocked FSM pair or no always block.

## Known Issues And Residual Risk

- State Transition Graph is structure-discovered: clocked current<=next pairs drive next-state positive cases and current-state negative cases. Skipped modules have no structural FSM pair under the current extractor.
- Signal Kernel Graph uses a bounded deep-call budget: 4 signal graph attempts per module and 400 total attempts. Skipped candidates are counted explicitly.
- Wave Preview uses source-discovered always/process records when workspace symbol extraction does not expose process nodes; module fallback remains explicit.
- Empty outline source files are classified as preprocessor/comment-only, guarded, skipped, or real outline failures instead of being hidden.
