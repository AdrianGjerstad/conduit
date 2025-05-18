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
    name = "dns",
    srcs = ["dns.cc"],
    hdrs = ["dns.h"],
    deps = [
        ":net",
        ":socket",
        "//conduit",
        "//conduit:promise",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "net",
    srcs = select({
        "@platforms//os:linux": [
            "net.cc",
            "net_linux.cc",
        ],
        "//conditions:default": ["net.cc"],
    }),
    hdrs = ["net.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "socket",
    srcs = select({
        "@platforms//os:linux": ["socket_linux.cc"],
        "//conditions:default": [],
    }),
    hdrs = ["socket.h"],
    deps = [
        ":net",
        "//conduit",
        "//conduit:event",
        "//conduit:promise",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
    ],
    visibility = ["//visibility:public"],
)

