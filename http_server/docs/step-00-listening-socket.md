# Step 0 — A listening socket on port 8080

## Goal

Hold port 8080 and nothing else. No client is served, no byte is read. The only
question being answered is: *does this process own the port?*

## The four calls

The whole step is four system calls, in this order, and the order matters:

```
socket()  -> create an endpoint. Attached to nothing. Reachable by nobody.
bind()    -> attach it to an address: 0.0.0.0, port 8080.
listen()  -> tell the kernel to start queueing incoming connections.
accept()  -> (step 1) take one connection out of that queue.
```

A common wrong mental model is "listen tells me a request arrived". It does not.
`listen()` only flips the socket into a mode where the kernel is willing to
accept connections on its behalf. Nothing is reported back to the program.

## The code

```c
int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
```

Creates the endpoint and returns a **file descriptor** — a small integer that
indexes the kernel's table of open things for this process. This is why a socket
can later be `read()` and `write()` exactly like a file: to the program, it *is*
a file. See [glossary](glossary.md#file-descriptor).

- `AF_INET` — IPv4 address family (`AF_INET6` for IPv6).
- `SOCK_STREAM` — a reliable, ordered **byte stream**: TCP. The alternative,
  `SOCK_DGRAM`, is UDP (messages, unordered, no delivery guarantee).
  "Byte stream" is the single most consequential word in this project; see
  [the trap](#the-trap-that-shapes-all-of-http) below.
- `0` — use the default protocol for that combination, i.e. TCP.

```c
int opt = 1;
setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

Without this, stopping the server and restarting it immediately fails with
`bind: Address already in use` for up to a minute. The kernel keeps the old
connection in `TIME_WAIT` to absorb late packets from the previous connection.
`SO_REUSEADDR` says "accept that risk, give me the port now". Restarting happens
hundreds of times while learning, so this line is not optional in practice.

```c
struct sockaddr_in addr;
memset(&addr, 0, sizeof(addr));
addr.sin_family      = AF_INET;
addr.sin_addr.s_addr = htonl(INADDR_ANY);
addr.sin_port        = htons(8080);
```

`sockaddr_in` is the IPv4 address struct: family, port, IP. The `memset` zeroes
it because the struct contains padding (`sin_zero`) that must be zero, and
uninitialised stack memory is garbage.

`INADDR_ANY` is `0.0.0.0` — "accept connections arriving on **any** interface of
this machine". Writing `127.0.0.1` instead would restrict it to clients on this
same machine.

`htons` = **h**ost **to** **n**etwork **s**hort. See
[byte order](glossary.md#network-byte-order). Skipping it is a silent bug: the
port becomes 36895 and no error is ever printed.

```c
bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
```

Now the socket has an address. The cast to `struct sockaddr *` is an old C
idiom: `bind` must serve IPv4, IPv6 and Unix sockets, so it takes a generic
pointer plus the size, and works out which real struct it was handed.

```c
listen(listen_fd, 16);
```

Flips the socket from "could be used to dial out" to "waits for connections in".
The `16` is the **backlog**: how many *completed* connections the kernel may hold
in a queue while the program is busy elsewhere. Client 17, arriving before the
program catches up, gets refused.

```c
pause();
```

Blocks forever. A placeholder with no purpose other than keeping the process
alive long enough to inspect it from outside. It is deleted in step 1 and
replaced by `accept()`.

## Proof

```
$ ss -ltn | grep 8080
LISTEN 0      16           0.0.0.0:8080       0.0.0.0:*
```

Reading that line:

- `LISTEN` — the socket's state, set by `listen()`.
- `0` — Recv-Q. On a *listening* socket this is the number of finished
  connections waiting in the accept queue. Nobody has connected yet.
- `16` — Send-Q. On a listening socket this is the **backlog**, straight from
  the `listen()` argument.
- `0.0.0.0:8080` — the bound address, from `INADDR_ANY` and `htons(8080)`. If
  `htons` had been forgotten, this would read `0.0.0.0:36895`.

## The experiment worth doing

With the server running, run `curl localhost:8080` in another terminal. It hangs
forever. Predict why before running it, then look at the sockets while it hangs:

```
$ ss -ltn | grep 8080
LISTEN 1      16           0.0.0.0:8080       0.0.0.0:*
$ ss -tn | grep 8080
ESTAB      77     0          127.0.0.1:8080       127.0.0.1:49498
ESTAB      0      0          127.0.0.1:49498      127.0.0.1:8080
```

Three facts, and they are the point of the whole step:

1. **Recv-Q went `0` -> `1`.** One completed connection is sitting in the accept
   queue, waiting to be picked up.
2. **The state is `ESTAB`.** The connection is fully *established*. The kernel
   performed the entire TCP three-way handshake — SYN, SYN-ACK, ACK — by itself,
   while this program was asleep inside `pause()`.
3. **`ESTAB 77`.** Seventy-seven bytes already sit in the kernel's receive
   buffer for that connection. That is curl's HTTP request, delivered in full,
   and read by nobody.

The conclusion to carry into step 1:

> **`accept()` does not create the connection.** It hands over a connection the
> kernel has already finished building.

And that explains the hang. curl is not waiting to *connect* — it connected
instantly and successfully, and it has already sent its request. It is waiting
for a response that no code exists to write.

## The trap that shapes all of HTTP

`SOCK_STREAM` means a **byte stream**. TCP guarantees that bytes arrive, in
order, without corruption. It guarantees **nothing** about grouping: no concept
of "a message" exists at this level. One `write()` on the client side may arrive
as three `read()`s, and three writes may arrive as one read.

This is why HTTP needs `\r\n\r\n` to mark the end of headers, and why it needs
`Content-Length` to mark the end of a body. Those exist purely because the layer
underneath refuses to say where anything ends. This trap is safely ignored
through step 4 and must be confronted at step 5.

## Checks

1. What does `socket()` return, and why can a socket later be `read()` like a file?
2. Why does `htons()` exist, and what breaks silently without it?
3. The program is in `pause()` and a client connects. Where does that connection
   go, given that `accept()` is never called?
4. What is `SO_REUSEADDR` protecting against?
