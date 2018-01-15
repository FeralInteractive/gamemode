# Install the service file
install_data('gamemoded.service', install_dir: path_systemd_unit_dir)

# Give cpugovctl the permissions it needs
meson.add_install_script(
    'cpugovctl_perms.sh',
    dependencies: cpugovctl,
)
