import { Command } from "commander";
import { existsSync, statSync, readdirSync } from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";
import chalk from "chalk";
import {
  ROOT_DIR,
  log,
  success,
  warn,
  error,
  which,
} from "../utils.js";

interface UploadOptions {
  bucket?: string;
  prefix?: string;
  key?: string;
  contentType?: string;
  remote?: boolean;
  dryRun?: boolean;
}

// Required env vars for wrangler to talk to R2 non-interactively. Wrangler
// reads CLOUDFLARE_API_TOKEN + CLOUDFLARE_ACCOUNT_ID directly from the env
// when --remote is used; we surface a clear error if either is missing.
const REQUIRED_ENV = ["CLOUDFLARE_ACCOUNT_ID", "CLOUDFLARE_API_TOKEN"] as const;

export const uploadCommand = new Command("upload")
  .description(
    "Upload build artifacts to Cloudflare R2 via wrangler. " +
      "Files are taken from positional args or stdin (one path per line)."
  )
  .argument("[files...]", "Files to upload (or pipe paths via stdin)")
  .option("-b, --bucket <name>", "R2 bucket name (or $R2_BUCKET)")
  .option(
    "-p, --prefix <prefix>",
    "Key prefix prepended to each uploaded file's basename",
    ""
  )
  .option(
    "-k, --key <key>",
    "Explicit object key (only valid when uploading a single file)"
  )
  .option(
    "--content-type <type>",
    "Override Content-Type (auto-detected by extension when omitted)"
  )
  .option(
    "--no-remote",
    "Upload to local wrangler simulator instead of the real bucket"
  )
  .option("--dry-run", "Print what would be uploaded without calling wrangler")
  .action(async (files: string[], opts: UploadOptions) => {
    const bucket = opts.bucket || process.env.R2_BUCKET;
    if (!bucket) {
      error(
        "Bucket not specified. Pass --bucket <name> or set R2_BUCKET in the environment."
      );
      process.exit(1);
    }

    if (!opts.dryRun && opts.remote !== false) {
      const missing = REQUIRED_ENV.filter((k) => !process.env[k]);
      if (missing.length > 0) {
        error(
          `Missing required env var(s): ${missing.join(", ")}.\n` +
            "Set these before uploading to a real R2 bucket:\n" +
            "  export CLOUDFLARE_ACCOUNT_ID=<account-id>\n" +
            "  export CLOUDFLARE_API_TOKEN=<token-with-r2-edit>\n" +
            "Or pass --no-remote to target the local wrangler simulator."
        );
        process.exit(1);
      }
    }

    const wrangler = which("wrangler") || which("npx");
    if (!wrangler) {
      error(
        "wrangler CLI not found. Install it with: npm i -g wrangler\n" +
          "Or run via npx (requires npx in PATH)."
      );
      process.exit(1);
    }

    const inputs = await collectInputs(files);
    if (inputs.length === 0) {
      error(
        "No files to upload. Pass paths as arguments or pipe them via stdin."
      );
      process.exit(1);
    }

    if (opts.key && inputs.length !== 1) {
      error("--key can only be used when uploading exactly one file.");
      process.exit(1);
    }

    const targets = expandTargets(inputs);
    log(
      `Uploading ${targets.length} file(s) to R2 bucket ${chalk.cyan(bucket)}` +
        (opts.remote === false ? chalk.dim(" (local simulator)") : "")
    );

    let failures = 0;
    for (const t of targets) {
      const key = computeKey(t, opts);
      const contentType =
        opts.contentType || guessContentType(t.absPath) || undefined;
      if (opts.dryRun) {
        console.log(
          chalk.dim(
            `[dry-run] ${t.absPath} -> r2://${bucket}/${key}` +
              (contentType ? ` (${contentType})` : "")
          )
        );
        continue;
      }
      const ok = await uploadOne({
        bucket,
        key,
        filePath: t.absPath,
        contentType,
        remote: opts.remote !== false,
      });
      if (!ok) failures += 1;
    }

    if (failures > 0) {
      error(`${failures} file(s) failed to upload.`);
      process.exit(1);
    }
    success(opts.dryRun ? "Dry-run complete" : "All files uploaded");
  });

interface FileTarget {
  absPath: string;
  // Path used to derive the key when no --key is set: file's basename, or
  // for directories the path relative to the directory root.
  relPath: string;
}

async function collectInputs(args: string[]): Promise<string[]> {
  if (args.length > 0) return args;
  if (process.stdin.isTTY) return [];
  const data = await readStdin();
  return data
    .split(/\r?\n/)
    .map((s) => s.trim())
    .filter((s) => s.length > 0);
}

function readStdin(): Promise<string> {
  return new Promise((resolve, reject) => {
    let buf = "";
    process.stdin.setEncoding("utf-8");
    process.stdin.on("data", (chunk) => (buf += chunk));
    process.stdin.on("end", () => resolve(buf));
    process.stdin.on("error", reject);
  });
}

// Resolve each input to one or more concrete files. Directories expand
// recursively so callers can do `dao upload dist/` to ship the whole folder.
function expandTargets(inputs: string[]): FileTarget[] {
  const out: FileTarget[] = [];
  for (const raw of inputs) {
    const abs = path.isAbsolute(raw) ? raw : path.resolve(ROOT_DIR, raw);
    if (!existsSync(abs)) {
      warn(`Skipping missing path: ${raw}`);
      continue;
    }
    const st = statSync(abs);
    if (st.isFile()) {
      out.push({ absPath: abs, relPath: path.basename(abs) });
    } else if (st.isDirectory()) {
      walkDir(abs, abs, out);
    } else {
      warn(`Skipping non-regular path: ${raw}`);
    }
  }
  return out;
}

function walkDir(root: string, dir: string, out: FileTarget[]): void {
  for (const name of readdirSync(dir)) {
    const p = path.join(dir, name);
    const st = statSync(p);
    if (st.isDirectory()) {
      walkDir(root, p, out);
    } else if (st.isFile()) {
      out.push({ absPath: p, relPath: path.relative(root, p) });
    }
  }
}

function computeKey(t: FileTarget, opts: UploadOptions): string {
  if (opts.key) return opts.key;
  const prefix = (opts.prefix || "").replace(/^\/+|\/+$/g, "");
  // Normalize Windows-style separators just in case.
  const rel = t.relPath.split(path.sep).join("/");
  return prefix ? `${prefix}/${rel}` : rel;
}

interface UploadArgs {
  bucket: string;
  key: string;
  filePath: string;
  contentType?: string;
  remote: boolean;
}

function uploadOne(args: UploadArgs): Promise<boolean> {
  const cliArgs = [
    "r2",
    "object",
    "put",
    `${args.bucket}/${args.key}`,
    "--file",
    args.filePath,
  ];
  if (args.contentType) {
    cliArgs.push("--content-type", args.contentType);
  }
  if (args.remote) {
    cliArgs.push("--remote");
  }

  console.log(
    chalk.dim(`$ wrangler ${cliArgs.map(shellEscape).join(" ")}`)
  );

  return new Promise((resolve) => {
    const child = spawn("wrangler", cliArgs, {
      cwd: ROOT_DIR,
      stdio: "inherit",
      env: process.env,
    });
    child.on("error", (err) => {
      error(`Failed to launch wrangler: ${err.message}`);
      resolve(false);
    });
    child.on("close", (code) => {
      if (code === 0) {
        success(`Uploaded ${args.key}`);
        resolve(true);
      } else {
        error(`wrangler exited with code ${code} for ${args.key}`);
        resolve(false);
      }
    });
  });
}

function shellEscape(s: string): string {
  return /^[\w.,/=:+@-]+$/.test(s) ? s : `'${s.replace(/'/g, `'\\''`)}'`;
}

const CONTENT_TYPES: Record<string, string> = {
  ".dmg": "application/x-apple-diskimage",
  ".zip": "application/zip",
  ".xml": "application/xml",
  ".json": "application/json",
  ".txt": "text/plain; charset=utf-8",
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "application/javascript",
  ".pkg": "application/octet-stream",
  ".tar": "application/x-tar",
  ".gz": "application/gzip",
  ".tgz": "application/gzip",
  ".sig": "application/octet-stream",
  // Sparkle binary delta patches (BinaryDelta archives produced by
  // generate_appcast). Served as octet-stream so browsers / proxies
  // don't try to sniff or transform them.
  ".delta": "application/octet-stream",
};

function guessContentType(filePath: string): string | null {
  return CONTENT_TYPES[path.extname(filePath).toLowerCase()] || null;
}
