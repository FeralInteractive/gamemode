data_conf = configuration_data()
data_conf.set('BINDIR', path_bindir)
data_conf.set('LIBEXECDIR', path_libexecdir)
data_conf.set('SYSCONFDIR', path_sysconfdir)
data_conf.set('GAMEMODE_PREFIX', path_prefix)
data_conf.set('GAMEMODE_VERSION', meson.project_version())
data_conf.set('GAMEMODE_PRIVILEGED_GROUP', with_privileged_group)

# Pull in the example config
config_example = run_command(
    'cat',
    join_paths(meson.source_root(), 'example', 'gamemode.ini'),
    check: true,
).stdout().strip()
data_conf.set('GAMEMODE_EXAMPLE_CONFIG', config_example)

if sd_bus_provider == 'systemd'
    if with_systemd_unit
        # Install systemd user unit
        configure_file(
            input: 'systemd/user/gamemoded.service.in',
            output: 'gamemoded.service',
            configuration: data_conf,
            install_dir: path_systemd_unit_dir,
        )
    endif
    if with_systemd_group
        # Install the sysusers.d file
        configure_file(
          input: 'systemd/sysusers.d/gamemode.conf.in',
          output: 'gamemode.conf',
          configuration: data_conf,
          install_dir: path_systemd_group_dir,
        )
    endif
endif

if with_pam_renicing
  # Install the limits.d configuration file
  configure_file(
      input: 'pam_limits/10-gamemode.conf.in',
      output: '10-gamemode.conf',
      configuration: data_conf,
      install_dir: path_pam_limits_dir,
  )
endif

# Install the D-BUS service file
configure_file(
  input: 'dbus/com.feralinteractive.GameMode.service.in',
  output: 'com.feralinteractive.GameMode.service',
  configuration: data_conf,
  install_dir: path_dbus_service_dir,
)


# Install the Polkit action & rule files for the privileged gamemode group
if with_privileged_group != ''
    configure_file(
        input: 'polkit/actions/com.feralinteractive.GameMode.policy.in',
        output: 'com.feralinteractive.GameMode.policy',
        configuration: data_conf,
        install_dir: path_polkit_action_dir,
    )
    configure_file(
        input: 'polkit/rules.d/gamemode.rules.in',
        output: 'gamemode.rules',
        configuration: data_conf,
        install_dir: path_polkit_rule_dir,
    )
endif

# Install the helper run script and man page
if get_option('default_library') == 'static'
    warning('gamemoderun will not be installed as a shared libgamemodeauto library is required')
else
    install_data(
        files('gamemoderun'),
        install_dir: path_bindir,
        install_mode: 'rwxr-xr-x',
    )

    gamemoderun_manpage = configure_file(
        input: files('gamemoderun.1.in'),
        output: 'gamemoderun.1',
        configuration: data_conf,
    )

    install_man(
        gamemoderun_manpage,
        install_dir: join_paths(path_mandir, 'man1')
    )
endif

# Install script to find processes with gamemode lib in runtime
install_data(
    files('gamemodelist'),
    install_dir: path_bindir,
    install_mode: 'rwxr-xr-x',
)

# Configure and install man pages
gamemoded_manpage = configure_file(
    input: files('gamemoded.8.in'),
    output: 'gamemoded.8',
    configuration: data_conf,
)

install_man(
    gamemoded_manpage,
    install_dir: join_paths(path_mandir, 'man8')
)

gamemodelist_manpage = configure_file(
    input: files('gamemodelist.1.in'),
    output: 'gamemodelist.1',
    configuration: data_conf,
)

install_man(
    gamemodelist_manpage,
    install_dir: join_paths(path_mandir, 'man1')
)

if with_examples
    example_manpage = configure_file(
        input: files('gamemode-simulate-game.1.in'),
        output: 'gamemode-simulate-game.1',
        configuration: data_conf,
    )

    install_man(
        example_manpage,
        install_dir: join_paths(path_mandir, 'man1')
    )
endif

# Install metainfo
metainfo_file = files('io.github.feralinteractive.gamemode.metainfo.xml')

install_data(
    metainfo_file,
    install_dir: path_metainfo,
)

# Validate metainfo
appstreamcli = find_program(
  'appstreamcli',
  required: false
)
if appstreamcli.found()
  test(
    'validate metainfo file',
    appstreamcli,
    args: ['validate', '--no-net', '--pedantic', metainfo_file],
  )
endif
