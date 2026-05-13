/**
 * Bootstrap Dao i18n infrastructure: regenerate dao_strings.grd outputs/translations
 * blocks (mirroring Chromium's locale set) and ensure empty .xtb skeletons exist for
 * every locale. Also regenerates the WebUI locales/<lang>.ts skeletons.
 *
 * Reads the canonical locale list from engine/src/ui/strings/ax_strings.grd so that
 * Dao stays in lockstep with whatever Chromium ships at the pinned version.
 *
 * Usage:
 *   tsx scripts/i18n-bootstrap.ts
 */

import {
  existsSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";
import { ENGINE_DIR, ROOT_DIR } from "./utils.js";

const AX_STRINGS_GRD = path.join(
  ENGINE_DIR,
  "src",
  "ui",
  "strings",
  "ax_strings.grd"
);

const STRINGS_DIR = path.join(ROOT_DIR, "src", "dao", "browser", "strings");
const TRANSLATIONS_DIR = path.join(STRINGS_DIR, "translations");
const GRD_PATH = path.join(STRINGS_DIR, "dao_strings.grd");

const WEBUI_LOCALES_DIR = path.join(
  ROOT_DIR,
  "src",
  "dao",
  "browser",
  "ui",
  "webui",
  "resources",
  "agent",
  "i18n",
  "locales"
);

interface OutputEntry {
  filename: string;
  lang: string;
  iosVariant?: { iosFilename: string }; // pak filename to use under is_ios
  pseudo?: boolean;
  comment?: string;
}

interface TranslationEntry {
  filename: string; // path under translations/
  lang: string;
  comment?: string;
}

/**
 * Parse ax_strings.grd to extract the authoritative locale list, including the
 * is_ios variants for es-419 / pt-BR and the iw/he, no/nb special cases.
 */
function parseAxStrings(): {
  outputs: OutputEntry[];
  translations: TranslationEntry[];
} {
  const xml = readFileSync(AX_STRINGS_GRD, "utf-8");

  const outputs: OutputEntry[] = [];
  const translations: TranslationEntry[] = [];

  // Crude but robust: walk sections by tag, ignoring whitespace.
  const outputsBlock = xml.slice(
    xml.indexOf("<outputs>"),
    xml.indexOf("</outputs>")
  );
  const translationsBlock = xml.slice(
    xml.indexOf("<translations>"),
    xml.indexOf("</translations>")
  );

  // Parse outputs.
  // Track is_ios variant overrides: when we see a non-ios <output> for a lang,
  // record it; if the same lang also has an is_ios alternate, store the iOS pak
  // filename on the entry.
  const outputRe =
    /<output\s+filename="([^"]+)"\s+type="data_package"\s+lang="([^"]+)"\s*\/>/g;
  const ifIosRe = /<if expr="is_ios">([\s\S]*?)<\/if>/g;
  const ifNotIosRe = /<if expr="not is_ios">([\s\S]*?)<\/if>/g;

  // First, collect is_ios overrides into a map: lang -> iosFilename
  const iosOverrides = new Map<string, string>();
  for (const m of outputsBlock.matchAll(ifIosRe)) {
    for (const o of m[1].matchAll(outputRe)) {
      iosOverrides.set(o[2], o[1]);
    }
  }

  // Strip the is_ios blocks so we don't double-count.
  let nonIosBlock = outputsBlock
    .replace(ifIosRe, "")
    .replace(ifNotIosRe, (_, inner) => inner);

  // Extract default outputs in declaration order.
  const seen = new Set<string>();
  for (const m of nonIosBlock.matchAll(outputRe)) {
    const filename = m[1];
    const lang = m[2];
    if (seen.has(lang)) continue;
    seen.add(lang);

    const isPseudo = lang === "ar-XB" || lang === "en-XA";
    const entry: OutputEntry = {
      filename: filename.replace(/^ax_strings_/, "dao_strings_"),
      lang,
      pseudo: isPseudo,
    };
    if (iosOverrides.has(lang)) {
      entry.iosVariant = {
        iosFilename: iosOverrides
          .get(lang)!
          .replace(/^ax_strings_/, "dao_strings_"),
      };
    }
    outputs.push(entry);
  }

  // Parse translations (skip pseudo-locales — they are generated, not translated).
  const transRe =
    /<file\s+path="([^"]+)"\s+lang="([^"]+)"\s*\/>/g;
  for (const m of translationsBlock.matchAll(transRe)) {
    translations.push({
      filename: m[1].replace(/ax_strings_/, "dao_strings_"),
      lang: m[2],
    });
  }

  return { outputs, translations };
}

function buildGrd(
  outputs: OutputEntry[],
  translations: TranslationEntry[]
): string {
  const outLines: string[] = [];
  const isIosOverrides = outputs.filter((o) => o.iosVariant);

  for (const o of outputs) {
    if (o.pseudo) continue; // emit later
    if (o.iosVariant) {
      outLines.push(
        `    <if expr="is_ios">`,
        `      <output filename="${o.iosVariant.iosFilename}" type="data_package" lang="${o.lang}" />`,
        `    </if>`,
        `    <if expr="not is_ios">`,
        `      <output filename="${o.filename}" type="data_package" lang="${o.lang}" />`,
        `    </if>`
      );
    } else {
      // Match Chromium's tiny comment about 'no' -> 'nb' file naming quirk.
      if (o.lang === "no") {
        outLines.push(
          `    <!-- The translation console uses 'no' for Norwegian Bokmål. It should`,
          `         be 'nb'. -->`
        );
      }
      outLines.push(
        `    <output filename="${o.filename}" type="data_package" lang="${o.lang}" />`
      );
    }
  }
  // Pseudolocales last.
  outLines.push(`    <!-- Pseudolocales -->`);
  for (const o of outputs.filter((o) => o.pseudo)) {
    outLines.push(
      `    <output filename="${o.filename}" type="data_package" lang="${o.lang}" />`
    );
  }

  const transLines: string[] = [];
  for (const t of translations) {
    if (t.lang === "he") {
      transLines.push(
        `    <!-- The translation console uses 'iw' for Hebrew, but we use 'he'. -->`
      );
    }
    transLines.push(
      `    <file path="${t.filename}" lang="${t.lang}" />`
    );
  }

  return `<?xml version="1.0" encoding="UTF-8"?>

<!-- Dao Browser strings. Mirrors Chromium's locale set (see ax_strings.grd).

     IDS_DAO_* identifiers are owned by Dao. Add new strings to the <messages>
     block at the bottom of this file, then run \`tsx scripts/i18n-bootstrap.ts\`
     to make sure every locale has an empty .xtb skeleton on disk.

     This file is regenerated in part: the <outputs> and <translations> blocks
     are produced by scripts/i18n-bootstrap.ts. The <messages> block is hand-
     authored — only the surrounding scaffolding is generated. -->

<grit base_dir="." latest_public_release="0" current_release="1"
      source_lang_id="en">
  <outputs>
    <output filename="grit/dao_strings.h" type="rc_header">
      <emit emit_type="prepend"></emit>
    </output>
${outLines.join("\n")}
  </outputs>
  <translations>
${transLines.join("\n")}
  </translations>
  <release seq="1">
    <messages fallback_to_english="true">
      <!-- Sentinel string used to verify the i18n pipeline end-to-end. Safe to
           keep around indefinitely; it is not surfaced in real UI. -->
      <message name="IDS_DAO_TEST_PIPELINE" desc="Sentinel string used by the build to verify the Dao i18n pipeline is wired up correctly. Not user-facing.">
        Dao i18n pipeline OK
      </message>
    </messages>
  </release>
</grit>
`;
}

const EMPTY_XTB = (lang: string) =>
  `<?xml version="1.0" ?>
<!DOCTYPE translationbundle>
<translationbundle lang="${lang}">
</translationbundle>
`;

function ensureXtbSkeletons(translations: TranslationEntry[]): void {
  if (!existsSync(TRANSLATIONS_DIR)) {
    mkdirSync(TRANSLATIONS_DIR, { recursive: true });
  }
  let created = 0;
  for (const t of translations) {
    const fullPath = path.join(STRINGS_DIR, t.filename);
    if (existsSync(fullPath)) continue;
    const dir = path.dirname(fullPath);
    if (!existsSync(dir)) mkdirSync(dir, { recursive: true });
    writeFileSync(fullPath, EMPTY_XTB(t.lang));
    created++;
  }
  console.log(
    `xtb: ${created} created, ${translations.length - created} already present`
  );
}

const EMPTY_LOCALE_TS = (lang: string) =>
  `// Auto-generated by scripts/i18n-bootstrap.ts. Empty entries fall back to en.
// Lang: ${lang}
import en from './en.js';
import type { Dictionary } from '../i18n.js';

const dict: Dictionary = {
  ...en,
  // Override entries here as translations land.
};

export default dict;
`;

const EN_LOCALE_TS = `// Auto-generated by scripts/i18n-bootstrap.ts on first run; safe to extend.
// English source strings. Other locales import this for fallback coverage.
import type { Dictionary } from '../i18n.js';

const dict: Dictionary = {
  // Add entries as Dao WebUI strings are migrated to t().
};

export default dict;
`;

function ensureWebuiLocaleStubs(translations: TranslationEntry[]): void {
  if (!existsSync(WEBUI_LOCALES_DIR)) {
    mkdirSync(WEBUI_LOCALES_DIR, { recursive: true });
  }

  // Always ensure en.ts exists first (acts as the fallback dictionary).
  const enPath = path.join(WEBUI_LOCALES_DIR, "en.ts");
  if (!existsSync(enPath)) writeFileSync(enPath, EN_LOCALE_TS);

  // Build the union of locale codes used by the WebUI side. We mirror the .xtb
  // set, plus add 'en' explicitly (xtb has no 'en' since it is the source).
  const langs = new Set<string>(["en", ...translations.map((t) => t.lang)]);

  let created = 0;
  for (const lang of langs) {
    if (lang === "en") continue;
    const fullPath = path.join(WEBUI_LOCALES_DIR, `${lang}.ts`);
    if (existsSync(fullPath)) continue;
    writeFileSync(fullPath, EMPTY_LOCALE_TS(lang));
    created++;
  }
  console.log(
    `webui locales: ${created} created, ${langs.size - 1 - created} already present (excluding en)`
  );

  // Regenerate the GN fragment that lists every locale .ts file. The agent
  // BUILD.gn imports this so it doesn't need a manual entry per locale —
  // run this script after Chromium adds/removes a locale and the build
  // picks them up.
  const gniLines = [
    "# Auto-generated by scripts/i18n-bootstrap.ts. DO NOT EDIT BY HAND —",
    "# rerun the script to refresh after Chromium's locale list changes.",
    "#",
    "# Imported by ../BUILD.gn to extend ts_files with the locale dictionaries.",
    "",
    "dao_i18n_locale_ts_files = [",
    `  "i18n/i18n.ts",`,
    `  "i18n/locales/en.ts",`,
  ];
  for (const lang of [...langs].filter((l) => l !== "en").sort()) {
    gniLines.push(`  "i18n/locales/${lang}.ts",`);
  }
  gniLines.push("]", "");
  const gniPath = path.join(
    WEBUI_LOCALES_DIR,
    "..",
    "i18n_locale_files.gni"
  );
  writeFileSync(gniPath, gniLines.join("\n"));
  console.log(`wrote ${path.relative(ROOT_DIR, gniPath)}`);
}

function main(): void {
  if (!existsSync(STRINGS_DIR)) mkdirSync(STRINGS_DIR, { recursive: true });

  console.log(`Reading authoritative locale list from ${path.relative(ROOT_DIR, AX_STRINGS_GRD)}`);
  const { outputs, translations } = parseAxStrings();
  console.log(
    `parsed: ${outputs.length} outputs (incl. ${outputs.filter((o) => o.pseudo).length} pseudo, ${outputs.filter((o) => o.iosVariant).length} ios overrides), ${translations.length} translations`
  );

  // Only regenerate the .grd if it does not exist yet, OR if the user explicitly
  // requests it via --regen. Once authored, the <messages> block is hand-edited
  // and we don't want to clobber it. The bootstrap is idempotent w.r.t. xtb
  // skeletons regardless.
  const regen = process.argv.includes("--regen-grd");
  if (!existsSync(GRD_PATH) || regen) {
    writeFileSync(GRD_PATH, buildGrd(outputs, translations));
    console.log(
      `${regen ? "regenerated" : "wrote"} ${path.relative(ROOT_DIR, GRD_PATH)}`
    );
  } else {
    console.log(
      `kept existing ${path.relative(ROOT_DIR, GRD_PATH)} (use --regen-grd to overwrite)`
    );
  }

  ensureXtbSkeletons(translations);
  ensureWebuiLocaleStubs(translations);
}

main();
