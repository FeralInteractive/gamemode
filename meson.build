project('gamemode', 'c',
	default_options : ['c_std=c11'],
	version : '0.2',
	license : 'BSD' )
cc = meson.get_compiler('c')

libsystemd = cc.find_library('systemd')
libdl = cc.find_library('dl')

executable( 'gamemoded',
	'daemon/main.c',
	'daemon/gamemode.c',
	'daemon/logging.c',
	'daemon/daemonize.c',
	'daemon/dbus_messaging.c',
	'daemon/governors.c',
	dependencies: libsystemd,
	install: true )

# Main client library to message the daemon
shared_library( 'gamemode',
	'lib/client_impl.c',
	dependencies: libsystemd,
	install : true )

# install the service file
install_data( 'data/gamemoded.service', install_dir: '/etc/systemd/user' )

# Small target util to get and set cpu governors
executable( 'cpugovctl',
	'daemon/cpugovctl.c',
	'daemon/logging.c',
	install : true )

# Give cpugovctl the permissions it needs
meson.add_install_script( 'data/cpugovctl_perms.sh',
	dependencies : 'cpugovctl' )

# Small library to automatically use gamemode
shared_library( 'gamemodeauto',
	'lib/client_loader.c',
	dependencies : libdl,
	install : true )

# Install the gamemode_client header
install_headers( 'lib/gamemode_client.h' )

# An example game
libdir = include_directories('lib')
executable( 'example',
	'example/main.c',
	dependencies : libdl,
	include_directories : libdir )

