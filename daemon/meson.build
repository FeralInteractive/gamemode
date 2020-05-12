# Main daemon
daemon_sources = [
    'gamemoded.c',
    'gamemode-context.c',
    'gamemode-ioprio.c',
    'gamemode-sched.c',
    'gamemode-wine.c',
    'gamemode-tests.c',
    'gamemode-gpu.c',
    'gamemode-dbus.c',
    'gamemode-config.c',
]

gamemoded_includes = libgamemode_includes
gamemoded_includes += config_h_dir

gamemoded = executable(
    'gamemoded',
    sources: daemon_sources,
    dependencies: [
        link_daemon_common,
        dep_threads,
        dep_systemd,
        inih_dependency,
        libdl,
    ],
    include_directories: [
        gamemoded_includes,
    ],
    install: true,
)

# verify gamemoded compiled properly
test(
    'validate gamemoded compiled properly',
    gamemoded,
    args: ['-v'],
)
