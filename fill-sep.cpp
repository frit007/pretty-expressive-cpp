#include "benchmark.h"
#include <iostream>
#include <fstream> 

uint32_t fillSep (const std::vector<string>& xs) {
    if (xs.empty()) return createText("");
    uint32_t acc = createText(xs[0]);
    for (size_t i = 1; i < xs.size(); ++i) {
        auto align = createConcat(acc,(createConcat(createText(" "), createAlign(createText(xs[i])))));
        auto newline = createConcat(acc, createConcat(createNewline(), createText(xs[i])));
        acc = createChoice(align, newline);
    }
    return acc;
}


int main(int argc, char *argv[]) {
    Config cfg = parseArgs(argc, argv);

    const char* envPath = std::getenv("BENCHDATA");
    std::string basePath = envPath != nullptr ? envPath : "../data";
    std::string fullPath = basePath + "/words";
    
    ifstream wordFile(fullPath);
    std::vector<string> xs ={};
    int i = cfg.size;
    
    std::string line;
    while (getline (wordFile, line) && i-- > 0) {
        xs.push_back(line);
    }

    uint32_t parent = fillSep(xs);
    
    runBenchmark ("fill-sep", cfg, parent);
    return 0;
}
