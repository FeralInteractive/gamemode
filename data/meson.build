data_conf = configuration_data()
data_conf.set('BINDIR', path_bindir)
data_conf.set('LIBEXECDIR', path_libexecdir)

# Install the D-BUS service file
configure_file(
    input: 'com.feralinteractive.GameMode.service.in',
    output: 'com.feralinteractive.GameMode.service',
    configuration: data_conf,
    install_dir: path_dbus_service_dir,
)

# Install the Polkit action file
configure_file(
    input: 'com.feralinteractive.GameMode.policy.in',
    output: 'com.feralinteractive.GameMode.policy',
    configuration: data_conf,
    install_dir: path_polkit_action_dir,
)
