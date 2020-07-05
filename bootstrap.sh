#!/bin/sh
# Originally created by FeralInteractive <https://www.feralinteractive.com/en/> under BSD 3-Clause License <https://github.com/FeralInteractive/gamemode/blob/master/LICENSE.txt> in 2017 bootstrap.sh
# Performed full refact by Jacob Hrbek <kreyren@rixotstudio.cz> under GPLv3 license <https://www.gnu.org/licenses/gpl-3.0.en.html> in 05.07.2020 04:28:55 CET

# shellcheck shell=sh # Written to be POSIX-compatible

###! Simple bootstrap script to build and run the daemon
###! We need:
###! - FIXME-DOCS
###! Requires:
###! - FIXME
###! Exit codes:
###! - FIXME-DOCS(Krey): Defined in die()
###! Platforms:
###! - [ ] Linux
###!  - [ ] Debian
###!  - [ ] Ubuntu
###!  - [ ] Fedora
###!  - [ ] NixOS
###!  - [ ] Archlinux
###!  - [ ] Alpine
###! - [ ] FreeBSD
###! - [ ] Darwin
###! - [ ] Redox
###! - [ ] ReactOS
###! - [ ] Windows
###! - [ ] Windows/Cygwin

# Maintainer info
UPSTREAM="https://github.com/FeralInteractive"
MAINTAINER="https://github.com/Kreyren"

# Command overrides
[ -z "$PRINTF" ] && PRINTF="printf"
[ -z "$WGET" ] && WGET="wget"
[ -z "$CURL" ] && CURL="curl"
[ -z "$ARIA2C" ] && ARIA2C="aria2c"
[ -z "$CHMOD" ] && CHMOD="chmod"
[ -z "$UNAME" ] && UNAME="uname"
[ -z "$TR" ] && TR="tr"
[ -z "$SED" ] && SED="sed"
[ -z "$GREP" ] && GREP="grep"
[ -z "$DPKG" ] && DPKG="dpkg"
[ -z "$APT_GET" ] && APT_GET="apt-get"
[ -z "$TOR" ] && TOR="tor"
[ -z "$NINJA" ] && NINJA="ninja"
[ -z "$MESON" ] && MESON="meson"

# Customization of the output
## efixme
[ -z "$EFIXME_FORMAT_STRING" ] && EFIXME_FORMAT_STRING="FIXME: %s\n"
[ -z "$EFIXME_FORMAT_STRING_LOG" ] && EFIXME_FORMAT_STRING="${logPrefix}FIXME: %s\n"
[ -z "$EFIXME_FORMAT_STRING_DEBUG" ] && EFIXME_FORMAT_STRING_DEBUG="FIXME($myName:$0): %s\n"
[ -z "$EFIXME_FORMAT_STRING_DEBUG_LOG" ] && EFIXME_FORMAT_STRING_DEBUG_LOG="${logPrefix}FIXME($myName:$0): %s\n"
## eerror
[ -z "$EERROR_FORMAT_STRING" ] && EERROR_FORMAT_STRING="ERROR: %s\\n"
[ -z "$EERROR_FORMAT_STRING_LOG" ] && EERROR_FORMAT_STRING_LOG="${logPrefix}ERROR: %s\\n"
[ -z "$EERROR_FORMAT_STRING_DEBUG" ] && EERROR_FORMAT_STRING_DEBUG="ERROR($myName:$0): %s\\n"
[ -z "$EERROR_FORMAT_STRING_DEBUG_LOG" ] && EERROR_FORMAT_STRING_DEBUG_LOG="${logPrefix}ERROR($myName:$0): %s\\n"
## edebug
[ -z "$EDEBUG_FORMAT_STRING" ] && EDEBUG_FORMAT_STRING="DEBUG: %s\\n"
[ -z "$EDEBUG_FORMAT_STRING_LOG" ] && EDEBUG_FORMAT_STRING_LOG="${logPrefix}DEBUG: %s\\n"
[ -z "$EDEBUG_FORMAT_STRING_DEBUG" ] && EDEBUG_FORMAT_STRING_DEBUG="DEBUG($myName:$0): %s\\n"
[ -z "$EDEBUG_FORMAT_STRING_DEBUG_LOG" ] && EDEBUG_FORMAT_STRING_DEBUG_LOG="${logPrefix}DEBUG($myName:$0): %s\\n"
## einfo
[ -z "$EINFO_FORMAT_STRING" ] && EINFO_FORMAT_STRING="INFO: %s\\n"
[ -z "$EINFO_FORMAT_STRING_LOG" ] && EINFO_FORMAT_STRING_LOG="${logPrefix}INFO: %s\\n"
[ -z "$EINFO_FORMAT_STRING_DEBUG" ] && EINFO_FORMAT_STRING_DEBUG="INFO($myName:$0): %s\\n"
[ -z "$EINFO_FORMAT_STRING_DEBUG_LOG" ] && EINFO_FORMAT_STRING_DEBUG_LOG="${logPrefix}INFO($myName:$0): %s\\n"
## die
[ -z "$DIE_FORMAT_STRING" ] && DIE_FORMAT_STRING="FATAL: %s in script '$myName' located at '$0'\\n"
[ -z "$DIE_FORMAT_STRING_LOG" ] && DIE_FORMAT_STRING_LOG="${logPath}FATAL: %s in script '$myName' located at '$0'\\n"
[ -z "$DIE_FORMAT_STRING_DEBUG" ] && DIE_FORMAT_STRING_DEBUG="FATAL($myName:$1): %s\n"
[ -z "$DIE_FORMAT_STRING_DEBUG_LOG" ] && DIE_FORMAT_STRING_DEBUG_LOG="${logPrefix}FATAL($myName:$1): %s\\n"
### Fixme trap
[ -z "$DIE_FORMAT_STRING_FIXME" ] && DIE_FORMAT_STRING_FIXME="FATAL: %s in script '$myName' located at '$0', fixme?\n"
[ -z "$DIE_FORMAT_STRING_FIXME_LOG" ] && DIE_FORMAT_STRING_FIXME_LOG="${logPrefix}FATAL: %s, fixme?\n"
[ -z "$DIE_FORMAT_STRING_FIXME_DEBUG" ] && DIE_FORMAT_STRING_FIXME_DEBUG="FATAL($myName:$1): %s, fixme?\n"
[ -z "$DIE_FORMAT_STRING_FIXME_DEBUG_LOG" ] && DIE_FORMAT_STRING_FIXME_DEBUG_LOG="${logPrefix}FATAL($myName:$1): %s, fixme?\\n"
### Unexpected trap
[ -z "$DIE_FORMAT_STRING_UNEXPECTED" ] && DIE_FORMAT_STRING_UNEXPECTED="FATAL: Unexpected happend while %s in $myName located at $0\\n"
[ -z "$DIE_FORMAT_STRING_UNEXPECTED_LOG" ] && DIE_FORMAT_STRING_UNEXPECTED_LOG="${logPrefix}FATAL: Unexpected happend while %s\\n"
[ -z "$DIE_FORMAT_STRING_UNEXPECTED_DEBUG" ] && DIE_FORMAT_STRING_UNEXPECTED_DEBUG="FATAL($myName:$1): Unexpected happend while %s in $myName located at $0\\n"
[ -z "$DIE_FORMAT_STRING_UNEXPECTED_DEBUG_LOG" ] && DIE_FORMAT_STRING_UNEXPECTED_DEBUG="${logPrefix}FATAL($myName:$1): Unexpected happend while %s\\n"

# Exit on anything unexpected
set -e

# NOTICE(Krey): By default busybox outputs a full path in '$0' this is used to strip it
myName="${0##*/}"

# Used to prefix logs with timestemps, uses ISO 8601 by default
logPrefix="[ $(date -u +"%Y-%m-%dT%H:%M:%SZ") ] "
# Path to which we will save logs
# NOTICE(Krey): To avoid storing file '$HOME/.some-name.sh.log' we are stripping the '.sh' here
logPath="${XDG_DATA_HOME:-$HOME/.local/share}/${myName%%.sh}.log"

# inicialize the script in logs
"$PRINTF" '%s\n' "Started $myName on $("$UNAME" -s) at $(date -u +"%Y-%m-%dT%H:%M:%SZ")" >> "$logPath"

# NOTICE(Krey): Aliases are required for posix-compatible line output (https://gist.github.com/Kreyren/4fc76d929efbea1bc874760e7f78c810)
die() { funcname=die
	case "$2" in
		38|fixme) # FIXME
			if [ "$DEBUG" = 0 ] || [ -z "$DEBUG" ]; then
				"$PRINTF" "$DIE_FORMAT_STRING_FIXME" "$3"
				"$PRINTF" "$DIE_FORMAT_STRING_FIXME_LOG" "$3" >> "$logPath"
				funcname="$myName"
			elif [ "$DEBUG" = 1 ]; then
				"$PRINTF" "$DIE_FORMAT_STRING_FIXME_DEBUG" "$3"
				"$PRINTF" "$DIE_FORMAT_STRING_FIXME_DEBUG_LOG" "$3" >> "$logPath"
				funcname="$myName"
			else
				# NOTICE(Krey): Do not use die() here
				"$PRINTF" 'FATAL: %s\n' "Unexpected happend while processing variable DEBUG with value '$DEBUG' in $funcname"
			fi

			exit 38
		;;
		255) # Unexpected trap
			if [ "$DEBUG" = 0 ] || [ -z "$DEBUG" ]; then
				"$PRINTF" "$DIE_FORMAT_STRING_UNEXPECTED" "$3"
				"$PRINTF" "$DIE_FORMAT_STRING_UNEXPECTED_LOG" "$3" >> "$logPath"
				funcname="$myName"
			elif [ "$DEBUG" = 1 ]; then
				"$PRINTF" "$DIE_FORMAT_STRING_UNEXPECTED_DEBUG" "$3"
				"$PRINTF" "$DIE_FORMAT_STRING_UNEXPECTED_DEBUG_LOG" "$3" >> "$logPath"
				funcname="$myName"
			else
				# NOTICE(Krey): Do not use die() here
				"$PRINTF" "$DIE_FORMAT_STRING" "Unexpected happend while processing variable DEBUG with value '$DEBUG' in $funcname"
			fi
		;;
		*)
			if [ "$DEBUG" = 0 ] || [ -z "$DEBUG" ]; then
				"$PRINTF" "$DIE_FORMAT_STRING" "$3"
				"$PRINTF" "$DIE_FORMAT_STRING_LOG" "$3" >> "$logPath"
				funcname="$myName"
			elif [ "$DEBUG" = 1 ]; then
				"$PRINTF" "$DIE_FORMAT_STRING_DEBUG" "$3"
				"$PRINTF" "$DIE_FORMAT_STRING_DEBUG_LOG" "$3" >> "$logPath"
				funcname="$myName"
			else
				# NOTICE(Krey): Do not use die() here
				"$PRINTF" 'FATAL: %s\n' "Unexpected happend while processing variable DEBUG with value '$DEBUG' in $funcname"
			fi
	esac

	exit "$2"

	# In case invalid argument has been parsed in $2
	exit 255
}; alias die='die "$LINENO"'

einfo() { funcname=einfo
	if [ "$DEBUG" = 0 ] || [ -z "$DEBUG" ]; then
		"$PRINTF" "$EINFO_FORMAT_STRING" "$2"
		"$PRINTF" "$EINFO_FORMAT_STRING_LOG" "$2" >> "$logPath"
		funcname="$myName"
		return 0
	elif [ "$DEBUG" = 1 ]; then
		"$PRINTF" "$EINFO_FORMAT_STRING_DEBUG" "$2"
		"$PRINTF" "$EINFO_FORMAT_STRING_DEBUG_LOG" "$2" >> "$logPath"
		funcname="$myName"
		return 0
	else
		die 255 "processing variable DEBUG with value '$DEBUG' in $funcname"
	fi
}; alias einfo='einfo "$LINENO"'

ewarn() { funcname=ewarn
	if [ "$DEBUG" = 0 ] || [ -z "$DEBUG" ]; then
		"$PRINTF" "$EWARN_FORMAT_STRING" "$2"
		"$PRINTF" "$EWARN_FORMAT_STRING_LOG" "$2" >> "$logPath"
		funcname="$myName"
		return 0
	elif [ "$DEBUG" = 1 ]; then
		"$PRINTF" "$EWARN_FORMAT_STRING_DEBUG" "$2"
		"$PRINTF" "$EWARN_FORMAT_STRING_DEBUG_LOG" "$2" >> "$logPath"
		funcname="$myName"
		return 0
	else
		die 255 "processing variable DEBUG with value '$DEBUG' in $funcname"
	fi
}; alias ewarn='ewarn "$LINENO"'

eerror() { funcname=eerror
	if [ "$DEBUG" = 0 ] || [ -z "$DEBUG" ]; then
		"$PRINTF" "$EERROR_FORMAT_STRING" "$2"
		"$PRINTF" "$EERROR_FORMAT_STRING_LOG" "$2" >> "$logPath"
		funcname="$myName"
		return 0
	elif [ "$DEBUG" = 1 ]; then
		"$PRINTF" "$EERROR_FORMAT_STRING_DEBUG" "$2"
		"$PRINTF" "$EERROR_FORMAT_STRING_DEBUG_LOG" "$2" >> "$logPath"
		funcname="$myName"
		return 0
	else
		die 255 "processing variable DEBUG with value '$DEBUG' in $funcname"
	fi
}; alias eerror='eerror "$LINENO"'

edebug() { funcname=edebug
	if [ "$DEBUG" = 0 ] || [ -z "$DEBUG" ]; then
		"$PRINTF" "$EDEBUG_FORMAT_STRING" "$2"
		"$PRINTF" "$EDEBUG_FORMAT_STRING_LOG" "$2" >> "$logPath"
		funcname="$myName"
		return 0
	elif [ "$DEBUG" = 1 ]; then
		"$PRINTF" "$EDEBUG_FORMAT_STRING_DEBUG" "$2"
		"$PRINTF" "$EDEBUG_FORMAT_STRING_DEBUG_LOG" "$2" >> "$logPath"
		funcname="$myName"
		return 0
	else
		die 255 "processing variable DEBUG with value '$DEBUG' in $funcname"
	fi
}; alias edebug='edebug "$LINENO"'

efixme() { funcname=efixme
	if [ "$IGNORE_FIXME" = 1 ]; then
		true
	elif [ "$IGNORE_FIXME" = 0 ] || [ -z "$IGNORE_FIXME" ]; then
		if [ "$DEBUG" = 0 ] || [ -z "$DEBUG" ]; then
			"$PRINTF" "$EFIXME_FORMAT_STRING" "$2"
			"$PRINTF" "$EFIXME_FORMAT_STRING" "$2" >> "$logPath"
			funcname="$myName"
			return 0
		elif [ "$DEBUG" = 1 ]; then
			"$PRINTF" "$EFIXME_FORMAT_STRING_DEBUG" "$2"
			"$PRINTF" "$EFIXME_FORMAT_STRING_DEBUG_LOG" "$2" >> "$logPath"
			funcname="$myName"
			return 0
		else
			die 255 "processing DEBUG variable with value '$DEBUG' in $funcname"
		fi
	else
		die 255 "processing variable IGNORE_FIXME with value '$IGNORE_FIXME' in $0"
	fi
}; alias efixme='efixme "$LINENO"'

edebug "Resolving root on user with ID '$(id -u)"
if [ "$(id -u)" = 0 ]; then
	edebug "Script has been executed as user with ID 0, assuming root"
	funcname="$myName"
	return 0
# NOTICE(Krey): The ID 33333 is used by gitpod
elif [ "$(id -u)" = 1000 ] || [ "$(id -u)" = 33333 ]; then
	ewarn "Script $myName is not expected to run as non-root, trying to elevate root.."
	if command -v sudo 1>/dev/null; then
		einfo "Found 'sudo' that can be used for root elevation"
		rootEle="sudo"
		funcname="$myName"
		return 0
	elif command -v su 1>/dev/null; then
		einfo "Found 'su' that can be used for a root elevation"
		ewarn "This will require the end-user to parse a root password multiple times assuming that root has a password set"
		rootEle="su -c"
		funcname="$myName"
		return 0
	elif ! command -v sudo 1>/dev/null && ! command -v su 1>/dev/null; then
		die 3 "Script $myName depends on root permission to install packages where commands 'sudo' nor 'su' are available for root elevation"
		funcname="$myName"
		return 0
	else
		die 225 "processing root on non-root"
	fi
else
	die 3 "Unknown user ID '$(id -u)' has been parsed in script $myName"
fi

if command -v "$UNAME" 1>/dev/null; then
	unameKernel="$("$UNAME" -s)"
	edebug "Identified the kernel as '$unameKernel"
	case "$unameKernel" in
		Linux)
			KERNEL="$unameKernel"

			# Assume Linux Distro and release
			# NOTICE(Krey): We are expecting this to return a lowercase value
			if command -v "$LSB_RELEASE" 1>/dev/null; then
				assumedDistro="$("$LSB_RELEASE" -si | "$TR" :[upper]: :[lower]:)"
				assumedRelease="$("$LSB_RELEASE" -cs | "$TR" :[upper]: :[lower]:)"
			elif ! command -v "$LSB_RELEASE" 1>/dev/null && [ -f /etc/os-release ]; then
				assumedDistro="$("$GREP" -o "^ID\=.*" /etc/os-release | "$SED" s/ID=//gm)"
				assumedRelease="$("$GREP" -o"^VERSION_CODENAME\=.*" /etc/os-release | "$SED" s/VERSION_CODENAME=//gm)"
			elif ! command -v "$LSB_RELEASE" 1>/dev/null && [ ! -f /etc/os-release ]; then
				die 1 "Unable to identify linux distribution using  command 'lsb_release' nor file '/etc/os-release'"
			else
				die 255 "attempting to assume linux distro and release"
			fi

			edebug "Identified distribution as '$assumedDistro'"
			edebug "Identified distribution release as '$assumedRelease'"

			# Verify Linux Distro
			efixme "Add sanitization logic for other linux distributions"
			case "$assumedDistro" in
				ubuntu | debian | fedora | nixos | opensuse | gentoo | exherbo)
					DISTRO="$assumedDistro"
				;;
				*) die fixme "Unexpected Linux distribution '$assumedDistro' has been detected."
			esac

			# Verify Linux Distro Release
			efixme "Sanitize verification of linux distro release"
			assumedRelease="$RELEASE"
		;;
		FreeBSD | Redox | Darwin | Windows)
			KERNEL="$unameKernel"
		;;
		*) die 255 "Unexpected kernel '$unameKernel'"
	esac
elif ! command -v "$UNAME" 1>/dev/null; then
	die 1 "Standard command '$UNAME' is not available on this system, unable to identify kernel"
else
	die 255 "Identifying system"
fi

# Argument management
while [ "$#" -gt 0 ]; do case "$1" in
	build)
		case "$KERNEL/$DISTRO-$RELEASE" in
			linux/devuan-beowulf)
			# Check if CPU supports governon
			scalingGovFile="/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
				if [ ! -f "$scalingGovFile" ]; then
					while true; do
						ewarn "CPU governon control features is not supported - CPUFreq scaling governon device file '$scalingGovFile' doesn't exists which indicates that processor scheduling is disabled in BIOS, See README.md or GitHub issue #44 (https://github.com/FeralInteractive/gamemode/issues/44) for more info. Other features are still supported, do you want to continue? (y/n)"
						read -r continueCheck
						case "$continueCheck" in
								"y"|"Y"|"Yes"|"YES"|"yes")
									edebug "Accepted userinput '$continueCheck' as confirmation to continue on system without '$scalingGovFile'"
									unset continueCheck
									break ;;
								"n"|"N"|"No"|"NO")
									die 0 "Terminating as instructed.."
								;;
								# NOTICE(Krey): Yes, i think i am funny~
								"PLEASE FOR THE LOVE OF GOD STOP! STOP NOW OH MY GOD STOP! STOOOOOOOOOOOP!!!")
									printf '%s\n' "OK OK OK CALM DOWN! CAAALM DOOOWNNN!!, jeez.."
									die 0 "Terminating as instructed.."
								;;
								*)
									eerror "Input '$continueCheck' is not valid, expecting either: y, Y, Yes, YES, yes or n, N, No, no, \"PLEASE FOR THE LOVE OF GOD STOP! STOP NOW OH MY GOD STOP! STOOOOOOOOOOOP!!!\""
									einfo "Asking again for confirmation.."
						esac
					done
				elif [ -f "$scalingGovFile" ]; then
					edebug "File '$scalingGovFile' exists, assuming CPU governnon control supported"
				else
					die 255 "processing variable scalingGovFile with value '$scalingGovFile' to check wether CPU governon control features are supported"
				fi

				# Run meson
				# DNM: Sanitize meson, info from upstream required
				# shellcheck disable=SC2154 # We are expecting end-users to parse cmdPrefix if required
				"$MESON" builddir --prefix="${prefix:=/usr}" --buildtype debugoptimized -Dwith-systemd-user-unit-dir=/etc/systemd/user "$@" || true

				errCode="$?"

				case "$errCode" in
					*) die 255 "exit code '$errCode' has been provided from meson"
				esac

				# Run ninja
				# DNM: Sanitize ninja, info from upstream required
				"$NINJA" install -C builddir || true

				errCode="$?"

				case "$errCode" in
					*) die 255 "exit code '$errCode' has been provided from ninja"
				esac

				efixme "Some witchery with init might be required here"
			;;
			*) die fixme "Script '$myName' doesn't have implemented logic for kernel '$KERNEL' using distribution '$DISTRO' with release '$RELEASE'"
		esac
	;;
	*) die 2 "Command '$1' is not recognized by $myName"
esac; done

die 23 "Script '$myName' escaped sanitization"
