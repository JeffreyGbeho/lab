# Building an HTTP server from scratch, in C

The goal is **not** a fast or complete HTTP server. The goal is to understand
every single line. So it is built in very small steps, and each step must be
provable from outside the program with a real tool (`ss`, `curl`, `nc`) before
moving on to the next.

## Where we are

Steps 0 to 2 are done. **Step 3 is next: write a hardcoded response.**

The full plan — every step from an empty file to a server worth calling good — is
in [plan.md](plan.md). It is the source of truth for status and ordering.

## Ground rules

- One step at a time. One commit per step.
- A step is finished when it can be observed from outside, not when it compiles.
- Do not start step N+1 until step N can be explained out loud without looking.
- No comments in the source. Explanations live here and in commit messages.

## The six phases

| Phase | Steps | What it is about |
|-------|-------|------------------|
| 1 — Transport | 0-4 | moving bytes in and out of a socket; HTTP not involved yet |
| 2 — Protocol | 5-9 | giving those bytes meaning: method, path, headers, files |
| 3 — Correctness | 10-14 | fixing the bugs phase 2 deliberately left in place |
| 4 — HTTP/1.1 | 15-18 | what the version string actually promises |
| 5 — Concurrency | 19-23 | more than one client at a time, worst design first |
| 6 — Optional | 24-28 | routing, TLS, benchmarking, tests |

## The real goal

The code is the by-product. The goal is the skill of deciding what to build:

- [Becoming the person who decides](architect-practice.md) — what architects
  actually do, why there is no good design without constraints, how to turn a
  business statement into a number, the question checklist, decomposition as a
  drill, and how to build a library of systems worth copying.
- [Decision records](decisions/README.md) — one file per decision that had a real
  alternative, with what it cost and what would change it.
- [The five questions](thinking-out-loud.md) — the fixed sequence to run on any
  decision (Outcome, Constraint, Options, Door, Bet), worked in full three times
  on unrelated problems so the shape repeats until it is automatic.

## Understanding the code

If the code feels unwritable rather than unreadable, start here:

- [Reading the code](reading-the-code.md) — the whole program explained block by
  block, plus the method: how to read a man page, what a system call is, the five
  idioms this code repeats, how to debug without asking anyone, and the exercise
  that turns reading into writing.
- [The C you actually need here](c-toolkit.md) — the language subset this project
  uses: buffers and `'\0'`, why there are four integer types, `sizeof`, `&` and
  `*`, structs, casts, exit codes, `#include`.

## Documentation

One file per completed step, recording the line-by-line reasoning and the real
terminal output that proved it:

- [Step 0 — a listening socket on port 8080](step-00-listening-socket.md)
- [Step 1 — accept one connection](step-01-accept-a-connection.md)
- [Step 2 — read the request and print it raw](step-02-read-the-request.md)

Plus [glossary.md](glossary.md) for the vocabulary that keeps coming back: file
descriptors, network byte order, value-result arguments, byte stream, backlog,
accept queue, ephemeral port.

## Build and run

```sh
gcc -Wall -Wextra -o server server.c
./server
```

In another terminal:

```sh
ss -ltn | grep 8080        # is the port held?
curl -v localhost:8080     # talk to it
nc localhost 8080          # talk to it by hand
```

`nc` matters more than `curl` from step 10 onward: it is the only easy way to send
a deliberately broken or deliberately slow request.

The compiled `server` binary is gitignored. Only `server.c` is tracked.
