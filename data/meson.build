data_conf = configuration_data()
data_conf.set('BINDIR', path_bindir)
data_conf.set('LIBEXECDIR', path_libexecdir)

if with_systemd == true
    # Install systemd user unit
    configure_file(
        input: 'gamemoded.service.in',
        output: 'gamemoded.service',
        configuration: data_conf,
        install_dir: path_systemd_unit_dir,
    )
else
    # Install the D-BUS service file
    configure_file(
        input: 'com.feralinteractive.GameMode.service.in',
        output: 'com.feralinteractive.GameMode.service',
        configuration: data_conf,
        install_dir: path_dbus_service_dir,
    )
endif

# Install the Polkit action file in all cases
configure_file(
    input: 'com.feralinteractive.GameMode.policy.in',
    output: 'com.feralinteractive.GameMode.policy',
    configuration: data_conf,
    install_dir: path_polkit_action_dir,
)
