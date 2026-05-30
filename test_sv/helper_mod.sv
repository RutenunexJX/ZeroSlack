// 跨文件跳转测试用的第二个文件
module helper_mod (
    input  logic clk,
    output logic done
);
    logic [3:0] state;
    assign done = 1'b1;
endmodule
