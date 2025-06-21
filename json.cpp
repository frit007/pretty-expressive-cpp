#include <fstream>
#include "benchmark.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;


uint32_t combine(const std::function<uint32_t(const uint32_t&, const uint32_t&)>& f, const std::vector<uint32_t>& xs) {
    if (xs.empty()) return createText("");
    uint32_t result = xs[0];
    for (size_t i = 1; i < xs.size(); ++i) {
        result = f(result, xs[i]);
    }
    return result;
}
uint32_t hcat(const std::vector<uint32_t>& xs, std::string sep) {
    return combine([sep](const uint32_t& l, const uint32_t& r) {
        return createConcat(createFlatten(l), createAlign(createConcat(createText(sep), createAlign(r))));
    }, xs);
}

uint32_t vcat(const std::vector<uint32_t>& xs, std::string sep) {
    return combine([sep](const uint32_t& l, const uint32_t& r) {
        return createConcat(createConcat(createConcat(l, createNewline()), createText(sep)), r);
    }, xs);
}

// uint32_t sep(const std::vector<uint32_t>& xs) {
//     return createChoice(hsep(xs), vsep(xs));
//     // return vsep(xs);
// }
// def enclose_sep (left right sep : Doc) (ds : Array Doc) : FormatM Doc :=
//   match ds with
//   | #[] => return left <+> right
//   | #[d] => return left <+> d <+> right
//   | ds =>
//     let vcat := (combine (.<>Doc.hardNl<>sep<>.) ds)
//     -- so we do not leave any space after the comma?
//     let hcat := (combine (fun l r => (flattenDoc l)<+>sep<+>r) ds)
//     -- return left <+> (vcat <^> hcat) <+> right
//     return alignDoc (left <> (vcat <^> hcat) <> right)
uint32_t encloseSep (const std::string& left, const std::string& right, const std::string& sep, const std::vector<uint32_t>& ds) {
    if (ds.empty()) return createConcat(createText(left), createText(right));
    if (ds.size() == 1) return createConcat(createText(left), createConcat(ds[0], createText(right)));
    
    auto choice = createChoice(vcat(ds, sep), hcat(ds, sep));
    return createAlign(createConcat(createText(left), createConcat(choice, createText(right))));
}

uint32_t pp(const json json) {
    if (json.is_null()) {
        return createText("null");
    } else if (json.is_boolean()) {
        return createText(json.get<bool>() ? "true" : "false");
    } else if (json.is_number_integer() || json.is_number_unsigned() || json.is_number_float()) {
        return createText(std::to_string(json.get<double>()) + ".0");
    } else if (json.is_string()) {
        return createText("\"" + json.get<std::string>() + "\"");
    } else if (json.is_array()) {
        std::vector<uint32_t> elements;
        for (const auto& item : json) {
            elements.push_back(pp(item));
        }
        return encloseSep("[", "]", ",",elements);
    } else if (json.is_object()) {
        std::vector<uint32_t> elements;
        for (auto it = json.begin(); it != json.end(); ++it) {
            elements.push_back(createConcat(createText("\""+it.key() + "\""+": "), pp(it.value())));
        }
        // return createConcat(createText("{"), createConcat(hsep(elements), createText("}")));
       return encloseSep("{", "}", ",", elements);
    } else {
        throw std::runtime_error("Unsupported JSON type");
    }
}


int main(int argc, char *argv[])
{
    Config cfg = parseArgs(argc, argv);

    const char* envPath = std::getenv("BENCHDATA");
    
    std::string basePath = envPath != nullptr ? envPath : "../data";
    std::string fullPath = basePath + (cfg.size == 1 ? "/1k.json" : "/10k.json");


    std::ifstream f(fullPath);
    json data = json::parse(f);

    // auto v = convert(data);

    
    uint32_t parent = pp(data);
    runBenchmark ("sexpr-random", cfg, parent);

}
