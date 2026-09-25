# Glossary

Vocabulary that keeps coming back in this project.

## File descriptor

A small non-negative integer that indexes the kernel's table of open things for a
process. Not a pointer, not a handle to a struct — just an index.

Every process starts with three already open:

| fd | name | |
|----|------|--|
| 0 | stdin | |
| 1 | stdout | what `printf` writes to |
| 2 | stderr | what `perror` writes to |

So the first socket created is normally fd 3, the next 4, and so on. A closed fd
is reused: after `close(4)`, the next new socket can be 4 again.

Because files, pipes, terminals and sockets are all file descriptors, the same
`read()` / `write()` / `close()` calls work on all of them. That uniformity is
why a network server in C looks like file I/O.

## Client

A **role**, not a kind of program. The client is whoever called `connect()`; the
server is whoever called `listen()` and `accept()`. That is the whole distinction,
and it only applies while the connection is being set up. Afterwards TCP is
perfectly symmetric: both ends hold a file descriptor and both use the same
`read()` / `write()` / `close()` calls. Nothing on the wire marks which end is
which, and `ss` prints one row per end with identical shape.

Three different things get called "the client", and only the third one exists
inside the program:

| What | Where it lives | What can be done with it |
|------|----------------|--------------------------|
| the client *program* (curl, `nc`, a browser, a script) | another process, maybe another machine | nothing — it is never seen, its name and PID are unknowable |
| the *connection* | in the kernel, identified by the 4-tuple `client_ip:port -> server_ip:port` | nothing directly; the kernel owns it |
| `conn_fd`, the *handle* on that connection | this process only | `read`, `write`, `close` |

**In the code, the client is `conn_fd`** — a single integer. There is no client
object and no session. Proof: curl, `nc` and a Python script connecting in turn
produce byte-for-byte identical server output apart from the
[ephemeral port](#ephemeral-port). The program cannot tell them apart because it
never receives a program, only a file descriptor.

`struct sockaddr_in client_addr` is **not** the client. It holds six bytes — a
4-byte IP and a 2-byte port — describing where the other end is. It is caller ID,
not the caller. Passing `NULL, NULL` to `accept()` instead drops it entirely and
changes nothing about the server's behaviour.

### The client has no memory

After `close(conn_fd)` nothing about that client remains in the program. The fd
number is recycled and means nothing; the next client will probably be handed 4
again. A server looping over twenty requests cannot know whether that was twenty
people or one person twenty times.

This is why HTTP has no memory of who anyone is. The TCP connection is the only
thread of identity that exists, and closing it cuts that thread. Cookies, sessions
and tokens all exist to rebuild an identity the layer underneath discards every
time. Keep-alive (step 15) lets one `conn_fd` survive several requests, but that is
still not identity — only a connection that has not hung up yet.

## Network byte order

The order in which the bytes of a multi-byte integer are laid out in memory.

- **Little-endian** — low byte first. What x86 and ARM do. `8080` = `0x1F90` is
  stored as `90 1F`.
- **Big-endian** — high byte first. What every network protocol mandates. `8080`
  is sent as `1F 90`.

Big-endian is therefore also called *network byte order*. Four conversion
functions bridge the two:

| function | meaning | used for |
|----------|---------|----------|
| `htons` | host to network short (16-bit) | setting `sin_port` |
| `htonl` | host to network long (32-bit) | setting `sin_addr.s_addr` |
| `ntohs` | network to host short | reading `sin_port` |
| `ntohl` | network to host long | reading `sin_addr.s_addr` |

Forgetting one is a **silent** bug: no compiler warning, no runtime error, just
the wrong number. `htons(8080)` omitted means listening on port 36895, which only
`ss` will reveal.

Rule: every number inside a `sockaddr_in` is in network byte order, always
convert on the way in and on the way out.

## Value-result argument

A pointer parameter that carries information in *both* directions. The caller
sets it to the size of the buffer available; the callee overwrites it with the
size actually used.

```c
socklen_t client_len = sizeof(client);        /* in:  I have this much room */
accept(listen_fd, (struct sockaddr *)&client, &client_len);
                                              /* out: I used this much */
```

This is why the parameter is `&client_len` and not `client_len`. Common in the
socket API (`accept`, `getsockname`, `getpeername`, `recvfrom`).

## Byte stream

What `SOCK_STREAM` (TCP) provides. Bytes arrive reliably, in order, uncorrupted.

Grouping is **not** provided. There is no notion of a message. One `write()` on
the sending side may surface as three `read()`s on the receiving side, and three
writes may surface as one read. Nothing in the TCP layer says where anything
begins or ends.

This is the reason HTTP needs `\r\n\r\n` to delimit the end of headers and
`Content-Length` to delimit the end of a body. Those markers exist to rebuild, at
the application layer, the message boundaries that TCP deliberately does not keep.

## Backlog

The second argument to `listen()`. The maximum number of *completed* connections
the kernel will hold in a queue while the program is not calling `accept()`.

Visible as the Send-Q column of a `LISTEN` row in `ss -ltn`. The Recv-Q column of
that same row shows how many connections are currently sitting in the queue.

## Accept queue

Where the kernel parks connections it has fully established but which the program
has not collected yet.

The crucial consequence: the kernel completes the TCP three-way handshake
**without the program's involvement**. A client therefore connects successfully,
and can send a whole request, while the server process is doing something else
entirely — or nothing at all. `accept()` does not build a connection, it collects
one that is already built.

## Ephemeral port

The port on the *client* side of a connection, picked at random from a high range
by the kernel rather than chosen by the program. A TCP connection is identified by
the pair of endpoints (`ip:port` on both sides), so the client needs a port too,
even though nothing is listening on it.

Seen as the `36820` in `client connected from 127.0.0.1:36820`.
