# Convenience library for the duplicated logging functionality
common_sources = [
    'logging.c',
    'governors-query.c',
]

daemon_common = static_library(
    'daemon-common',
    sources: common_sources,
    install: false,
)

link_daemon_common = declare_dependency(
    link_with: daemon_common,
)

# Main daemon
daemon_sources = [
    'main.c',
    'gamemode.c',
    'daemonize.c',
    'dbus_messaging.c',
    'governors.c',
    'daemon_config.c',
]

executable(
    'gamemoded',
    sources: daemon_sources,
    dependencies: [
        link_daemon_common,
        dep_threads,
        dep_systemd,
        inih_dependency,
    ],
    include_directories: [
        config_h_dir,
    ],
    install: true,
)

# Small target util to get and set cpu governors
cpugovctl_sources = [
    'cpugovctl.c',
]

cpugovctl = executable(
    'cpugovctl',
    sources: cpugovctl_sources,
    dependencies: [
        link_daemon_common,
    ],
    install: true,
    install_dir: path_libexecdir,
)
