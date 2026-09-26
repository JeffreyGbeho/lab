# The C you actually need here

This project uses a small, fixed subset of C. Not "all of C" — this much, and it
recurs on nearly every line. It is worth knowing cold, because once it is
automatic, the socket calls are the easy part.

---

## 1. There is no string type

C has no string. It has an **array of `char`** with a zero byte marking the end.

```c
char buf[4096];
```

That is 4096 bytes of raw memory. It is not empty — it contains whatever was last
on the stack at that address. There is no length field, no capacity, no bounds
checking. Nothing anywhere records how many of those 4096 bytes are meaningful.

A "string" is a convention layered on top: the bytes up to the first `'\0'`.

```c
buf[n] = '\0';
```

This is what makes the bytes usable by `printf("%s", ...)`, `strlen`, `strcmp` and
every other `str*` function: they all walk forward until they find a zero byte.
Without the terminator they keep walking past your data into whatever follows.

**`'\0'` vs `"\0"`.** Single quotes are one character; double quotes are a string.
`'\0'` is the zero *byte*. They are not interchangeable.

**The consequence to internalise:** in C, whenever you have bytes, you must also
track *how many* — separately, by hand, in your own variable. That variable is `n`
in this code. Half of C's famous bugs are somebody losing track of `n`.

---

## 2. Why so many integer types

`int`, `size_t`, `ssize_t`, `socklen_t` all appear in this file. They are not
decoration.

| type | signed? | means |
|------|---------|-------|
| `int` | yes | a plain integer. File descriptors are `int` |
| `size_t` | **no** | a size or count. Cannot be negative. What `sizeof` returns, and what `read` takes as its count |
| `ssize_t` | yes | a size *or an error*. The `s` is "signed". What `read` returns, because it must be able to say `-1` |
| `socklen_t` | no | the length of an address struct. Historically its own type; effectively an unsigned int |

The pattern to see: **`size_t` for "how big", `ssize_t` for "how big, or failure".**
A function that can fail by returning `-1` cannot use an unsigned type, because
unsigned types have no `-1`.

**The trap.** Mixing signed and unsigned in a comparison silently converts the
signed one to unsigned, and `-1` becomes a huge positive number:

```c
ssize_t n = read(fd, buf, sizeof(buf));
if (n < sizeof(buf)) { ... }     /* BUG: n is converted to unsigned */
```

If `read` returned `-1`, that `-1` becomes 18446744073709551615, which is *not*
less than 4096, so the branch is skipped. This is why the error check is written
`if (n < 0)` against a literal `0` and done *first*, before `n` is used in any
comparison involving a size.

**`%zd` and `%zu`** in `printf` are the format specifiers for `ssize_t` and
`size_t`. Using `%d` for them is wrong on 64-bit systems, and `-Wall` will say so.

---

## 3. `sizeof` is not a function

```c
sizeof(buf)        /* 4096  - the whole array */
sizeof(addr)       /* 16    - the whole struct */
sizeof(opt)        /* 4     - one int */
```

It is evaluated by the **compiler**, not at runtime, and it answers "how many bytes
does this type occupy". No cost, no call.

The one thing that will bite you: `sizeof` on an array gives the array's size, but
**an array passed into a function decays to a pointer**, and `sizeof` on that gives
the pointer's size (8), not the array's. So `sizeof` only tells the truth about an
array in the scope where the array was declared. This is why buffer sizes get
passed around as explicit parameters in real code.

---

## 4. `&` and `*`

Two symbols, and each means two different things depending on where it appears.
This is the single most common source of confusion for people arriving from
Python or JavaScript.

**In a declaration**, `*` means "this is a pointer":

```c
char *p;              /* p holds an address where a char lives */
void *buf;            /* buf holds an address; type unspecified */
```

**In an expression**, `&` means "address of" and `*` means "value at":

```c
int opt = 1;
&opt                  /* the address where opt lives */
*p                    /* the value stored at address p */
```

### Why `&` appears all over this file

```c
setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
```

C passes arguments **by value** — a copy. A function receiving a copy cannot change
your variable. So whenever a function must either *read a struct you built* or
*write into a variable you own*, you hand it the **address** instead, and it works
through that address on the original.

Three reasons `&` shows up, all visible above:

1. `&opt` — "here is where my value is, go read it". (A struct or buffer is passed
   by address to avoid copying it, too.)
2. `&addr` — same, for a struct you filled in.
3. `&client_len` — "here is where my variable is, **go write into it**". This one is
   the interesting case; see below.

### `void *` — the generic pointer

```c
ssize_t read(int fd, void *buf, size_t count);
```

`void *` means "an address, and I am not telling you what type lives there".
`read` genuinely does not care: it moves `count` bytes into that memory whether it
holds chars, ints or a struct. Any pointer converts to `void *` without a cast,
which is how `read(conn_fd, buf, ...)` compiles with `buf` being a `char[]`.

`void *` is C's way of saying "generic". It is also C's way of throwing away all
type safety, which is why it appears in exactly the places where the compiler can
no longer protect you.

---

## 5. Structs, and filling one in

```c
struct sockaddr_in addr;
```

A struct is several values glued together at one address. `sockaddr_in` is the IPv4
address struct, and it contains roughly:

```c
struct sockaddr_in {
    sa_family_t    sin_family;   /* AF_INET */
    in_port_t      sin_port;     /* 16-bit port, network byte order */
    struct in_addr sin_addr;     /* 32-bit IP, network byte order */
    char           sin_zero[8];  /* padding, must be zero */
};
```

Declaring it allocates 16 bytes on the stack **and initialises nothing**. Those 16
bytes hold garbage until written.

```c
memset(&addr, 0, sizeof(addr));
```

`memset` writes a byte value over a region: "starting at this address, write
`sizeof(addr)` copies of `0`". It is the idiomatic way to zero a struct, and it
matters here because `sin_zero` must be zero and nothing else will do it for you.

```c
addr.sin_family = AF_INET;
addr.sin_addr.s_addr = htonl(INADDR_ANY);
```

`.` reaches into a struct you hold directly. `sin_addr` is itself a struct holding
one field `s_addr`, which is why that line has two dots. (If you held a *pointer*
to a struct you would write `p->sin_family`, which is shorthand for
`(*p).sin_family`. That form appears the moment code is split into functions.)

---

## 6. The cast `(struct sockaddr *)`

```c
bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
```

A cast tells the compiler "treat this address as a pointer to that type instead;
I know what I am doing". Here it is not a trick, it is the socket API's design:

`bind`, `accept`, `connect` and friends must work with IPv4 (`sockaddr_in`), IPv6
(`sockaddr_in6`) and Unix sockets (`sockaddr_un`), which are different sizes and
shapes. C predates generics, so the API declares one common type,
`struct sockaddr`, and every real address struct starts with the same family field.
You pass the address plus **the size**, and the kernel reads the family field to
work out what it actually received.

That is why the size argument is always there next to the cast. The cast throws the
type away; the size is what lets the kernel recover the meaning.

---

## 7. `main`, `return`, and exit codes

```c
int main(void) { ... return 0; }
```

`void` means "takes no arguments" (the alternative is
`int main(int argc, char **argv)` for command-line arguments). The `int` returned
is the process's **exit status**, which the shell reads:

- `0` means success. Always. In C, zero is success and non-zero is failure.
- anything else means failure.

```sh
./server; echo $?        # prints the exit status
```

`return 0` from `main` and `exit(0)` do the same thing. `exit(1)` is used inside the
error branches because it can be called from anywhere, not just from `main`.

This inversion — 0 is good — is the opposite of C's boolean convention, where 0 is
false. Both are true at the same time and it is a genuine wart. Exit statuses and
syscall returns use "0 means fine, negative means broken"; `if` statements use "0
means false".

---

## 8. `#include` — what it actually does

```c
#include <stdio.h>
```

A **textual paste**, performed before compilation. The preprocessor finds that file
and inserts its contents. Header files contain *declarations* — the signatures of
functions and the definitions of types and constants — so the compiler knows
`read` exists, what it takes, and what it returns.

The code of those functions is not in the header; it lives in the C library and is
attached later by the linker. That is the split to hold onto:

- **header** (`<unistd.h>`) — the promise that `read` exists, with this shape.
- **library** — the actual machine code, found at link time.

What each header in this file is for:

| header | provides |
|--------|----------|
| `<stdio.h>` | `printf`, `perror`, `putchar` |
| `<stdlib.h>` | `exit` |
| `<string.h>` | `memset` |
| `<unistd.h>` | `read`, `write`, `close` — the generic file descriptor calls |
| `<sys/socket.h>` | `socket`, `bind`, `listen`, `accept`, `setsockopt`, `struct sockaddr` |
| `<netinet/in.h>` | `struct sockaddr_in`, `INADDR_ANY`, `htons`, `htonl` |
| `<arpa/inet.h>` | `inet_ntoa` |

Forgetting one produces `implicit declaration of function 'read'` — which means
"you used something I have never heard of". The fix is always: find which header
declares it (`man 2 read` says so at the top) and include it.

**Constants like `AF_INET` and `SO_REUSEADDR`** are `#define`d integers from these
headers. `AF_INET` is literally `2`. The name exists so the code is readable and so
the number can differ between systems. Never write the number.
