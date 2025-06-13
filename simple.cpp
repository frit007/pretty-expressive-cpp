#include "doc.h"

// uint32_t ab() {
//     uint32_t doc = createText("end");
//     for (int i = 0; i < 20000; i++) {
//     for (int i = 0; i < 20000; i++) {
//         uint32_t space = createConcat(createText ("a"), createText (" "));
//         uint32_t nl = createConcat(createText ("b"), createNewline());
//         doc = createConcat(createChoice(space, nl), doc);
//     }
//     return doc;
// }

int main() {
    // uint32_t parent = createChoice(createText("Hell"), createText("World"));
    // uint32_t one = createChoice(createText("Hell"), createText("World"));
    // uint32_t two = createChoice(createText("huh!!"), createText("func"));
    // uint32_t parent = createConcat(one, two);
    // cout << "measureset:"<<sizeof(Cost)<< endl;
    // cout << "costSize:"<<sizeof(Cost)<< endl;
    uint32_t parent = createConcat(createText("hello"), createAlign(createConcat(createNewline(), createText("World"))));
    cout << parent << endl;
    Output out = print(parent);
    cout << out.layout << endl;
    return 0;
}