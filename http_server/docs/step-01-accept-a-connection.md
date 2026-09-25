# Step 1 — Accept one connection

## Goal

Take one connection out of the kernel's accept queue, print who the client is,
hang up on them, exit. Still no byte is read and no byte is written.

`pause()` is deleted. `accept()` takes its place.

## The code

```c
struct sockaddr_in client;
socklen_t client_len = sizeof(client);

int conn_fd = accept(listen_fd, (struct sockaddr *)&client, &client_len);
if (conn_fd < 0) {
    perror("accept");
    exit(1);
}

printf("client connected from %s:%d (conn_fd = %d)\n",
       inet_ntoa(client.sin_addr),
       ntohs(client.sin_port),
       conn_fd);

close(conn_fd);
close(listen_fd);
```

### `struct sockaddr_in client;`

`accept()` writes the client's address into this. Note the reversal from step 0:
there, a `sockaddr_in` was *filled in* to say where to bind; here an *empty* one
is handed over to be filled in. Same struct, opposite direction.

Passing `NULL, NULL` instead is legal and common when the client's address is not
needed. It is passed here only to make the address visible.

### `socklen_t client_len = sizeof(client);`

A **value-result argument** — an old C pattern that recurs throughout the socket
API. It is set *before* the call to mean "this is how much room I have", and the
kernel overwrites it *after* the call to mean "this is how much I actually used".
That is why `accept()` takes `&client_len`, a pointer, rather than a plain
number. Leaving it uninitialised can make `accept()` decline to fill in the
address at all.

### `accept(listen_fd, ...)`

Blocks until a connection is available in the queue, then removes one from the
queue and **returns a brand new file descriptor** for it. The generic
`struct sockaddr *` cast is there for the same reason as in `bind()`.

### `inet_ntoa(...)` and `ntohs(...)`

Conversions back to human-readable form. `inet_ntoa` is network-to-ASCII: the
32-bit address `0x7F000001` becomes the string `"127.0.0.1"`. `ntohs` is `htons`
run backwards, needed because `sin_port` inside that struct is still big-endian.

Rule of thumb: **every number inside a `sockaddr_in` is in network byte order**
and must be converted whenever it is read or written.

### The two `close()` calls

`close(conn_fd)` hangs up on this one client. `close(listen_fd)` gives up port
8080 entirely. Mixing these two up is the classic bug of step 4: closing
`listen_fd` inside the loop kills the server after one client.

## Proof

```
$ ./server
listening on port 8080 (listen_fd = 3)
client connected from 127.0.0.1:36820 (conn_fd = 4)

$ curl -v localhost:8080
* Connected to localhost (127.0.0.1) port 8080
* Empty reply from server
```

Three things in that output, each worth more than the code:

### `listen_fd = 3` and `conn_fd = 4` are different numbers

`accept()` did not modify `listen_fd`. It returned a **second socket**.

- `3` is the doorman. It never carries client data. It stays at port 8080 and
  keeps producing new connections, one per `accept()` call.
- `4` is the private line to this one client. Every `read()` and `write()` of
  this client's data uses `4`, never `3`.

Descriptor numbering starts at 3 because 0, 1 and 2 are already taken at process
startup: stdin, stdout, stderr. The first socket gets the next free slot.

### `127.0.0.1:36820`

The client's address, filled in by `accept()`. Port 36820 was not chosen by curl
— the kernel picked a random free high port ("ephemeral port") for the client
side. A TCP connection is identified by the full pair of endpoints, which is why
`ss` prints it as `127.0.0.1:36820 -> 127.0.0.1:8080`.

### `Empty reply from server`

Different from step 0's infinite hang, and the difference is informative.

- **Step 0:** nobody picked the connection up, so curl waited forever for a
  response.
- **Step 1:** the connection was picked up and immediately closed, so curl's read
  returned end-of-stream: connection closed, zero bytes received.

Same non-answer, but now it is a *fast* non-answer, and curl can name it.

### The process exits

One `accept()`, one client, done. Serving a second client requires going back to
`accept()` — that is step 4.

## Where did the request bytes go?

In step 0, 77 bytes of curl's request were visible sitting in the kernel's
receive buffer. This step never calls `read()`, so those bytes were never
collected — `close(conn_fd)` discarded the buffer along with the connection.

The bytes are real, they arrive on their own, and they wait. Step 2 finally reads
them.

## Checks

1. `listen_fd` is 3 and `conn_fd` is 4. A second client connects — which
   descriptor is `accept()` called on, and what number should come back?
2. Why `Empty reply from server` this time instead of step 0's infinite hang?
3. What is `client_len` before the call, what is it after, and why is it a pointer?
4. Where did the 77 bytes of request data go this time?
