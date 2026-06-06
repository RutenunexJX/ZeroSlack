module rel_top(
  input  logic       top_clk,
  input  logic       top_rst_n,
  input  logic       req_valid,
  input  logic [7:0] req_data,
  output logic [7:0] rsp_data
);
  logic [7:0] stage_data;

  task automatic capture_sample;
  endtask

  rel_stage u_stage(
    .stage_clk(top_clk),
    .stage_rst_n(top_rst_n),
    .enable_i(req_valid),
    .data_i(req_data),
    .data_o(stage_data)
  );

  always_ff @(posedge top_clk or negedge top_rst_n) begin
    if (!top_rst_n) begin
      rsp_data <= '0;
    end else if (req_valid) begin
      capture_sample();
      rsp_data <= stage_data;
    end
  end
endmodule
