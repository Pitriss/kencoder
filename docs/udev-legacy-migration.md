# Migration of the original OMEN Encoder udev rule

The `encoder` Debian package installs `70-encoder-uaccess.rules` and
`99-encoder.rules` under `/usr/lib/udev/rules.d/`. The original upstream file,
`97-omen_encoder.rules`, grants `MODE="0666"` and `GROUP="users"` access to
HP OMEN Encoder's HID interface. That old rule should not be active alongside
the new package rules.

During `postinst configure`, the package checks for the **byte-for-byte
unchanged upstream rule**. If an unowned copy is found in
`/etc/udev/rules.d/97-omen_encoder.rules`, it is moved to
`97-omen_encoder.rules.kencoder-disabled` in that same directory and replaced
by a symlink to `/dev/null`. A filename ending in `.kencoder-disabled` is not
processed by udev. The new, packaged rules continue to apply.

If the old rule is installed in `/usr/lib/udev/rules.d/`,
`/usr/local/lib/udev/rules.d/`, `/lib/udev/rules.d/` or `/run/udev/rules.d/`,
a matching `/etc/udev/rules.d/97-omen_encoder.rules -> /dev/null` mask is used,
without modifying the vendor file. Different or customized rules, already
existing backups, and files in `/etc` owned by another Debian package are
**never overwritten**. Instead, installation prints a warning for manual review.
An administrator's existing `/dev/null` mask is preserved.

The migration is idempotent across repeated installations and upgrades.
It reloads udev rules, but **does not trigger or rebind** a connected keyboard:
unplug/replug the keyboard if its existing HID permissions have not refreshed.

When `encoder` is **purged**, only a mask recorded as created by `encoder` is
removed, and a backed-up original `/etc` file is restored if it is still
unchanged. This restores the previous, insecure rule on purge, so review it
before removing the CLI package. Removing without purging keeps the mask.
The administrator's manually modified rules and pre-existing masks are never
removed automatically.
