# Third-party Notices

Dao Browser Android consumes Mozilla Android Components and GeckoView under the Mozilla Public License 2.0.

- Mozilla Public License 2.0: https://www.mozilla.org/MPL/2.0/
- Android Components source: https://searchfox.org/firefox-main/source/mobile/android/android-components
- GeckoView source: https://searchfox.org/firefox-main/source/mobile/android/geckoview

The release packaging task must generate the complete transitive dependency notice set before public distribution.

## uBlock Origin

Dao Browser Android bundles a GeckoView compatibility variant of the Firefox
extension from uBlock Origin 1.72.2 under the GNU General Public License 3.0.
The variant removes only the GeckoView-unsupported `menus` permission and
`commands` manifest entry; blocking code and filter assets are unchanged.

- Source: https://github.com/gorhill/uBlock
- Release: https://github.com/gorhill/uBlock/releases/tag/1.72.2
- Artifact: `uBlock0_1.72.2.firefox.signed.xpi`
- SHA-256: `40c315b0da7871868155ecfae7a50a58dfa0920aebd865e008214986f1b7c578`
- License text: `third_party/ublock_origin/LICENSE.txt`

The corresponding source is available from the source and release links above.
Release packaging must preserve this notice, license text, and source access.

## KISS Translator

Dao Browser Android bundles the unmodified public Firefox extension KISS
Translator 2.0.29 under the GNU General Public License v3.0 only.

- Source: https://github.com/fishjar/kiss-translator
- Release: https://github.com/fishjar/kiss-translator/releases/tag/v2.0.29
- Listing: https://addons.mozilla.org/firefox/addon/kiss-translator/
- Artifact: `kiss_translator-2.0.29.xpi`
- SHA-256: `0316f026b1b0c3d171262b3f4f369e34013abdf1d1bbe9d348290c3db53f4092`
- License text: `app/src/main/assets/notices/kiss_translator_license.txt`

Translation is performed through the service selected inside KISS Translator.
Release packaging must preserve this notice, license text, and source access.
