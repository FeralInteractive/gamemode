
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

# Small target util to get and set gpu clocks values
gpuclockctl_sources = [
    'gpuclockctl.c',
]

gpuclockctl = executable(
    'gpuclockctl',
    sources: gpuclockctl_sources,
    dependencies: [
        link_daemon_common,
    ],
    install: true,
    install_dir: path_libexecdir,
)

# Small target util to park and unpark cores
cpucorectl_sources = [
    'cpucorectl.c',
]

cpucorectl = executable(
    'cpucorectl',
    sources: cpucorectl_sources,
    dependencies: [
        link_daemon_common,
    ],
    install: true,
    install_dir: path_libexecdir,
)

# Small target util to set values in /proc/sys/
procsysctl_sources = [
    'procsysctl.c',
]

procsysctl = executable(
    'procsysctl',
    sources: procsysctl_sources,
    dependencies: [
        link_daemon_common,
    ],
    install: true,
    install_dir: path_libexecdir,
)
