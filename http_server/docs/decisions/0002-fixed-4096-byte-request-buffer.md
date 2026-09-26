# 0002 — Read requests into a fixed 4096-byte stack buffer

- **Status:** accepted
- **Date:** 2026-09-25

## Context

Step 2 needs somewhere to put the bytes that arrive from a client. In C that
decision must be made explicitly and early, because it determines who owns the
memory, how failure is handled, and what happens when input exceeds expectations.

Constraints and facts:

- A typical HTTP request line plus headers is 200–800 bytes. A browser request with
  cookies can reach 2–4 KB. The protocol sets no limit at all.
- Real servers impose one anyway: nginx defaults to 8 KB for request headers,
  Apache to 8 KB. So the limit is not a shortcut — it is what production does.
- At this step there is one client and one request, and neither streaming nor
  concurrency exists yet.
- An oversized request must eventually produce `431` or `414`, not a crash.

## Options

**A. Fixed buffer on the stack, `char buf[4096]`.** Zero allocation, zero
bookkeeping, nothing to free, impossible to leak. Cannot grow: a larger request is
truncated, and truncation is silent unless explicitly detected. Stack lifetime ends
with the function, so the buffer cannot outlive the call that made it — which will
matter as soon as the code is split into functions.

**B. Heap buffer that grows, `malloc` then `realloc`.** Handles any size. Costs a
free path on every exit route, a growth policy, an allocation-failure branch, and
the possibility of a leak or a dangling pointer. In C the leak is the realistic
outcome, not a hypothetical one.

**C. Ring buffer.** The right answer once connections are long-lived and partially
read — step 15's keep-alive, or a state machine that resumes mid-request. Materially
more code, and solves a problem that does not exist for another thirteen steps.

## Decision

Option A, 4096 bytes on the stack.

The reason is that the number is not a guess: it is one page, and it is within a
factor of two of what nginx ships. A fixed limit on request size is not a
simplification that must later be removed — it is the production answer, because the
alternative is letting any client allocate unbounded memory on the server. Option B
would be *more* code implementing a *worse* policy.

Option C is deferred rather than rejected. It becomes correct at step 15, and that
is a different decision made with information not available yet.

## Consequences

**Easy:** no allocation, no `free`, no leak, no allocation-failure path. One line
declares it. The code stays readable while attention is on sockets.

**Hard — and not yet handled:** a request larger than 4095 bytes is silently
truncated. `read` fills the buffer, the parser sees a request with no `\r\n\r\n`,
and behaviour is undefined. This is a real bug, present from step 2, and it must
become an explicit `413`/`414` response at step 12.

**Breaks later:** the buffer is a local in `main`. The moment request handling moves
into its own function, this buffer cannot simply be returned — it dies with its
frame. That forces a choice at step 8 or so: pass the buffer in from the caller, or
move to the heap. Passing it in is the answer, and it is worth noticing that *this*
decision is what creates that constraint.

**Accepted risk:** 4096 is comfortable for curl and for handmade `nc` requests, and
tight for a real browser carrying cookies. Testing with an actual browser before
step 12 will probably expose the truncation, which is a good outcome — the bug shows
up under a realistic client rather than in production.

## Revisit when

- A real browser request is truncated. Expected around step 8, when a browser is
  first pointed at the server.
- Step 12 implements size limits, at which point the truncation must become a status
  code and the limit must be enforced, not assumed.
- Step 15 introduces keep-alive, where one buffer may hold part of a request plus the
  start of the next, and option C becomes correct.
