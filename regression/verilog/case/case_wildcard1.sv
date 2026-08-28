module main(input clk, input [1:0] sel);

  reg r1, r2;

  // 1800-2017 12.5.1: the case statement compares using 4-state
  // equality, i.e., x and z in a case item are not wildcards.
  // Only casex and casez have wildcards.
  always @(*) begin
    case(sel)
      2'b0z: r1 = 1;
      default: r1 = 0;
    endcase
  end

  // casez, for comparison: here the z is a wildcard.
  always @(*) begin
    casez(sel)
      2'b0z: r2 = 1;
      default: r2 = 0;
    endcase
  end

  // sel is two-valued, and hence never matches 2'b0z.
  p0: assert property (@(posedge clk) r1 == 0);

  // These hold.
  p1: assert property (@(posedge clk) sel[1] == 0 |-> r2 == 1);
  p2: assert property (@(posedge clk) sel[1] == 1 |-> r2 == 0);

endmodule
