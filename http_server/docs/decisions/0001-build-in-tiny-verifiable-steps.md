# 0001 — Build in tiny steps, each provable from outside the program

- **Status:** accepted
- **Date:** 2026-09-24

## Context

The goal of this project is understanding, not a working server. A working server
already exists — nginx — and could be installed in ten seconds. So the only thing
this project can produce that has value is knowledge in one person's head.

That reframes the constraint. The scarce resource is not time to write code; it is
*attention per line*. Anything that produces working code while leaving the author
unable to explain it is a failure, even though it compiles.

Relevant facts:

- The author can read C but has not written socket code.
- The author's stated weakness is decomposing a problem, not typing code.
- An HTTP server has a natural dependency order: no response without a connection,
  no parsing without bytes.

## Options

**A. Write the whole server, then read through it.** Fastest to working code. The
failure mode is the one this project exists to avoid: 300 lines that look sensible
and cannot be reproduced from a blank file. Also gives no feedback signal — when
something does not work there are 300 lines of suspects.

**B. Split by component: a socket module, a parser module, a response module.** The
way a working codebase is organised. But each module is independently untestable
until the others exist, so nothing runs until nearly everything is written. Wrong
for learning: the reward and the feedback both arrive at the end.

**C. Split by observable behaviour — each step adds one thing and must be provable
from outside the program before the next begins.** Slower. Produces some throwaway
code (`pause()` in step 0 exists only to be deleted). Steps do not map to modules,
so the code is rearranged repeatedly.

## Decision

Option C. Ordered by dependency, and each step ends with a command — `ss`, `curl`,
`nc` — whose output decides whether the step is done.

The reason is the feedback loop. A step that can be checked from outside takes the
verdict away from the author's own judgement, which is precisely the judgement being
built and therefore cannot be trusted yet. "It compiles" and "it looks right" are
both worthless signals at this stage. `ss -ltn | grep 8080` is not.

The secondary reason: with one change per step, a failure has exactly one suspect.
That is worth more than any amount of care while writing.

## Consequences

**Easy:** every step has an unambiguous done-condition. Bugs have one suspect.
Progress is visible. Each step is a commit, so the history is a narrative rather
than a snapshot.

**Hard:** the code is knowingly wrong for long stretches. Step 2 reads once and
pretends that is a whole request; the fix waits until step 10. That is deliberate —
a bug that has been *watched* teaches what a precaution cannot — but it means the
repository contains code that must not be copied, and the docs must say so loudly at
each point.

**Costly:** roughly 29 steps where a competent developer would write four files in a
day. The ratio of explanation to code is about 30:1. Accepted, because the code is
the by-product and the explanation is the product.

**Given up:** any sense of what the finished architecture looks like while in the
middle of it. Steps are behaviour-shaped, not module-shaped, so the design only
emerges at phase 3. Mitigated by [plan.md](../plan.md) holding the whole shape from
the start.

## Revisit when

- A step takes more than one sitting to finish, which means it was not one step.
- Two consecutive steps need no code change, which means they were one step.
- The author can predict a step's outcome correctly before running it, twice in a
  row. At that point the steps are too small and should be merged.
