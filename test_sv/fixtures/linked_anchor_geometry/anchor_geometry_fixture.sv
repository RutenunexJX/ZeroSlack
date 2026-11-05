module anchor_geometry_fixture (
    input  logic clk,
    input  logic rst_n,
    output logic done
);
logic [31:0] count, mirror;

always_ff @(posedge clk) begin
    if (!rst_n) begin
        count <= 32'd0;
        mirror <= 32'd0;
    end else begin
        count <= count + 32'd1;
        mirror <= count;
    end
end

assign done = (count == 32'd16);
// independent geometry fixture row 019
// independent geometry fixture row 020
// independent geometry fixture row 021
// independent geometry fixture row 022
// independent geometry fixture row 023
// independent geometry fixture row 024
// independent geometry fixture row 025
// independent geometry fixture row 026
// independent geometry fixture row 027
// independent geometry fixture row 028
// independent geometry fixture row 029
// independent geometry fixture row 030
// independent geometry fixture row 031
// independent geometry fixture row 032
// independent geometry fixture row 033
// independent geometry fixture row 034
// independent geometry fixture row 035
// independent geometry fixture row 036
// independent geometry fixture row 037
// independent geometry fixture row 038
// independent geometry fixture row 039
// independent geometry fixture row 040
// independent geometry fixture row 041
// independent geometry fixture row 042
// independent geometry fixture row 043
// independent geometry fixture row 044
// independent geometry fixture row 045
// independent geometry fixture row 046
// independent geometry fixture row 047
// independent geometry fixture row 048
// independent geometry fixture row 049
// independent geometry fixture row 050
// independent geometry fixture row 051
// independent geometry fixture row 052
// independent geometry fixture row 053
// independent geometry fixture row 054
// independent geometry fixture row 055
// independent geometry fixture row 056
// independent geometry fixture row 057
// independent geometry fixture row 058
// independent geometry fixture row 059
// independent geometry fixture row 060
// independent geometry fixture row 061
// independent geometry fixture row 062
// independent geometry fixture row 063
// independent geometry fixture row 064
// independent geometry fixture row 065
// independent geometry fixture row 066
// independent geometry fixture row 067
// independent geometry fixture row 068
// independent geometry fixture row 069
// independent geometry fixture row 070
// independent geometry fixture row 071
// independent geometry fixture row 072
// independent geometry fixture row 073
// independent geometry fixture row 074
// independent geometry fixture row 075
// independent geometry fixture row 076
// independent geometry fixture row 077
// independent geometry fixture row 078
// independent geometry fixture row 079
// independent geometry fixture row 080
// independent geometry fixture row 081
// independent geometry fixture row 082
// independent geometry fixture row 083
// independent geometry fixture row 084
// independent geometry fixture row 085
// independent geometry fixture row 086
// independent geometry fixture row 087
// independent geometry fixture row 088
// independent geometry fixture row 089
// independent geometry fixture row 090
// independent geometry fixture row 091
// independent geometry fixture row 092
// independent geometry fixture row 093
// independent geometry fixture row 094
// independent geometry fixture row 095
// independent geometry fixture row 096
// independent geometry fixture row 097
// independent geometry fixture row 098
// independent geometry fixture row 099
// independent geometry fixture row 100
endmodule
