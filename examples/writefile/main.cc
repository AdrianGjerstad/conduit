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
// File: main.cc
// -----------------------------------------------------------------------------
//
// This file demonstrates how to write to a file asynchronously.
//

#include <memory>

#include "absl/status/statusor.h"
#include "conduit/conduit.h"
#include "conduit/stream/file.h"

int main(int argc, char** argv) {
  cd::Conduit conduit;

  absl::StatusOr<std::shared_ptr<cd::WriteFileStream>> stream_s =
    cd::WriteFileStream::OpenAppend(&conduit, "examples/writefile/hello.txt");
  
  if (!stream_s.ok()) {
    std::cerr << stream_s.status() << std::endl;
  }

  auto stream = stream_s.value();

  stream->Write("Hello, stream!\n");
  std::cout << "Writing stream opened for examples/writefile/hello.txt" <<
    std::endl;

  conduit.RunForever();
  return 0;
}

