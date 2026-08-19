`include "manifest_defs.svh"

module manifest_child #(
    parameter int             WIDTH      = `MANIFEST_DEFAULT_WIDTH,
    parameter manifest_mode_t RESET_MODE = MANIFEST_IDLE
) (
    input  logic                  clk_i,
    input  logic                  rst_ni,
    input  logic [WIDTH-1:0]      data_i,
    input  manifest_mode_t        mode_i,
    input  manifest_payload_t     payload_i,
    input  logic [3:0]            samples_i [1:0],
    manifest_control_if.dut       control,
    output logic [WIDTH:0]        data_o
);
    logic [WIDTH:0] next_data;

    always_comb begin
        next_data = payload_i.valid && control.request
            ? {1'b0, data_i ^ samples_i[0]}
            : '0;
        control.ready = |control.command;
        data_o = next_data;
    end
endmodule
