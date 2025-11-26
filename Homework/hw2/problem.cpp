// IT5440 - Homework 2 (Chapter II: Static Program Analysis)
// C++17 single-file demo
// (1) Dataflow Analysis: Reaching Definitions trên CFG
// (2) CFG: biểu diễn node/edge + xuất DOT
// (3) Memory allocation/free: stack (automatic) + heap (dynamic)
// (4) Pointer: dùng/free pointer + minh hoạ use-after-free (được chặn bằng wrapper)
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <set>
#include <algorithm> // sort
#include <sstream>   // ostringstream, istringstream
#include <iomanip>   // setw, left
#include <stdexcept> // runtime_error
#include <utility>   // pair, move
#include <cctype>    // isspace
using namespace std;

// -------------------------
// Part (2): CFG
// -------------------------
struct Node {
    int id;
    string stmt;
    vector<int> succ;
};

struct CFG {
    int entry, exit;
    unordered_map<int, Node> nodes;
};

static CFG build_example_cfg() {
    // Toy program:
    //   sum=0; i=1;
    //   while(i<11){ sum=sum+i; i=i+1; }
    //   print(sum); print(i)
    CFG cfg;
    cfg.entry = 0;
    cfg.exit  = 8;

    cfg.nodes[0] = Node{0, "Start", {1}};
    cfg.nodes[1] = Node{1, "sum = 0", {2}};
    cfg.nodes[2] = Node{2, "i = 1", {3}};
    cfg.nodes[3] = Node{3, "while (i < 11)", {4, 6}}; // true->4, false->6
    cfg.nodes[4] = Node{4, "sum = sum + i", {5}};
    cfg.nodes[5] = Node{5, "i = i + 1", {3}};         // back edge
    cfg.nodes[6] = Node{6, "print(sum)", {7}};
    cfg.nodes[7] = Node{7, "print(i)", {8}};
    cfg.nodes[8] = Node{8, "End", {}};
    return cfg;
}

static string cfg_to_dot(const CFG& cfg) {
    ostringstream out;
    out << "digraph CFG {\n  node [shape=box];\n";
    // nodes
    vector<int> ids;
    ids.reserve(cfg.nodes.size());
    for (auto& kv : cfg.nodes) ids.push_back(kv.first);
    sort(ids.begin(), ids.end());

    for (int id : ids) {
        const auto& n = cfg.nodes.at(id);
        string label = to_string(n.id) + ": " + n.stmt;
        // escape quotes
        string esc;
        for (char c : label) esc += (c == '"' ? "\\\"" : string(1, c));
        out << "  n" << id << " [label=\"" << esc << "\"];\n";
    }
    // edges
    for (int id : ids) {
        const auto& n = cfg.nodes.at(id);
        for (int s : n.succ) out << "  n" << id << " -> n" << s << ";\n";
    }
    out << "}\n";
    return out.str();
}

// Part (1): Dataflow Analysis (Reaching Definitions)
using Def = pair<string,int>; // (var, node_id)

static set<string> defs_in_stmt(const string& stmt) {
    string s = stmt;
    auto trim = [](string& t){
        auto issp = [](unsigned char c){ return isspace(c); };
        while(!t.empty() && issp(t.front())) t.erase(t.begin());
        while(!t.empty() && issp(t.back()))  t.pop_back();
    };
    trim(s);

    if (s.rfind("while", 0) == 0 || s.rfind("if", 0) == 0) return {};
    auto pos = s.find('=');
    if (pos == string::npos) return {};
    string lhs = s.substr(0, pos);
    trim(lhs);
    if (lhs.empty()) return {};
    string var;
    {
        istringstream iss(lhs);
        while (iss >> var) {}
    }
    if (var.empty()) return {};
    return {var};
}

struct RDResult {
    unordered_map<int, set<Def>> IN, OUT;
};

static RDResult reaching_definitions(const CFG& cfg) {
    set<Def> U;
    for (auto& kv : cfg.nodes) {
        int nid = kv.first;
        for (auto& v : defs_in_stmt(kv.second.stmt)) U.insert({v, nid});
    }

    unordered_map<int, set<Def>> GEN, KILL;
    for (auto& kv : cfg.nodes) {
        int nid = kv.first;
        auto defs = defs_in_stmt(kv.second.stmt);
        set<Def> gen, kill;
        for (auto& v : defs) {
            gen.insert({v, nid});
            for (auto& d : U) {
                if (d.first == v && d.second != nid) kill.insert(d);
            }
        }
        GEN[nid] = move(gen);
        KILL[nid] = move(kill);
    }

    unordered_map<int, vector<int>> pred;
    for (auto& kv : cfg.nodes) pred[kv.first] = {};
    for (auto& kv : cfg.nodes) {
        int from = kv.first;
        for (int to : kv.second.succ) pred[to].push_back(from);
    }

    RDResult res;
    for (auto& kv : cfg.nodes) {
        res.IN[kv.first] = {};
        res.OUT[kv.first] = {};
    }

    vector<int> ids;
    ids.reserve(cfg.nodes.size());
    for (auto& kv : cfg.nodes) ids.push_back(kv.first);
    sort(ids.begin(), ids.end());

    bool changed = true;
    while (changed) {
        changed = false;
        for (int nid : ids) {
            set<Def> newIN, newOUT;

            for (int p : pred[nid]) {
                newIN.insert(res.OUT[p].begin(), res.OUT[p].end());
            }

            newOUT = GEN[nid];
            for (auto& d : newIN) if (!KILL[nid].count(d)) newOUT.insert(d);

            if (newIN != res.IN[nid] || newOUT != res.OUT[nid]) {
                res.IN[nid] = move(newIN);
                res.OUT[nid] = move(newOUT);
                changed = true;
            }
        }
    }
    return res;
}

// -------------------------
// Part (3)+(4): Memory + Pointer (stack + heap simulator)
// -------------------------
struct HeapError : runtime_error {
    using runtime_error::runtime_error;
};

struct HeapBlock {
    size_t size;
    vector<int> data;
    bool freed;
};

class Heap {
    long long next_addr = 1000; 
    unordered_map<long long, HeapBlock> blocks;

public:
    long long malloc_block(size_t size) {
        if (size == 0) throw invalid_argument("size must be > 0");
        long long addr = next_addr;
        next_addr += (long long)size;
        blocks[addr] = HeapBlock{size, vector<int>(size, 0), false};
        return addr;
    }

    void free_block(long long addr) {
        auto it = blocks.find(addr);
        if (it == blocks.end()) throw HeapError("free: invalid address");
        if (it->second.freed) throw HeapError("free: double free");
        it->second.freed = true;
    }

    int read(long long addr, long long offset = 0) {
        auto it = blocks.find(addr);
        if (it == blocks.end()) throw HeapError("read: invalid address");
        auto& b = it->second;
        if (b.freed) throw HeapError("read: use-after-free");
        if (offset < 0 || (size_t)offset >= b.size) throw HeapError("read: out-of-bounds");
        return b.data[(size_t)offset];
    }

    void write(long long addr, int value, long long offset = 0) {
        auto it = blocks.find(addr);
        if (it == blocks.end()) throw HeapError("write: invalid address");
        auto& b = it->second;
        if (b.freed) throw HeapError("write: use-after-free");
        if (offset < 0 || (size_t)offset >= b.size) throw HeapError("write: out-of-bounds");
        b.data[(size_t)offset] = value;
    }
};

struct Pointer {
    long long base;
    long long offset;
    Heap* heap;

    Pointer add(long long delta) const { return Pointer{base, offset + delta, heap}; }
    int load() const { return heap->read(base, offset); }
    void store(int v) const { heap->write(base, v, offset); }
};

class Stack {
    vector<unordered_map<string,int>> frames;

public:
    void enter(const string& name) {
        frames.push_back({{"__name__", 0}});
        (void)name;
    }
    void exit() {
        if (frames.empty()) throw runtime_error("stack underflow");
        frames.pop_back();
    }
    void set_local(const string& var, int value) {
        if (frames.empty()) throw runtime_error("no active frame");
        frames.back()[var] = value;
    }
    int get_local(const string& var) const {
        if (frames.empty()) throw runtime_error("no active frame");
        auto it = frames.back().find(var);
        if (it == frames.back().end()) throw runtime_error("undefined local");
        return it->second;
    }
};

static string defs_to_string(const set<Def>& s) {
    vector<string> items;
    for (auto& d : s) items.push_back("(" + d.first + "," + to_string(d.second) + ")");
    sort(items.begin(), items.end());
    ostringstream out;
    out << "{";
    for (size_t i=0;i<items.size();i++){
        if (i) out << ", ";
        out << items[i];
    }
    out << "}";
    return out.str();
}

int main() {
    // (2) CFG
    CFG cfg = build_example_cfg();
    cout << "=== (2) CFG edges ===\n";
    {
        vector<pair<int,int>> edges;
        vector<int> ids;
        for (auto& kv : cfg.nodes) ids.push_back(kv.first);
        sort(ids.begin(), ids.end());
        for (int id : ids) for (int s : cfg.nodes[id].succ) edges.push_back({id, s});
        for (auto& e : edges) cout << "(" << e.first << " -> " << e.second << ")\n";
    }

    cout << "\nCFG as DOT (Graphviz):\n";
    cout << cfg_to_dot(cfg);

    // (1) Reaching Definitions
    auto rd = reaching_definitions(cfg);
    cout << "\n=== (1) Reaching Definitions (OUT sets) ===\n";
    vector<int> ids;
    for (auto& kv : cfg.nodes) ids.push_back(kv.first);
    sort(ids.begin(), ids.end());

    for (int nid : ids) {
        const string& stmt = cfg.nodes[nid].stmt;
        if (stmt == "Start" || stmt == "End") continue;
        cout << "node " << setw(2) << nid
             << " | " << left << setw(18) << stmt << " | OUT = "
             << defs_to_string(rd.OUT[nid]) << "\n";
    }

    // (3)+(4) Memory + Pointer
    cout << "\n=== (3)+(4) Memory + Pointer demo ===\n";
    Stack stack;
    Heap heap;

    stack.enter("main");
    stack.set_local("x", 10);
    cout << "stack local x = " << stack.get_local("x") << "\n";

    long long addr = heap.malloc_block(4);
    Pointer p{addr, 0, &heap};
    p.store(123);                 // *p = 123
    auto p1 = p.add(1);
    p1.store(456);                // *(p+1) = 456

    cout << "*p = " << p.load() << ", *(p+1) = " << p1.load() << "\n";

    heap.free_block(addr);        
    try {
        (void)p.load();           
    } catch (const HeapError& e) {
        cout << "after free, deref p -> " << e.what() << "\n";
    }

    stack.exit();
    return 0;
}
