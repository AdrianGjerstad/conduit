# Conduit

Conduit is an asynchronous event-handling runtime for C++.

## Motivation

Most languages, particularly compiled ones such as C++, are "synchronous." In
other words, they execute statements in a given order and then exit. Any system
calls they make typically block. Traditionally, the only two ways around this
are by using threads or multiprocessing. The issue is that, while both allow for
concurrency/"asynchronicity," managing the cores that they use can make your
program vastly slower that it should be.

The modern way of handling a situation in which you have many, e.g., sockets to
wait for requests from, AND a server socket that you want to accept new
connections on, is to acknowledge that formerly, the code wasn't doing anything
until a system call like `read` or `accept` finished. The choice to use threads
came from the fact that you could only call these system calls with one file
descriptor at a time &mdash; a problem for anyone writing a production server.

## How it Works

While attempts will be and are being made to keep Conduit
cross-platform-extensible, the main operating system functionality that it uses
to achieve semi-concurrency is
[`epoll(7)`](https://man7.org/linux/man-pages/man7/epoll.7.html). `epoll` is a
Linux-only set of system calls that allows userspace processes to define a set
of file descriptors to "listen" for events on. The process then calls
[`epoll_wait(2)`](https://man7.org/linux/man-pages/man2/epoll_wait.2.html),
which blocks the calling thread until something happens with one of those file
descriptors. These events could be, for example, "data that was being read from
disk is now available," or "a new client would like to connect to your server."

Please note: efforts are made to ensure thread-safety, but Conduit itself does
not spin up threads. Conduit is semi-concurrent, meaning that if there is work
to be done with data, it is done in a single thread.

## Code Style

This project uses the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).

## License

This project is licensed under the Apache License, version 2.0. For a full copy
of the license text, see `LICENSE`. Every source file must have the
Apache License boilerplate, followed by a description of what the file is for.

