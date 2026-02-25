load("@rules_cc//cc:defs.bzl", "cc_executable", "cc_library")

cc_executable(
    name = "crawler_node",
    srcs = ["crawler_node.cpp"],
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