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

# Find systemd via pkgconfig
dep_systemd = dependency('libsystemd')

# On non glibc systems this might be a stub, i.e. for musl
libdl = cc.find_library('dl', required: false)

# If the path isn't explicitly set, ask systemd for the systemd user unit directory
path_systemd_unit_dir = get_option('with-systemd-user-unit-dir')
if path_systemd_unit_dir == ''
    message('Asking pkg-config for systemd\'s directories')
    pkgconfig_systemd = dependency('systemd')
    path_systemd_unit_dir = pkgconfig_systemd.get_pkgconfig_variable('systemduserunitdir')
endif

with_daemon = get_option('with-daemon')
with_examples = get_option('with-examples')

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
    '    includedir:                             @0@'.format(path_includedir),
    '    systemd user unit directory:            @0@'.format(path_systemd_unit_dir),
    '',
    '    Options:',
    '    ========',
    '',
    '    daemon:                                 @0@'.format(with_daemon),
    '    examples:                               @0@'.format(with_examples),
]

# Output some stuff to validate the build config
message('\n\n\n' + '\n'.join(report) + '\n\n')
