package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "stats_lib",
    srcs = ["stats_impl.cc"],
    hdrs = ["stats_impl.h"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "statsd_lib",
    srcs = ["statsd.cc"],
    hdrs = ["statsd.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
    ],
)

envoy_cc_library(
    name = "thread_local_store_lib",
    srcs = ["thread_local_store.cc"],
    hdrs = ["thread_local_store.h"],
    deps = [
        ":stats_lib",
        "//include/envoy/thread_local:thread_local_interface",
    ],
)