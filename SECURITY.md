# Security Policy

## What this project is

pasinux is a hobby, educational x86 kernel. It is **not hardened, not
audited, and not intended to run on hardware or in environments where a
crash, memory-safety bug, or arbitrary code execution would have real
consequences.** There is no threat model where pasinux is expected to
resist a malicious actor — it boots in QEMU, VirtualBox, or on
throwaway hardware for the purpose of learning and building.

That said, real memory-safety bugs are still worth knowing about,
especially in the code paths that parse **untrusted input**:

- **`net/`** — Ethernet/ARP/IPv4/TCP parsing (`net_eth.c`, `net_arp.c`,
  `net_ip.c`, `net_tcp.c`) and the HTTP client (`http.c`) all parse bytes
  that came off the wire, unauthenticated and unvalidated by anything
  upstream.
- **`fs/fat12.c`** — parses the on-disk FAT12 boot sector, FAT table,
  and directory entries. A malformed or crafted disk image is untrusted
  input in the same sense a malformed network packet is.
- **`drivers/ata.c`** — raw sector reads feeding directly into the FS
  parser above.

Buffer overflows, out-of-bounds reads/writes, integer overflows, or
unchecked lengths in any of these paths are the kind of thing worth a
report — not because pasinux is deployed anywhere sensitive, but because
they're real bugs and this is exactly the code that's supposed to get
more robust over time.

## Supported versions

pasinux does not maintain release branches or backport fixes. Only the
tip of `main` (and the most recent tagged release, once one exists) is
supported. If you find an issue, please check whether it still
reproduces on the latest `main` before reporting.

| Version | Supported |
|---|---|
| `main` (latest) | ✅ |
| Older commits / older tags | ❌ |

## Reporting a vulnerability

Please use GitHub's private vulnerability reporting instead of opening a
public issue, so there's a window to fix things before details are
public:

**[Report a vulnerability](https://github.com/lekovicpavle13-lgtm/pasinux/security/advisories/new)**
(repo → Security tab → "Report a vulnerability")

If that's not available for some reason, open a regular issue with as
little exploit detail as possible and a note that you have a security
concern to discuss privately, and it'll get followed up on.

When reporting, it helps to include:

- Which subsystem (network stack, FAT12, ATA driver, scheduler, etc.)
- A minimal reproduction — a crafted packet, disk image, or input that
  triggers the issue
- What you observed (crash, hang, out-of-bounds access, incorrect
  behavior) vs. what you expected
- Whether you've confirmed it in QEMU, VirtualBox, or real hardware

## What to expect

This is a solo hobby project maintained in spare time, not a funded
project with an SLA. There's no guaranteed response time, but reports
on the untrusted-input parsing paths above (network stack, FAT12) will
get real attention since they're the most realistic bug class for a
kernel like this. Reports about things like "the shell doesn't have
privilege separation" or "there's no ASLR" will likely be
acknowledged as known, out-of-scope-for-now limitations rather than
treated as vulnerabilities — pasinux doesn't claim those properties yet.

## Out of scope

- Lack of memory protection between ring-3 processes (no per-process
  address space isolation yet — tracked separately as a feature gap,
  not a vulnerability)
- Denial of service via resource exhaustion in a hobby kernel with no
  resource limits
- Anything requiring physical access to a machine already running pasinux
- Social engineering, phishing, or anything not about the code itself
