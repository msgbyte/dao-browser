import { ImageResponse } from 'next/og';

// Build-time-generated 1200×630 OG image. The output is a real PNG produced once
// at build time and shipped as a static asset, satisfying the spec's "static PNG"
// requirement without needing an external image tool (ImageMagick, etc.).
//
// next/og uses Satori under the hood — every container with multiple children
// must declare `display: flex` (or `display: none`), and there is no <br>;
// stack rows by adding sibling <div>s with `display: flex`.

export const dynamic = 'force-static';
export const contentType = 'image/png';
export const size = { width: 1200, height: 630 };
export const alt = 'Dao Browser — An opinionated browser, built on Chromium.';

export default function OpengraphImage() {
  return new ImageResponse(
    (
      <div
        style={{
          width: '100%',
          height: '100%',
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          justifyContent: 'center',
          background: 'rgb(231, 238, 245)',
          fontFamily: 'system-ui, -apple-system, sans-serif',
          padding: '80px',
          position: 'relative',
        }}
      >
        <div
          style={{
            display: 'flex',
            fontSize: 24,
            color: 'rgba(30, 20, 40, 0.40)',
            letterSpacing: '0.18em',
            textTransform: 'uppercase',
            marginBottom: 32,
          }}
        >
          DAO BROWSER
        </div>
        <div
          style={{
            display: 'flex',
            fontSize: 72,
            fontWeight: 600,
            color: 'rgba(30, 20, 40, 0.95)',
            letterSpacing: '-0.025em',
            lineHeight: 1.1,
            textAlign: 'center',
          }}
        >
          An opinionated browser,
        </div>
        <div
          style={{
            display: 'flex',
            fontSize: 72,
            fontWeight: 600,
            color: 'rgba(30, 20, 40, 0.95)',
            letterSpacing: '-0.025em',
            lineHeight: 1.1,
            textAlign: 'center',
            marginBottom: 32,
          }}
        >
          built on Chromium.
        </div>
        <div
          style={{
            display: 'flex',
            fontSize: 32,
            color: 'rgba(30, 20, 40, 0.60)',
            textAlign: 'center',
          }}
        >
          Vertical tabs, soft corners, content first.
        </div>
        <div
          style={{
            position: 'absolute',
            bottom: 48,
            right: 60,
            display: 'flex',
            alignItems: 'center',
            gap: 12,
          }}
        >
          <div
            style={{
              display: 'flex',
              width: 32,
              height: 32,
              borderRadius: 7,
              background: 'rgb(70, 120, 190)',
              alignItems: 'center',
              justifyContent: 'center',
              color: '#fff',
              fontWeight: 600,
              fontSize: 18,
            }}
          >
            D
          </div>
          <div
            style={{
              display: 'flex',
              fontSize: 20,
              color: 'rgba(30, 20, 40, 0.60)',
            }}
          >
            dao.msgbyte.com
          </div>
        </div>
      </div>
    ),
    { ...size },
  );
}
