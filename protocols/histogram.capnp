@0xa21a6e2a897ef963;

# The result returned to the work scheduler
# Result of requesting a histogram of the image

struct HistogramResult {
	filename  @0 : Text;
	histogram @1 : List(Float64);
}
