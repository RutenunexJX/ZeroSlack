`ifndef ZEROSLACK_MANIFEST_DEFS_SVH
`define ZEROSLACK_MANIFEST_DEFS_SVH

typedef enum logic [1:0] {
    MANIFEST_IDLE = 2'd0,
    MANIFEST_RUN  = 2'd1
} manifest_mode_t;

typedef struct packed {
    logic                              valid;
    logic [`MANIFEST_DEFAULT_WIDTH-1:0] payload;
} manifest_payload_t;

interface manifest_control_if;
    logic       request;
    logic [2:0] command;
    logic       ready;

    modport dut (
        input  request,
        input  command,
        output ready
    );
endinterface

`endif
