# Main client library to message the daemon
gamemode = shared_library(
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
gamemodeauto = shared_library(
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

# Generate a pkg-config files
pkg = import('pkgconfig')
desc = 'GameMode temporarily applies game specific optimisations to the host OS.'
pkg.generate(
  name: 'gamemode',
  description: desc,
  filebase: 'gamemode',
  version: meson.project_version(),
  libraries: [
    libdl
  ],
)

pkg.generate(
  name: 'gamemode',
  description: desc,
  filebase: 'gamemode-auto',
  libraries: gamemodeauto,
  version: meson.project_version(),
  libraries_private: [
    libdl
  ],
)

