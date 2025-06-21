#include "benchmark.h"

uint32_t pp (uint64_t n) {
    if (n == 0) {
        return createText("line");
    } else  {
        return createConcat(group(pp(n - 1)), createConcat(createNewline(), createText("line")));
    }
}

int main(int argc, char *argv[]) {
    Config cfg = parseArgs(argc, argv);
    
    uint32_t parent = pp(cfg.size);
    
    runBenchmark ("flatten", cfg, parent);
}
