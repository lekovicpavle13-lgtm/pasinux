Write or update the README.md for the GitHub repository lekovicpavle13-lgtm/pasinux.

STEP 0 — VERIFY BEFORE WRITING (always do this first, don't skip it):
Don't trust the "baseline facts" below as current truth — they're a snapshot.
Before writing anything:
1. Read the repo's existing README.md. Treat it as your starting point: keep
   what's still accurate, rewrite only what's changed. Don't regenerate from
   a blank slate if a working README already exists.
2. Read operator-handoff.md at the repo root AND pasinux/operator-handoff.md.
   These are the project's own status/roadmap notes and are the source of
   truth for what's actually done vs. pending — if they disagree with the
   README's roadmap or status line, the handoff docs win.
3. Read pasinux/kernel/README.md (kernel-core build notes) and reconcile any
   build/run details there with the top-level README.
4. Compare .github/workflows/c-cpp.yml against pasinux/kernel/Makefile.
   Last known state: the workflow runs an Autotools pipeline (./configure,
   make, make check, make distcheck) but the repo only ships a plain
   Makefile, so CI fails as committed. Check if this has been fixed; update
   or remove the CI warning accordingly — don't repeat it if it's stale.
5. Skim kernel.c, mm.c/h, scheduler.c/h, driver.c/h, ipc.c/h, and boot.asm
   for anything not reflected below — a VGA driver, interrupt handlers, or
   boot.asm wired into a freestanding build would flip roadmap items from
   unchecked to checked and probably add a new Features bullet.
If you don't have direct repo access in this session (e.g. you're a plain
chat model without file/browse tools), ASK the user to paste the current
README.md, both operator-handoff.md files, and the CI workflow file rather
than silently reusing the snapshot below.

BASELINE FACTS (last verified 2026-07-20 — confirm, don't assume):
- pasinux: hobby x86_64 OS kernel in C and assembly. Currently a hosted C
  simulator so memory management, scheduling, drivers, and IPC can be built
  and debugged before it becomes a real freestanding, bootable kernel.
- Status: early-stage/active. C sources build and run as a userspace
  simulator (`kernel_sim`); boot.asm is a valid placeholder, not yet wired in.
- Memory management: heap allocator over a static 1 MiB arena — first-fit
  search, 16-byte aligned blocks, splitting on alloc, coalescing on free.
  kmalloc/kcalloc/krealloc/kfree, plus live stats (current/peak usage,
  alloc/free/failure counts).
- Scheduler: circular ready queue, priorities (LOW/NORMAL/HIGH), states
  (READY/RUNNING/SLEEPING/ZOMBIE), sleep/wakeup queue, time-slice
  preemption, round-robin or strict-priority policy, process_exit(),
  stats (context switches, idle vs work time, created/terminated counts).
- Drivers: minimal registry (char/block/net/input) via driver_ops_t, console
  driver wired in at boot.
- IPC: priority message queue, exercised via a small chess protocol (moves,
  resignations, draw offers, board state).
- Boot sector: valid legacy BIOS boot.asm, reserved for future freestanding build.
- CI: GitHub Actions workflow scaffolded but currently mismatched with the
  actual Makefile (see Step 0.4).
- Structure:
  pasinux/
  ├── .github/workflows/c-cpp.yml
  ├── .gitignore
  ├── LICENSE
  ├── operator-handoff.md          # project status & roadmap notes
  └── pasinux/
      ├── operator-handoff.md      # kernel-core status notes
      └── kernel/
          ├── boot.asm
          ├── kernel.c
          ├── mm.c / mm.h
          ├── scheduler.c / scheduler.h
          ├── driver.c / driver.h
          ├── ipc.c / ipc.h
          ├── Makefile
          └── README.md
- Build: `cd pasinux/kernel && make`, or
  `gcc -std=c11 -Wall -Wextra -Wpedantic -g -o kernel_sim kernel.c mm.c scheduler.c driver.c ipc.c`
- Run: `make run` — boots the simulator, spawns init (high priority, sends a
  chess move over IPC), worker (normal, replies), idle-demo (low, shows
  scheduler idling); drains IPC queue; prints scheduler + memory stats.
- Other targets: `make syntax`, `make clean`.
- Roadmap as of last check:
  [x] Heap allocator with splitting/coalescing
  [x] Scheduler with sleep/wakeup, preemption, priority policy option
  [ ] VGA text-mode driver
  [ ] Interrupt handlers
  [ ] Wire boot.asm into a freestanding build + QEMU testing
  [ ] Fix CI workflow to match the actual Makefile
- License: MIT. Author: lekovicpavle13-lgtm.

OUTPUT REQUIREMENTS:
1. One-paragraph summary + a blockquoted "Status" line up top.
2. H2 sections: Features, Project Structure, Getting Started
   (Prerequisites/Build/Run/Other targets/Continuous Integration),
   Architecture, Roadmap, License, Author.
3. Features as bullets, bold lead term per bullet, technical register — no
   marketing filler ("blazing fast," "easy to use," etc.).
4. Project structure as a fenced tree. Build/run commands as fenced,
   copy-pasteable shell blocks.
5. Continuous Integration note must be honest about whether CI currently
   passes, based on what you verified in Step 0.4 — not assumed.
6. Roadmap as GitHub checkboxes, completed items first.
7. If updating an existing README, preserve its structure and heading order
   unless something you verified in Step 0 justifies changing it. Don't
   reorder or rewrite sections that are still accurate just for style.
8. Don't invent features, benchmarks, contributors, badges, or links not
   confirmed in Step 0. If unsure, omit rather than guess.
9. Tone: precise, low-level systems-programming register, written for
   kernel/OS hobbyists. No emoji.
10. Output the README.md content only — no commentary inside it.
11. Immediately after the README, add a short separate "Changes made" note
    (not part of the README) listing exactly what you changed and which
    source file justified each change, so the user can sanity-check the
    diff before committing.
