# Parallelism

This program can be configured to run with a threaded implementation of the
library transformation functions, or it can be run with its own job-based
threading.

Using job-based threading with libraries that also implement internal threading
will likely incur a significant overhead. For efficiency purposes, it is likely
better to just rely on the library parallelism.

Ideally, you should use a non-threaded implementation of ImageMagick (and LibRaw
if loading RAW image files). This allows job-based parallelism without a lot of
contention.

# Results

Library Parallel, Job Parallel: ./Exposure --client  881.98s user 130.25s system 1041% cpu 1:37.16 total
Library Parallel, Job Linear: ./Exposure --client  867.84s user 76.18s system 464% cpu 3:23.32 total
Library Linear, Job Linear: ./Exposure --client  675.23s user 35.99s system 148% cpu 7:57.84 total
Library Linear, Job Parallel: ./Exposure --client  830.40s user 55.20s system 908% cpu 1:37.43 total

Clearly, it is worth having at least some level of parallelism, but also, it is
clear that job-level parallelism is a lot more efficient (considering the CPU
utilisation). Moreover, it also ends up being about twice as fast (if network is
not a bottleneck).
