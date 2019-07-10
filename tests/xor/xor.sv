
module Rxor(output wire [7:0] y, output bit [7:0]  a, output bit [7:0] b, output bit [7:0] count);

   initial forever begin
      #10;
      count = count + 1;
      a = $urandom();
      b = $urandom();
   end
   
//   assign y = (count=='d55) ? ~(a^b) : a^b;
   assign y = a^b;

endmodule // xor
