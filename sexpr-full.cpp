#include "doc.h"
#include <tuple>
#include <functional>
#include <chrono>
#include <ctime>


class SExpr {
public:
    bool isAtom;
    string atom;
    vector<SExpr*> list;
};



tuple<SExpr*, uint32_t> testExpr (uint32_t n,uint32_t c) {
    if (n == 0) {
        auto s = new SExpr();
        s->isAtom = true;
        s->atom = to_string(c);
        return make_tuple(s, c + 1);
    } else {
        // auto s = new SExpr();
        auto [t1, c1] = testExpr(n-1, c);
        auto [t2, c2] = testExpr(n-1, c1);
        
        
        auto s = new SExpr();
        s->isAtom = false;
        s->list.push_back(t1);
        s->list.push_back(t2);
        // auto second = testExpr(n, first.);
        return make_tuple(s, c2);
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

int main() {
    cout << "start" << endl;
    auto [t,c] = testExpr(16,0);

    cout << "created the expr" << endl;
    uint32_t parent = pp(t);
    cout << parent << endl;
    
    auto start = std::chrono::system_clock::now();
    Output out = print(parent);
    auto end = std::chrono::system_clock::now();

    cout << out.layout << endl;
    int count = 0;
    for (int i = 0; i < out.layout.length(); i++) {
        if (out.layout[i] == '\n') {
            count++;
        }
    }
    cout << "lines:"<<count << endl;
    cout << "tainted:"<< out.isTainted << endl;
    cout << "took:"<< std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms" << endl;
    cout << "width:"<< out.cost.widthCost << " lines:"<<out.cost.lineCost<< endl;
    // printDoc(parent, 0);
    return 0;
}

