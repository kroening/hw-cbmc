module main;

  typedef bit [7:0] eight_bits;

  p0: assert final (eight_bits'('hffff) == 'hff);

endmodule
