#!/bin/sh
set -eu
# Adds or removes the bootstrap unit in the system image (the root filesystem is read-only on the device: it is remounted
# writable only for this and restored afterwards). Original: timomacquis, 0.1.3. DEX_PREFIX (tests only) redirects the paths
# and skips the remount.
P=${DEX_PREFIX:-}
UNIT=$P/usr/lib/systemd/system/drive-exec-bootstrap.service
LINK=$P/usr/lib/systemd/system/multi-user.target.wants/drive-exec-bootstrap.service
ROOT_RO=0
if [ -z "$P" ]; then
 case ",$(findmnt -rn -M / -o OPTIONS)," in *,ro,*) ROOT_RO=1; mount -o remount,rw /;; esac
fi
restore_root() { sync; if [ "$ROOT_RO" = 1 ]; then mount -o remount,ro /; fi; }
trap restore_root EXIT
case "$1" in
 install)
  [ ! -e "$UNIT" ] && [ ! -L "$LINK" ] || { echo 'Bootstrap already exists'; exit 1; }
  mkdir -p "$P/usr/lib/systemd/system/multi-user.target.wants"
  cp "$2" "$UNIT"
  chmod 644 "$UNIT"
  ln -s ../drive-exec-bootstrap.service "$LINK"
  ;;
 remove)
  rm -f "$LINK" "$UNIT"
  ;;
 *) exit 1;;
esac
