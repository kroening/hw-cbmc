module sub #(parameter type T = int)();

  var T my_var;

endmodule

module main;

  sub #(byte) submodule();

  p1: assert property ($bits(submodule.my_var) == 8);

endmodule // main
