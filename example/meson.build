# An example game
executable(
    'gamemode-simulate-game',
    sources: [
        'main.c',
    ],
    dependencies: [
        gamemode_dep,
    ],
    install: true,
    install_dir: path_bindir,
)

# An example configuration
install_data(
    files('gamemode.ini'),
    install_dir: path_sysconfdir,
)
