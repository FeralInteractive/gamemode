# Main client library to message the daemon
shared_library(
    'gamemode',
    sources: [
        'client_impl.c',
    ],
    dependencies: [
        dep_systemd,
    ],
    install: true,
)

libgamemode_includes = [
    include_directories('.'),
]

# Small library to automatically use gamemode
shared_library(
    'gamemodeauto',
    sources: [
        'client_loader.c',
    ],
    dependencies: [
        libdl,
    ],
    install: true,
)

# Install the gamemode_client header
gamemode_headers = [
    'gamemode_client.h',
]

install_headers(gamemode_headers)
