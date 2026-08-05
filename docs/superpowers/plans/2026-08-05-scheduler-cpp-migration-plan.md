# Scheduler C++ Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate the hosted-simulator scheduler from C to C++ with zero caller changes.

**Architecture:** `process_t` stays a C-compatible POD struct with `extern "C"` linkage. A `scheduler.hpp` header declares `ProcessList`, `SleepQueue`, and `Scheduler` classes in `pasinux::sched`. `scheduler.cpp` implements them and provides `extern "C"` wrappers for all C callers.

**Tech Stack:** C++17 with `-fno-rtti -fno-exceptions`, G++/MinGW, no STL.

## Global Constraints

- Zero changes to C callers (`kernel.c`, `kernel.h`, shell code). `process_t` struct layout must be ABI-identical.
- `-fno-rtti -fno-exceptions` — no exception handling or runtime type information.
- No C++ standard library — no `std::vector`, `std::string`, `iostream`, etc.
- Only the hosted scheduler (`sched/scheduler.c` → `.cpp`). The freestanding `sched_fs.c` stays C.
- `operator new`/`delete` backed by `kmalloc`/`kfree`.
- Namespace: `pasinux::sched`.
- All existing C-linkage function names in `scheduler.h` must remain available as `extern "C"`.

---

### Task 1: Set up the C/C++ header boundary

**Files:**
- Modify: `pasinux/pasinux/kernel/sched/scheduler.h`
- Create: `pasinux/pasinux/kernel/sched/scheduler.hpp`

**Interfaces:**
- Consumes: existing `process_t`, `scheduler_stats_t`, `scheduler_config_t` struct definitions
- Produces: `scheduler.hpp` with `pasinux::sched::ProcessList`, `pasinux::sched::SleepQueue`, `pasinux::sched::Scheduler` classes

---

**Step 1.1: Add `extern "C"` guards to `scheduler.h`**

Replace the current header guard section with:

```c
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
```

And at the bottom, before the final `#endif`:

```c
#ifdef __cplusplus
}
#endif

#endif /* SCHEDULER_H */
```

The rest of `scheduler.h` — the `process_t` struct, `scheduler_stats_t`, `scheduler_config_t`, all function declarations — stays **exactly as-is**.

---

**Step 1.2: Create `sched/scheduler.hpp`**

```cpp
#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

#include "scheduler.h"
#include <stdint.h>

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

#endif // SCHEDULER_HPP
```

---

**Step 1.3: Verify the header is valid C++**

Run:
```bash
cd pasinux/pasinux/kernel
g++ -std=c++17 -fno-rtti -fno-exceptions \
    -Iboot -Iarch -Imm -Isched -Idrivers -Inet -Iipc -Ikernel -Iuser -Igui \
    -fsyntax-only sched/scheduler.hpp
```

Expected: no errors or warnings.

---

### Task 2: Write the C++ scheduler implementation

**Files:**
- Create: `pasinux/pasinux/kernel/sched/scheduler.cpp` (replaces `scheduler.c`)
- The old `sched/scheduler.c` should be renamed or deleted

**Interfaces:**
- Consumes: `scheduler.hpp` (classes), `mm.h` (kmalloc/kfree), `<stdio.h>` (printf), `<string.h>` (memset/memcpy)
- Produces: `extern "C"` functions matching all declarations in `scheduler.h`

---

**Step 2.1: Create `sched/scheduler.cpp` with includes, namespace alias, and operator new/delete**

```cpp
#include "scheduler.hpp"
#include "mm.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Global operator new/delete — backed by the kernel's kmalloc/kfree
// -fno-exceptions means returning nullptr on failure is well-defined.
// ---------------------------------------------------------------------------
void* operator new(size_t s)          { return kmalloc(s); }
void* operator new[](size_t s)        { return kmalloc(s); }
void  operator delete(void* p) noexcept    { kfree(p); }
void  operator delete[](void* p) noexcept  { kfree(p); }
void  operator delete(void* p, size_t) noexcept    { kfree(p); }
void  operator delete[](void* p, size_t) noexcept  { kfree(p); }

namespace sched = pasinux::sched;
using sched::Scheduler;
```

---

**Step 2.2: Implement ProcessList methods**

Add these right after the namespace alias:

```cpp
// ---------------------------------------------------------------------------
// ProcessList — circular doubly-linked list over process_t embedded pointers
// ---------------------------------------------------------------------------
void sched::ProcessList::insert(process_t& proc) {
    if (!head_) {
        proc.next = &proc;
        proc.prev = &proc;
        head_ = &proc;
        return;
    }
    process_t* tail = head_->prev;
    proc.next = head_;
    proc.prev = tail;
    tail->next = &proc;
    head_->prev = &proc;
}

void sched::ProcessList::remove(process_t& proc) {
    if (!head_) return;

    // Verify proc is actually in this list
    process_t* cursor = head_;
    bool found = false;
    do {
        if (cursor == &proc) { found = true; break; }
        cursor = cursor->next;
    } while (cursor != head_);
    if (!found) return;

    if (proc.next == &proc) {
        // Only element
        head_ = nullptr;
    } else {
        proc.prev->next = proc.next;
        proc.next->prev = proc.prev;
        if (head_ == &proc) {
            head_ = proc.next;
        }
    }
    proc.next = nullptr;
    proc.prev = nullptr;
}

bool sched::ProcessList::contains(const process_t& proc) const {
    if (!head_) return false;
    const process_t* cursor = head_;
    do {
        if (cursor == &proc) return true;
        cursor = cursor->next;
    } while (cursor != head_);
    return false;
}
```

---

**Step 2.3: Implement SleepQueue methods**

```cpp
// ---------------------------------------------------------------------------
// SleepQueue — singly-linked list of sleeping processes, checked on each tick
// ---------------------------------------------------------------------------
void sched::SleepQueue::add(process_t& proc, uint64_t wake_tick) {
    proc.wake_tick = wake_tick;
    proc.sleep_next = head_;
    head_ = &proc;
}

void sched::SleepQueue::remove(process_t& proc) {
    process_t** cursor = &head_;
    while (*cursor && *cursor != &proc) {
        cursor = &(*cursor)->sleep_next;
    }
    if (*cursor == &proc) {
        *cursor = proc.sleep_next;
    }
    proc.sleep_next = nullptr;
}

void sched::SleepQueue::tick(uint64_t current_tick) {
    process_t* sleep = head_;
    while (sleep) {
        process_t* next = sleep->sleep_next;
        if (sleep->wake_tick <= current_tick) {
            remove(*sleep);
            Scheduler::instance().addProcess(sleep);
        }
        sleep = next;
    }
}
```

---

**Step 2.4: Implement Scheduler singleton and helper methods**

```cpp
// ---------------------------------------------------------------------------
// Scheduler — singleton managing the process lifecycle and scheduling policy
// ---------------------------------------------------------------------------
Scheduler& Scheduler::instance() {
    static Scheduler inst;
    return inst;
}

static void copy_name(char* dst, const char* src) {
    if (!src || !src[0]) src = "process";
    size_t i = 0;
    for (; src[i] && i < 31u; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static void idle_entry(void) {
    printf("[SCHED] idle\n");
}
```

---

**Step 2.5: Implement Scheduler::init() and process creation/destruction**

```cpp
void Scheduler::init() {
    memset(&stats_, 0, sizeof(stats_));
    memset(&idle_, 0, sizeof(idle_));

    idle_.pid = 0;
    idle_.state = PROC_STATE_READY;
    idle_.priority = 0;
    idle_.entry_point = idle_entry;
    idle_.stack_base = nullptr;
    idle_.stack_ptr = nullptr;
    copy_name(idle_.name, "idle");

    current_ = &idle_;
    ready_.remove(idle_);  // ensure clean state
    stats_.processes_created = 1;

    printf("[SCHED] scheduler ready\n");
}

process_t* Scheduler::createProcess(process_entry_t entry, const char* name, uint8_t priority) {
    if (!entry) return nullptr;

    process_t* proc = (process_t*)kcalloc(1, sizeof(process_t));
    if (!proc) return nullptr;

    proc->stack_base = kmalloc(4096u);
    if (!proc->stack_base) {
        kfree(proc);
        return nullptr;
    }

    proc->pid = nextPid_++;
    proc->ppid = current_ ? current_->pid : 0;
    proc->state = PROC_STATE_READY;
    proc->priority = priority;
    proc->entry_point = entry;
    proc->stack_ptr = (unsigned char*)proc->stack_base + 4096u;
    copy_name(proc->name, name);

    addProcess(proc);
    stats_.processes_created++;

    printf("[SCHED] created %s pid=%llu priority=%u\n",
           proc->name, (unsigned long long)proc->pid, proc->priority);

    return proc;
}

void Scheduler::destroyProcess(process_t* proc) {
    if (!proc || proc == &idle_) return;
    removeProcess(proc);
    if (proc->stack_base) kfree(proc->stack_base);
    printf("[SCHED] destroyed %s pid=%llu\n",
           proc->name, (unsigned long long)proc->pid);
    kfree(proc);
    stats_.processes_terminated++;
}
```

---

**Step 2.6: Implement queue management and scheduling logic**

```cpp
void Scheduler::addProcess(process_t* proc) {
    if (!proc || proc == &idle_) return;
    if (ready_.contains(*proc)) return;
    proc->state = PROC_STATE_READY;
    ready_.insert(*proc);
}

void Scheduler::removeProcess(process_t* proc) {
    if (!proc || proc == &idle_) return;
    ready_.remove(*proc);
}

process_t* Scheduler::pickNext() {
    if (ready_.empty()) {
        stats_.idle_time++;
        return &idle_;
    }

    process_t* selected = nullptr;

    if (config_.scheduling_policy == 1) {
        // Strict-priority: pick the highest-priority process with least CPU time
        uint8_t best_pri = 0;
        process_t* cursor = ready_.head();
        do {
            if (cursor->priority > best_pri) best_pri = cursor->priority;
            cursor = cursor->next;
        } while (cursor != ready_.head());

        cursor = ready_.head();
        do {
            if (cursor->priority == best_pri) {
                if (!selected || cursor->cpu_time < selected->cpu_time) {
                    selected = cursor;
                }
            }
            cursor = cursor->next;
        } while (cursor != ready_.head());
    } else {
        // Round-robin: take the head
        selected = ready_.head();
    }

    ready_.remove(*selected);
    stats_.context_switches++;
    return selected;
}
```

---

**Step 2.7: Implement yield, sleep, wakeup, tick**

```cpp
void Scheduler::yield() {
    if (!current_ || current_ == &idle_) return;
    current_->state = PROC_STATE_READY;
    addProcess(current_);
}

void Scheduler::sleep(uint64_t ticks) {
    if (!current_ || current_ == &idle_) return;
    removeProcess(current_);
    current_->state = PROC_STATE_SLEEPING;
    sleep_.add(*current_, stats_.scheduler_ticks + ticks);
}

void Scheduler::wakeup(process_t* proc) {
    if (!proc || proc->state != PROC_STATE_SLEEPING) return;
    sleep_.remove(*proc);
    addProcess(proc);
}

void Scheduler::tick() {
    stats_.scheduler_ticks++;
    sleep_.tick(stats_.scheduler_ticks);
}
```

---

**Step 2.8: Implement run() — the core scheduling loop**

```cpp
void Scheduler::run(uint64_t ticks) {
    for (uint64_t i = 0; i < ticks; i++) {
        tick();

        process_t* next = pickNext();
        process_t* old = current_;
        current_ = next;
        current_->state = PROC_STATE_RUNNING;
        current_->cpu_time++;

        // Context switch via setjmp/longjmp
        if (old && old->pc == 0) {
            old->pc = setjmp(old->context);
        }

        next->pc = setjmp(current_->context);

        if (old && old->pc != 0 && current_->pc != 0 && old->pc == 0) {
            longjmp(current_->context, 1);
        }

        if (current_->entry_point) {
            current_->entry_point();
        }

        if (current_ != &idle_) {
            stats_.total_process_time++;
            if (current_->state == PROC_STATE_RUNNING) {
                exitProcess(current_);
            }
        }
    }
}
```

---

**Step 2.9: Implement exit, join, reap, dump, and accessor methods**

```cpp
void Scheduler::exitProcess(process_t* proc) {
    if (!proc || proc == &idle_) return;
    printf("[SCHED] %s pid=%llu exiting\n",
           proc->name, (unsigned long long)proc->pid);
    removeProcess(proc);
    proc->state = PROC_STATE_ZOMBIE;

    // Orphan children to idle
    process_t* cursor = ready_.head();
    if (cursor) {
        do {
            if (cursor->ppid == proc->pid) {
                cursor->ppid = idle_.pid;
            }
            cursor = cursor->next;
        } while (cursor != ready_.head());
    }
}

void Scheduler::reapZombies() {
    process_t* cursor = ready_.head();
    if (!cursor) return;
    do {
        process_t* next = cursor->next;
        if (cursor->state == PROC_STATE_ZOMBIE) {
            printf("[SCHED] reaping zombie %s pid=%llu\n",
                   cursor->name, (unsigned long long)cursor->pid);
            destroyProcess(cursor);
        }
        cursor = next;
    } while (cursor && cursor != ready_.head());
}

int Scheduler::joinProcess(process_t* proc, uint64_t timeout_ticks) {
    if (!proc || proc == &idle_) return -1;
    for (uint64_t t = 0; t < timeout_ticks; t++) {
        if (proc->state == PROC_STATE_ZOMBIE) {
            printf("[SCHED] joining %s pid=%llu\n",
                   proc->name, (unsigned long long)proc->pid);
            if (proc->stack_base) kfree(proc->stack_base);
            kfree(proc);
            stats_.processes_terminated++;
            return 0;
        }
        tick();
    }
    printf("[SCHED] join %s pid=%llu timed out (state=%u)\n",
           proc->name, (unsigned long long)proc->pid, proc->state);
    return -1;
}

void Scheduler::dumpState() const {
    printf("[SCHED] ticks=%llu switches=%llu created=%llu terminated=%llu idle=%llu work=%llu\n",
           (unsigned long long)stats_.scheduler_ticks,
           (unsigned long long)stats_.context_switches,
           (unsigned long long)stats_.processes_created,
           (unsigned long long)stats_.processes_terminated,
           (unsigned long long)stats_.idle_time,
           (unsigned long long)stats_.total_process_time);

    printf("[SCHED] ready queue:");
    if (ready_.empty()) {
        printf(" empty\n");
        return;
    }
    const process_t* cursor = ready_.head();
    do {
        printf(" %s(pid=%llu,pri=%u,state=%u)",
               cursor->name,
               (unsigned long long)cursor->pid,
               cursor->priority,
               cursor->state);
        cursor = cursor->next;
    } while (cursor != ready_.head());
    printf("\n");
}
```

---

**Step 2.10: Add the `extern "C"` wrapper block at the end of `scheduler.cpp`**

```cpp
// ---------------------------------------------------------------------------
// C-linkage wrappers — these are what kernel.c and the shell actually call
// Every function declared in scheduler.h has a matching wrapper here.
// ---------------------------------------------------------------------------
extern "C" {

void scheduler_init(void) {
    Scheduler::instance().init();
}

process_t* create_process(process_entry_t entry, const char* name, uint8_t priority) {
    return Scheduler::instance().createProcess(entry, name, priority);
}

void destroy_process(process_t* process) {
    Scheduler::instance().destroyProcess(process);
}

void scheduler_add_process(process_t* process) {
    Scheduler::instance().addProcess(process);
}

void scheduler_remove_process(process_t* process) {
    Scheduler::instance().removeProcess(process);
}

void scheduler_yield(void) {
    Scheduler::instance().yield();
}

void scheduler_sleep(uint64_t ticks) {
    Scheduler::instance().sleep(ticks);
}

void scheduler_wakeup(process_t* process) {
    Scheduler::instance().wakeup(process);
}

void scheduler_tick(void) {
    Scheduler::instance().tick();
}

void scheduler_run(uint64_t ticks) {
    Scheduler::instance().run(ticks);
}

process_t* scheduler_get_current(void) {
    return Scheduler::instance().current();
}

scheduler_stats_t* get_scheduler_stats(void) {
    return Scheduler::instance().stats();
}

void scheduler_dump_state(void) {
    Scheduler::instance().dumpState();
}

process_t* scheduler_get_ready_head(void) {
    return Scheduler::instance().readyHead();
}

process_t* process_get_next(const process_t* process) {
    return process ? process->next : nullptr;
}

uint64_t scheduler_get_idle_pid(void) {
    return Scheduler::instance().idlePid();
}

scheduler_config_t* scheduler_get_config(void) {
    return Scheduler::instance().config();
}

void process_exit(process_t* proc) {
    Scheduler::instance().exitProcess(proc);
}

int process_join(process_t* proc, uint64_t timeout_ticks) {
    return Scheduler::instance().joinProcess(proc, timeout_ticks);
}

void process_reap_zombies(void) {
    Scheduler::instance().reapZombies();
}

} // extern "C"
```

---

**Step 2.11: Remove old scheduler.c**

Run:
```bash
rm pasinux/pasinux/kernel/sched/scheduler.c
```

Verify it's gone:
```bash
ls pasinux/pasinux/kernel/sched/
```
Expected: `scheduler.h`, `scheduler.hpp`, `scheduler.cpp`, `.gitkeep`

---

### Task 3: Update Makefile and verify end-to-end build

**Files:**
- Modify: `pasinux/pasinux/kernel/Makefile`

---

**Step 3.1: Add CXX, CXXFLAGS, and .cpp pattern rule to Makefile**

Insert these lines after the `CFLAGS` block (around line 3):

```makefile
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -fno-rtti -fno-exceptions $(CFLAGS)
```

And add the pattern rule for `.cpp` files after the existing `%.o: %.c` rule (around line 52):

```makefile
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
```

---

**Step 3.2: Full build**

Run:
```bash
cd pasinux/pasinux/kernel
make clean && make
```

Expected: compiles without warnings or errors. The linker produces `kernel_sim.exe`.

---

**Step 3.3: Run the demo**

Run:
```bash
make run
```

Expected output must be identical to the old C version:
```
[KERNEL] pasinux kernel core starting
[MM] heap ready: 1048576 bytes
[SCHED] scheduler ready
[DRIVER] registered console
[DRIVER] driver core ready
[IPC] ipc ready
[SCHED] created init pid=1 priority=10
[SCHED] created worker pid=2 priority=5
[SCHED] created idle-demo pid=3 priority=1
...
[SCHED] ticks=8 switches=8 created=3 terminated=0 idle=0 work=8
[MM] allocations=6 frees=0 current=12672 peak=12672 failed=0
[KERNEL] shutdown complete
```

---

**Step 3.4: Verify other build targets still work**

```bash
make syntax     # should pass (checks C files only)
make sanitize   # should build and link
```

---

**Step 3.5: Commit**

```bash
git add -A
git commit -m "feat: migrate scheduler from C to C++

Rewrite sched/scheduler.c as sched/scheduler.cpp with:
- ProcessList template class for the ready queue
- SleepQueue class for sleeping processes
- Scheduler singleton class wrapping all scheduling logic
- operator new/delete backed by kmalloc/kfree
- extern \"C\" wrappers for all C callers (zero caller changes)
- pasinux::sched namespace for symbol scoping

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---