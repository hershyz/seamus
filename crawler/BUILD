load("@rules_cc//cc:defs.bzl", "cc_executable", "cc_library")

cc_executable(
    name = "crawler_node",
    srcs = ["driver.cpp"],
    deps = [
        "//frontier:Frontier",
        "//url_store:UrlStore",
        "//lib:utils",
        "//lib:vector",
        "//lib:string",
        "//lib:unordered_map",
        "//lib:priority_queue",
        "//lib:thread_pool",
    ],
)

cc_library(
    name = "crawler_node",
    hdrs = ["crawler_node.h"],
    deps = [
        "//frontier:Frontier",
        "//url_store:UrlStore",
        "//lib:utils",
        "//lib:vector",
        "//lib:string",
        "//lib:unordered_map",
        "//lib:priority_queue",
        "//lib:thread_pool",
    ],
)

cc_library(
    name = "domain_carousel",
    hdrs = ["domain_carousel.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//lib:deque",
        "//lib:string",
        "//lib:utils",
    ],
)
