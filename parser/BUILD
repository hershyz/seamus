load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "parser",
    hdrs = [
        "HtmlTags.h",
        "parser.h",
        "word_array.h"
    ],
    deps = [
        "//lib:buffer",
        "//lib:string",
        "//lib:url_store"
        "//lib:utils"
    ],
    visibility = ["//visibility:public"]
)