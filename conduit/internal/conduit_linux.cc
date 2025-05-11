// Copyright 2025 Adrian Gjerstad
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: conduit_linux.cc
// -----------------------------------------------------------------------------
//
// This file defines the backend implementation details for cd::Conduit on
// Linux.
//
// This implementation uses epoll(7) to create, in kernel memory, a set of file
// descriptors to epoll_wait(2) for events on, and then handles those events.
//

#include "conduit/internal/conduit.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <time.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/time/time.h"

#include "conduit/event.h"

namespace cd {

namespace internal {

namespace {

uint32_t GenerateEPollFlags(std::shared_ptr<::cd::EventListener> l) {
  uint32_t result = 0;

  if (l->HasReadable()) {
    result |= EPOLLIN;
  }

  if (l->HasWritable()) {
    result |= EPOLLOUT;
  }

  if (l->HasAcceptable()) {
    result |= EPOLLIN;
  }

  if (l->HasHangup()) {
    // Not technically necessary: "epoll_wait(2) will always wait for this
    // event; it is not necessary to set it in events when calling epoll_ctl()."
    result |= EPOLLHUP;
  }

  return result;
}

void DispatchHandlers(std::shared_ptr<::cd::EventListener> l, uint32_t events) {
  if (events & EPOLLIN) {
    l->HandleReadable();
    l->HandleAcceptable();
  }

  if (events & EPOLLOUT) {
    l->HandleWritable();
  }

  if (events & EPOLLHUP) {
    l->HandleHangup();
  }
}

}

ConduitImpl::ConduitImpl() {
  linux_.epfd_ = epoll_create(1);

  if (linux_.epfd_ < 0) {
    // SAFETY: The only time a ConduitImpl should EVER be instantiated is when
    // the process is starting. EINVAL cannot be returned based on the
    // invocation above, and EMFILE, ENFILE, and ENOMEM are errors that (a) can
    // only happen if the process has been running for a while, and/or (b) are
    // unrecoverable anyways.
    perror("epoll_create");
    abort();
  }
}

absl::Status ConduitImpl::Register(int fd,
                                   std::shared_ptr<::cd::EventListener> l) {
  if (listeners_.contains(fd)) {
    return absl::AlreadyExistsError("listener already registered");
  }

  struct epoll_event ev;
  ev.events = GenerateEPollFlags(l);
  ev.data.fd = fd;

  if (epoll_ctl(linux_.epfd_, EPOLL_CTL_ADD, fd, &ev)) {
    return absl::ErrnoToStatus(errno, "epoll_ctl failed");
  }

  // Listener registered successfully
  listeners_.emplace(fd, l);

  return absl::OkStatus();
}

absl::Status ConduitImpl::Unregister(int fd,
                                     std::shared_ptr<::cd::EventListener> l) {
  if (!listeners_.contains(fd)) {
    return absl::NotFoundError("listener not registered");
  }

  struct epoll_event ev;

  if (epoll_ctl(linux_.epfd_, EPOLL_CTL_DEL, fd, &ev)) {
    return absl::ErrnoToStatus(errno, "epoll_ctl failed");
  }

  listeners_.erase(fd);
  
  return absl::OkStatus();
}

void ConduitImpl::Refresh(int fd, std::shared_ptr<::cd::EventListener> l) {
  struct epoll_event ev;

  ev.events = GenerateEPollFlags(l);
  ev.data.fd = fd;
  
  // We don't care about errors here.
  epoll_ctl(linux_.epfd_, EPOLL_CTL_MOD, fd, &ev);
}

void ConduitImpl::WaitAndProcessEvents(absl::Duration timeout) {
#define MAX_EVENTS (128)
  struct epoll_event events[MAX_EVENTS];
  struct timespec ts_timeout = absl::ToTimespec(timeout);

  // Refresh any changed listener sets
  for (const auto& it : listeners_) {
    if (it.second->HasListenerSetChanged()) {
      Refresh(it.first, it.second);
    }
  }

  int nfds;
  if (timeout == absl::InfiniteDuration()) {
    // We may block as long as we want.
    nfds = epoll_pwait2(linux_.epfd_,  // EPoll File Descriptor
                        events,        // Event Buffer
                        MAX_EVENTS,    // Event Buffer Size
                        NULL,          // Timeout
                        NULL);         // Sigset
  } else {
    nfds = epoll_pwait2(linux_.epfd_,  // EPoll File Descriptor
                        events,        // Event Buffer
                        MAX_EVENTS,    // Event Buffer Size
                        &ts_timeout,   // Timeout
                        NULL);         // Sigset
  }

  if (nfds < 0) {
    // Potential errors and why we're ignoring them:
    // - EBADF: We don't allow access to epfd_, so this shouldn't be possible.
    //          If it happens, then there's nothing we can do anyways.
    // - EFAULT: events is allocated on the stack. I should hope we have write
    //           access to our own stack.
    // - EINTR: This is okay. Conduit needs to be able to properly process
    //          signals anyways.
    // - EINVAL: epfd_ was created with epoll_create, so the first case isn't
    //           possible. maxevents == MAX_EVENTS == 128, which is greater than
    //           zero.
    return;
  }

  for (int i = 0; i < nfds; ++i) {
    // events[i].data.fd is the file descriptor that had an event.
    // events[i].events is the set of events that occurred.
    if (!listeners_.contains(events[i].data.fd)) {
      // We don't own that fd. Don't know why it's here, but we'll ignore it.
      continue;
    }

    DispatchHandlers(listeners_.at(events[i].data.fd), events[i].events);
  }
}

}

}

