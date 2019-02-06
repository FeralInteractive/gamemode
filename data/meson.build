data_conf = configuration_data()
data_conf.set('BINDIR', path_bindir)
data_conf.set('LIBEXECDIR', path_libexecdir)
data_conf.set('GAMEMODE_LIB_DIR', path_libdir)

# Pull in the example config
config_example = run_command(
    'cat',
    join_paths(meson.source_root(), 'example', 'gamemode.ini')
).stdout().strip()
data_conf.set('GAMEMODE_EXAMPLE_CONFIG', config_example)

if with_systemd == true
    # Install systemd user unit
    configure_file(
        input: 'gamemoded.service.in',
        output: 'gamemoded.service',
        configuration: data_conf,
        install_dir: path_systemd_unit_dir,
    )
endif

# Install the D-BUS service file
configure_file(
  input: 'com.feralinteractive.GameMode.service.in',
  output: 'com.feralinteractive.GameMode.service',
  configuration: data_conf,
  install_dir: path_dbus_service_dir,
)


# Install the Polkit action file in all cases
configure_file(
    input: 'com.feralinteractive.GameMode.policy.in',
    output: 'com.feralinteractive.GameMode.policy',
    configuration: data_conf,
    install_dir: path_polkit_action_dir,
)

# Install the helper run script
configure_file(
    input: 'gamemoderun.in',
    output: 'gamemoderun',
    configuration: data_conf,
    install_dir: 'bin',
    install_mode: 'rwxr-xr-x',
)

# Configure and install the man page
manpage = configure_file(
    input: files('gamemoded.8.in'),
    output: 'gamemoded.8',
    configuration: data_conf,
)
install_man(manpage)
