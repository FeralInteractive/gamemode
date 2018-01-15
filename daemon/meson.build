# Convenience library for the duplicated logging functionality
common_sources = [
    'logging.c',
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
]

executable(
    'gamemoded',
    sources: daemon_sources,
    dependencies: [
        link_daemon_common,
        dep_systemd,
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
)
