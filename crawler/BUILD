load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "crawler_lib",
    hdrs = [
        "bucket_manager.h",
        "crawler_listener.h",
        "crawler_worker.h",
        "domain_carousel.h",
        "network_util.h",
        "parser.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//lib:consts",
        "//lib:deque",
        "//lib:rpc_crawler",
        "//lib:rpc_listener",
        "//lib:string",
        "//lib:utils",
        "//lib:vector",
    ],
)

cc_binary(
    name = "crawler",
    srcs = ["main.cpp"],
    deps = [":crawler_lib"],
)

cc_library(
    name = "domain_carousel",
    srcs = ["domain_carousel.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//lib:consts",
        "//lib:deque",
        "//lib:string",
        "//lib:utils",
    ],
)
