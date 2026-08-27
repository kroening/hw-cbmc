typedef struct {
  logic field1;
  logic field2;
} structure_t;

module main(input wire clk, input wire rst);

  structure_t s;

  always @(posedge clk) begin
    s.field2 <= 0;
  end

  // The member select in the named property yields
  // "unknown identifier field2".
  property p;
    @(posedge clk) rst |=> ##1 s.field2 == 0;
  endproperty

  assert property (p);

endmodule
