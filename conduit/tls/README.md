# Conduit TLS

Updated 22 May 2025 by Adrian Gjerstad.

In the era of modern networking, security is paramount to a good and safe
application. The world's most common and best-understood method for doing this
is a protocol known as Transport Layer Security, or its predecessor Secure
Sockets Layer, which has since been deprecated. With the intention of building
a framework upon which all asynchronous jobs and networked services can be
built, Conduit comes with TLS socket/server capabilities, assuming its
dependencies are compatible with your target platform.

This document describes the architecture for this built-in support, from its
underlying implementation to the use of Conduit's public API.

## Table of Contents

- [Use of OpenSSL](#use-of-openssl)
- [Contexts](#contexts)
- [Sessions](#sessions)
  - [Client Sessions](#client-sessions)
  - [Server Sessions](#server-sessions)

## Use of OpenSSL

Conduit makes use of OpenSSL for its underlying cryptographic primitives,
certificate (chain), key management, and TLS protocol implementation for the
following reasons:

- There is absolutely no reason to reinvent the wheel here. A library has
  already been written to do the grunt work here, so it would be far better to
  just write an API that incorporates OpenSSL's capabilities into the current
  ecosystem.
- I may be a confident developer, but I know where my limits are. I know how to
  safely use cryptographic primitives and manage secrets by principle, but the
  actual implementation of a scalable TLS API is entirely too dangerous. All I
  have to do here for my rationale is point at the infamous
  [Heartbleed](https://www.heartbleed.com) bug (which, ironically, affects old
  OpenSSL versions).
- Unlike the DNS protocol implementation (see `cd::NameResolver` in
  `conduit/net/dns.h`), where there were no existing solutions for doing lookups
  asynchronously that were easily integratable, OpenSSL offers native support
  for it without any compromise. Handling events for OpenSSL-enabled sockets is
  just as easy as handling events for raw file descriptors.
- OpenSSL is time- and load-tested, ensuring good performance on production
  systems. It also has an active, healthy developer base, ensuring that any bugs
  discovered are patched quickly and without unnecessary disclosure to the
  public.

...plus many others that aren't as significant to mention here.

## Contexts

After the OpenSSL library is properly initialized for the use cases of your
application, the first thing needed for any TLS-related operations is a context,
referred to in documentation as `SSL_CTX`. The API for this context allows you
to configure many different parameters regarding how the protocol will behave.
Additionally, it stores trusted root certificates, reusable session keys, and
other cache data.

Conduit provides two Context classes, named `cd::TLSClientContext`, and
`cd::TLSServerContext`. The former contains a single `SSL_CTX` object configured
to safely connect to any TLS-enabled server, while the latter contains one or
more `SSL_CTX` objects, each configured not only for the baseline of
server-safety, but also each with a certificate chain and key pair specific to
a domain being served. Both types of contexts are intended to live for the
entire duration of an application because their caching features can improve
user-experienced load times.

Note that the goal of these contexts is to be "safe by default," meaning even
predictable misuse of the API can still keep the application safe. Their
safety-related configurations cannot be manipulated by a Conduit user. Those
unsafe changes must be made within [Sessions](#sessions).

## Sessions

To explain what a session is and how it works, this section must be split up
into two parts, one for clients and one for servers.

### Client Sessions

In order to connect to a server using TLS, the Conduit user must create a
`cd::TLSClientSession`, given a client context that contains all of the shared
data. This class is simply a wrapper around an `SSL`, but also provides
high-level manipulation methods for altering the behavior of the "layer" in
"Transport Layer Security."

What do we do with this session? Why, we give it the file descriptor of the
connection-mode socket we want to use! We then tell use it by performing
basic non-blocking operations with the socket, such as `DoHandshake`, `Read`,
`Write`, and `Shutdown`. These operations are then further extended in public
APIs to provide asynchronous/event-driven stream abstractions on top of the
session.

For the nerds that actually care about how all of this is done after creating
the `SSL` for the first time, I have two pieces of advice for you:

1. Read the source code and find out for yourself.
2. The creation of `BIO` (OpenSSL's I/O abstraction layer) is handled
   automatically, and so is the assignment of a file descriptor to the `BIO` and
   the `BIO` to the `SSL`. Low-level operations are then implemented by
   performing said operations with the `SSL`.

### Server Sessions

The name "Server Session" may be a bit misleading at first glance. It does not
represent a TLS-enabled server, but rather a handle for a TLS-enabled connection
on the server's side. It contains a pointer to the server context created
earlier and an `SSL` handle that is used to interact with the actual connection
itself. The `SSL` uses the default context until discovered that it should do
otherwise, providing clients with the default certificate chain if they provide
an unknown domain name (via SNI), or fail to provide one at all. If no default
chain/key pair exists, the handshake is failed.

Creation requires a file descriptor that represents a socket (that will be set
to a non-blocking mode), and the server context. The socket passed in would have
been provisioned through a typical system call such as `accept`. The actual
method that creates a server session is called `cd::TLSServerSession::Accept`,
because it attempts to perform a handshake on the file descriptor immediately.
The method then returns a promise, resolving if the session is ready for data
transfer and rejecting otherwise. Beyond that, the same basic non-blocking
operations from client sessions are provided here.

The `Accept` method creates an `SSL` and a `BIO`, initializing and configuring
the two as necessary. It then handles the SNI TLS extension to ensure that
clients know that they can trust the server. This method of handling server-side
connections also provides the capability to applications of performing a TLS
handshake after some data has already been exchanged, such as with `SMTP`'s
`STARTTLS` command.

