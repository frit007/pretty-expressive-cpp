#include <sstream>
#include <string>
#include <vector>
#include <iostream>
#include <climits>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#define MEASURE_ARENA_SIZE 250
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
    vector<Measure*>* sets;
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
vector<unordered_map<uint64_t, DocCache>> cache;
// Since measures might be short or long lived we allocate them in bulk, 
// and if they are no longer need then return them to the pool
// Measures are the part of the program that would have to optimized more,
vector<Measure*> measurePool;
vector<vector<Measure*>*> measureContainerPool;
vector<TaintedTrunk*> taintedTrunkPool;
#if CLEAN_MEMORY
// if the program has to continue afterwards we must free all of the memory we have allocated
vector<vector<Measure*>*> persistentMeasureContainers; //TODO: free after program is done
vector<void*> memoryLeaks;
#endif


#define MeasureContainer vector<Measure*>*

vector<Measure*>* borrowMeasureContainer() {
    if (measureContainerPool.size() == 0) {
        measureContainerPool.push_back(new vector<Measure*>);
    }
    auto take = measureContainerPool[measureContainerPool.size() - 1];
    measureContainerPool.pop_back();
    return take;
}

void releaseMeasureContainer(vector<Measure*>* container) {
    container->clear();
    measureContainerPool.push_back(container);
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
    return measure;
}

TaintedTrunk* allocateTaintedTrunk(TaintedTrunkType type, uint32_t col, uint32_t indent, bool flatten) {
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
    TaintedTrunk* trunk = taintedTrunkPool[taintedTrunkPool.size() - 1];
    trunk->col = col;
    trunk->indent = indent;
    trunk->flatten = flatten;
    trunk->type = type;
    taintedTrunkPool.pop_back();
    return trunk;
}

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

uint32_t createFlatten(uint32_t inner) {
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
uint32_t group(uint32_t inner) {
    return createChoice(inner, createFlatten(inner));
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
    return newMeasure;
}


int mergeList(MeasureContainer leftArr, MeasureContainer rightArr, MeasureContainer result) {
    int leftIndex = 0;
    int rightIndex = 0;
    while (leftIndex < leftArr->size() && rightIndex < rightArr->size()) {
        Measure* left = (*leftArr)[leftIndex];
        Measure* right = (*rightArr)[rightIndex];
        if (measureLEQ(left, right)) {
            rightIndex++;
        } else if(measureLEQ(right, left)) {
            leftIndex++;
        } else if(left->last > right->last) {
            result->push_back(left);
            leftIndex++;
        } else {
            result->push_back(right);
            rightIndex++;
        }
    }
    
    
    while (leftIndex < leftArr->size()) {
        Measure* left = (*leftArr)[leftIndex];
        result->push_back(left);
        leftIndex++;
    }

    while (rightIndex < rightArr->size()) {
        Measure* right = (*rightArr)[rightIndex];
        result->push_back(right);
        rightIndex++;
    }

    return result->size();
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

void _docToString (uint32_t docId, uint32_t indent, stringbuf & sb) {
    char ch = ' ';
    int n = indent;

    string repeated(n, ch);  
    Doc* doc = &docs[docId];
    switch (doc->type)
    {
        case DocType::TEXT : {
            sb.sputn(repeated.c_str(), repeated.length());
            sb.sputn("Text: \"", 7);
            sb.sputn(strings[doc->text.stringRef].c_str(), doc->text.stringLength);
            return;
        }
        case DocType::NEWLINE : {
            sb.sputn(repeated.c_str(), repeated.length());
            sb.sputn("Newline:", 8);
            return;
        }
        case DocType::ALIGN :
            sb.sputn(repeated.c_str(), repeated.length());
            return _docToString (doc->align.alignDoc, indent + 2, sb);
        case DocType::CONCAT :{
            sb.sputn(repeated.c_str(), repeated.length());
            sb.sputn("Concat l:", 9);
            _docToString(doc->concat.leftDoc, indent+2, sb);
            sb.sputn(repeated.c_str(), repeated.length());
            sb.sputn("Concat r:", 9);
            _docToString(doc->concat.rightDoc, indent+2, sb);
            return;
        }
            
        case DocType::CHOICE : {
            sb.sputn(repeated.c_str(), repeated.length());
            sb.sputn("choice l:", 9);
            _docToString(doc->choice.leftDoc, indent + 2, sb);
            sb.sputn(repeated.c_str(), repeated.length());
            sb.sputn("choice r:", 9);
            _docToString(doc->choice.rightDoc, indent + 2, sb);
            return;
        }
        case DocType::FLATTEN :{
            sb.sputn(repeated.c_str(), repeated.length());
            sb.sputn("flatten:", 8);
            return _docToString (doc->flatten.flattenDoc, indent + 2, sb);
        }
        case DocType::NEST : {
            sb.sputn(repeated.c_str(), repeated.length());
            sb.sputn("nest:", 5);
            return _docToString (doc->nest.nestedDoc, indent + 2, sb);
        }
    }
}
string docToString(uint32_t docId) {
    stringbuf buf;
    _docToString(docId, 0, buf);
    return buf.str();
}
void printDoc (uint32_t docId, uint32_t indent) {
    stringbuf buf;
    _docToString(docId, indent, buf);
    cout << buf.str() << endl;
}


MeasureSet mergeSet(MeasureSet leftSet, MeasureSet rightSet, MeasureContainer result) {
    if (rightSet.type == MeasureSetType::TAINTED) {
        if (leftSet.type == MeasureSetType::TAINTED) {
            return leftSet;
        }
        MeasureSet ms;
        ms.type = MeasureSetType::SET;
        ms.set.sets = result;
        // we must copy to result, because the container we are currently using is not going to survive
        for (int i = 0; i < leftSet.set.sets->size(); i++) {
            result->push_back((*leftSet.set.sets)[i]);
        }
        return ms;
    } else if (leftSet.type == MeasureSetType::TAINTED) {
        MeasureSet ms;
        ms.type = MeasureSetType::SET;
        ms.set.sets = result;
        // we must copy to result, because the container we are currently using is not going to survive
        for (int i = 0; i < rightSet.set.sets->size(); i++) {
            result->push_back((*rightSet.set.sets)[i]);
        }
        return ms;
    } else {
        int size = mergeList(leftSet.set.sets, rightSet.set.sets, result);
        MeasureSet ms;
        ms.type = MeasureSetType::SET;
        ms.set.sets = result;
        return ms;
    }
}

MeasureSet resolve (uint32_t docId, uint32_t col, uint32_t indent, bool flatten, MeasureContainer outputArena);
MeasureSet resolveCached (uint32_t docId, uint32_t col, uint32_t indent, bool flatten, MeasureContainer outputArena);

MeasureSet processConcat (MeasureSet leftSet, uint32_t rightDocId, uint32_t col, uint32_t indent, bool flatten, MeasureContainer outputArena) {
    if (leftSet.type == MeasureSetType::TAINTED) {
        TaintedTrunk* trunk = allocateTaintedTrunk(TaintedTrunkType::LEFT, col, indent, flatten);
        trunk->left.leftTrunk = leftSet.tainted.trunk;
        trunk->left.rightDoc = rightDocId;

        MeasureSet ms;
        ms.type = MeasureSetType::TAINTED;
        ms.tainted.trunk = trunk;
        return ms;
    }
    MeasureContainer one = borrowMeasureContainer();
    MeasureContainer two = borrowMeasureContainer();
    MeasureContainer childArena = borrowMeasureContainer();

    // swap pointers because we can't keep writing to the same arena.
    MeasureContainer nextArena = one; 
    MeasureContainer currentArena = two; 
    MeasureContainer tmp; // used for swapping
    
    bool hasResult = false;
    MeasureSet result;

    for (int leftIndex = 0; leftIndex < leftSet.set.sets->size(); leftIndex++) {
        Measure* leftMeasure = (*leftSet.set.sets)[leftIndex];
        childArena->clear();
        MeasureSet rightSet = resolveCached(rightDocId, leftMeasure->last, indent, flatten, childArena);
        if (rightSet.type == MeasureSetType::TAINTED) {
            TaintedTrunk* trunk = allocateTaintedTrunk(TaintedTrunkType::RIGHT, col, indent, flatten);
            trunk->right.rightTrunk = rightSet.tainted.trunk;

            trunk->right.leftMeasure = *leftMeasure;
    
            MeasureSet ms;
            ms.type = MeasureSetType::TAINTED;
            ms.tainted.trunk = trunk;
            if (!hasResult) {
                hasResult = true;
                result = ms;
            } else {
                result = mergeSet(result, ms, nextArena);
                tmp = nextArena; // swap pointers
                nextArena = currentArena; // swap pointers
                currentArena = tmp;
                nextArena->clear();
            }
        } else {
            // dedup algorithm
            // 
            outputArena->clear();
            bool sawAFreeOption = false;
            Measure* best = measureConcat(leftMeasure, (*rightSet.set.sets)[0]);
            sawAFreeOption = best->cost.widthCost == 0 || sawAFreeOption;

            for (int rightIndex = 1; rightIndex < rightSet.set.sets->size(); rightIndex++) {
                Measure* rightMeasure = (*rightSet.set.sets)[rightIndex];
                auto cost = costAdd(leftMeasure->cost, rightMeasure->cost);
                if (costLEQ(cost, best->cost)) {
                    best = measureConcat(leftMeasure, rightMeasure);
                } else {
                    outputArena->push_back(measureConcat(leftMeasure, rightMeasure));
                }
            }

            outputArena->push_back(best);
            // dedupSize++;
            int dedupSize = outputArena->size();
            for (int i = 0; i < outputArena->size() / 2; ++i) {
                std::swap((*outputArena)[i], (*outputArena)[outputArena->size() - 1 - i]);
            }

            if (!hasResult) {
                hasResult = true;
                result.type = MeasureSetType::SET;
                result.set.sets = nextArena;
                for (int i = 0; i < outputArena->size(); i ++) {
                    result.set.sets->push_back((*outputArena)[i]);
                }
                tmp = nextArena; // swap pointers
                nextArena = currentArena; // swap pointers
                currentArena = tmp;
                nextArena->clear();
            } else {
                MeasureSet dedupedSet;
                dedupedSet.type = MeasureSetType::SET;
                dedupedSet.set.sets = outputArena;
                result = mergeSet(result, dedupedSet, nextArena);

                tmp = nextArena; // swap pointers
                nextArena = currentArena; // swap pointers
                currentArena = tmp;
                nextArena->clear();
            }
        }
    }
    if (result.type == MeasureSetType::TAINTED) {
        
        releaseMeasureContainer(one);
        releaseMeasureContainer(two);
        releaseMeasureContainer(childArena);
        return result;
    } else {
        MeasureSet ms;
        ms.type = MeasureSetType::SET;
        ms.set.sets = outputArena;
        outputArena->clear();
        for (int i = 0; i < result.set.sets->size(); i++) {
            auto a = (*result.set.sets)[i];
            outputArena->push_back(a);
        }
        releaseMeasureContainer(one);
        releaseMeasureContainer(two);
        releaseMeasureContainer(childArena);
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

MeasureSet resolveCached (uint32_t docId, uint32_t col, uint32_t indent, bool flatten, MeasureContainer arena) {
    Doc* doc = &docs[docId];
    if (doc->cache_id != 0) {
        auto key = cacheKey(col, indent, flatten);
        auto c = &cache[doc->cache_id];
        auto it = c->find(key);
        if (it != c->end()) {
            return (*it).second.ms;
        }

        // auto find = findCacheIndex(cache[doc->cache_id], key);
        // if (find.found) {
        //     return find.foundCache->ms;
        // }

        MeasureSet ms = resolve(docId, col, indent, flatten, arena);
        
        if (ms.type == MeasureSetType::SET) {
            // we must move the pointers out of the arena, because the arena will disappear later
            // also make sure the measures are not being garbage collected
            // note that we do not move the actual measures here, since they are already spatialy close due to being created at the same time.
            // MeasureContainer persitentStorage = (MeasureContainer) persistentAlloc(sizeof(Measure*) * ms.set.sets->size());
            MeasureContainer persistentStorage = new vector<Measure*>();
            persistentStorage->reserve(ms.set.sets->size());
            #if CLEAN_MEMORY
            persistentMeasureContainers.push_back(persistentStorage);
            #endif
            for (int i = 0; i < ms.set.sets->size(); i++) {
                Measure* m = (*ms.set.sets)[i];
                persistentStorage->push_back(m);
            }
            ms.set.sets = persistentStorage;
        }
        DocCache dc = DocCache::Create(key, ms);
        // insert into the array and ensure that it stays sorted
        // cache[doc->cache_id].insert(cache[doc->cache_id].begin() + find.missingIndex, c);
        // c[key] = c;
        // c->insert({key,dc});
        c->emplace(key,dc);
        return dc.ms;
    } else {
        return resolve(docId, col, indent, flatten, arena);
    }
}

MeasureSet measureSetForText(uint32_t stringRef, uint32_t strLen, uint32_t col, MeasureContainer arena) {
    if (col + strLen <= computationWidth) {
        MeasureSet ms;
        ms.type = MeasureSetType::SET;
        ms.set.sets = arena;
        Measure* measure = allocateMeasure();
        measure->type = MeasureType::TEXT;
        measure->text.stringRef = stringRef;
        measure->cost = costText(col, strLen);
        measure->last = strLen + col;
        ms.set.sets->push_back(measure);
        return ms;
    } else {
        TaintedTrunk* trunk = allocateTaintedTrunk(TaintedTrunkType::VALUE, col, 0, false);
        trunk->type = TaintedTrunkType::VALUE;
        trunk->value.measure.type = MeasureType::TEXT;
        trunk->value.measure.text.stringRef = stringRef;
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
MeasureSet resolve (uint32_t docId, uint32_t col, uint32_t indent, bool flatten, MeasureContainer arena) {
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
            ms.set.sets = arena;
            Measure* measure = allocateMeasure();
            measure->type = MeasureType::NEWLINE;
            measure->newline.indent = indent;
            measure->cost = costNl();
            measure->last = indent;
            ms.set.sets->push_back(measure);
            return ms;
        }
    }
    case DocType::ALIGN :
        return resolveCached (doc->align.alignDoc, col, col, flatten, arena); // pass through the arena
    case DocType::CONCAT :{
        // Measure* childArena [MEASURE_ARENA_SIZE];
        MeasureContainer childArena = borrowMeasureContainer();
        MeasureSet leftSet = resolveCached (doc->concat.leftDoc, col, indent, flatten, childArena);
        // bool safe= left.type == MeasureSetType::SET && canReturnPerfect(left.set.sets, left.set.count); 
        // use parent arena, because processConcat is used to return the value and therefore the value should survive.
        MeasureSet ms =  processConcat(leftSet, doc->concat.rightDoc, col, indent, flatten, arena);

        releaseMeasureContainer(childArena);
        // bool safeAfter= ms.type == MeasureSetType::SET && canReturnPerfect(ms.set.sets, ms.set.count); 
        // if (safe && !safeAfter && ms.type == MeasureSetType::SET) {
        //     cout << "here " << endl;
        // }
        return ms;
    }
        
    case DocType::CHOICE : {
        MeasureContainer childArenaLeft = borrowMeasureContainer();
        MeasureContainer childArenaRight = borrowMeasureContainer();

        Doc* leftDoc = &docs[doc->concat.leftDoc];
        Doc* rightDoc = &docs[doc->concat.rightDoc];
        
        if (rightDoc->nlCount < leftDoc->nlCount) {
            MeasureSet leftSet = resolveCached (doc->choice.leftDoc, col, indent, flatten, childArenaLeft);
            MeasureSet rightSet = resolveCached (doc->choice.rightDoc, col, indent, flatten, childArenaRight);
            MeasureSet ms = mergeSet(leftSet, rightSet, arena);
            releaseMeasureContainer(childArenaRight);
            releaseMeasureContainer(childArenaLeft);
            return ms;
        } else {
            MeasureSet rightSet = resolveCached (doc->choice.rightDoc, col, indent, flatten, childArenaRight);
            MeasureSet leftSet = resolveCached (doc->choice.leftDoc, col, indent, flatten, childArenaLeft);

            
            MeasureSet ms = mergeSet(rightSet, leftSet, arena);
            releaseMeasureContainer(childArenaRight);
            releaseMeasureContainer(childArenaLeft);
            return ms;
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
        // Measure* arena [MEASURE_ARENA_SIZE];
        MeasureContainer arena = borrowMeasureContainer();
        MeasureSet ms = resolve(trunk->left.rightDoc, leftMeasure->last, trunk->indent, trunk->flatten, arena);
        if (ms.type == MeasureSetType::TAINTED) {
            Measure* expanded = expandTainted(ms.tainted.trunk);
            return measureConcat(leftMeasure, expanded);
        } else {
            Measure* m = measureConcat(leftMeasure, (*ms.set.sets)[0]); // return the first result (we can't release this memory since it is used to render, sorry borrowMeasureContainer)
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
//usefull for debugging
string renderChoiceLessNow (Measure* choiceLess) {
    try {
        stringbuf buf;
        renderChoiceLess(choiceLess, buf);
        return buf.str();
    } catch (const char* e) {
        return "nope";
    }
}
string renderChoiceLessSetNow (MeasureSet choiceLess) {
    try {
        stringbuf buf;
        if (choiceLess.type == MeasureSetType::TAINTED) {
            renderChoiceLess(expandTainted(choiceLess.tainted.trunk), buf);
        } else if(choiceLess.set.sets->size() == 0) {
            return "";
        } else  {
            renderChoiceLess((*choiceLess.set.sets)[0], buf);
        }
        return buf.str();
    } catch (const char* e) {
        return "nope";
    }
}

struct Output {
    string layout;
    Cost cost;
    bool isTainted;
};

Output print(uint32_t docId) {
    // Measure* arena [MEASURE_ARENA_SIZE];
    MeasureContainer arena = borrowMeasureContainer();
    MeasureSet ms = resolveCached(docId, 0, 0, false, arena);
    Measure* measure;
    bool isTainted = ms.type == MeasureSetType::TAINTED;
    if (isTainted) {
        measure = expandTainted(ms.tainted.trunk);
    } else {
        measure = (*ms.set.sets)[0];
    }
    stringbuf buf;
    renderChoiceLess(measure, buf);
    return {buf.str(), measure->cost, isTainted};
}
