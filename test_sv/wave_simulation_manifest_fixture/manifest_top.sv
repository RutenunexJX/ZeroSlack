module manifest_top;
    logic clk;
    logic rst_n;
    logic [5:0] fast_data;
    logic [9:0] wide_data;
    logic [3:0] fast_samples [1:0];
    logic [3:0] wide_samples [1:0];
    manifest_control_if fast_control();
    manifest_control_if wide_control();

    manifest_child #(
        .WIDTH      (6),
        .RESET_MODE (MANIFEST_RUN)
    ) u_fast (
        .clk_i     (clk),
        .rst_ni    (rst_n),
        .data_i    (fast_data),
        .mode_i    (MANIFEST_RUN),
        .payload_i ('0),
        .samples_i (fast_samples),
        .control   (fast_control.dut),
        .data_o    ()
    );

    manifest_child #(
        .WIDTH (10)
    ) u_wide (
        .clk_i     (clk),
        .rst_ni    (rst_n),
        .data_i    (wide_data),
        .mode_i    (MANIFEST_IDLE),
        .payload_i ('0),
        .samples_i (wide_samples),
        .control   (wide_control.dut),
        .data_o    ()
    );
endmodule
