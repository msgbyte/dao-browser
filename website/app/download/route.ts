import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { NextResponse, type NextRequest } from 'next/server';

interface PlatformInfo {
  label: string;
  url: string;
}

interface DownloadConfig {
  version: string;
  chromiumVersion: string;
  releasedAt: string;
  platforms: Record<string, PlatformInfo>;
  default: string;
}

function detectPlatformKey(userAgent: string, config: DownloadConfig): string {
  const ua = userAgent.toLowerCase();
  if (ua.includes('mac')) {
    if (config.platforms.macArm64) return 'macArm64';
  }
  if (ua.includes('win') && config.platforms.win) return 'win';
  if (ua.includes('linux') && config.platforms.linux) return 'linux';
  return config.default;
}

async function loadConfig(): Promise<DownloadConfig> {
  const filePath = path.join(process.cwd(), 'public', 'info.json');
  const raw = await readFile(filePath, 'utf-8');
  return JSON.parse(raw) as DownloadConfig;
}

export async function GET(request: NextRequest) {
  const accept = request.headers.get('accept') ?? '';
  const wantsHtml = accept.includes('text/html');

  if (wantsHtml) {
    const url = request.nextUrl.clone();
    url.pathname = '/download-browser';
    return NextResponse.rewrite(url);
  }

  try {
    const config = await loadConfig();
    const userAgent = request.headers.get('user-agent') ?? '';
    const key = detectPlatformKey(userAgent, config);
    const target =
      config.platforms[key]?.url ?? config.platforms[config.default]?.url;

    if (!target) {
      return new NextResponse('No download URL configured for this platform.', {
        status: 500,
      });
    }

    return NextResponse.redirect(target, 302);
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    return new NextResponse(`Failed to resolve download: ${message}`, {
      status: 500,
    });
  }
}
