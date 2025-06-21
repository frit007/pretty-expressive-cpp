#include <fstream>
#include "benchmark.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

class SExpr {
public:
    bool isAtom;
    string atom;
    vector<SExpr*> list;
};

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Failed to open file: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string simpleFormattingContext(const std::string& str, int indent) {
    std::istringstream iss(str);
    std::ostringstream oss;
    std::string line;
    std::string padding(indent, ' ');
    while (std::getline(iss, line)) {
        oss << padding << line << '\n';
    }
    return oss.str();
}

SExpr* convert(const json& v) {
    if (v.is_string()) {
        auto atom = new SExpr;
        atom->isAtom = true;
        atom->atom = v.get<std::string>();
        return atom;
    } else if (v.is_array()) {
        auto list = new SExpr;
        list->isAtom = false;
        for (const auto& item : v) {
            list->list.push_back(convert(item));
        }
        return list;
    } else {
        throw std::runtime_error("bad input");
    }
}

uint32_t combine(const std::function<uint32_t(const uint32_t&, const uint32_t&)>& f, const std::vector<uint32_t>& xs) {
    if (xs.empty()) return createText("");
    uint32_t result = xs[0];
    for (size_t i = 1; i < xs.size(); ++i) {
        result = f(result, xs[i]);
    }
    return result;
}
uint32_t hsep(const std::vector<uint32_t>& xs) {
    return combine([](const uint32_t& l, const uint32_t& r) {
        return createConcat(l, createAlign(createConcat(createText(" "), createAlign(r))));
    }, xs);
}

uint32_t vsep(const std::vector<uint32_t>& xs) {
    return combine([](const uint32_t& l, const uint32_t& r) {
        return createConcat(createConcat(l, createNewline()), r);
    }, xs);
}

uint32_t sep(const std::vector<uint32_t>& xs) {
    return createChoice(hsep(xs), vsep(xs));
    // return vsep(xs);
}

uint32_t pp(SExpr* expr) {
    if (expr->isAtom) {
        return createText(expr->atom);
    } else {
        vector<uint32_t> ppl ={};
        for (auto x : expr->list) {
            ppl.push_back(pp(x));
        }
        return createConcat(
            createText("("), 
            createAlign(createConcat( sep(ppl), createAlign(createText(")"))))
        );
    }
}

int main(int argc, char *argv[])
{
    Config cfg = parseArgs(argc, argv);

    const char* envPath = std::getenv("BENCHDATA");
    
    std::string basePath = envPath != nullptr ? envPath : "../data";
    std::string fullPath = basePath + "/random-tree-" + std::to_string(cfg.size) + ".sexp";


    std::ifstream f(fullPath);
    json data = json::parse(f);
    auto v = convert(data);

    
    uint32_t parent = pp(v);
    runBenchmark ("sexpr-random", cfg, parent);

}
