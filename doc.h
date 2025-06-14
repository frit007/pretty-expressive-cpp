#include <sstream>
#include <string>
#include <vector>
#include <iostream>
#include <climits>
#include <algorithm>

#define MEASURE_ARENA_SIZE 500
#define NO_GC UINT32_MAX
using namespace std;
enum class DocType {TEXT, NEWLINE, CONCAT, NEST, ALIGN, CHOICE, FLATTEN};

struct TextDoc
{
    uint32_t stringRef;
    uint32_t stringLength;
};
struct NewlineDoc
{
};
struct ConcatDoc
{
    uint32_t leftDoc;
    uint32_t rightDoc;
};
struct NestDoc
{
    uint32_t nestedDoc;
    uint32_t indent;
};
struct ChoiceDoc
{
    uint32_t leftDoc;
    uint32_t rightDoc;
};
struct AlignDoc
{
    uint32_t alignDoc;
};
struct FlattenDoc
{
    uint32_t flattenDoc;
};

struct Doc {
    DocType type;
    uint32_t nlCount;
    uint32_t cache_id;
    // DocData data;
    union {
        TextDoc text;
        NewlineDoc nl;
        ConcatDoc concat;
        NestDoc nest;
        AlignDoc align;
        ChoiceDoc choice;
        FlattenDoc flatten;
    };
};


struct Cost {
    // unfortunately widthCost can reach high values, therefore we must store them as 64 bit. Although we probably will not reach that many lines
    uint64_t widthCost;
    uint64_t lineCost;
};

enum class MeasureType {CONCAT, TEXT, NEWLINE};
struct Measure; // forward declaration

struct MeasureConcat {
    Measure* parentLeft;
    Measure* parentRight;
};

struct MeasureText {
    uint32_t stringRef;
};
struct MeasureNewline {
    uint32_t indent; // technically this can be infered based on last, however we have free space due to the union
};

// Measure must
//  - Transform a document with choices to a choice less document
//  - remember the cost of the document
struct Measure {
    union {
        MeasureConcat concat;
        MeasureText text;
        MeasureNewline newline;
    };
    MeasureType type;
    uint16_t last;
    uint32_t rc; // we need to keep track of how many a measure is reference before it is returned(TODO: might not be necessary)
    Cost cost;
};


enum class TaintedTrunkType {LEFT, RIGHT, VALUE};
struct TaintedTrunk; // forward declaration
struct TaintedTrunkLeft
{
    TaintedTrunk* leftTrunk;
    uint32_t rightDoc;
};
struct TaintedTrunkRight
{
    Measure leftMeasure;
    TaintedTrunk* rightTrunk;
};
struct TaintedTrunkValue
{
    Measure measure;
};

// tainted must be freed
struct TaintedTrunk {
    uint32_t col;
    uint32_t indent;
    uint32_t rc;
    bool flatten;
    TaintedTrunkType type;
    union {
        TaintedTrunkLeft left;
        TaintedTrunkRight right;
        TaintedTrunkValue value;
    };
};



enum class MeasureSetType {SET, TAINTED};

struct MeasureSetTainted
{
    TaintedTrunk* trunk;
};
struct MeasureSetValue
{
    Measure** sets;
    int count;
};

struct MeasureSet
{
    MeasureSetType type;
    union {
        MeasureSetTainted tainted;
        MeasureSetValue set;
    };
};

// they key ensures that we sort based on indent.
uint64_t cacheKey(uint32_t col, uint32_t indent, bool flatten) {
    return ((uint64_t)col<<1) | (((uint64_t)indent) << 32) | (flatten ? 1 : 0);
}

struct DocCache {
    uint64_t key;
    MeasureSet ms;
    
    static DocCache Create(uint64_t cacheKey, MeasureSet m) {
        DocCache res;
        res.key = cacheKey;
        res.ms = m;
        return res;
    }
};

struct BlockAlloc {
    void* start;
    uint32_t remainingBytes;
};

uint32_t cacheDistance = 7;
uint32_t pageWidth = 80;
uint32_t computationWidth = 100;
// Keep documents grouped together in memory
vector<Doc> docs;
// parallel array with docs, 
vector<int> cacheWeight;
// 
#define SPACE_STRING_REF 0
vector<string> strings = {" "};
vector<vector<DocCache>> cache;
BlockAlloc blockAlloc = {nullptr, 0};
// Since measures might be short or long lived we allocate them in bulk, 
// and if they are no longer need then return them to the pool
// Measures are the part of the program that would have to optimized more,
vector<Measure*> measurePool;
vector<TaintedTrunk*> taintedTrunkPool;
#if CLEAN_MEMORY
// if the program has to continue afterwards we must free all of the memory we have allocated
vector<void*> memoryLeaks;
#endif

// Allocate memory that will stay there until the end of the program(or if you want to clean up memory leaks you can using the memoryLeaks vector)
void* persistentAlloc(uint32_t bytes) {
    if (blockAlloc.remainingBytes < bytes) {
        // if we can't service the request allocate more memory. Note that this implementation leaves memory unused, which likely isn't great.
        uint32_t allocationSize = 131072;
        void* newblock = malloc(allocationSize);
        blockAlloc.remainingBytes = allocationSize;
        blockAlloc.start = newblock;
        #if CLEAN_MEMORY
        memoryLeaks.push_back(newblock);
        #endif
    }
    void* p = blockAlloc.start;
    blockAlloc.start = (((bool*)blockAlloc.start) + (sizeof(bool)*bytes));
    blockAlloc.remainingBytes = blockAlloc.remainingBytes - bytes;
    return p;
    
}

Measure* allocateMeasure() {
    if (measurePool.size() == 0) {
        // if the pool is empty, then fill the pool
        int allocationSize = 10000;
        void* m = malloc(sizeof(Measure) * allocationSize);
        #if CLEAN_MEMORY
        memoryLeaks.push_back(m)
        #endif
        Measure* measures = (Measure*) m;
        for (int i = 0; i < allocationSize; i++) {
            measurePool.push_back(&measures[i]);
        }
    }
    // take the last element in the pool
    Measure* measure = measurePool[measurePool.size() - 1];
    measurePool.pop_back();
    measure->rc = 1;
    return measure;
}

void decMeasureRc(Measure* m) {
    if (m->rc == UINT_MAX) { // these are the cached measures, there is no reason to rc them
        return;
    }
    // m->rc--;
    // if (m->rc == 0) {
    //     if (m->type == MeasureType::CONCAT) {
    //         decMeasureRc(m->concat.parentLeft);
    //         decMeasureRc(m->concat.parentRight);
    //     }
    //     measurePool.push_back(m);
    // }
}
void incMeasureRc(Measure* m) {
    if (m->rc == UINT_MAX) { // these are the cached measures, there is no reason to rc them
        return;
    }
    m->rc++;
}

TaintedTrunk* allocateTaintedTrunk() {
    if (taintedTrunkPool.size() == 0) {
        // if the pool is empty, then fill the pool
        int allocationSize = 1000;
        void* m = malloc(sizeof(TaintedTrunk) * allocationSize);
        #if CLEAN_MEMORY
        memoryLeaks.push_back(m)
        #endif
        TaintedTrunk* trunks = (TaintedTrunk*) m;
        for (int i = 0; i < allocationSize; i++) {
            taintedTrunkPool.push_back(&trunks[i]);
        }
    }
    // take the last element in the pool
    TaintedTrunk* measure = taintedTrunkPool[taintedTrunkPool.size() - 1];
    taintedTrunkPool.pop_back();
    measure->rc = 1;
    return measure;
}

void decTaintedTrunkRc(TaintedTrunk* t) {
    if (t->rc == UINT_MAX) { // these are the cached trunks, there is no reason to rc them
        return;
    }
    t->rc--;
    if (t->rc == 0) {
        if (t->type == TaintedTrunkType::LEFT) {
            decTaintedTrunkRc(t->left.leftTrunk);
        } else if (t->type == TaintedTrunkType::RIGHT) {
            decTaintedTrunkRc(t->right.rightTrunk);
        } else if (t->type == TaintedTrunkType::VALUE) {
            // nothing to do
        }
        taintedTrunkPool.push_back(t);
    }
}
void incTaintedTrunkRc(TaintedTrunk* m) {
    if (m->rc == UINT_MAX) { // these are the cached trunks, there is no reason to rc them
        return;
    }
    m->rc++;
}


// the hard problem is temporary measure sets

void updateCache (uint32_t docId, int maxChildCacheDistance) {
    if (maxChildCacheDistance > cacheDistance) {
        docs[docId].cache_id = cache.size();
        cache.push_back({});
        cacheWeight.push_back(0);
    } else {
        cacheWeight.push_back(maxChildCacheDistance + 1);
        docs[docId].cache_id = 0;
    }
}

uint32_t createText(string s) {
    strings.push_back(s);
    uint32_t stringId = strings.size() - 1;
    Doc doc;
    doc.type = DocType::TEXT;
    doc.nlCount = 0;
    doc.text = {stringId, (uint32_t) s.length()};
    docs.push_back(doc); 

    uint32_t docId = docs.size() - 1;
    updateCache(docId, 0);
    return docId;
}

std::string defaultString = "";
uint32_t createNewline() {
    Doc doc;
    doc.type = DocType::NEWLINE;
    doc.nlCount = 1;
    docs.push_back(doc); 

    uint32_t docId = docs.size() - 1;
    updateCache(docId, 0);
    return docId;
}

uint32_t createConcat(uint32_t left, uint32_t right) {
    Doc doc;
    doc.type = DocType::CONCAT;
    doc.nlCount = docs[left].nlCount + docs[right].nlCount;
    doc.concat.leftDoc = left;
    doc.concat.rightDoc = right;
    docs.push_back(doc); 

    uint32_t docId = docs.size() - 1;
    updateCache(docId, max(cacheWeight[left],cacheWeight[right]));
    return docId;
}

uint32_t createChoice(uint32_t left, uint32_t right) {
    Doc doc;
    doc.type = DocType::CHOICE;
    doc.nlCount = max(docs[left].nlCount, docs[right].nlCount);
    doc.choice.leftDoc = left;
    doc.choice.rightDoc = right;
    docs.push_back(doc); 

    uint32_t docId = docs.size() - 1;
    updateCache(docId, max(cacheWeight[left],cacheWeight[right]));
    return docId;
}

uint32_t creatFlatten(uint32_t inner) {
    Doc doc;
    doc.type = DocType::FLATTEN;
    doc.nlCount = 0;
    doc.flatten.flattenDoc = inner;
    docs.push_back(doc); 

    uint32_t docId = docs.size() - 1;
    updateCache(docId, cacheWeight[inner]);
    return docId;
}

uint32_t createAlign(uint32_t inner) {
    Doc doc;
    doc.type = DocType::ALIGN;
    doc.nlCount = docs[inner].nlCount;
    doc.align.alignDoc = inner;
    docs.push_back(doc); 

    uint32_t docId = docs.size() - 1;
    updateCache(docId, cacheWeight[inner]);
    return docId;
}

uint32_t createNest(uint32_t inner, uint32_t indent) {
    Doc doc;
    doc.type = DocType::NEST;
    doc.nlCount = docs[inner].nlCount;
    doc.nest.nestedDoc = inner;
    doc.nest.indent = indent;
    docs.push_back(doc); 

    uint32_t docId = docs.size() - 1;
    updateCache(docId, cacheWeight[inner]);
    return docId;
}


bool costLEQ (Cost left, Cost right) {
    if (left.widthCost == right.widthCost) {
        return left.lineCost <= right.lineCost;
    } else {
        return left.widthCost < right.widthCost;
    }
}


bool measureLEQ (Measure* left, Measure* right) {
    return costLEQ(left->cost, right->cost) && left->last <= right->last;
}

Cost costAdd (Cost l, Cost r) {
    return {
        l.widthCost + r.widthCost, 
        l.lineCost + r.lineCost
    };
}

Measure* measureConcat(Measure* left, Measure* right) {
    Measure* newMeasure = allocateMeasure();
    newMeasure->type = MeasureType::CONCAT;
    newMeasure->concat.parentLeft = left;
    newMeasure->concat.parentRight = right;
    newMeasure->cost = costAdd(left->cost, right->cost);
    newMeasure->last = right->last;
    incMeasureRc(left);
    incMeasureRc(right);
    return newMeasure;
}

bool canReturnPerfect(Measure** arr, int size) {
    for (int i=0; i < size; i++){
        if (arr[i]->cost.widthCost == 0) {
            return true;
        }
    }
    return false;
}

int mergeList(Measure** leftArr, int lsize, Measure** rightArr, int rsize, Measure** result, int resultSize) {
    int leftIndex = 0;
    int rightIndex = 0;
    int resultIndex = 0;
    while (leftIndex < lsize && rightIndex < rsize && resultIndex < resultSize) {
        Measure* left = leftArr[leftIndex];
        Measure* right = rightArr[rightIndex];
        if (measureLEQ(left, right)) {
            decMeasureRc(right);
            rightIndex++;
        } else if(measureLEQ(right, left)) {
            decMeasureRc(left);
            leftIndex++;
        } else if(left->last > right->last) {
            result[resultIndex] = left;
            resultIndex++;
            leftIndex++;
        } else {
            result[resultIndex] = right;
            resultIndex++;
            rightIndex++;
        }
    }
    
    
    while (leftIndex < lsize && resultIndex < resultSize) {
        Measure* left = leftArr[leftIndex];
        result[resultIndex] = left;
        resultIndex++;
        leftIndex++;
    }

    while (rightIndex < rsize && resultIndex < resultSize) {
        Measure* right = rightArr[rightIndex];
        result[resultIndex] = right;
        resultIndex++;
        rightIndex++;
    }
    return resultIndex;
}


struct FoundOrIndex {
    bool found;
    union {
        int missingIndex;
        DocCache* foundCache;
    };
    
    static FoundOrIndex Found(DocCache* ptr) {
        FoundOrIndex res;
        res.found = true;
        res.foundCache = ptr;
        return res;
    }

    static FoundOrIndex Miss(int index) {
        FoundOrIndex res;
        res.found = false;
        res.missingIndex = index;
        return res;
    }
};

// binary search to find the correct index
FoundOrIndex findCacheIndex(std::vector<DocCache>& arr, uint64_t key) {
    size_t low = 0;
    size_t high = arr.size();
    while (low < high) {
        size_t mid = (low + high) / 2;
        DocCache& entry = arr[mid];

        if (key == entry.key) {
            return FoundOrIndex::Found(&entry);
        } else if (key < entry.key) {
            high = mid;
        } else {
            low = mid + 1;
        }
    }

    return FoundOrIndex::Miss(static_cast<int>(low));
}

void printDoc (uint32_t docId, uint32_t indent) {
    char ch = ' ';
    int n = indent;

    string repeated(n, ch);  
    Doc* doc = &docs[docId];
    switch (doc->type)
    {
        case DocType::TEXT : {
            cout << repeated << "Text: \"" << strings[doc->text.stringRef]<<"\""<<endl;
            return;
        }
        case DocType::NEWLINE : {
            cout << repeated << "Newline:" <<endl;
            return;
        }
        case DocType::ALIGN :
            cout << repeated << "Align:" <<endl;
            return printDoc (doc->align.alignDoc, indent + 2); 
        case DocType::CONCAT :{
            
            cout << repeated << "Concat l:" <<endl;
            printDoc(doc->concat.leftDoc, indent+2);
            cout << repeated << "Concat r:" <<endl;
            printDoc(doc->concat.rightDoc, indent+2);
            return;
            // Measure* childArena [MEASURE_ARENA_SIZE];
            // MeasureSet left = resolve (doc->concat.leftDoc, col, indent, flatten, childArena);
            // use parent arena, because processConcat is used to return the value and therefore the value should survive.
            // return processConcat(left, doc->concat.rightDoc, col, indent, flatten, arena); 
        }
            
        case DocType::CHOICE : {
            cout << repeated << "choice l:" <<endl;
            printDoc(doc->choice.leftDoc, indent + 2);
            cout << repeated << "choice r:" <<endl;
            printDoc(doc->choice.rightDoc, indent + 2);
            return;
        }
        case DocType::FLATTEN :{
            cout << repeated << "flatten:" <<endl;
            return printDoc (doc->flatten.flattenDoc, indent + 2); 
        }
        case DocType::NEST : {
            cout << repeated << "nest:" <<endl;
            return printDoc (doc->nest.nestedDoc, indent + 2); 
        }
    }
}


MeasureSet mergeSet(MeasureSet left, MeasureSet right, Measure** result, int resultSize) {
    if (right.type == MeasureSetType::TAINTED) {
        decTaintedTrunkRc(right.tainted.trunk);
        return left;
    } else if (left.type == MeasureSetType::TAINTED) {
        decTaintedTrunkRc(left.tainted.trunk);
        return right;
    } else {
        int size = mergeList(left.set.sets, left.set.count, right.set.sets, right.set.count, result, resultSize);
        MeasureSet ms;
        ms.type = MeasureSetType::SET;
        ms.set.sets = result;
        ms.set.count = size;
        return ms;
    }
}

MeasureSet resolve (uint32_t docId, uint32_t col, uint32_t indent, bool flatten, Measure** outputArena);
MeasureSet resolveCached (uint32_t docId, uint32_t col, uint32_t indent, bool flatten, Measure** outputArena);

MeasureSet processConcat (MeasureSet left, uint32_t rightDocId, uint32_t col, uint32_t indent, bool flatten, Measure** outputArena) {
    //
    Measure* one [MEASURE_ARENA_SIZE];
    Measure* two [MEASURE_ARENA_SIZE];
    Measure* childArena[MEASURE_ARENA_SIZE];

    // swap pointers because we can't keep writing to the same arena.
    Measure** nextArena = one; 
    Measure** currentArena = two; 
    Measure** tmp; // used for swapping
    
    bool hasResult = false;
    MeasureSet result;

    if (left.type == MeasureSetType::TAINTED) {
        TaintedTrunk* trunk = allocateTaintedTrunk();
        trunk->type = TaintedTrunkType::LEFT;
        trunk->left.leftTrunk = left.tainted.trunk;
        trunk->left.rightDoc = rightDocId;

        MeasureSet ms;
        ms.type = MeasureSetType::TAINTED;
        ms.tainted.trunk = trunk;
        return ms;
    } else {
        for (int leftIndex = 0; leftIndex < left.set.count; leftIndex++) {
            Measure* leftMeasure = left.set.sets[leftIndex];
            MeasureSet rightSet = resolveCached(rightDocId, leftMeasure->last, indent, flatten, childArena);
            if (rightSet.type == MeasureSetType::TAINTED) {
                TaintedTrunk* trunk = allocateTaintedTrunk();
                trunk->type = TaintedTrunkType::RIGHT;
                trunk->right.rightTrunk = rightSet.tainted.trunk;

                trunk->right.leftMeasure = *leftMeasure;
                trunk->right.leftMeasure.rc = NO_GC;
        
                MeasureSet ms;
                ms.type = MeasureSetType::TAINTED;
                ms.tainted.trunk = trunk;
                if (!hasResult) {
                    hasResult = true;
                    result = ms;
                } else {
                    result = mergeSet(result, ms, nextArena, MEASURE_ARENA_SIZE);
                    tmp = nextArena; // swap pointers
                    nextArena = currentArena; // swap pointers
                    currentArena = tmp;
                }
            } else {
                // dedup algorithm
                int dedupSize = 0;
                bool sawAFreeOption = false;
                Measure* best = measureConcat(leftMeasure, rightSet.set.sets[0]);
                sawAFreeOption = best->cost.widthCost == 0 || sawAFreeOption;
                outputArena[0] = best;
                for (int rightIndex = 1; rightIndex < rightSet.set.count; rightIndex++) {
                    Measure* rightMeasure = rightSet.set.sets[rightIndex];
                    auto cost = costAdd(leftMeasure->cost, rightMeasure->cost);
                    if (costLEQ(cost, best->cost)) {
                        decMeasureRc(best);
                        best = measureConcat(leftMeasure, rightMeasure);
                    } else {
                        outputArena[dedupSize] = measureConcat(leftMeasure, rightMeasure);
                        dedupSize++;
                    }
                }

                outputArena[dedupSize] = best;
                dedupSize++;
                for (int i = 0; i < dedupSize / 2; ++i) {
                    std::swap(outputArena[i], outputArena[dedupSize - 1 - i]);
                }

                if (sawAFreeOption && !canReturnPerfect(outputArena, dedupSize)){
                    cout  << "dedup committed the murder"  <<endl;
                }


                if (!hasResult) {
                    hasResult = true;
                    result.type = MeasureSetType::SET;
                    result.set.sets = nextArena;
                    result.set.count = dedupSize;
                    for (int i = 0; i < dedupSize; i ++) {
                        result.set.sets[i] = outputArena[i];
                    }
                    tmp = nextArena; // swap pointers
                    nextArena = currentArena; // swap pointers
                    currentArena = tmp;
                } else {
                    MeasureSet dedupedSet;
                    dedupedSet.type = MeasureSetType::SET;
                    dedupedSet.set.sets = outputArena;
                    dedupedSet.set.count = dedupSize;
                    result = mergeSet(result, dedupedSet, nextArena, MEASURE_ARENA_SIZE);

                    tmp = nextArena; // swap pointers
                    nextArena = currentArena; // swap pointers
                    currentArena = tmp;
                }
                for (int i=0;i<rightSet.set.count; i ++ ) {
                    decMeasureRc(rightSet.set.sets[i]);
                }
            }
        }
        
        for (int i=0;i<left.set.count; i ++ ) {
            decMeasureRc(left.set.sets[i]);
        }
    }
    if (result.type == MeasureSetType::TAINTED) {
        return result;
    } else {
        MeasureSet ms;
        ms.type = MeasureSetType::SET;
        ms.set.sets = outputArena;
        ms.set.count = result.set.count;
        for (int i = 0; i < result.set.count; i++) {
            outputArena[i] = result.set.sets[i];
        }
        return ms;
    }
}

Cost costText (uint32_t col, uint32_t length) {
    uint32_t stop = col + length;
    if (stop > pageWidth) {
        uint32_t maxwc = max(pageWidth, col);
        uint32_t a = maxwc - pageWidth;
        uint32_t b = stop - maxwc;
        return { b * (2 * a + b), 0 };
    } else {
        return { 0, 0 };
    }
}

Cost costNl () {
    return { 0, 1 };
}

MeasureSet resolveCached (uint32_t docId, uint32_t col, uint32_t indent, bool flatten, Measure** arena) {
    Doc* doc = &docs[docId];
    if (doc->cache_id != 0) {
        auto key = cacheKey(col, indent, flatten);
        auto find = findCacheIndex(cache[doc->cache_id], key);
        if (find.found) {
            return find.foundCache->ms;
        }

        MeasureSet ms = resolve(docId, col, indent, flatten, arena);
        
        if (ms.type == MeasureSetType::SET) {
            // we must move the pointers out of the arena, because the arena will disappear later
            // also make sure the measures are not being garbage collected
            // note that we do not move the actual measures here, since they are already spatialy close due to being created at the same time.
            Measure** persitentStorage = (Measure**) persistentAlloc(sizeof(Measure*) * ms.set.count);
            for (int i = 0; i < ms.set.count; i++) {
                Measure* m = ms.set.sets[i];
                m->rc = NO_GC; // TODO: maybe mark parents as NO_GC (although this might cause unnecessary cache misses)
                persitentStorage[i] = m;
            }
            ms.set.sets = persitentStorage;
        }
        DocCache c = DocCache::Create(key, ms);
        // insert into the array and ensure that it stays sorted
        cache[doc->cache_id].insert(cache[doc->cache_id].begin() + find.missingIndex, c);
        return c.ms;
    } else {
        return resolve(docId, col, indent, flatten, arena);
    }
}

MeasureSet measureSetForText(uint32_t stringRef, uint32_t strLen, uint32_t col, Measure** arena) {
    if (col + strLen <= computationWidth) {
        MeasureSet ms;
        ms.type = MeasureSetType::SET;
        ms.set.count = 1;
        ms.set.sets = arena;
        Measure* measure = allocateMeasure();
        measure->type = MeasureType::TEXT;
        measure->text.stringRef = stringRef;
        measure->cost = costText(col, strLen);
        measure->last = strLen + col;
        ms.set.sets[0] = measure;
        return ms;
    } else {
        TaintedTrunk* trunk = allocateTaintedTrunk();
        trunk->type = TaintedTrunkType::VALUE;
        trunk->value.measure.type = MeasureType::TEXT;
        trunk->value.measure.text.stringRef = stringRef;
        trunk->value.measure.rc = NO_GC;
        trunk->value.measure.cost = costText(col, strLen);
        trunk->value.measure.last = strLen + col;
        MeasureSet ms;
        ms.type = MeasureSetType::TAINTED;
        ms.tainted.trunk = trunk;
        return ms;
    }
}

/**
 * The arena is used to allow children to allow returning a list of pointers without allocation arrays themselves.
 *
 */
MeasureSet resolve (uint32_t docId, uint32_t col, uint32_t indent, bool flatten, Measure** arena) {
    // allocate a local arena on the stack children can write to
    // printDoc(docId, 0);
    // cout << endl;
    Doc* doc = &docs[docId];
    switch (doc->type)
    {
    case DocType::TEXT : {
        return measureSetForText(doc->text.stringRef, doc->text.stringLength, col, arena);
    }
    case DocType::NEWLINE : {
        if (flatten) {
            return measureSetForText(SPACE_STRING_REF, 1, col, arena);
        } else {
            MeasureSet ms;
            ms.type = MeasureSetType::SET;
            ms.set.count = 1;
            ms.set.sets = arena;
            Measure* measure = allocateMeasure();
            measure->type = MeasureType::NEWLINE;
            measure->newline.indent = indent;
            measure->cost = costNl();
            measure->last = indent;
            ms.set.sets[0] = measure;
            return ms;
        }
    }
    case DocType::ALIGN :
        return resolveCached (doc->align.alignDoc, col, col, flatten, arena); // pass through the arena
    case DocType::CONCAT :{
        Measure* childArena [MEASURE_ARENA_SIZE];
        MeasureSet left = resolveCached (doc->concat.leftDoc, col, indent, flatten, childArena);
        // bool safe= left.type == MeasureSetType::SET && canReturnPerfect(left.set.sets, left.set.count); 
        // use parent arena, because processConcat is used to return the value and therefore the value should survive.
        MeasureSet ms =  processConcat(left, doc->concat.rightDoc, col, indent, flatten, arena); 
        // bool safeAfter= ms.type == MeasureSetType::SET && canReturnPerfect(ms.set.sets, ms.set.count); 
        // if (safe && !safeAfter && ms.type == MeasureSetType::SET) {
        //     cout << "here " << endl;
        // }
        return ms;
    }
        
    case DocType::CHOICE : {
        Measure* childArenaLeft [MEASURE_ARENA_SIZE];
        Measure* childArenaRight [MEASURE_ARENA_SIZE];

        Doc* leftDoc = &docs[doc->concat.leftDoc];
        Doc* rightDoc = &docs[doc->concat.rightDoc];
        
        if (leftDoc->nlCount < rightDoc->nlCount) {
            MeasureSet left = resolveCached (doc->choice.leftDoc, col, indent, flatten, childArenaLeft);
            MeasureSet right = resolveCached (doc->choice.rightDoc, col, indent, flatten, childArenaRight);
            
            MeasureSet ms = mergeSet(left, right, arena, MEASURE_ARENA_SIZE);
            return ms;
        } else {
            MeasureSet right = resolveCached (doc->choice.rightDoc, col, indent, flatten, childArenaRight);
            MeasureSet left = resolveCached (doc->choice.leftDoc, col, indent, flatten, childArenaLeft);
            return mergeSet(right, left, arena, MEASURE_ARENA_SIZE);
        }
    }
    case DocType::FLATTEN :{
        return resolveCached (doc->flatten.flattenDoc, col, indent, true, arena); // pass through the arena
    }
    case DocType::NEST : {
        return resolveCached (doc->nest.nestedDoc, col, indent + doc->nest.indent, flatten, arena); // pass through the arena
    }
    }
    throw "unhandled syntax";
}

Measure* expandTainted (TaintedTrunk* trunk) {
    if (trunk->type == TaintedTrunkType::VALUE) {
        return &trunk->value.measure; // works if we never release the tainted trunk
    } else if(trunk->type == TaintedTrunkType::RIGHT) {
        Measure* rightMeasure = expandTainted(trunk->right.rightTrunk);
        return measureConcat(&trunk->right.leftMeasure, rightMeasure);
    }else {
        Measure* leftMeasure = expandTainted(trunk->left.leftTrunk);
        Measure* arena [MEASURE_ARENA_SIZE];
        MeasureSet ms = resolve(trunk->left.rightDoc, leftMeasure->last, trunk->indent, trunk->flatten, arena);
        if (ms.type == MeasureSetType::TAINTED) {
            return expandTainted(ms.tainted.trunk);
        } else {
            Measure* m = ms.set.sets[0]; // return the first result (we can't release this memory since it is used to render)
            m->rc = NO_GC;
            return m;
        }
    }
}


void renderChoiceLess (Measure* choiceLess, stringbuf& buf) {
    switch (choiceLess->type)
    {
    case MeasureType::CONCAT:{
        renderChoiceLess(choiceLess->concat.parentLeft, buf);
        renderChoiceLess(choiceLess->concat.parentRight, buf);
        return;
    }

    case MeasureType::TEXT:{
        auto str = &strings[choiceLess->text.stringRef];
        buf.sputn(str->c_str(), str->length());
        return;
    }

    case MeasureType::NEWLINE:{
        char ch = ' ';
        string repeated(choiceLess->newline.indent, ch);  
        buf.sputn("\n", 1);
        buf.sputn(repeated.c_str(), repeated.size());
        return;
    }
    }
    throw "Render missing case";
}

struct Output {
    string layout;
    Cost cost;
    bool isTainted;
};

Output print(uint32_t docId) {
    Measure* arena [MEASURE_ARENA_SIZE];
    MeasureSet ms = resolveCached(docId, 0, 0, false, arena);
    Measure* measure;
    bool isTainted = ms.type == MeasureSetType::TAINTED;
    if (isTainted) {
        measure = expandTainted(ms.tainted.trunk);
    } else {
        measure = ms.set.sets[0];
        // for (int i=0; i < ms.set.count; i ++) {
        //     cout << "option: width: " << ms.set.sets[i]->cost.widthCost << "  line: " << ms.set.sets[i]->cost.lineCost << endl;
        // }
    }
    stringbuf buf;
    renderChoiceLess(measure, buf);
    return {buf.str(), measure->cost, isTainted};
}
