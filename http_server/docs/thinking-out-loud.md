# The five questions

A fixed sequence, same order, every time, regardless of domain. The value is in
the repetition of the *shape*, not in memorising any particular answer below —
every worked example here reaches a different conclusion using the identical five
steps.

1. **OUTCOME** — what observable thing proves this is done?
2. **CONSTRAINT** — why do we actually need this? Keep asking "why" until a number
   or a named consequence appears. Never accept a stated solution as the
   requirement — "real-time" is not a requirement, "under 90 seconds" is.
3. **OPTIONS** — at least two honest paths, with the real cost of each stated
   plainly. An option written only to be dismissed means the decision was already
   made before this step ran.
4. **DOOR** — reversible, or not? This answer decides how long steps 2-5 are
   allowed to take. A two-way door gets minutes. A one-way door gets an hour and a
   written record in [decisions/](decisions/README.md).
5. **BET** — what is chosen, what it costs, and what fact would reverse it. "What
   would change my mind" is the test that separates a decision from a belief: a
   belief has no answer to it.

This is a compression of the fuller checklist in
[architect-practice.md §4](architect-practice.md#4-the-checklist). That one is for
when there is time to be thorough. This one is for running in your head in under a
minute, on anything, until it is automatic.

---

## Rep 1 — retroactive: the 4096-byte buffer

Already decided in [0002](decisions/0002-fixed-4096-byte-request-buffer.md).
Reconstructed here the way it should have been reasoned the first time.

**Outcome.** A real client's request lands fully in the buffer, and no client can
use the buffer to exhaust server memory. Two outcomes, in tension — hold onto that,
it is not resolved until the bet.

**Constraint.** Why a buffer at all? `read()` needs somewhere to write bytes. Why
4096 and not 40, or 40,000? "Big enough for a request" is not a number, it is a
hope. Pushing further: a typical `GET` is 200-800 bytes; a browser carrying a
session cookie can reach 2-4KB. The real constraint is "cover realistic browser
traffic, without letting one client claim unbounded memory" — the second half is
the part a lazier pass at this question would have missed entirely.

**Options.** (a) Fixed stack buffer — cheap, but truncates anything larger,
silently, unless truncation is explicitly detected. (b) Heap buffer that grows via
`realloc` — handles any size, costs a free path on every exit route and, in C
realistically, an eventual leak. (c) Stop guessing and check what production
already does: nginx defaults to 8KB for request headers. This is not really a
decision to invent from nothing — it has already been made by people who got
paged when they got it wrong.

**Door.** Two-way. Wrong, it is a one-line change with nothing downstream
depending on the exact number. Spend five minutes, not fifty.

**Bet.** 4096, fixed, on the stack — the same order of magnitude as nginx,
converting "unbounded memory per client" into a visible, explicit limit rather
than an accident. Cost accepted: a real browser request can be silently truncated
right now, a live bug until it becomes an explicit `413` at step 12. Reverses the
moment a real browser request is actually observed getting truncated — which
should be expected and gone looking for, not waited on.

---

## Rep 2 — a business decision, no code at all

*"We need real-time notifications when a job finishes."*

**Outcome.** Unstated. Nobody said what would prove "real-time" was delivered.
That absence is itself the first finding: as stated, this cannot be verified, so
it is not yet a requirement.

**Constraint.** Why "real-time"? Asked: "users keep refreshing the page to
check." Asked again: how long can they wait without refreshing? Pushed further:
"if it shows up within a minute or two they're fine — they just hate that there's
*no* update, ever." The number was hiding behind a word. The actual constraint is
roughly 90 seconds of latency, not "real-time" — a completely different
engineering problem.

**Options.** (a) WebSocket, a persistent connection, true push, sub-second
latency. (b) Poll every 20 seconds. (c) Poll every 20 seconds, only while the tab
is visible. Cost of (a): a stateful connection per active user, reconnection
logic, a new failure mode when the socket server itself goes down, weeks of
work. Cost of (b): trivial, slightly wasteful of requests, shippable today.

**Door.** Two-way, and the cheap option is also the reversible one: if polling
feels laggy later, upgrading to WebSockets does not discard the polling code's
value, it only replaces the transport underneath it. Cheap now and reversible
later is the strongest possible signal toward the cheap option.

**Bet.** Poll every 20 seconds. Meets the actual constraint with a five-times
margin and ships today; every future measurement is evidence not yet in hand.
Reverses the moment real usage data shows users still manually refreshing
*despite* the poll — which would mean the complaint was never latency, and that
would need diagnosing before reaching for WebSockets.

---

## Rep 3 — forward, live: step 3's response

**Outcome.** `curl -v localhost:8080` prints a status line, at least one header,
and a chosen body, then exits cleanly — instead of step 1 and 2's
`Empty reply from server`.

**Constraint.** Why does a response need a status line and headers at all,
minimally? Look at what a real server actually sent, captured with
`curl -sv https://example.com` — that is the floor, not a guess at it: a status
line (`HTTP/1.1 200 OK`), a `Content-Length`, probably a `Content-Type`, a blank
line, the body. Pushed on "why": the client needs to know when the body ends.
That is the one header that is not optional — without `Content-Length`, over a
real connection, the client waits for bytes that will never arrive.

**Options.** (a) Hardcode the entire response as one string literal, byte for
byte. (b) Build it from pieces at write time — status line, headers, body — with
`Content-Length` computed from the body's actual length. (c) A small struct with
fields, serialised just before sending. Cost of (a): trivial, but
`Content-Length` is pasted in by hand and rots silently the moment the body
changes. Cost of (b): one step more honest — the length cannot drift because it
is computed, not typed. Cost of (c) is real engineering, and the right shape
eventually, but it solves a problem — multiple response types, reuse — that does
not exist yet at step 3.

**Door.** Two-way. Nothing downstream depends on how this is built internally,
only on the bytes that hit the wire.

**Bet.** Option (b) — a body as a string, a status line and headers built with
`snprintf` referencing `strlen(body)` rather than a hardcoded number. Cost
accepted: more typing than one literal. What it buys: `Content-Length` genuinely
cannot be wrong, which matters because a wrong `Content-Length` is the most common
way a hand-rolled HTTP response silently breaks — some clients hang, some
truncate. Reverses toward option (c) the moment step 6 needs more than one
distinct response, since duplicated inline construction is the signal that a
struct has become worth its cost.

---

## The handoff

Three reps, three domains, one skeleton. What is worth keeping is not any of the
three conclusions — it is that every time, "why" got pushed until a number or a
named consequence appeared, and every option got its real cost stated before
anything was chosen.

Step 3 above is modelled; take it as given. **Step 5 — parsing the request line —
runs cold, before any code is written**, using this exact sequence. Where the
reasoning breaks is the useful signal, not whether the conclusion matches anyone
else's. If the shape has not held after one attempt, that is normal — run it
again on the next decision rather than assume one pass was enough.
