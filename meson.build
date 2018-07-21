project(
    'gamemode',
    'c',
    default_options : ['c_std=c11'],
    version: '1.3-dev',
    license: 'BSD',
)

am_cflags = [
    '-fstack-protector',
    '-Wall',
    '-pedantic',
    '-Wstrict-prototypes',
    '-Wundef',
    '-fno-common',
    '-Werror-implicit-function-declaration',
    '-Wformat',
    '-Wformat-security',
    '-Werror=format-security',
    '-Wconversion',
    '-Wunused-variable',
    '-Wunreachable-code',
    '-W',
]

# Add our main flags
add_global_arguments(am_cflags, language: 'c')

cc = meson.get_compiler('c')

path_prefix = get_option('prefix')
path_bindir = join_paths(path_prefix, get_option('bindir'))
path_datadir = join_paths(path_prefix, get_option('datadir'))
path_includedir = join_paths(path_prefix, get_option('includedir'))
path_libdir = join_paths(path_prefix, get_option('libdir'))
path_libexecdir = join_paths(path_prefix, get_option('libexecdir'))

# Find systemd via pkgconfig
dep_systemd = dependency('libsystemd')

# Allow meson to figure out how the compiler sets up threading
dep_threads = dependency('threads')

# On non glibc systems this might be a stub, i.e. for musl
libdl = cc.find_library('dl', required: false)

with_systemd = get_option('with-systemd')
if with_systemd == true
    # If the path isn't explicitly set, ask systemd for the systemd user unit directory
    path_systemd_unit_dir = get_option('with-systemd-user-unit-dir')
    if path_systemd_unit_dir == ''
        message('Asking pkg-config for systemd\'s directories')
        pkgconfig_systemd = dependency('systemd')
        path_systemd_unit_dir = pkgconfig_systemd.get_pkgconfig_variable('systemduserunitdir')
    endif
endif

# Set the dbus path as appropriate.
path_dbus_service_dir = get_option('with-dbus-service-dir')
if path_dbus_service_dir == ''
  path_dbus_service_dir = join_paths(path_datadir, 'dbus-1', 'services')
endif

path_polkit_action_dir = join_paths(path_datadir, 'polkit-1', 'actions')

with_daemon = get_option('with-daemon')
with_examples = get_option('with-examples')

# Provide a config.h
cdata = configuration_data()
cdata.set_quoted('LIBEXECDIR', path_libexecdir)
cdata.set_quoted('GAMEMODE_VERSION', meson.project_version())
config_h = configure_file(
    configuration: cdata,
    output: 'config.h',
)
config_h_dir = include_directories('.')

# Library is always required
subdir('lib')

# The daemon can be disabled if necessary, allowing multilib builds of the
# main library
if with_daemon == true
    # inih currently only needed by the daemon
    inih = subproject('inih')
    inih_dependency = inih.get_variable('inih_dependency')

    subdir('daemon')

    # All installed data is currently daemon specific
    subdir('data')
endif

# Optionally allow building of examples
if with_examples == true
    subdir('example')
endif


report = [
    '    Build configuration:',
    '    ====================',
    '',
    '    prefix:                                 @0@'.format(path_prefix),
    '    bindir:                                 @0@'.format(path_bindir),
    '    datadir:                                @0@'.format(path_datadir),
    '    libdir:                                 @0@'.format(path_libdir),
    '    libexecdir:                             @0@'.format(path_libexecdir),
    '    includedir:                             @0@'.format(path_includedir),
]

if with_systemd == true
report += [
    '    systemd user unit directory:            @0@'.format(path_systemd_unit_dir),
]
endif
report += [
    '    D-BUS service directory:                @0@'.format(path_dbus_service_dir),
]

report += [

    '    PolKit Action Directory:                @0@'.format(path_polkit_action_dir),
    '',
    '    Options:',
    '    ========',
    '',
    '    daemon:                                 @0@'.format(with_daemon),
    '    examples:                               @0@'.format(with_examples),
    '    systemd:                                @0@'.format(with_systemd),
]

# Output some stuff to validate the build config
message('\n\n\n' + '\n'.join(report) + '\n\n')
