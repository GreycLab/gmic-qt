#!/usr/bin/env bash
function usage()
{
  cat <<EOF
Usage:
       `basename $0` file.ts

   Call lrelease, lrelease-qt5 or lrelease-qt6 on file, depending on which command exists.
   The LRELEASE environment variable may be used to force a given command.

EOF
  exit 0
}

function die()
{
  local message="$@"
  >&2 echo "$message"
  exit 1
}

function exists()
{
  type "$1" >& /dev/null
}

# Prints the lrelease command to use, or returns 1 if none is found.
function find_lrelease()
{
  local cmd qmake bins
  if [[ -n "$LRELEASE" ]] && exists "$LRELEASE" ; then
    echo "$LRELEASE"
    return 0
  fi
  for cmd in lrelease-qt6 lrelease-qt5 lrelease lrelease6 ; do
    if exists "$cmd" ; then
      echo "$cmd"
      return 0
    fi
  done
  # The Qt tools are not always in the PATH (e.g. /usr/lib64/qt6/bin on Fedora)
  for qmake in qmake6 qmake-qt6 qmake-qt5 qmake ; do
    if exists "$qmake" ; then
      bins=$("$qmake" -query QT_INSTALL_BINS 2>/dev/null)
      if [[ -n "$bins" && -x "$bins/lrelease" ]] ; then
        echo "$bins/lrelease"
        return 0
      fi
    fi
  done
  return 1
}

in="$1"
[[ -z "$in" ]] && usage
[[ ! -e "$in" ]] && die "File not found: $in"
[[ ! $in =~ .*\.ts$  ]] && die "Not a ts file: $in"

lrelease_command=$(find_lrelease) || die "No lrelease(-qt5|-qt6) command available."
exec "$lrelease_command" -compress "$in"
