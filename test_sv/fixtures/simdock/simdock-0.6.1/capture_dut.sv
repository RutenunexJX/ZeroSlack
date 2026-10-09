package capture_types;
  typedef enum logic [1:0] { IDLE=0, ACTIVE=1, DONE=2 } mode_t;
endpackage
module capture_dut import capture_types::*; #(parameter WIDTH=8)
(input logic clk, rst_n, enable, valid, out_ready,
 input logic [WIDTH-1:0] data, input logic signed [7:0] signed_data,
 input mode_t mode, input logic [3:0] unknown_bus,
 output logic ready, out_valid, output logic [7:0] out_data, output logic tx);
  assign ready=1'b1;
  assign tx=1'b1;
  always_ff @(posedge clk) begin
    if (!rst_n) begin out_valid<=0; out_data<=0; end
    else begin out_valid<=valid; out_data<=data; end
  end
endmodule
