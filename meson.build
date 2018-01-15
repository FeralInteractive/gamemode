project(
    'gamemode',
    'c',
    default_options : ['c_std=c11'],
    version: '0.2',
    license: 'BSD',
)

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

# Set the dbus path as appropriate.
path_dbus_service_dir = get_option('with-dbus-service-dir')
if path_dbus_service_dir == ''
    path_dbus_service_dir = join_paths(path_datadir, 'dbus-1', 'services')
endif

path_polkit_action_dir = join_paths(path_datadir, 'polkit-1', 'actions')

with_daemon = get_option('with-daemon')
with_examples = get_option('with-examples')

# Provide config.h so the daemon knows where the helper is
cdata = configuration_data()
cdata.set_quoted('LIBEXECDIR', path_libexecdir)
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
    '    D-BUS service directory:                @0@'.format(path_dbus_service_dir),
    '    PolKit Action Directory:                @0@'.format(path_polkit_action_dir),
    '',
    '    Options:',
    '    ========',
    '',
    '    daemon:                                 @0@'.format(with_daemon),
    '    examples:                               @0@'.format(with_examples),
]

# Output some stuff to validate the build config
message('\n\n\n' + '\n'.join(report) + '\n\n')
