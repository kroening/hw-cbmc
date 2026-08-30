module main(input clk, input [1:0] sel);

  reg rx, rz;

  // casex: both x and z bits in the case item are wildcards.
  always @(*) begin
    casex(sel)
      2'b0x: rx = 1;
      default: rx = 0;
    endcase
  end

  // casez: only z (and ?) bits are wildcards; x is not.
  always @(*) begin
    casez(sel)
      2'b0x: rz = 1;
      default: rz = 0;
    endcase
  end

  // 1800-2017 12.5.1: for casex, the x in 2'b0x is a wildcard, so
  // the item matches whenever sel[1] == 0.
  p0: assert property (@(posedge clk) sel[1] == 0 |-> rx == 1);
  p1: assert property (@(posedge clk) sel[1] == 1 |-> rx == 0);

  // For casez, the x in 2'b0x is not a wildcard, and sel is
  // two-valued, so the item is never matched.
  p2: assert property (@(posedge clk) rz == 0);

endmodule
