// Ports can be arrays.
module sub(input [31:0] array [9:0]);

  always assert p0: array[0] == 0;
  always assert p9: array[9] == 9;

endmodule

module main;
  reg [31:0] array [9:0];

  initial begin
    integer i;
    for(i=0; i<10; i=i+1)
      array[i] = i;
  end

  sub s(array);

endmodule
