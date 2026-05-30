//==========================================================================
// test_symbols.sv  —  ZeroSlack 符号提取 / 补全 / 跳转 测试用例
//--------------------------------------------------------------------------
// 自包含单文件，Slang 单文件 elaboration 可直接解析（无外部依赖）。
//
// 测试命令前缀（行首输入 "<prefix> " 后看补全）：
//   r / w / l        reg / wire / logic 变量
//   m / i / p        module / interface / parameter
//   t / f            task / function
//   e / ee / ne      枚举变量 / 枚举值 / 枚举类型
//   s / sp           unpacked / packed struct 变量
//   ns / nsp         unpacked / packed struct 类型
//
// 跳转测试（Ctrl+点击）：
//   - 类型名 fsm_state_t / pixel_t / record_t -> 跳到 typedef
//   - 枚举值 STATE_IDLE / ON                  -> 跳到 enum 体内定义
//   - 成员 pixel.red / rec.state              -> 跳到 struct 成员定义
//   - 实例 u_adder 的 .a/.b/.sum              -> 跳到 adder 端口
//   - import types_pkg::* 的 types_pkg        -> 跳到 package
//==========================================================================

package types_pkg;

    // typedef enum (命名) -> sym_typedef(dataType=enum) + sym_enum_value(moduleScope=fsm_state_t)
    typedef enum logic [1:0] {
        STATE_IDLE,
        STATE_RUN,
        STATE_DONE
    } fsm_state_t;

    // typedef packed struct -> sym_packed_struct + sym_struct_member(moduleScope=pixel_t)
    typedef struct packed {
        logic [7:0] red;
        logic [7:0] green;
        logic [7:0] blue;
    } pixel_t;

    // typedef unpacked struct -> sym_unpacked_struct + sym_struct_member(moduleScope=record_t)
    typedef struct {
        int         id;
        fsm_state_t state;
    } record_t;

    localparam int PKG_WIDTH = 8;

endpackage : types_pkg


//--------------------------------------------------------------------------
// 被实例化的子模块（INSTANTIATES 关系 + 端口跳转）
//--------------------------------------------------------------------------
module adder #(
    parameter int WIDTH = 8
) (
    input  logic [WIDTH-1:0] a,
    input  logic [WIDTH-1:0] b,
    output logic [WIDTH-1:0] sum
);
    assign sum = a + b;
endmodule


//--------------------------------------------------------------------------
// 顶层模块
//--------------------------------------------------------------------------
module top #(
    parameter int DATA_WIDTH = 16
) (
    input  logic                  clk,
    input  logic                  rst_n,
    input  logic [DATA_WIDTH-1:0] data_in,
    output logic [DATA_WIDTH-1:0] data_out,
    inout  wire                   io_pin
);

    import types_pkg::*;

    // --- reg / wire / logic 变量（r / w / l 测试）---
    reg  [7:0]             counter;
    wire [7:0]             net_sig;
    logic                  enable;
    logic [DATA_WIDTH-1:0] result;

    // --- localparam（p 测试）---
    localparam int LOCAL_MAX = 255;

    // --- enum 变量（e / ee / ne 测试，来自 import 的 fsm_state_t）---
    fsm_state_t current_state;
    fsm_state_t next_state;

    // --- struct 变量（s / sp 测试）---
    pixel_t  pixel;   // packed struct 变量   -> sp
    record_t rec;     // unpacked struct 变量 -> s

    // --- 模块内部 typedef enum（内部作用域 ne 测试）---
    typedef enum { ON, OFF } power_e;
    power_e power;

    // --- 内联匿名 enum / struct（无 typedef）---
    // 期望：MODE_A/MODE_B 的 moduleScope = mode；hi/lo 的 moduleScope = byte_split
    enum { MODE_A, MODE_B }                         mode;
    struct packed { logic [3:0] hi; logic [3:0] lo; } byte_split;

    // --- 实例化（INSTANTIATES + 引脚跳转）---
    adder #(.WIDTH(8)) u_adder (
        .a   (counter),
        .b   (net_sig),
        .sum (result[7:0])
    );

    // --- function（f 测试）---
    function automatic logic [7:0] add_one(input logic [7:0] x);
        return x + 8'd1;
    endfunction

    // --- task（t 测试）---
    task automatic do_reset();
        counter <= 8'd0;
        enable  <= 1'b0;
    endtask

    // --- 时序逻辑 + struct 成员访问（var.member 补全/跳转）---
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            current_state <= STATE_IDLE;   // 枚举值跳转
            pixel.red     <= 8'd0;         // 成员跳转 -> pixel_t.red
            rec.state     <= STATE_IDLE;
            power         <= OFF;
        end
        else begin
            current_state <= next_state;
            counter       <= add_one(counter);
            power         <= ON;
        end
    end

    // --- 组合逻辑：枚举值用于 case（ee 补全）---
    always_comb begin
        next_state = current_state;
        case (current_state)
            STATE_IDLE: next_state = STATE_RUN;
            STATE_RUN:  next_state = STATE_DONE;
            STATE_DONE: next_state = STATE_IDLE;
            default:    next_state = STATE_IDLE;
        endcase
    end

    assign data_out = result;

endmodule : top
