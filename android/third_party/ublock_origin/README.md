# uBlock Origin

Dao Browser Android redistributes a GeckoView compatibility variant of the
Firefox extension from the uBlock Origin 1.72.2 release.

- Project: https://github.com/gorhill/uBlock
- Release: https://github.com/gorhill/uBlock/releases/tag/1.72.2
- Artifact: `uBlock0_1.72.2.firefox.signed.xpi`
- SHA-256: `40c315b0da7871868155ecfae7a50a58dfa0920aebd865e008214986f1b7c578`
- Extension ID: `uBlock0@raymondhill.net`
- License: GNU General Public License 3.0

The official artifact is extracted into
`app/src/main/assets/extensions/ublock_origin/`, then the unsupported `menus`
permission and `commands` manifest entry are removed. uBlock already treats
both APIs as optional on Android, so its blocking code and filter assets remain
unchanged. For upgrades, replace the complete directory from a verified
official Firefox XPI, reapply and verify this compatibility change, and update
every pinned version and checksum in the build verifier, notices, tests, and
checklist.
