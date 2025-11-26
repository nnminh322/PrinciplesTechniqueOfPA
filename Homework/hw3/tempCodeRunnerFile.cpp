#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>
#include <sstream>
#include <algorithm>
#include <cmath>

using std::string;

static string join(const std::vector<string>& v, const char* sep) {
    std::ostringstream oss;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) oss << sep;
        oss << v[i];
    }
    return oss.str();
}

struct ExecContext {
    std::vector<string> stack;

    void push(const string& tag) { stack.push_back(tag); }
    void pop() { if (!stack.empty()) stack.pop_back(); }

    string str() const {
        if (stack.empty()) return "<main>";
        return "<main/" + join(stack, "/") + ">";
    }
};

struct TraceEvent {
    int eid = -1;          
    int stmt = -1;         
    string idx;            
    std::vector<std::pair<string, long long>> reads;
    std::vector<std::pair<string, long long>> writes;
    std::vector<std::pair<uint64_t, long long>> mem_reads;   
    std::vector<std::pair<uint64_t, long long>> mem_writes;  

    // For slicing: “use -> last_def event id at that moment”
    std::unordered_map<string, int> use_def_var;
    std::unordered_map<uint64_t, int> use_def_mem;
};

struct Tracer {
    ExecContext* ctx = nullptr;
    std::vector<TraceEvent> evs;

    std::unordered_map<string, int> last_def_var;   
    std::unordered_map<uint64_t, int> last_def_mem; 

    explicit Tracer(ExecContext* c) : ctx(c) {}

    int begin_stmt(int stmt_id) {
        TraceEvent e;
        e.eid = (int)evs.size();
        e.stmt = stmt_id;
        e.idx = (ctx ? ctx->str() : "<main>");
        evs.push_back(std::move(e));
        return (int)evs.size() - 1;
    }

    void read_var(int eid, const string& v, long long val) {
        auto& e = evs.at(eid);
        e.reads.push_back({v, val});
        auto it = last_def_var.find(v);
        e.use_def_var[v] = (it == last_def_var.end() ? -1 : it->second);
    }

    void write_var(int eid, const string& v, long long val) {
        auto& e = evs.at(eid);
        e.writes.push_back({v, val});
    }

    void read_mem(int eid, uint64_t addr, long long val) {
        auto& e = evs.at(eid);
        e.mem_reads.push_back({addr, val});
        auto it = last_def_mem.find(addr);
        e.use_def_mem[addr] = (it == last_def_mem.end() ? -1 : it->second);
    }

    void write_mem(int eid, uint64_t addr, long long val) {
        auto& e = evs.at(eid);
        e.mem_writes.push_back({addr, val});
    }

    void end_stmt(int eid) {
        auto& e = evs.at(eid);
        for (auto& [v, _val] : e.writes) last_def_var[v] = eid;
        for (auto& [addr, _val] : e.mem_writes) last_def_mem[addr] = eid;
    }

    void dump_trace(bool show_mem = true) const {
        std::cout << "=== TRACE (control+value";
        if (show_mem) std::cout << "+memory";
        std::cout << ") ===\n";
        for (auto& e : evs) {
            std::cout << "E" << e.eid << "  S" << e.stmt << "  " << e.idx << "\n";
            if (!e.reads.empty()) {
                std::cout << "  R: ";
                for (auto& [v,val] : e.reads) std::cout << v << "=" << val << " ";
                std::cout << "\n";
            }
            if (!e.writes.empty()) {
                std::cout << "  W: ";
                for (auto& [v,val] : e.writes) std::cout << v << "=" << val << " ";
                std::cout << "\n";
            }
            if (show_mem && !e.mem_reads.empty()) {
                std::cout << "  MR: ";
                for (auto& [a,val] : e.mem_reads) std::cout << "*(0x" << std::hex << a << std::dec << ")=" << val << " ";
                std::cout << "\n";
            }
            if (show_mem && !e.mem_writes.empty()) {
                std::cout << "  MW: ";
                for (auto& [a,val] : e.mem_writes) std::cout << "*(0x" << std::hex << a << std::dec << ")=" << val << " ";
                std::cout << "\n";
            }
        }
    }

    std::set<int> thin_dynamic_slice_stmt_ids_from_event(int start_eid) const {
        std::unordered_set<int> seen_eids;
        std::set<int> slice_stmt_ids;

        std::vector<int> st;
        st.push_back(start_eid);

        while (!st.empty()) {
            int eid = st.back(); st.pop_back();
            if (eid < 0 || eid >= (int)evs.size()) continue;
            if (seen_eids.count(eid)) continue;
            seen_eids.insert(eid);

            const auto& e = evs[eid];
            slice_stmt_ids.insert(e.stmt);

            for (auto& [v, _val] : e.reads) {
                auto it = e.use_def_var.find(v);
                if (it != e.use_def_var.end() && it->second != -1) st.push_back(it->second);
            }
            for (auto& [addr, _val] : e.mem_reads) {
                auto it = e.use_def_mem.find(addr);
                if (it != e.use_def_mem.end() && it->second != -1) st.push_back(it->second);
            }
        }
        return slice_stmt_ids;
    }

    int last_event_of_stmt(int stmt_id) const {
        for (int i = (int)evs.size() - 1; i >= 0; --i) {
            if (evs[i].stmt == stmt_id) return i;
        }
        return -1;
    }
};

// -------------------- Part (1)+(2): Tracing + Dynamic Slicing demo --------------------
//
// Statements:
//  S1: z = 0
//  S2: a = 0
//  S3: b = 2
//  S4: p = &b
//  S5: i = 1
//  S6: while (i <= input)   // we trace condition evaluation as a statement event too
//  S7: if (i%2==0) p=&a     // traced; write occurs only when true
//  S8: a = a + 1
//  S9: z = 2 * (*p)         // memory read via deref p
//  S10: print(z)
//
static void demo_trace_and_slice(int input) {
    ExecContext ctx;
    Tracer tr(&ctx);

    int z = 0, a = 0, b = 0, i = 0;
    int* p = nullptr;

    // S1: z=0
    {
        int eid = tr.begin_stmt(1);
        tr.write_var(eid, "z", 0);
        z = 0;
        tr.end_stmt(eid);
    }
    // S2: a=0
    {
        int eid = tr.begin_stmt(2);
        tr.write_var(eid, "a", 0);
        a = 0;
        tr.end_stmt(eid);
    }
    // S3: b=2
    {
        int eid = tr.begin_stmt(3);
        tr.write_var(eid, "b", 2);
        b = 2;
        // also track memory write (address of b)
        tr.write_mem(eid, (uint64_t)(uintptr_t)(&b), b);
        tr.end_stmt(eid);
    }
    // S4: p=&b
    {
        int eid = tr.begin_stmt(4);
        tr.write_var(eid, "p", (long long)(uintptr_t)(&b));
        p = &b;
        tr.end_stmt(eid);
    }
    // S5: i=1
    {
        int eid = tr.begin_stmt(5);
        tr.write_var(eid, "i", 1);
        i = 1;
        tr.end_stmt(eid);
    }

    int loop_iter = 0;
    while (true) {
        // S6: while condition
        {
            ++loop_iter;
            ctx.push("W#" + std::to_string(loop_iter));

            int eid = tr.begin_stmt(6);
            tr.read_var(eid, "i", i);
            tr.read_var(eid, "input", input);
            bool cond = (i <= input);
            tr.write_var(eid, "cond", cond ? 1 : 0);
            tr.end_stmt(eid);

            if (!cond) {
                ctx.pop();
                break;
            }
        }

        // S7: if (i%2==0) p=&a
        {
            int eid = tr.begin_stmt(7);
            tr.read_var(eid, "i", i);
            bool even = (i % 2 == 0);
            tr.write_var(eid, "even", even ? 1 : 0);
            if (even) {
                tr.write_var(eid, "p", (long long)(uintptr_t)(&a));
                p = &a;
            }
            tr.end_stmt(eid);
        }

        // S8: a=a+1
        {
            int eid = tr.begin_stmt(8);
            tr.read_var(eid, "a", a);
            a = a + 1;
            tr.write_var(eid, "a", a);
            tr.write_mem(eid, (uint64_t)(uintptr_t)(&a), a);
            tr.end_stmt(eid);
        }

        // S9: z = 2*(*p)
        {
            int eid = tr.begin_stmt(9);
            tr.read_var(eid, "p", (long long)(uintptr_t)p);
            // memory deref
            int pv = *p;
            tr.read_mem(eid, (uint64_t)(uintptr_t)p, pv);
            z = 2 * pv;
            tr.write_var(eid, "z", z);
            tr.end_stmt(eid);
        }

        i = i + 1;
        ctx.pop(); // end of this iteration context
    }

    // S10: print(z)
    {
        int eid = tr.begin_stmt(10);
        tr.read_var(eid, "z", z);
        tr.end_stmt(eid);
        std::cout << "Program output: z=" << z << "\n";
    }

    tr.dump_trace(true);

    // Dynamic slicing criterion: <S10, z>
    int print_eid = tr.last_event_of_stmt(10);
    auto slice_stmt_ids = tr.thin_dynamic_slice_stmt_ids_from_event(print_eid);

    std::cout << "=== THIN DYNAMIC SLICE for criterion <S10, z> ===\n";
    std::cout << "{ ";
    for (int s : slice_stmt_ids) std::cout << s << " ";
    std::cout << "}\n";
}

// -------------------- Part (3): Execution Indexing demo --------------------
//
// Idea: use execution index for setting breakpoints / reproducing a specific instance. :contentReference[oaicite:6]{index=6}
static void demo_execution_indexing() {
    ExecContext ctx;
    Tracer tr(&ctx);

    auto foo = [&](int& x, int call_id) {
        ctx.push("foo#" + std::to_string(call_id));
        // S101: x = x + 1
        int eid = tr.begin_stmt(101);
        tr.read_var(eid, "x", x);
        x = x + 1;
        tr.write_var(eid, "x", x);
        tr.end_stmt(eid);
        ctx.pop();
    };

    int x = 0;

    // main: execute two calls in different branches and a loop
    // S100: init x=0
    {
        int eid = tr.begin_stmt(100);
        tr.write_var(eid, "x", 0);
        x = 0;
        tr.end_stmt(eid);
    }

    for (int k = 1; k <= 2; ++k) {
        ctx.push("LoopK#" + std::to_string(k));

        // S102: if (k==1) ...
        {
            int eid = tr.begin_stmt(102);
            tr.read_var(eid, "k", k);
            bool cond = (k == 1);
            tr.write_var(eid, "cond", cond ? 1 : 0);
            tr.end_stmt(eid);

            if (cond) {
                ctx.push("T");
                foo(x, 1);
                ctx.pop();
            } else {
                ctx.push("F");
                foo(x, 2);
                ctx.pop();
            }
        }

        ctx.pop();
    }

    // S103: print x
    {
        int eid = tr.begin_stmt(103);
        tr.read_var(eid, "x", x);
        tr.end_stmt(eid);
        std::cout << "Execution-index demo output: x=" << x << "\n";
    }

    std::cout << "=== EXECUTION INDEXES (each event has a context index) ===\n";
    for (auto& e : tr.evs) {
        std::cout << "E" << e.eid << " S" << e.stmt << " " << e.idx << "\n";
    }
}

// -------------------- Part (4): Fault Localization demo --------------------
//
// Idea: suspiciousness ranking / coverage-based fault localization. :contentReference[oaicite:7]{index=7}
static void demo_fault_localization() {
    struct Test {
        int x, lo, hi;
        int expected;
    };

    auto clamp_bug = [&](int x, int lo, int hi, std::unordered_set<int>& cov) -> int {
        cov.insert(201);
        if (x < lo) { cov.insert(202); return lo; }
        cov.insert(203);
        if (x > hi) { cov.insert(204); return lo; } // BUG HERE
        cov.insert(205);
        return x;
    };

    std::vector<Test> tests = {
        { 5,   0, 10,  5 },  // pass
        {-3,   0, 10,  0 },  // pass
        {11,   0, 10, 10 },  // fail (returns 0)
        {20,   0, 10, 10 },  // fail (returns 0)
        {10,   0, 10, 10 },  // pass
        { 0,   0, 10,  0 },  // pass
    };

    struct Run {
        bool pass;
        std::unordered_set<int> cov;
    };
    std::vector<Run> runs;

    int total_fail = 0, total_pass = 0;
    for (auto& t : tests) {
        Run r;
        int got = clamp_bug(t.x, t.lo, t.hi, r.cov);
        r.pass = (got == t.expected);
        runs.push_back(std::move(r));
        if (runs.back().pass) total_pass++;
        else total_fail++;
    }

    std::map<int, std::pair<int,int>> counts; // stmt -> (fail, pass)
    for (auto& r : runs) {
        for (int s : r.cov) {
            auto& p = counts[s];
            if (r.pass) p.second++;
            else p.first++;
        }
    }

    struct Scored {
        int stmt;
        double score;
        int fail_exec;
        int pass_exec;
    };
    std::vector<Scored> scored;
    for (auto& [stmt, fp] : counts) {
        int f = fp.first, p = fp.second;
        double denom = std::sqrt((double)total_fail * (double)(f + p));
        double ochiai = (denom == 0.0) ? 0.0 : (double)f / denom;
        scored.push_back({stmt, ochiai, f, p});
    }

    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b){
        if (a.score != b.score) return a.score > b.score;
        return a.stmt < b.stmt;
    });

    std::cout << "=== TEST RESULTS ===\n";
    for (size_t i = 0; i < runs.size(); ++i) {
        std::cout << "T" << i << " : " << (runs[i].pass ? "PASS" : "FAIL") << " ; covered { ";
        std::vector<int> v(runs[i].cov.begin(), runs[i].cov.end());
        std::sort(v.begin(), v.end());
        for (int s : v) std::cout << s << " ";
        std::cout << "}\n";
    }

    std::cout << "=== FAULT LOCALIZATION (Ochiai suspiciousness) ===\n";
    std::cout << "total_fail=" << total_fail << ", total_pass=" << total_pass << "\n";
    for (auto& sc : scored) {
        std::cout << "S" << sc.stmt
                  << " score=" << sc.score
                  << " (fail_exec=" << sc.fail_exec
                  << ", pass_exec=" << sc.pass_exec << ")\n";
    }

    std::cout << "Expected: statement S204 is the most suspicious (buggy branch).\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  ./hw3 trace_slice <input>\n"
                  << "  ./hw3 exec_index\n"
                  << "  ./hw3 fault_loc\n";
        return 1;
    }

    string mode = argv[1];
    if (mode == "trace_slice") {
        int input = (argc >= 3) ? std::stoi(argv[2]) : 1;
        demo_trace_and_slice(input);
        return 0;
    }
    if (mode == "exec_index") {
        demo_execution_indexing();
        return 0;
    }
    if (mode == "fault_loc") {
        demo_fault_localization();
        return 0;
    }

    std::cerr << "Unknown mode: " << mode << "\n";
    return 1;
}