
module test_xor1();

   wire [7:0] y, a, b, count;

   Rxor rx1(y, a, b, count);

   always @(y) begin
      assert (y == (a^b)) else
        $display("Error @%d : a:%h b:%h y:%h", count, a, b, y);
   end

`ifdef FSDB_DUMP
   initial begin
      $fsdbDumpvars(0, "+all");
   end
`endif

endmodule // test_xor
