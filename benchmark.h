#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include "doc.h"
#include <unistd.h>


struct Config {
    size_t size = 4;
    size_t pageWidth = 80;
    size_t computationWidth = 100;
    std::string program = "";
    std::string out = "";
    bool viewCost = false;
};


// Split string by delimiter
std::vector<std::string> split(const std::string& str, char delimiter) {
    std::stringstream ss(str);
    std::string item;
    std::vector<std::string> elems;
    while (std::getline(ss, item, delimiter)) {
        elems.push_back(item);
    }
    return elems;
}

// Simple MD5 hash using system command
std::string md5Hash(const std::string& input) {
    const std::string filename = "tmpFile.txt";
    std::ofstream outFile(filename);
    outFile << input;
    outFile.close();

    std::string result;
    FILE* pipe = popen(("md5sum " + filename).c_str(), "r");
    if (!pipe) return "error";
    char buffer[128];
    if (fgets(buffer, 128, pipe)) {
        result = buffer;
    }
    pclose(pipe);
    std::remove(filename.c_str());

    auto parts = split(result, ' ');
    return parts.empty() ? "error" : parts[0];
}

// Argument parser
Config parseArgs(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto nextArg = [&]() -> std::string {
            if (i + 1 < argc) return argv[++i];
            return "";
        };

        if (arg == "--size") cfg.size = std::stoul(nextArg());
        else if (arg == "--page-width") cfg.pageWidth = std::stoul(nextArg());
        else if (arg == "--computation-width") cfg.computationWidth = std::stoul(nextArg());
        else if (arg == "--program") cfg.program = nextArg();
        else if (arg == "--out") cfg.out = nextArg();
        else if (arg == "--view-cost") cfg.viewCost = true;
        else {

        }
    }
    return cfg;
}

void runBenchmark(const std::string& program, const Config& cfg, uint32_t doc) {
    computationWidth = cfg.computationWidth;
    pageWidth = cfg.pageWidth;
    auto start = std::chrono::steady_clock::now();
    Output result = print(doc);
    auto stop = std::chrono::steady_clock::now();
    std::chrono::duration<double> duration = stop - start;

    if (cfg.out.size() > 0) {
        if (cfg.out == "-") {
            std::cout << result.layout;
        } else {
            std::ofstream file(cfg.out);
            file << result.layout;
        }
    }

    if (cfg.viewCost) {
        std::cout << "(width: " << result.cost.widthCost <<  " line: " << result.cost.lineCost <<")\n";
    }

    size_t lineCount = 1;
    for (char c : result.layout) {
        if (c == '\n') ++lineCount;
    }

    std::string md5 = md5Hash(result.layout);

    std::cout << "((target pretty-expressive-cpp)"
              << " (program " << program << ")"
              << " (duration " << duration.count() << ")"
              << " (lines " << lineCount << ")"
              << " (size " << cfg.size << ")"
              << " (md5 " << md5 << ")"
              << " (page-width " << cfg.pageWidth << ")"
              << " (computation-width " << cfg.computationWidth << ")"
              << " (tainted? " << (result.isTainted ? "true" : "false") << "))";
}
