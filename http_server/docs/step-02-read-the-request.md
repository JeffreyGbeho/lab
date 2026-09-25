# Step 2 — Read the request and print it raw

## Goal

Call `read()` on `conn_fd` and print what comes out. No parsing, no response.
The point is to see, for the first time, what a client actually sends.

## The code

```c
char buf[4096];
ssize_t n = read(conn_fd, buf, sizeof(buf) - 1);
if (n < 0) {
    perror("read");
    exit(1);
}
buf[n] = '\0';
```

### `char buf[4096];`

A fixed buffer on the stack. 4096 is arbitrary — a page size, and comfortably
larger than a typical request. "What if the request is bigger?" is a real question
with a real answer, and it is deferred to step 12 (`414 Request-URI Too Long`).

### `read(conn_fd, buf, sizeof(buf) - 1)`

Reads from **`conn_fd`**, the client's socket — not `listen_fd`, which never
carries data. The `- 1` reserves space for the terminator added below.

### `ssize_t n`

The *signed* size type, signed because `read` returns `-1` on failure. Three
outcomes must be distinguished:

| return value | meaning |
|--------------|---------|
| `> 0` | this many bytes are now in the buffer |
| `0` | **end of stream**: the client closed their end. Not an error |
| `-1` | a real error; inspect `errno` |

The `0` case becomes important at step 4, where it is how a client hanging up is
detected. Treating `0` as an error there produces spurious failures; treating `-1`
as data produces a buffer with `-1` bytes in it.

### `buf[n] = '\0';`

The line everybody forgets once. **`read()` does not null-terminate.** It is not a
string function; it copies bytes and has no idea they are destined to be treated as
a C string. Without this, `printf("%s", buf)` walks past the data into whatever is
on the stack.

Reading `sizeof(buf) - 1` guarantees `n <= 4095`, so `buf[n]` is always in bounds.
That is the whole reason for the `- 1`.

## Proof

```
$ ./server
listening on port 8080 (listen_fd = 3)
client connected from 127.0.0.1:57798 (conn_fd = 4)
read() returned 77 bytes
----- raw -----
GET / HTTP/1.1
Host: localhost:8080
User-Agent: curl/8.5.0
Accept: */*

----- end -----
----- with line endings made visible -----
GET / HTTP/1.1\r\n
Host: localhost:8080\r\n
User-Agent: curl/8.5.0\r\n
Accept: */*\r\n
\r\n
----- end -----
```

**77 bytes** — the same number that was visible sitting unread in the kernel's
receive buffer back in [step 0](step-00-listening-socket.md#the-experiment-worth-doing).
Those were always these bytes.

The first conclusion is the simplest and the most freeing: **HTTP is a text
format.** There is no binary framing and nothing hidden. Anything that can print a
string can speak it.

## Anatomy of the request

```
GET / HTTP/1.1\r\n          <- request line: method, path, version
Host: localhost:8080\r\n    <- headers, one "Name: value" per line
User-Agent: curl/8.5.0\r\n
Accept: */*\r\n
\r\n                        <- empty line: headers are over
                            <- body would start here (GET has none)
```

- **Request line** — three tokens separated by single spaces. All of step 5.
- **Headers** — `Name: value`, one per line. curl sends `Host` first; in HTTP/1.1
  that header is mandatory, because one IP address can serve many domains.
- **The empty line** — the single most important byte sequence in the request, and
  invisible without the second dump: `\r\n\r\n`. Two CRLFs in a row mean "no more
  headers". Everything after it is body.

## Why `\r\n` and not `\n`

`\r` is carriage return (13), `\n` is line feed (10). HTTP requires **both, in that
order**, at the end of every line — inherited from teletypes, where the carriage
returned and then the paper advanced, and kept by every internet text protocol
since.

The practical consequence arrives at step 5: **split on `\r\n`, never on `\n`
alone.** Splitting on `\n` leaves an invisible trailing `\r` on every value, and
`"localhost:8080\r"` does not compare equal to `"localhost:8080"` — while looking
completely identical in a terminal. That is the reason this step prints the dump
with escapes made visible.

## The lie left in this code, on purpose

This step calls `read()` exactly once and then treats the result as a whole
request. It is not. Sending the same request in two pieces:

```
$ { printf 'GET / HTTP/1.1\r\n'; sleep 1; printf 'Host: localhost\r\n\r\n'; } | nc -q0 localhost 8080
```

```
read() returned 16 bytes
GET / HTTP/1.1\r\n
```

Sixteen bytes. The `Host` header never arrived, because the server read once and
gave up while the client was still typing.

> **`read()` does not return "a request". It returns whatever bytes had arrived by
> the time it was called.**

It may be 16 bytes, or 77, or — if a client pipelines two requests — more than one
request in a single buffer. A browser request carrying cookies can easily arrive in
several pieces over a real network. The 77-byte read above *looked* complete purely
because curl is fast, localhost has no latency, and the request fit in one packet.

This is the [byte stream](glossary.md#byte-stream) property promised back in step 0,
arriving as a concrete bug. Every real server reads **in a loop** until it has seen
`\r\n\r\n`. That is step 10, and the bug stays in place until then on purpose: the
fix means nothing to somebody who has not watched it fail.

## Checks

1. `read()` returned 77. What are the two other kinds of value it can return, and
   what does each mean?
2. Why `sizeof(buf) - 1` rather than `sizeof(buf)`?
3. What goes wrong if `buf[n] = '\0';` is deleted, and why is the bug intermittent
   rather than constant?
4. What exactly marks the end of the headers, and why is it invisible in the first
   dump?
5. A client sends a 200-byte request that arrives in three TCP segments. How many
   bytes does this code print, and how many should it?
