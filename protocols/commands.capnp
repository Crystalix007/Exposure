@0x92d22a8dee462238;

struct HistogramResult {
	filename  @0 : Text;
	histogram @1 : List(Float32);
}

struct ProtocolCommand {
	command @0 : Text;
	data       : union {
		helo     @1 : Void;
		ehlo     @2 : Void;
		heatbeat @3 : Text;
		job      @4 : Text;
		result   @5 : HistogramResult;
	}
}
