# Convenience library for the duplicated logging functionality
common_sources = [
    'common-logging.c',
    'common-governors.c',
    'common-external.c',
    'common-helpers.c',
    'common-gpu.c',
    'common-pidfds.c',
]

daemon_common = static_library(
    'daemon-common',
    sources: common_sources,
    install: false,
    include_directories: [config_h_dir]
)

link_daemon_common = declare_dependency(
    link_with: daemon_common,
    include_directories: [include_directories('.')]
)
