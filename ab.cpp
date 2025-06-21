#include "doc.h"

uint32_t ab() {
    uint32_t doc = createText("end");
    for (int i = 0; i < 20000; i++) {
    for (int i = 0; i < 20000; i++) {
        uint32_t space = createConcat(createText ("a"), createText (" "));
        uint32_t nl = createConcat(createText ("b"), createNewline());
        doc = createConcat(createChoice(space, nl), doc);
    }
    return doc;
}

int main() {
    uint32_t parent = ab();
    cout << parent << endl;
    cout << parent << endl;
    Output out = print(parent);
    cout << out.layout << endl;
    return 0;
}