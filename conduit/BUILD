# Copyright 2025 Adrian Gjerstad
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cc_library(
    name = "conduit",
    srcs = ["conduit.cc"],
    hdrs = ["conduit.h"],
    deps = [
        ":event",
        ":timer",
        "//conduit/internal:conduit",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "event",
    srcs = ["event.cc"],
    hdrs = ["event.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "promise",
    hdrs = ["promise.h"],
    deps = [
        ":timer",
        "//conduit",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/time",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "timer",
    srcs = ["timer.cc"],
    hdrs = ["timer.h"],
    deps = [
        "@abseil-cpp//absl/time",
    ],
    visibility = ["//visibility:public"],
)

