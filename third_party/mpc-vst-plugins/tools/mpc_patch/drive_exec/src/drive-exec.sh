#!/bin/sh
# Drive exec: only one folder on a drive becomes executable (a private bind mount of that folder, remounted with exec);
# the drive itself keeps its noexec. Original work: "ForceHD VST Exec 0.1.3" by timomacquis (issue #150), contributed to
# mpc-vst-plugins. Changes in 0.2.0: the drive and the folder come from the config (any /media/<name>, spaces allowed, the folder
# is "Synths" or "vst" or another plain name) instead of the fixed /media/ForceHD/vst, mount points are compared the way
# /proc/self/mountinfo writes them (a space is \040), and the drive name and folder are validated strictly because the config
# is sourced by a root service. The mount logic and its checks are otherwise his.
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
CONFIG=${DRIVE_EXEC_CONFIG:-/etc/drive-exec/config}
STATE=${DRIVE_EXEC_STATE:-/run/drive-exec}
[ "$(id -u)" = 0 ] || { echo 'Root required'; exit 1; }
[ -f "$CONFIG" ] || { echo 'Configuration missing'; exit 1; }
DRIVE_ROOT=; EXEC_DIR=
. "$CONFIG"
# the config is shell: accept only plain names (letters, digits, . _ + ( ) - and single spaces), never ".", ".." or a hidden name
printf '%s\n' "$DRIVE_ROOT" | grep -Eq '^/media/[A-Za-z0-9][A-Za-z0-9._+() -]*$' || { echo 'Unsupported drive path'; exit 1; }
printf '%s\n' "$EXEC_DIR" | grep -Eq '^[A-Za-z0-9][A-Za-z0-9._+() -]*$' || { echo 'Unsupported folder name'; exit 1; }
case "$DRIVE_ROOT" in /media/az01-internal|/media/az01-internal-*|/media/acvs-synths) echo 'The internal storage is not a target'; exit 1;; esac
TARGET=$DRIVE_ROOT/$EXEC_DIR
mkdir -p "$STATE"
chmod 700 "$STATE"
exec 9>"$STATE/lock"
attempt=0
while ! flock -n 9; do
 [ "$attempt" -lt 10 ] || { echo 'Another operation is in progress'; exit 1; }
 sleep 1
 attempt=$((attempt+1))
done
# /proc/self/mountinfo writes a space in a path as \040 (and a backslash as \134): compare paths in that form
mi_path() { printf '%s' "$1" | sed 's/\\/\\134/g; s/ /\\040/g'; }
mount_id() { P=$(mi_path "$1") awk '$5==ENVIRON["P"]{v=$1} END{if(v)print v}' /proc/self/mountinfo; }
mount_options() { P=$(mi_path "$1") awk '$5==ENVIRON["P"]{v=$6} END{if(v)print v}' /proc/self/mountinfo; }
owner_id() { [ ! -f "$STATE/owned.mount" ] || awk 'NR==1{print $1}' "$STATE/owned.mount"; }
log_state() {
 if [ "$(cat "$STATE/last-status" 2>/dev/null || true)" != "$1" ]; then
  printf '%s\n' "$1" | tee "$STATE/last-status"
 fi
}
drive_mounted() {
 [ -n "$(mount_id "$DRIVE_ROOT")" ]
}
allow_exec() {
 options=$(mount_options "$TARGET")
 # Keep all other per-mount flags, especially nosuid/nodev/nosymfollow.
 options=$(printf '%s' "$options" | sed 's/,noexec//g;s/^noexec,//')
 mount -o "remount,bind,$options,exec" "$TARGET"
 case ",$(mount_options "$TARGET")," in *,noexec,*) echo 'noexec remains active'; return 1;; esac
}
revert_mount() {
 actual=$(mount_id "$TARGET")
 owned=$(owner_id)
 if [ -z "$actual" ]; then rm -f "$STATE/owned.mount"; return 0; fi
 [ -n "$owned" ] && [ "$actual" = "$owned" ] || {
  echo 'Mount not created by this patch: removal refused'; return 1;
 }
 umount "$TARGET"
 rm -f "$STATE/owned.mount"
 log_state 'VST execution permission removed'
}
case "${1:-status}" in
 apply)
  wait_seconds=0
  if [ "${2:-}" = --wait ]; then
   wait_seconds=${3:-20}
   case "$wait_seconds" in ''|*[!0-9]*) echo 'Invalid wait duration'; exit 1;; esac
   [ "$wait_seconds" -le 30 ] || { echo 'Maximum wait is 30 seconds'; exit 1; }
  elif [ "$#" -gt 1 ]; then echo 'Usage: apply [--wait 0..30]'; exit 1; fi
  while ! drive_mounted && [ "$wait_seconds" -gt 0 ]; do sleep 1; wait_seconds=$((wait_seconds-1)); done
  if ! drive_mounted; then
   log_state "Waiting for $DRIVE_ROOT to mount; no changes made"
   exit 0
  fi
  [ ! -L "$TARGET" ] || { echo 'Symbolic-link folder refused'; exit 1; }
  parent_id=$(mount_id "$DRIVE_ROOT")
  parent_options=$(mount_options "$DRIVE_ROOT")
  actual=$(mount_id "$TARGET")
  owned=$(owner_id)
  if [ -n "$actual" ]; then
   [ -n "$owned" ] && [ "$actual" = "$owned" ] || { echo 'Unmanaged existing mount on the folder: refused'; exit 1; }
   recorded_parent=$(awk 'NR==1{print $2}' "$STATE/owned.mount")
   [ "$recorded_parent" = "$parent_id" ] || { echo 'Parent mount changed: manual removal required'; exit 1; }
   case ",$(mount_options "$TARGET")," in *,noexec,*) allow_exec;; esac
   log_state "VST Exec active on $TARGET"
   exit 0
  fi
  rm -f "$STATE/owned.mount"
  mkdir -p "$TARGET"
  mount --bind "$TARGET" "$TARGET"
  new_id=$(mount_id "$TARGET")
  printf '%s %s\n' "$new_id" "$parent_id" > "$STATE/owned.mount"
  rollback_new() {
   if [ "$(mount_id "$TARGET")" = "$new_id" ]; then
    if umount "$TARGET"; then rm -f "$STATE/owned.mount"; fi
   fi
  }
  trap rollback_new EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  mount --make-private "$TARGET"
  allow_exec
  [ "$(mount_id "$DRIVE_ROOT")" = "$parent_id" ] && [ "$(mount_options "$DRIVE_ROOT")" = "$parent_options" ] || {
   echo 'Parent mount modified: aborting'; exit 1;
  }
  trap - EXIT INT TERM
  log_state "VST Exec active on $TARGET"
  ;;
 revert)
  revert_mount
  ;;
 status)
  printf 'Version: 0.2.0\nDrive: %s\nFolder: %s\n' "$DRIVE_ROOT" "$TARGET"
  findmnt -rn -M "$DRIVE_ROOT" -o SOURCE,TARGET,FSTYPE,OPTIONS || true
  findmnt -rn -M "$TARGET" -o SOURCE,TARGET,FSTYPE,OPTIONS || true
  if drive_mounted && [ -n "$(mount_id "$TARGET")" ] && [ "$(mount_id "$TARGET")" = "$(owner_id)" ]; then
   case ",$(mount_options "$TARGET")," in *,noexec,*) echo 'State: BLOCKED'; exit 1;; esac
   echo 'State: ACTIVE'
  else echo 'State: INACTIVE or disk absent'; exit 1; fi
  ;;
 *) echo 'Usage: drive-exec.sh apply [--wait 0..30] | revert | status'; exit 1;;
esac
