# Reading the code

Written for someone who can read C but would not know what to type into an empty
file. The aim is not to explain these 80 lines once — it is to leave behind a
*method* for understanding any systems code, so the next 80 lines can be worked out
without help.

Companion document: [c-toolkit.md](c-toolkit.md) covers the language mechanics
(pointers, `sizeof`, types, structs, casts). This one covers the program.

---

## 1. The reframe: this program barely does anything

Run the server under `strace`, which prints every system call a process makes:

```sh
strace -e trace=socket,setsockopt,bind,listen,accept,read,close ./server
```

```
socket(AF_INET, SOCK_STREAM, IPPROTO_IP) = 3
setsockopt(3, SOL_SOCKET, SO_REUSEADDR, [1], 4) = 0
bind(3, {sa_family=AF_INET, sin_port=htons(8080), sin_addr=inet_addr("0.0.0.0")}, 16) = 0
listen(3, 16)                           = 0
accept(3, {sa_family=AF_INET, sin_port=htons(35092), sin_addr=inet_addr("127.0.0.1")}, [16]) = 4
read(4, "GET / HTTP/1.1\r\nHost: localhost:"..., 4095) = 77
close(4)                                = 0
close(3)                                = 0
```

**Eight lines. That is the entire program.** Everything else in `server.c` is error
checking and printing.

This is the reframe that makes systems programming approachable: *the program is a
list of requests to the kernel.* It does not implement TCP. It does not touch the
network card. It does not manage a connection. The kernel does all of that. The
program's job is to ask for the right things in the right order.

So "I could not have written this" is a smaller problem than it feels like. Nothing
here was invented. What is needed is knowing which calls exist, in what order, and
how to read their documentation.

`strace` is worth reaching for constantly while learning. It is ground truth: not
what the code looks like it does, but what it actually asked the kernel for.

---

## 2. What a system call actually is

A process cannot touch hardware. It cannot write to a network card, read a disk
sector, or see another process's memory. The CPU enforces this in hardware: user
code runs in a restricted mode where those instructions are unavailable.

That is not a limitation, it is the only reason a multi-user machine works. If any
program could write to the network card directly, any program could read anyone
else's traffic.

So there is exactly one door: the **system call**. The process puts a number and
some arguments in registers and executes a special instruction. The CPU switches to
kernel mode, the kernel does the privileged work, and control returns with a result.

Consequences that explain the shape of this code:

- **A file descriptor is not a pointer to anything you own.** It is an index into a
  kernel-side table. The socket lives in the kernel. `3` is a ticket number. This is
  why you cannot inspect a socket by dereferencing it, and why every operation on
  it is a call rather than a field access.
- **Syscalls are comparatively expensive** — a mode switch each time. This is why
  `read`-ing one byte at a time works but is slow, and why buffers exist.
- **Syscalls can fail for reasons outside the program.** The port is taken, the
  client vanished, the machine is out of descriptors. Failure is normal input, not
  an exceptional event. Hence the error check on every single call.

`man 2 read` — section 2 is *system calls*. `man 3 printf` — section 3 is *library
functions*, ordinary code running in your process. Knowing which section a thing
lives in tells you whether it crosses that boundary. `printf` is section 3, but it
calls `write` (section 2) underneath, which is why `strace` shows a `write` you never
typed.

---

## 3. The senior skill: reading a man page

This is the habit that separates someone who needs the code explained from someone
who explains it. There is no memorisation involved; there is a document, and a way
to read it.

First, install them — they are not there by default on Debian/Ubuntu:

```sh
sudo apt install manpages-dev manpages-posix-dev
```

Then `man 2 read`. Every page has the same sections, and two of them carry almost
all the value.

### SYNOPSIS — the shape

```c
ssize_t read(int fd, void *buf, size_t count);
```

Read it right to left and the whole function is there:

- `count` is `size_t` — unsigned, so "how many bytes at most", never negative.
- `buf` is `void *` — an address, type unspecified; it will write bytes there.
- `fd` is `int` — a descriptor.
- the return is `ssize_t` — **signed**, so it can return `-1`. That single letter
  `s` is the documentation telling you the call can fail.

Learning to read a signature this way means never having to be told what a function
does. The types say it.

### RETURN VALUE — the part that is skipped and should not be

For `read`:

> On success, the number of bytes read is returned (zero indicates end of file)...
> On error, -1 is returned, and `errno` is set appropriately.

Three outcomes, and the middle one is the trap: **`0` is not an error.** It means
end of stream — the peer closed. Code that treats `0` as failure reports phantom
errors; code that treats `-1` as a count corrupts its buffer. Every `read` in
production code branches three ways, and this paragraph is where you learn that.

Compare with `accept`, whose RETURN VALUE says it returns a *new* file descriptor.
That one sentence is the entire lesson of [step 1](step-01-accept-a-connection.md) —
available for free, before writing a line.

### ERRORS — the list of realities

`EADDRINUSE` on `bind` is why `SO_REUSEADDR` exists. `EINTR` on `read` means a
signal interrupted it and it should be retried. `EAGAIN` is the foundation of the
whole event loop in step 22. Skimming this section before writing code prevents most
of the bugs that the code would otherwise be written to discover.

**The practice:** before using any unfamiliar call, read SYNOPSIS and RETURN VALUE.
Two minutes. That is the habit; everything else is a consequence of it.

---

## 4. The five idioms this file repeats

`server.c` has 80 lines but only five distinct patterns. Recognising them turns a
wall of text into five things.

### Idiom 1 — call, check, report, exit

```c
int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
if (listen_fd < 0) {
    perror("socket");
    exit(1);
}
```

Appears five times, identically. The convention: **negative means failure**, and the
reason is in the global `errno`.

`perror("socket")` prints your label, a colon, and the human-readable text for the
current `errno` — `socket: Permission denied`. The argument is a label to tell you
*which* call failed, not a description.

Why check every call, when nothing here can realistically fail? Because in C nothing
warns you. There is no exception to propagate. An unchecked failure returns `-1`,
execution continues, and `-1` gets used as a file descriptor — producing a confusing
failure hundreds of lines later, in the wrong place. **The check is not defensive
padding; it is how the error gets reported at all.**

This is the single most important habit in C, and the most commonly skipped.

### Idiom 2 — fill a struct, pass its address and its size

```c
struct sockaddr_in addr;
memset(&addr, 0, sizeof(addr));
addr.sin_family = AF_INET;
...
bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
```

Declare, zero, fill, then pass `&addr` **with** `sizeof(addr)`. The address and the
size always travel together, because the cast to `struct sockaddr *` destroys the
type information and the size is how the kernel recovers it. See
[c-toolkit §6](c-toolkit.md#6-the-cast-struct-sockaddr-).

### Idiom 3 — the value-result argument

```c
socklen_t client_len = sizeof(client_addr);
accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
```

`client_len` goes *in* as "this is how much room I have" and comes *out* as "this is
how much I used". That is why it is `&client_len` and not `client_len`: the kernel
must be able to write to it.

Spotting this pattern in an unfamiliar API is a real skill — the tell is a
non-const pointer to a size sitting next to a buffer.

### Idiom 4 — fixed buffer, reserve one byte, terminate

```c
char buf[4096];
ssize_t n = read(conn_fd, buf, sizeof(buf) - 1);
buf[n] = '\0';
```

The `- 1` guarantees `n <= 4095`, so `buf[n]` is always inside the array. `read` does
not terminate anything, so the terminator is added by hand, and only then may the
bytes be treated as a string.

Three lines, three separate C truths: buffers have no length, sizes must be tracked
by hand, and strings are a convention rather than a type.

### Idiom 5 — everything is a file descriptor

`read`, `write` and `close` are not socket functions. They work on any descriptor:
files, pipes, terminals, sockets. That is why `<unistd.h>` supplies them rather
than `<sys/socket.h>`.

The practical payoff: the code that handles a socket in step 8 will also read a file
from disk, using the same calls.

---

## 5. The program, block by block

Eighty lines in seven blocks. Each links to the step that introduced it, where the
reasoning lives in full.

### Lines 1–7 — includes

Textual pastes of declarations, so the compiler knows the names. The table of which
header provides what is in [c-toolkit §8](c-toolkit.md#8-include--what-it-actually-does).

### Line 9 — `int main(void)`

Takes no arguments; returns the process exit status, where `0` means success.

### Lines 10–14 — create the socket

```c
int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
```

IPv4, TCP byte stream, default protocol. Returns a descriptor — `3`, because 0, 1
and 2 are stdin, stdout and stderr. Attached to no address yet, so unreachable.
→ [step 0](step-00-listening-socket.md)

### Lines 16–20 — allow immediate rebinding

```c
int opt = 1;
setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

Options are set by passing the *address* of a value plus its size, because the same
function sets options of many different types — the same address-plus-size design as
`bind`. `opt = 1` means "on".

Without it, restarting the server within about a minute fails with
`bind: Address already in use`, because the kernel holds the old connection in
`TIME_WAIT`. → [step 0](step-00-listening-socket.md)

### Lines 22–31 — describe the address and claim it

```c
struct sockaddr_in addr;
memset(&addr, 0, sizeof(addr));
addr.sin_family      = AF_INET;
addr.sin_addr.s_addr = htonl(INADDR_ANY);
addr.sin_port        = htons(8080);
bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
```

`memset` because `sin_zero` must be zero. `INADDR_ANY` is `0.0.0.0`, meaning "any
interface on this machine". `htons`/`htonl` convert to network byte order — omit
them and the port silently becomes 36895. → [glossary](glossary.md#network-byte-order)

After `bind`, the socket has an address. Verify from outside, never by assumption:

```sh
ss -ltn | grep 8080
```

### Lines 33–36 — start queueing connections

```c
listen(listen_fd, 16);
```

Flips the socket to accepting mode and sets the backlog to 16. From here the
**kernel** completes TCP handshakes on its own and parks finished connections in a
queue, whether or not the program ever collects them.
→ [step 0](step-00-listening-socket.md#the-experiment-worth-doing)

### Lines 40–52 — collect one connection

```c
struct sockaddr_in client_addr;
socklen_t client_len = sizeof(client_addr);
int conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
```

Blocks until a connection is in the queue, removes one, and returns **a new
descriptor** — `4`. `listen_fd` is untouched and keeps producing more.

`client_addr` is filled in by the kernel, the reverse direction from `addr` above:
same struct type, opposite role. It is *not* the client; the client, in this program,
is `conn_fd`. → [step 1](step-01-accept-a-connection.md), [glossary](glossary.md#client)

`inet_ntoa` and `ntohs` convert the address back to human form for printing, because
everything inside that struct is in network byte order.

### Lines 54–75 — read the bytes and print them

```c
char buf[4096];
ssize_t n = read(conn_fd, buf, sizeof(buf) - 1);
buf[n] = '\0';
```

Reads from `conn_fd` — never `listen_fd`, which carries no data. `n` is `ssize_t`
because `read` can return `-1`. → [step 2](step-02-read-the-request.md)

The loop that follows prints the buffer a second time with `\r` and `\n` shown as
escapes, because `\r\n\r\n` — the marker that ends the header block — is invisible
otherwise. `buf[i]` is how a single byte of an array is read; `putchar` writes one
byte.

**This block contains a deliberate bug.** One `read` is treated as a whole request.
It is not: `read` returns whatever had arrived. Step 10 fixes it; the bug stays until
then so the fix has a reason.

### Lines 77–79 — hang up

```c
close(conn_fd);
close(listen_fd);
return 0;
```

`close(conn_fd)` ends this client's connection. `close(listen_fd)` gives up the port.
Confusing the two is the classic step-4 bug: closing `listen_fd` inside the loop
kills the server after one client.

---

## 6. How to answer your own questions

The goal is to stop needing an explanation. Six tools, in the order they are usually
worth reaching for.

| Question | Tool |
|----------|------|
| What does this call do, and what can it return? | `man 2 <name>` — SYNOPSIS and RETURN VALUE |
| What is the program *actually* doing? | `strace -e trace=<calls> ./server` |
| What is the state of the network? | `ss -ltn`, `ss -tn` |
| What is in this variable? | `printf` it. Unglamorous, fastest, always available |
| Why did it crash, and where? | `gcc -g`, then `gdb ./server`, then `bt` |
| Am I reading or writing out of bounds? | `valgrind ./server`, or `gcc -fsanitize=address` |

Two things that are not optional:

**Compile with `-Wall -Wextra` always.** The compiler already knows about the wrong
format specifier, the unused variable, the comparison between signed and unsigned.
Warnings are free bug reports; a project should build with zero of them.

**`errno` is only meaningful immediately after a failed call.** The next call
overwrites it. Check it, or `perror` it, *first* — before any other call, including
`printf`.

---

## 7. From reading to writing

Reading code produces **recognition**: it looks sensible, it follows. Writing code
requires **production**: staring at an empty file and knowing what goes first. These
are different skills, and only the second one matters when it is your turn. Reading
more explanation does not build it.

The exercise that does, and it is uncomfortable on purpose:

1. **Read this file and the step docs once.** Close them.
2. **Open an empty file and type the program from memory.** Man pages allowed;
   `server.c` not. Expect to get stuck after three lines the first time.
3. **When stuck, look up the call in `man`, not in `server.c`.** The point is
   practising the lookup, which is the real skill, rather than copying.
4. **Compile. Read every error. Fix them one at a time.** Compiler errors are the
   feedback loop; being fluent at reading them is most of being fluent in C.
5. **Diff yours against `server.c`** only at the very end. Every difference is a
   thing you now know you did not know.
6. **Delete yours and do it again tomorrow.** Second attempt is usually half the
   time. Third is usually fluent.

Three rounds of that and this program is yours. It is 80 lines and eight system
calls — genuinely within reach in an evening, which is not true of most code that
feels this intimidating.

Two habits that compound from here:

**Break it on purpose.** Delete `buf[n] = '\0';` and watch what happens. Remove
`htons` and find the port with `ss`. Drop `SO_REUSEADDR` and restart twice quickly.
Comment out a `close`. Each broken version teaches more than the working one,
because a line whose absence you have *seen* is a line you understand.

**Predict, then verify.** Before running anything, say out loud what the output will
be. When it differs, the gap is exactly the thing worth learning. This is the whole
method, and the [Checks](step-02-read-the-request.md#checks) at the end of each step
doc exist to force it.
