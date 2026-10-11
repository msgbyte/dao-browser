// @vitest-environment node
import { describe, expect, it } from 'vitest';
import { NextRequest } from 'next/server';
import { GET } from '../route';

// A desktop-mode iPad can send this same user agent as a real Mac.
const mac = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15';

describe('/download', () => {
  it.each(['text/html', '*/*'])('defers device detection to the page for Accept: %s', async accept => {
    const response = await GET(new NextRequest('https://dao.example/download', {
      headers: { 'user-agent': mac, accept },
    }));
    expect(response.status).toBe(302);
    expect(response.headers.get('location')).toBe('https://dao.example/download-browser');
  });
});
