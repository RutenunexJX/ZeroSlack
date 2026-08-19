module manifest_unresolved_top (
    input logic       clk_i,
    input logic [7:0] data_i
);
    missing_vendor_core #(
        .WIDTH (8),
        .MODE  (1)
    ) u_core (
        .clk_i  (clk_i),
        .data_i (data_i),
        .data_o ()
    );

    missing_vendor_core #(
        .WIDTH (16)
    ) u_second (
        .clk_i  (clk_i),
        .enable (1'b1),
        .data_o ()
    );
endmodule

module manifest_unrelated;
    unrelated_missing u_unrelated ();
endmodule
