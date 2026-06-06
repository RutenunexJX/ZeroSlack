import rel_pkg::*;

module rel_stage(
  input  logic       stage_clk,
  input  logic       stage_rst_n,
  input  logic       enable_i,
  input  logic [7:0] data_i,
  output logic [7:0] data_o
);
  state_t state;

  always_ff @(posedge stage_clk or negedge stage_rst_n) begin
    if (!stage_rst_n) begin
      state <= REL_IDLE;
      data_o <= '0;
    end else if (enable_i) begin
      state <= REL_BUSY;
      data_o <= data_i;
    end
  end
endmodule
