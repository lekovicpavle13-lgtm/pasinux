# Scheduler C++ Migration

**Date:** 2026-08-05
**Status:** Draft

## Overview

Migrate the hosted-simulator process scheduler (`sched/scheduler.c`) from C to C++, keeping `process_t` as a C-compatible POD struct with `extern "C"` linkage so that all C callers (`kernel.c`, the shell, etc.) require zero changes. This is the first C++ code in the kernel — a trial run before broader adoption.

## Motivation

- **RAII** — process stack memory automatically freed when a `Process`-managed object goes out of scope
- **Templates** — a `ProcessList<process_t>` template replaces hand-written circular linked-list operations
- **Namespaces** — `pasinux::sched` scoping for all scheduler symbols
- **`operator new`/`delete`** — typed allocation backed by existing `kmalloc`/`kfree`
- **`constexpr` / stronger typing** — compile-time constants, references instead of raw pointers where appropriate

## Constraints

1. **Zero changes to C callers** — `kernel.c`, `kernel.h`, and any code that includes `scheduler.h` must compile unchanged. The `process_t` struct layout is frozen.
2. **No RTTI, no exceptions** — `-fno-rtti -fno-exceptions` mandatory for freestanding compatibility
3. **No C++ standard library** — no `std::vector`, `std::string`, `iostream`, etc. All containers are kernel-own.
4. **C++17** — new enough for `if constexpr`, structured bindings, `constexpr` lambdas; old enough for rock-solid GCC/Clang support
5. **Only the hosted scheduler** — `sched_fs.c` (freestanding preemptive scheduler) stays C for now

## Architecture

### File changes

| File | Status | Description |
|---|---|---|
| `sched/scheduler.h` | Modified | Add `extern "C"` guards (`#ifdef __cplusplus` / `extern "C"`). Guard `#include <stdbool.h>` behind `#ifndef __cplusplus`. Structs and function declarations unchanged. |
| `sched/scheduler.hpp` | **New** | C++-only header. `pasinux::sched` namespace containing `ProcessList`, `SleepQueue`, `Scheduler` classes. |
| `sched/scheduler.cpp` | **New** | Full C++ rewrite of `scheduler.c`. Imports `scheduler.hpp`. Defines global `operator new`/`delete`. Implements `extern "C"` wrappers. |
| `Makefile` | Modified | Add `CXX`, `CXXFLAGS`, `%.o: %.cpp` pattern rule. |

### C/C++ boundary

The single header `sched/scheduler.h` is included by both C and C++ translation units:

```c
// sched/scheduler.h
#ifndef SCHEDULER_H
#define SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

/* process_t — POD struct, layout must not change */
typedef struct process {
    uint64_t pid;
    uint64_t ppid;
    uint8_t state;
    uint8_t priority;
    uint64_t cpu_time;
    uint64_t wake_tick;
    process_entry_t entry_point;
    void* stack_base;
    void* stack_ptr;
    struct process* next;
    struct process* prev;
    struct process* sleep_next;
    char name[32];
    jmp_buf context;
    int pc;
} process_t;

/* ... scheduler_stats_t, scheduler_config_t unchanged ... */
/* ... all function declarations unchanged ... */

#ifdef __cplusplus
}
#endif

#endif /* SCHEDULER_H */
```

The `__cplusplus` guard around `<stdbool.h>` is the only content change — `bool` is a keyword in C++, so including the C header is harmless on most compilers but technically non-portable.

### C++ classes (in `scheduler.hpp`)

```cpp
namespace pasinux::sched {

class ProcessList {
public:
    ProcessList() = default;
    void insert(process_t& proc);
    void remove(process_t& proc);
    bool contains(const process_t& proc) const;
    bool empty() const { return head_ == nullptr; }
    process_t* head() const { return head_; }
    static process_t* next(const process_t& p) { return p.next; }
private:
    process_t* head_ = nullptr;
};

class SleepQueue {
public:
    SleepQueue() = default;
    void add(process_t& proc, uint64_t wake_tick);
    void remove(process_t& proc);
    void tick(uint64_t current_tick);
private:
    process_t* head_ = nullptr;
};

class Scheduler {
public:
    static Scheduler& instance();
    void init();
    process_t* createProcess(process_entry_t entry, const char* name, uint8_t priority);
    void destroyProcess(process_t* proc);
    void addProcess(process_t* proc);
    void removeProcess(process_t* proc);
    void yield();
    void sleep(uint64_t ticks);
    void wakeup(process_t* proc);
    void tick();
    void run(uint64_t ticks);
    void dumpState() const;
    void exitProcess(process_t* proc);
    int joinProcess(process_t* proc, uint64_t timeout_ticks);
    void reapZombies();
    process_t* current() const { return current_; }
    process_t* readyHead() const { return ready_.head(); }
    uint64_t idlePid() const { return idle_.pid; }
    scheduler_stats_t* stats() { return &stats_; }
    scheduler_config_t* config() { return &config_; }
private:
    Scheduler() = default;
    process_t* pickNext();
    ProcessList ready_;
    SleepQueue sleep_;
    process_t idle_{};
    process_t* current_ = nullptr;
    scheduler_stats_t stats_{};
    scheduler_config_t config_{};
    uint64_t nextPid_ = 1;
};

} // namespace pasinux::sched
```

### Global operators (in `scheduler.cpp`)

```cpp
// With -fno-exceptions, returning nullptr from operator new is well-defined
void* operator new(size_t s)          { return kmalloc(s); }
void* operator new[](size_t s)        { return kmalloc(s); }
void  operator delete(void* p) noexcept    { kfree(p); }
void  operator delete[](void* p) noexcept  { kfree(p); }
void  operator delete(void* p, size_t) noexcept    { kfree(p); }
void  operator delete[](void* p, size_t) noexcept  { kfree(p); }
```

### `extern "C"` wrappers (in `scheduler.cpp`)

Each C-linkage function from `scheduler.h` becomes a one-line delegate:

```cpp
extern "C" {
    void scheduler_init(void) {
        pasinux::sched::Scheduler::instance().init();
    }
    process_t* create_process(process_entry_t e, const char* n, uint8_t p) {
        return pasinux::sched::Scheduler::instance().createProcess(e, n, p);
    }
    // ... (15 total wrappers)
}
```

### Build system (Makefile)

```makefile
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -fno-rtti -fno-exceptions $(CFLAGS)

# New pattern rule
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
```

The `$(CFLAGS)` in `CXXFLAGS` carries all `-I` include paths. `OBJS` line is unchanged — `sched/scheduler.o` resolves through the new `.cpp` pattern rule.

## Implementation plan

The rewrite follows the structure of the existing `scheduler.c`, systematically replacing each section:

1. **Header setup** — Add `extern "C"` guards to `scheduler.h`. Create `scheduler.hpp` with class declarations.
2. **Global operators** — `operator new`/`delete` at top of `scheduler.cpp`.
3. **ProcessList** — Circular doubly-linked list as a template class. Methods: `insert()`, `remove()`, `contains()`, `empty()`, `head()`, `next()`.
4. **SleepQueue** — Linear singly-linked sleep queue class. Methods: `add()`, `remove()`, `tick()`.
5. **Scheduler class** — Static singleton. Port all logic from `scheduler.c`.
6. **`extern "C"` wrappers** — 15 one-liner delegates.
7. **Makefile** — Add `CXX`, `CXXFLAGS`, `.cpp` pattern rule.
8. **Build and test** — `make && make run` passes with identical output.

## Testing

- `make && make run` — must produce identical output to current C version
- `make syntax` — still works (it checks the C files only)
- `make sanitize` — must still pass (the sanitizer link still uses only C objects)
- The scheduler logic is exercised by the demo (init/worker/idle-demo processes exchanging chess moves over 8 ticks)