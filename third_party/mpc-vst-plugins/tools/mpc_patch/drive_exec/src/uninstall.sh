#!/bin/sh
set -eu
# Removes the patch: stops the timer, takes the executable mount off (a normal umount, never forced or lazy), deletes the files this
# patch installed. Plugins, skins, MPC.settings and fstab are not touched. Original: timomacquis, 0.1.3 (adapted: the drive and folder
# come from the config; DEX_PREFIX, tests only, redirects the paths).
P=${DEX_PREFIX:-}
if [ -z "$P" ]; then PATH=/usr/sbin:/usr/bin:/sbin:/bin; export PATH; fi   # fixed PATH on the device; tests (DEX_PREFIX) bring their own shims
[ "$(id -u)" = 0 ] || { echo 'Root required'; exit 1; }
[ -f "$P/etc/drive-exec/VERSION" ] || { echo 'Patch not installed'; exit 0; }
[ "$(cat "$P/etc/drive-exec/VERSION")" = 0.2.0 ] || { echo 'Different version: use its matching uninstaller'; exit 1; }
DRIVE_ROOT=; EXEC_DIR=
. "$P/etc/drive-exec/config"
mpc_service() {
 if systemctl cat acvs >/dev/null 2>&1; then echo acvs
 elif systemctl cat inmusic-mpc >/dev/null 2>&1; then echo inmusic-mpc
 else echo acvs; fi
}
for pid in $(pidof MPC || true); do
 if grep -qF "$DRIVE_ROOT/$EXEC_DIR/" "/proc/$pid/maps"; then
  echo "MPC is using a plugin from $DRIVE_ROOT/$EXEC_DIR. Save the project, stop MPC, then retry the removal."
  echo "Command after saving: systemctl stop $(mpc_service)"
  exit 1
 fi
done
# Retain the existing enabled state if revert fails.
ENABLED=0
systemctl is-enabled --quiet drive-exec.timer && ENABLED=1 || true
systemctl disable --now drive-exec.timer
systemctl stop drive-exec.service
if ! /bin/sh "$P/etc/drive-exec/drive-exec.sh" revert; then
 [ "$ENABLED" = 0 ] || systemctl enable --now drive-exec.timer
 echo 'Uninstallation cancelled: mount busy or unmanaged'
 exit 1
fi
/bin/sh "$P/etc/drive-exec/root-bootstrap.sh" remove
systemctl stop drive-exec-bootstrap.service 2>/dev/null || true
rm -f "$P/etc/systemd/system/drive-exec.service" "$P/etc/systemd/system/drive-exec.timer"
rm -f "$P/etc/drive-exec/drive-exec.sh" "$P/etc/drive-exec/config" "$P/etc/drive-exec/VERSION" "$P/etc/drive-exec/uninstall.sh" "$P/etc/drive-exec/bootstrap.sh" "$P/etc/drive-exec/root-bootstrap.sh"
rmdir "$P/etc/drive-exec" 2>/dev/null || true
systemctl daemon-reload
rm -f "${DRIVE_EXEC_STATE:-$P/run/drive-exec}/lock" "${DRIVE_EXEC_STATE:-$P/run/drive-exec}/last-status"
rmdir "${DRIVE_EXEC_STATE:-$P/run/drive-exec}" 2>/dev/null || true
sync
echo 'Patch removed. The drive itself stays mounted as it was.'
echo 'Backups remain in /data/mpc-vst-plugins/backups.'
echo "If MPC was stopped manually, restart it: systemctl start $(mpc_service)"
echo 'Plugins on the drive stay where they are, but they will not load again until the patch is back (the drive is noexec).'
