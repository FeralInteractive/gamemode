# Libtool like versioning (current.revision.age) for the libraries
# See https://www.sourceware.org/autobook/autobook/autobook_61.html#Library-Versioning
lt_current = '0'
lt_revision = '0'
lt_age = '0'
lt_version = '@0@.@1@.@2@'.format(lt_current, lt_age, lt_revision)

# Main client library to message the daemon
libgamemode = shared_library(
    'gamemode',
    sources: [
        'client_impl.c',
    ],
    dependencies: [
        link_lib_common,
        dep_dbus,
    ],
    install: true,
    soversion: lt_current,
    version: lt_version,
)

gamemode_headers_includes = [
    include_directories('.'),
]

# Small library to automatically use gamemode
libgamemodeauto = library(
    'gamemodeauto',
    sources: [
        'client_loader.c',
    ],
    dependencies: [
        libdl,
    ],
    install: true,
    soversion: lt_current,
    version: lt_version,
)

# Install the gamemode_client header
gamemode_headers = [
    'gamemode_client.h',
]

install_headers(
    gamemode_headers,
    install_dir: path_includedir,
)

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
  name: 'libgamemodeauto',
  description: desc,
  filebase: 'libgamemodeauto',
  libraries: libgamemodeauto,
  version: meson.project_version(),
)

# Dependency objects
gamemode_dep = declare_dependency(
  include_directories: gamemode_headers_includes,
  dependencies: libdl,
)
libgamemodeauto_dep = declare_dependency(
  link_with: libgamemodeauto,
)
