import { NextResponse, type NextRequest } from 'next/server';

export function GET(request: NextRequest) {
  // The page can distinguish desktop-mode iPads from Macs using touch support.
  const url = request.nextUrl.clone();
  url.pathname = '/download-browser';
  return NextResponse.redirect(url, 302);
}
