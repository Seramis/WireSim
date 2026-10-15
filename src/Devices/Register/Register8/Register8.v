module Register8 (
    input  wire       we,     // write enable: transparent latch while high
    input  wire [7:0] d_in,
    output wire [7:0] d_out
);
    reg [7:0] value = 8'h00;

    always @(*) begin
        if (we) value = d_in;
    end

    assign d_out = value;

endmodule
