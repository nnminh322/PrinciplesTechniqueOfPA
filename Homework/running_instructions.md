# IT5440 — Bộ bài tập (HW1–HW3)

Repo này gồm 3 homework cho học phần **IT5440 – Principles and Techniques of Program Analysis**.

- **HW1**: **báo cáo Word** (không kèm code chạy).  
- **HW2**: demo **CFG + Dataflow (Reaching Definitions)** và **mô phỏng Heap/Pointer** (phát hiện *use-after-free*).  
- **HW3**: demo **Tracing**, **Dynamic Slicing**, **Execution Indexing**, **Fault Localization**.

---

## Yêu cầu môi trường

### macOS / Linux
- `clang++` hỗ trợ **C++17**

Kiểm tra:
```bash
clang++ --version
```

### Windows (khuyến nghị dùng PowerShell)
Cần 1 trong 2 lựa chọn sau (chọn 1 là đủ):

**Lựa chọn A — LLVM/Clang cho Windows**
1. Cài **LLVM** (có `clang++`) và thêm vào **PATH**
2. Mở PowerShell, kiểm tra:
```powershell
clang++ --version
```

**Lựa chọn B — MSYS2 (MinGW-w64 + clang)**
1. Cài MSYS2
2. Cài toolchain clang:
```bash
pacman -S --needed mingw-w64-ucrt-x86_64-clang
```
3. Mở “MSYS2 UCRT64” và kiểm tra:
```bash
clang++ --version
```

---

## HW2 — build & chạy (kèm xuất file `.output`)

File code là `problem.cpp`.

### macOS / Linux
```bash
# 1) Build (log warning/error -> hw2_build.output)
clang++ -std=c++17 -O2 -Wall -Wextra problem.cpp -o problem 2> hw2_build.output
clang++ -std=c++17 -O2 -Wall -Wextra problem.cpp -o problem &> output.txt && ./problem >> output.txt 2>&1
# 2) Run (stdout+stderr -> hw2_run.output)
./problem > hw2_run.output 2>&1
```

### Windows (PowerShell)
```powershell
# 1) Build (log warning/error -> hw2_build.output)
clang++ -std=c++17 -O2 -Wall -Wextra .\problem.cpp -o problem.exe 2> hw2_build.output

# 2) Run (stdout+stderr -> hw2_run.output)
.\problem.exe > hw2_run.output 2>&1
```

Kết quả người đọc cần xem:
- `hw2_build.output`: warning khi compile (nếu có)
- `hw2_run.output`: toàn bộ output chương trình (CFG edges, DOT, RD OUT sets, heap/pointer demo)

---

## HW3 — build & chạy (kèm xuất file `.output`)

Giả sử file code là `tempCodeRunnerFile.cpp` và build ra `hw3`.

### macOS / Linux
```bash
# 1) Build (log warning/error -> hw3_build.output)
clang++ -std=c++17 -O2 -Wall -Wextra tempCodeRunnerFile.cpp -o hw3 2> hw3_build.output

# 2) Chạy các mode và lưu kết quả ra .output
./hw3 trace_slice 5 > hw3_trace_slice.output 2>&1
./hw3 exec_index    > hw3_exec_index.output 2>&1
./hw3 fault_loc     > hw3_fault_loc.output 2>&1
```

### Windows (PowerShell)
```powershell
# 1) Build (log warning/error -> hw3_build.output)
clang++ -std=c++17 -O2 -Wall -Wextra .\tempCodeRunnerFile.cpp -o hw3.exe 2> hw3_build.output

# 2) Chạy các mode và lưu kết quả ra .output
.\hw3.exe trace_slice 5 > hw3_trace_slice.output 2>&1
.\hw3.exe exec_index    > hw3_exec_index.output 2>&1
.\hw3.exe fault_loc     > hw3_fault_loc.output 2>&1
```

---

## Ghi chú ngắn 

- HW2:
  - Phần DOT xuất ra để vẽ CFG bằng Graphviz (nếu cần).
  - Reaching Definitions in `OUT` cho từng node (trừ Start/End).
  - Demo Heap/Pointer: sau `free`, nếu deref sẽ báo `read: use-after-free`.

- HW3:
  - `trace_slice <input>`: in trace (control + value + memory) và thin dynamic slice với tiêu chí `<S10, z>`.
  - `exec_index`: in execution index (ngữ cảnh chạy) cho từng event.
  - `fault_loc`: chạy test và xếp hạng statement nghi ngờ lỗi theo Ochiai.
