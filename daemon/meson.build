# Main daemon
daemon_sources = [
    'gamemoded.c',
    'gamemode-config.c',
    'gamemode-context.c',
    'gamemode-dbus.c',
    'gamemode-env.c',
    'gamemode-gpu.c',
    'gamemode-ioprio.c',
    'gamemode-proc.c',
    'gamemode-sched.c',
    'gamemode-tests.c',
    'gamemode-wine.c',
]

gamemoded_includes = gamemode_headers_includes
gamemoded_includes += config_h_dir

gamemoded = executable(
    'gamemoded',
    sources: daemon_sources,
    c_args: sd_bus_args,
    dependencies: [
        link_daemon_common,
        dep_threads,
        sd_bus_dep,
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
