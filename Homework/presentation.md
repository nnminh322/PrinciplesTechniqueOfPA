# IT5440 — Presentation (HW2 + HW3)

> Mỗi “slide” bên dưới gồm: **mục tiêu**, **pseudo-code**, và **output (crop)**

---

## HW2 — Slide 1: CFG (DOT) + Reaching Definitions (Fixpoint)

### Mục tiêu
- Dựng **CFG** (node/edge) và **in ra DOT (Graphviz)** bằng `cfg_to_dot(cfg)`.
- Reaching Definitions trong `reaching_definitions(cfg)`:
  - Tạo **GEN/KILL** cho mỗi node.
  - Lặp tính **IN/OUT** đến khi **hội tụ (fixpoint)**.

### Pseudo-code 
```cpp
// cfg_to_dot(cfg): in DOT để vẽ Graphviz (node + edge)
ids = sort(keys(cfg.nodes))                      // giữ thứ tự ổn định
print "digraph CFG { node [shape=box];"
for id in ids:
  stmt  = cfg.nodes[id].stmt
  label = to_string(id) + ": " + stmt
  print "n{id} [label=\"{label}\"];"
for id in ids:
  for s in cfg.nodes[id].succ:
    print "n{id} -> n{s};"
print "}"
```

```cpp
// reaching_definitions(cfg): giải bài RD theo vòng lặp fixpoint (may-analysis)
U = {}                                           // tập mọi định nghĩa (var, node_id)
for each node n:
  for each var v in defs_in_stmt(cfg.nodes[n].stmt):
    U.add( (v,n) )

for each node n:
  GEN[n]  = { (v,n) | v được gán ở node n }
  KILL[n] = { (v,m) in U | v trùng nhưng m != n } // “giết” định nghĩa cũ của cùng biến

pred = đảo cạnh từ succ để có danh sách tiền nhiệm
IN[n]=∅; OUT[n]=∅
while (có thay đổi):
  for n in ids:
    IN[n]  = UNION( OUT[p] for p in pred[n] )
    OUT[n] = GEN[n] ∪ (IN[n] − KILL[n])
```

### Output (crop)
```
CFG as DOT (Graphviz):
  n3 -> n4
  n3 -> n6
  n5 -> n3
  ...

=== (1) Reaching Definitions (OUT sets) ===
node 3 | while (i < 11) | OUT = {(i,2), (i,5), (sum,1), (sum,4)}
node 5 | i = i + 1      | OUT = {(i,5), (sum,4)}
```

---

## HW2 — Slide 2: Mô phỏng Heap + phát hiện Use-After-Free (Pointer wrapper)

### Mục tiêu
- Mô phỏng heap bằng `HeapBlock` có cờ `freed`.
- `Heap::read()`/`Heap::write()` **chặn lỗi use-after-free** bằng kiểm tra `freed`.
- `Pointer::load()` là wrapper, mọi deref đều đi qua `heap->read(...)`.

### Pseudo-code 
```cpp
// HeapBlock: metadata để phát hiện double-free / use-after-free
HeapBlock { size, data[size], freed=false }

malloc_block(size):
  addr = next_addr; next_addr += size
  blocks[addr] = HeapBlock(size, freed=false)
  return addr

free_block(addr):
  if addr not in blocks     -> throw HeapError("free: invalid address")
  if blocks[addr].freed     -> throw HeapError("free: double free")
  blocks[addr].freed = true
```

```cpp
read(addr, off=0):
  if addr not in blocks     -> throw HeapError("read: invalid address")
  if blocks[addr].freed     -> throw HeapError("read: use-after-free") // chặn UAF
  if off out-of-bounds      -> throw HeapError("read: out-of-bounds")
  return blocks[addr].data[off]

Pointer.add(delta)  => (base, offset+delta, heap)
Pointer.load()      => heap->read(base, offset)   // wrapper deref
Pointer.store(v)    => heap->write(base, v, offset)
```

### Output (crop)
```
=== (3)+(4) Memory + Pointer demo ===
stack local x = 10
*p = 123, *(p+1) = 456
after free, deref p -> read: use-after-free
```

---

## HW3 — Slide 1: Tracing (control + value + memory)

### Mục tiêu
- Ghi lại **dòng thực thi** theo event `TraceEvent`:
  - `stmt` (statement id), `idx` (execution index), `reads/writes`, `mem_reads/mem_writes`.
- Mỗi statement tạo 1 event bằng `begin_stmt(stmt_id)` và chốt bằng `end_stmt(eid)`.

### Pseudo-code 
```cpp
ExecContext.str():
  if stack rỗng -> "<main>"
  else -> "<main/" + join(stack,"/") + ">"

Tracer.begin_stmt(stmt_id):
  e.eid  = evs.size()
  e.stmt = stmt_id
  e.idx  = ctx.str()                 // execution index (ngữ cảnh chạy)
  evs.push_back(e)
  return e.eid

Tracer.read_var(eid, v, val):
  evs[eid].reads.push_back( (v,val) )
  evs[eid].use_def_var[v] = last_def_var.get(v, -1)

Tracer.read_mem(eid, addr, val):
  evs[eid].mem_reads.push_back( (addr,val) )
  evs[eid].use_def_mem[addr] = last_def_mem.get(addr, -1)

Tracer.end_stmt(eid):
  for (v,_) in evs[eid].writes:     last_def_var[v]  = eid
  for (a,_) in evs[eid].mem_writes: last_def_mem[a]  = eid
```

### Output (crop)
```
=== TRACE (control+value+memory) ===
E12  S9  <main/W#2>
  R:  p=...
  W:  z=4
  MR: *(0x...)=2
```

---

## HW3 — Slide 2: Dynamic Slicing (thin slice)

### Mục tiêu
- Thin dynamic slice theo tiêu chí ví dụ: **criterion `<S10, z>`**.
- Ý tưởng: từ event “print z”, **truy ngược** các phụ thuộc data:
  - biến đọc (`reads`) -> nhảy về event định nghĩa gần nhất (`use_def_var`)
  - ô nhớ đọc (`mem_reads`) -> nhảy về event ghi gần nhất (`use_def_mem`)

### Pseudo-code 
```cpp
thin_dynamic_slice_stmt_ids_from_event(start_eid):
  seen_eids = {}
  slice_stmt_ids = {}
  stack = [start_eid]

  while stack not empty:
    eid = stack.pop()
    if eid ngoài range hoặc eid đã seen -> continue
    seen_eids.add(eid)

    e = evs[eid]
    slice_stmt_ids.add(e.stmt)

    for (v,_) in e.reads:
      def_eid = e.use_def_var.get(v, -1)
      if def_eid != -1: stack.push(def_eid)

    for (addr,_) in e.mem_reads:
      def_eid = e.use_def_mem.get(addr, -1)
      if def_eid != -1: stack.push(def_eid)

  return slice_stmt_ids
```

### Output (crop)
```
=== THIN DYNAMIC SLICE for criterion <S10, z> ===
{ 2 5 7 8 9 10 }
```

---

## HW3 — Slide 3: Execution Indexing

### Mục tiêu
- Gắn mỗi event với **execution index** dạng `<main/LoopK#.../T|F/foo#...>`.
- Dùng `ctx.push(tag)` / `ctx.pop()` để phản ánh:
  - vòng lặp (LoopK#k), nhánh (T/F), lời gọi hàm (foo#call_id)

### Pseudo-code 
```cpp
ctx.push("LoopK#k")
  begin_stmt(102)   // if (k==1)
  if cond:
    ctx.push("T");  ctx.push("foo#1")
      begin_stmt(101)   // x = x + 1
      end_stmt(101)
    ctx.pop(); ctx.pop()
  else:
    ctx.push("F");  ctx.push("foo#2")
      begin_stmt(101)
      end_stmt(101)
    ctx.pop(); ctx.pop()
end_stmt(102)
ctx.pop()

// dump: mỗi event in ra "E? S? <main/...>"
```

### Output (crop)
```
=== EXECUTION INDEXES ===
E0 S100 <main>
E1 S102 <main/LoopK#1>
E2 S101 <main/LoopK#1/T/foo#1>
E3 S102 <main/LoopK#2>
E4 S101 <main/LoopK#2/F/foo#2>
E5 S103 <main>
```

---

## HW3 — Slide 4: Fault Localization (Ochiai)

### Mục tiêu
- Coverage-based fault localization:
  - Mỗi test ghi tập statement “đi qua” (coverage).
  - Tính điểm nghi ngờ (suspiciousness) và xếp hạng.
- Dùng **Ochiai**:
  - `score(s) = fail_exec(s) / sqrt(total_fail * (fail_exec(s) + pass_exec(s)))`

### Pseudo-code 
```cpp
runs = []
for each test t:
  cov = {}                                 // tập stmt được cover
  got = clamp_bug(t.x,t.lo,t.hi, cov)      // hàm có BUG ở S204
  pass = (got == t.expected)
  runs.push( (pass, cov) )

counts[stmt] = (fail_exec, pass_exec)
for run in runs:
  for stmt in run.cov:
    if run.pass: counts[stmt].pass_exec++
    else:        counts[stmt].fail_exec++

for each stmt in counts:
  f = fail_exec(stmt); p = pass_exec(stmt)
  score = f / sqrt(total_fail * (f+p))     // Ochiai
sort stmt theo score giảm dần
```

### Output (crop)
```
=== FAULT LOCALIZATION (Ochiai suspiciousness) ===
S204 score=1 (fail_exec=2, pass_exec=0)
S203 score=0.632456 ...
S201 score=0.57735 ...
Expected: statement S204 is the most suspicious (buggy branch).
```
