@0x92d22a8dee462238;

struct HistogramResult {
	filename  @0 : Text;
	histogram @1 : List(Float32);
}

struct EqualisationResult {
	filename   @0 : Text;
	tiffResult @1 : List(Data);
}

struct HistogramJob {
	filename @0 : Text;
}

struct EqualisationJob {
	filename         @0 : Text;
	histogramMapping @1 : List(Float32);
}

struct ProtocolJob {
	type @0 : Text;
	data    : union {
		histogram    @1 : HistogramJob;
		equalisation @2 : EqualisationJob;
	}
}

struct ProtocolResult {
	type @0 : Text;
	data    : union {
		histogram    @1 : HistogramResult;
		equalisation @2 : EqualisationResult;
	}
}

struct ProtocolCommand {
	command @0 : Text;
	data       : union {
		helo     @1 : Void;
		ehlo     @2 : Void;
		heatbeat @3 : Text;
		job      @4 : ProtocolJob;
		result   @5 : ProtocolResult;
		bye      @6 : Void;
	}
}
