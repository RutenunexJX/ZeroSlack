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
    output logic [WIDTH:0]        data_o
);
    always_comb begin
        data_o = payload_i.valid ? {1'b0, data_i} : '0;
    end
endmodule
