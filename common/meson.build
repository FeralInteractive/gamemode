# Convenience library for the duplicated logging functionality
common_sources = [
    'common-logging.c',
    'common-governors.c',
    'common-external.c',
    'common-helpers.c',
    'common-gpu.c',
]

daemon_common = static_library(
    'daemon-common',
    sources: common_sources,
    install: false,
)

link_daemon_common = declare_dependency(
    link_with: daemon_common,
)

include_daemon_common = include_directories('.')