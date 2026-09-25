# The plan

Every tiny task, from an empty file to a server worth calling good. This file is
the single source of truth for the project's shape and status.

The order is not arbitrary. Each phase can only be understood once the previous
one is in the hands, and several steps exist purely to *break* something built
earlier so the reason for the next step becomes obvious.

**How to read the tables.** *Proof* is the command that decides whether the step
is finished — a step is done when it can be observed from outside the program, not
when it compiles. *Key idea* is what the step is really teaching; the code is just
the delivery mechanism.

---

## Phase 1 — Transport: moving bytes

Nothing in this phase knows what HTTP is. It is pure socket work: how bytes get
into the process and back out. At the end of the phase there is a server that
answers every request with the same hardcoded reply, forever.

| Step | Task | Proof | Key idea |
|------|------|-------|----------|
| 0 ✅ | `socket` + `bind` + `listen`, then sleep | `ss -ltn \| grep 8080` shows `LISTEN 0 16` | the four calls, and that listening is not accepting |
| 1 ✅ | `accept` one connection, print the client, close, exit | `curl` says `Empty reply from server` | `accept` returns a **second** socket; the kernel builds connections without us |
| 2 ✅ | `read` into a buffer and print it raw | the real HTTP request text appears on screen | a request is just bytes; seeing the protocol for the first time |
| 3 | `write` a hardcoded response | `curl localhost:8080` prints a body | `\r\n`, the status line, and why `Content-Length` decides if a browser hangs |
| 4 | wrap `accept`/`read`/`write` in an infinite loop | three `curl` runs in a row are all answered | where the loop starts; which socket to close and which to keep |

**Trap planted here:** step 2 calls `read()` once and pretends that returned a
whole request. It did not. Step 10 is the repair.

---

## Phase 2 — Protocol: giving bytes meaning

The bytes stop being opaque. This phase turns the text into a method, a path and
headers, and makes the response depend on the request. At the end there is a
static file server that works for well-behaved clients.

| Step | Task | Proof | Key idea |
|------|------|-------|----------|
| 5 | parse the request line into method, path, version | echo the parsed path back in the body | tokenising on spaces and CRLF, in C, without a string library |
| 6 | route: `/` and `/hello`, anything else 404 | `curl -i localhost:8080/nope` shows `404` | status codes as the response's type system |
| 7 | parse headers into key/value pairs | `curl` and print the `Host` header back | the header block, the empty line that ends it, case-insensitive names |
| 8 | map a path to a file on disk and send it | `curl localhost:8080/index.html` returns the file | `open`/`read`/`fstat`, `Content-Length` from the file size, 404 on missing |
| 9 | read a request body | `curl -d "name=jeff" localhost:8080` prints the body | `Content-Length` is what tells you when to *stop* reading |

**Milestone.** After step 9 the server genuinely serves a website to a browser.
This is the point where it starts feeling real — and the point where it is still
quietly broken in half a dozen ways.

---

## Phase 3 — Correctness: making it true instead of lucky

Everything so far works because `curl` is polite and localhost is fast. This phase
is where the toy becomes a program. Each step here fixes a bug that is *already
present* in phase 2, which is why phase 2 must exist first.

| Step | Task | Proof | Key idea |
|------|------|-------|----------|
| 10 | read in a loop until `\r\n\r\n` is found | send a request one byte at a time with `nc` — still answered correctly | **the byte-stream trap, confronted**: `read` returns whatever arrived, not a message |
| 11 | write in a loop; ignore `SIGPIPE` | send a large file; kill `curl` mid-download and the server survives | `write` is also partial; a dead client must not kill the server |
| 12 | real error responses: 400, 405, 414, 505 | `printf 'GARBAGE\r\n\r\n' \| nc localhost 8080` returns 400, not a crash | a server is defined by how it handles input it did not expect |
| 13 | path safety and decoding | `curl --path-as-is localhost:8080/../../etc/passwd` is refused | directory traversal, percent-decoding, stripping the query string |
| 14 | `Content-Type` by extension, and `HEAD` | a browser renders CSS as CSS; `curl -I` returns headers with no body | why the browser needs to be told what it is looking at |

**Milestone.** After step 14 the server is safe to point a real browser at and
hard to crash on purpose. This is the honest definition of "it works".

---

## Phase 4 — HTTP/1.1 for real

Phase 2 implemented a subset that happens to be enough. This phase implements
what the version string `HTTP/1.1` actually promises.

| Step | Task | Proof | Key idea |
|------|------|-------|----------|
| 15 | persistent connections (keep-alive) | one `curl` fetching two URLs opens only one TCP connection (`ss` while it runs) | why HTTP/1.0 was slow; `Connection: close`; per-connection read timeouts |
| 16 | chunked transfer encoding | a response with no known length still terminates correctly | how to send a body whose size is not known in advance |
| 17 | conditional requests: `Last-Modified` / `If-Modified-Since`, 304 | a second `curl -H 'If-Modified-Since: ...'` returns 304 with no body | caching as a protocol feature, not a server feature |
| 18 | `Date` and `Server` headers, access logging, graceful shutdown on `SIGINT` | `Ctrl-C` finishes the in-flight response before exiting | a server is an operated thing, not only a running thing |

---

## Phase 5 — Concurrency: more than one client at a time

Up to here the server handles exactly one client at a time: client B waits until
client A is completely finished. The fix is deliberately attempted three times,
worst design first, because comparing them is the lesson.

| Step | Task | Proof | Key idea |
|------|------|-------|----------|
| 19 | demonstrate the problem | one `nc` that connects and stays silent blocks every other client | head-of-line blocking; the cost of blocking `read` |
| 20 | `fork` one process per connection | the silent `nc` no longer blocks anyone | process isolation, and reaping zombies with `SIGCHLD` |
| 21 | one thread per connection | same result, far less memory per client | shared address space, and what now needs a mutex |
| 22 | one thread, non-blocking sockets, `poll` then `epoll` | hundreds of idle connections held with one thread | the event loop — how real servers are actually built |
| 23 | limits and timeouts | a slowloris-style trickle of bytes gets dropped instead of held | resource exhaustion is the attack you get for free |

**Milestone.** After step 22 the architecture is the same one nginx uses. Steps
19-21 exist to make step 22 feel inevitable rather than clever.

---

## Phase 6 — Optional, once the core is solid

Pick by curiosity, not order. None of these teach more about HTTP than phase 3
did; they are about building on top of a working server.

| Step | Task | Key idea |
|------|------|----------|
| 24 | a real router: method + path table, path parameters | separating protocol from application |
| 25 | dynamic handlers, or CGI | where a web *application* begins |
| 26 | HTTPS by linking OpenSSL | TLS as a layer; **never** hand-rolled |
| 27 | benchmark with `wrk`, profile, then compare against nginx | measuring instead of guessing |
| 28 | a test suite that replays raw request bytes over a socket | testing a protocol rather than a function |

---

## Deliberately out of scope

Naming these keeps the project finite:

- **HTTP/2 and HTTP/3** — binary framing and QUIC are a different project.
- **Writing TLS by hand** — educational, but the wrong thing to trust.
- **Performance work before phase 3** — optimising an incorrect server is a way
  of avoiding the hard part.
- **A framework, config files, virtual hosts** — features of a product, not
  lessons about HTTP.

## Where the real difficulty sits

Worth knowing in advance, so the easy stretches are not mistaken for progress:

1. **Step 10** — reading until the request is genuinely complete. The single most
   important idea in the whole project, and the one that separates a demo from a
   server.
2. **Step 12** — handling malformed input without crashing. Tedious, unglamorous,
   and the actual job.
3. **Step 22** — the event loop. The design is simple; unlearning blocking code is
   not.

Everything else is comparatively mechanical once the previous step is understood.
