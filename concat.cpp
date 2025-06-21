#include "benchmark.h"

uint32_t pp (uint64_t n) {
    if (n == 0) {
        return createText("");
    } else  {
        return createConcat(createText("line"), pp(n - 1));
    }
}

int main(int argc, char *argv[]) {
    Config cfg = parseArgs(argc, argv);
    
    uint32_t parent = pp(cfg.size);
    
    runBenchmark ("concat", cfg, parent);
}
