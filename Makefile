all:
	# FIXME: Add nice message
	@ exit 2

build:
	@ bootstrap.sh build

exherbo-docker-test:
	@ sudo docker run \
		--interactive \
		--tty \
		--volume "$$(pwd)/dist/paludis/exherbo:/var/repositories/gamemode" \
		exherbo/exherbo-x86_64-pc-linux-gnu-base \
			bash -c "true \
				&& chown root:tty /dev/tty \
				&& usermod -a -G tty paludisbuild \
				&& printf '%s\n' \
					\"format = e\" \
					\"location = \"\$$\{root\}/var/db/paludis/repositories/gamemode\"\" \
					\"sync = file:///var/repositories/gamemode\" \
				> /etc/paludis/repositories/gamemode.conf \
				&& cat /etc/paludis/repositories/gamemode.conf \
				&& cave sync \
				&& cave resolve games-utils/gamemode \
			"
