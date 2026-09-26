# Decision records

One file per decision that had a real alternative. Numbered, append-only, never
deleted — a decision that turned out wrong is superseded by a later one, and both
stay. The wrong ones are the valuable ones.

## Why this is the single highest-leverage habit

Architecture judgement is not built by reading about architecture. It is built by
making a decision, writing down what you expected, and later finding out whether you
were right. Without the written prediction there is no feedback, and without feedback
nothing accumulates — ten years of experience becomes one year, ten times over.

An hour spent writing one of these is worth more than a week of reading, because it
forces three things that are easy to skip:

1. **Naming the alternatives.** If you cannot name what you did *not* choose, you did
   not decide — you defaulted. Most "architecture" is undetected defaulting.
2. **Stating the consequence you accept.** Every choice costs something. Writing the
   cost down converts a vague preference into a real tradeoff.
3. **Saying in advance what would change your mind.** This is the test of whether a
   decision is understood. If nothing could change it, it was a belief, not a choice.

Re-reading these after three months is where the learning actually lands. You will
find decisions that were obviously right, decisions that were wrong for reasons you
could have anticipated, and decisions that were wrong for reasons nobody could have
anticipated. Telling those three apart *is* seniority.

## The format

Short. Half a page. If it takes an hour to write, the decision is not clear enough
to make yet.

```markdown
# NNNN — <the decision, as a statement>

- **Status:** accepted | superseded by NNNN | revisited
- **Date:** YYYY-MM-DD

## Context

What situation forces a choice. Facts and constraints only — no solution yet.
The constraints are the important part: what actually limits this?

## Options

Each real option, with its cost. At least two, honestly stated — an option
written only to be dismissed is a sign the decision was already made.

## Decision

What was chosen, and the reason. One paragraph.

## Consequences

What this makes easy, what it makes hard, and what breaks later because of it.
Be specific enough to be embarrassing if wrong.

## Revisit when

The concrete trigger that should reopen this. A number, an event, a step.
```

## A warning about this format

It is easy to write these as justifications after the fact — picking the option
already taken and inventing reasons. That produces a tidy document and teaches
nothing.

The value is in writing it *before* the code, while the outcome is genuinely
unknown, and in stating the consequence precisely enough that reality can later
contradict it. A prediction that cannot be wrong is not a prediction.

## Records

| # | Decision | Status |
|---|----------|--------|
| [0001](0001-build-in-tiny-verifiable-steps.md) | Build in tiny steps, each provable from outside the program | accepted |
| [0002](0002-fixed-4096-byte-request-buffer.md) | Read requests into a fixed 4096-byte stack buffer | accepted |

### Still to be written

These decisions were already made in the code, silently, without being recorded —
which is exactly the habit this directory exists to break. Writing them up is an
exercise, not a formality: in each case the alternatives are real and the reasoning
is not obvious.

| # | Decision made silently | Why it is worth writing |
|---|------------------------|-------------------------|
| 0003 | `exit(1)` on every error | Fine in 80 lines. What breaks once one client's bad request must not kill the server? |
| 0004 | Everything in one `server.c` | When does this stop being right? What is the first seam that should become a module, and why that one? |
| 0005 | One client at a time, blocking | Deliberately wrong from step 4 to step 19. What makes a knowingly wrong choice legitimate? |
| 0006 | Phase order: transport, protocol, correctness | Correctness-first was equally defensible. What does this ordering optimise for, and what does it cost? |
| 0007 | HTTP/2 and hand-written TLS out of scope | A boundary is a decision. What makes this one right, and what would move it? |
