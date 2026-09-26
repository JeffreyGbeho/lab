# Becoming the person who decides

Notes on the gap between "I can write code when someone tells me what to write" and
"I decide what gets written". Written against this project, because the skill is only
learnable on something real.

---

## 1. Architects do not invent

The belief that blocks most technicians is that an architect stares at a problem and
produces a design out of nothing. That is not what happens. Three learnable things
happen instead.

**They ask a fixed set of questions.** Not a different flash of insight each time —
the same interrogation, applied to whatever is in front of them. The checklist is in
§4. Most of what looks like insight is someone asking "what happens when this is
slow?" out of habit while everyone else is discussing the happy path.

**They pattern-match against systems they have taken apart.** What reads as invention
is recall. Nobody in this project will invent the event loop at step 22; it will be
copied, as everyone copies it, from designs that already exist. The difference
between a senior and a junior is mostly the size of that library and the accuracy of
the matching. Libraries are built deliberately (§6), not accumulated by luck.

**They own a tradeoff out loud.** A technician asks *what should I do*. An architect
says *here is what I would do, here is what it costs, here is what would change my
mind*. The second sentence is the whole job. It is also why the transition is
frightening: the skill being added is accountability, not knowledge.

The uncomfortable corollary: while someone else answers the questions, decides the
order and picks the tradeoffs, no amount of writing their code builds this. Executing
someone else's decisions well is a different skill, and being excellent at it does
not convert.

---

## 2. There is no good design, only fit to constraints

The question "what is the best architecture for X" has no answer, and confident
answers to it are bluffing. Take this very project, imagined as a real product:

| Constraint | Right answer | Wrong answer |
|------------|--------------|--------------|
| Internal tool, 10 requests a day, one developer | `fork` per connection (step 20) | `epoll` — weeks spent, nothing gained |
| 100k concurrent connections | `epoll` (step 22) | `fork` — collapses in the low thousands |
| Must ship Friday | install nginx, write nothing | any line of this project |
| Must be understood by the author | this project, all 29 steps | nginx |

Same problem. Opposite answers. Nothing about the *problem* decided it — the
constraints did.

So the architect's first move is never a design. It is extracting the constraints,
and that is where "business rules" stop being a separate topic: **business rules are
the constraints.** Latency budget, cost ceiling, team size, tolerance for being
wrong, ship date, who maintains it after you leave.

### Turning a business statement into a constraint

Business statements arrive as solutions in disguise. The skill is asking "why" until
a number appears.

> "We need real-time updates on the dashboard."

- *Why?* — "Users complain the data is stale."
- *How stale is too stale?* — "If it is more than a minute old they do not trust it."

The constraint is **60 seconds of freshness**. That single number kills the WebSocket
design, permits polling, removes a persistent-connection infrastructure from the
project, and saves a month. Nobody lied; "real-time" was a guess at a solution, and
the actual requirement was a number nobody had said out loud.

The move: **never accept a stated solution as a requirement.** Ask what breaks if it
is absent, and keep asking until the answer contains a number, a cost, or a named
consequence. A requirement you cannot measure is not yet a requirement.

---

## 3. Decide fast or decide slowly, depending on the door

Not all decisions deserve equal care, and treating them equally is a common way to be
slow and wrong simultaneously.

**Two-way doors** — reversible cheaply. Buffer size. Directory layout. Which logging
format. Decide in minutes, write it down, move on. Deliberating over these is a way
of avoiding real work, and the cost of being wrong is one afternoon.

**One-way doors** — expensive or impossible to undo. The data model. The public API
other teams build against. The concurrency model, once thousands of lines assume it.
Anything that becomes someone else's dependency. These deserve the full checklist, a
written record, and a second opinion.

The practical test: *if this is wrong, what does it cost to change in six months?* An
afternoon, or a quarter? Most decisions are afternoons and get treated like quarters.
The few that are quarters usually get decided in a meeting nobody wrote down.

In this project: the buffer decision ([0002](decisions/0002-fixed-4096-byte-request-buffer.md))
is a two-way door and took one page. The concurrency model at steps 19–22 is a
one-way door — it shapes every function signature in the codebase — and deserves far
more.

---

## 4. The checklist

The portable part of this document. Not all of it applies every time; running through
it out loud takes five minutes and is the single highest-yield habit available.

### Load and failure

- What breaks **first** as this grows 10×? 100×? (There is always a first thing. Name it.)
- What happens when a dependency is **slow** rather than down? Slow is worse than
  down: down fails fast, slow exhausts every resource waiting.
- What is the failure mode — fail fast, degrade, queue, retry? Who chose, and is a
  retry storm possible?
- What happens if this restarts **mid-operation**? What is left half-done?

### Data

- What is the source of truth? What is derived, and can it be rebuilt from scratch?
- What must be consistent *right now*, and what may be stale? For how long exactly?
- What happens if the same message arrives twice?

### Boundaries

- What is the interface, and what is therefore free to change behind it?
- If this were split in two, which side does each piece of state live on? (State
  placement is usually the real design; the code follows.)
- What is explicitly **out** of scope? An unnamed boundary is not a boundary.

### Cost of being wrong

- Reversible, or a one-way door?
- When this is wrong, how far does the damage reach?
- What would I have to see to change my mind? (If nothing, it is a belief.)

### Operations

- Who debugs this at 3am with no context, and what do they need in the logs?
- What is measured to know it is working — before a user reports it?

### Simplicity

- What is the simplest thing that could possibly work?
- What am I building for a requirement nobody actually stated?

---

## 5. Decomposition is a drill, not a talent

Turning a vague goal into ordered, verifiable pieces is the most practical of these
skills, and the easiest to practise: every task is a rep.

The procedure:

1. **State the goal as an observable outcome.** Not "build a server" but "curl prints
   hello". If it cannot be observed, it cannot be finished.
2. **List what must be true for that outcome.** Dependencies only, no design.
3. **Order by dependency.** What does nothing else depend on? That is first.
4. **Write the verification before the work.** For each piece: what command or test
   says it is done? This step is the one everybody skips, and skipping it is why
   projects have a 90%-complete phase that lasts a month.
5. **Find the riskiest assumption.** If it is wrong, does the rest become pointless?
   If yes, test it first even though dependency order says otherwise. (For *learning*,
   order by dependency. For *delivery*, order by risk. Knowing which mode you are in
   matters.)
6. **Cut everything not needed for the next observable outcome.**

### Worked example: the first attempt at this project

The original decomposition, before any code:

> Task: Listener on port 8080 to know when you receive a new request
> Task: Open a TCP connection with the client
> Task: Read the received request. Find the method, path, headers, protocol
> Task: Return the response with protocol version, status code, status message and headers

What was right, and it is most of it: decomposed at all rather than "write a server";
ordered by data flow, which here matches dependency order; scoped small; no invented
requirements.

What was wrong is specific and instructive:

- **One concept sliced in two.** "Listener on port 8080" and "open a TCP connection"
  are `listen()` and `accept()` — two halves of one idea, and in the wrong order, since
  the client opens the connection, not the server.
- **Two concepts fused into one.** "Read the request" *and* "find the method, path,
  headers" are steps 2 and 5, separated by three others. Getting bytes and giving them
  meaning are different problems with different failure modes.
- **No verification anywhere.** Four tasks, no way to know any of them is done. This
  is the missing step 4 above, and it is the difference between a list and a plan.
- **Missing whatever is not in the happy path.** No sending, no closing, no loop, no
  malformed input, no second client. The happy path was decomposed; the other 80% of
  a server was invisible.

That last one is the most transferable diagnosis: **decomposing the happy path and
calling it a plan.** The checklist in §4 exists to catch exactly that, which is why it
is mostly questions about things going wrong.

---

## 6. Build the library deliberately

Pattern-matching needs patterns. They come from taking working systems apart and
asking one question of each: **what constraint made them choose this?**

- **nginx** — event loop, few processes. Read its architecture page after step 22 and
  compare against what this project built. Constraint: tens of thousands of mostly
  idle connections.
- **SQLite** — a database with no server process, in one file. Constraint: it must run
  where nothing can be installed, on a phone, inside another program.
- **Redis** — single-threaded on purpose, and fast because of it. Constraint:
  operations are short and memory-bound, so coordination costs more than it saves.
- **Unix pipes** — small programs, one text stream between them. Constraint: composition
  by people who never met.

Then the most underrated source: **public postmortems.** Cloudflare, GitHub, AWS all
publish them. They are the only writing that honestly answers "what breaks first",
and they are free. Reading one a week builds failure intuition faster than any book,
because each one is a real system meeting a constraint its designers missed.

One book, if there is only one: *Designing Data-Intensive Applications*. It is
structured as tradeoffs rather than solutions, which is the correct shape.

The habit that compounds: every time a tool is used at work, spend twenty minutes on
its architecture document and name the tradeoff it took. That is one pattern a week,
fifty a year.

---

## 7. What changes in this project from here

The decisions in this codebase were being made silently, by the mentor, and the
author was executing them — which trains precisely the skill that is already strong.
So the loop inverts.

**For each remaining step, the author goes first.** Before any code:

1. What is the observable outcome, and what command proves it?
2. What are the two or three ways this could be built, and what does each cost?
3. What is the riskiest assumption in the chosen one?
4. What does this make hard later?

Then critique of the *design*, then the code. The design being wrong is the point;
being wrong on paper costs nothing and is where the reps come from.

**Every real decision gets a record** in [decisions/](decisions/README.md). Two are
written as worked examples. Five more are listed there, already made silently in the
code, and writing them up is an exercise with real alternatives in each case.

### The architecture decisions this project still contains

Not busywork — every one of these has a genuine alternative and a real cost:

| Step | Decision | The tension |
|------|----------|-------------|
| 3 | What the response layer looks like | build the string by hand, or a struct plus a serialiser? |
| 5 | Parse in place, or copy out? | zero-copy pointers into the buffer are fast and dangerous |
| 6 | Routing as `if`/`else`, or a table? | at what number of routes does the chain stop being right? |
| 8 | Where does "HTTP" end and "application" begin? | the layering question, and the one that transfers furthest |
| 10 | Read loop: state machine, or read-until-complete? | keep-alive at step 15 will punish the wrong answer |
| 12 | Error handling: `exit`, return codes, or an error struct? | supersedes [0003] and touches every function signature |
| 19–22 | Concurrency model | a one-way door; it shapes every signature in the codebase |

Step 8 is the one worth slowing down for. "Where does the protocol stop and the
application start" is the same question as every service boundary, every API design,
every module split. Getting it wrong here is cheap and the lesson is identical.

---

## 8. What AI actually changed

AI is very good at producing code from a specification, and weak at deciding what to
build, which constraints dominate, and whether the result is correct. So the value
moved toward framing the problem, extracting constraints, decomposing, and verifying
— which is this document.

But the trap is worth naming precisely, because it is the current one:

- **AI as explainer** trains dependency. The explanation is always available, so the
  lookup habit never forms, and recognition is mistaken for ability.
- **AI as reviewer and sparring partner** trains judgement. Propose the design first,
  get it attacked, defend or fold. Same tool, opposite outcome.

The second mode requires going first, and going first requires being willing to be
wrong in front of something that will say so. That discomfort is the entire price of
admission, and it is cheaper than it feels.
