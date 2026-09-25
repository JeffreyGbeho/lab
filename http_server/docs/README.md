# Building an HTTP server from scratch, in C

The goal of this project is **not** a fast or complete HTTP server. The goal is
to understand every single line. So it is built in very small steps, and each
step must be provable from outside the program with a real tool (`ss`, `curl`,
`nc`) before moving on.

## Ground rules

- One step at a time. One commit per step.
- A step is finished when it can be observed from outside, not when it compiles.
- Do not start step N+1 until step N can be explained out loud without looking.
- No comments in the source. Explanations live here and in commit messages.

## Roadmap

| Step | What it does | How it is proven | Status |
|------|--------------|------------------|--------|
| [0](step-00-listening-socket.md) | `socket` + `bind` + `listen`, then sleep | `ss -ltn \| grep 8080` shows the port held | done |
| [1](step-01-accept-a-connection.md) | `accept` one connection, print who it is, close, exit | `curl` says `Empty reply from server` | done |
| 2 | `read` the bytes and print them raw | the real HTTP request text appears on screen | next |
| 3 | `write` a hardcoded response | `curl` prints the body | |
| 4 | wrap it in a loop | three `curl` runs in a row all answered | |
| 5 | parse the request line (method, path, version) | echo the path back in the body | |
| 6 | route `/` and `/hello`, else 404 | `curl` a bad path | |
| 7 | parse headers into key/value | print `Host` back | |
| 8 | serve a file from disk | `curl localhost:8080/index.html` | |
| 9 | read a `POST` body | `curl -d "name=jeff" ...` | |

Steps 0-4 build the *transport*: getting bytes in and out of a socket. Steps 5-9
build the *protocol*: giving those bytes meaning.

## Build and run

```sh
gcc -Wall -Wextra -o server server.c
./server
```

In another terminal:

```sh
ss -ltn | grep 8080        # is the port held?
curl -v localhost:8080     # talk to it
nc localhost 8080          # talk to it by hand
```

The compiled `server` binary is gitignored. Only `server.c` is tracked.

## Reference

- [Glossary](glossary.md) — file descriptors, byte order, and other vocabulary
  that keeps coming back.
