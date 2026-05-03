import Link from 'next/link';

export default function NotFound() {
  return (
    <main
      style={{
        minHeight: '70vh',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        justifyContent: 'center',
        gap: 16,
        padding: 24,
        textAlign: 'center',
      }}
    >
      <h1 style={{ fontSize: 32, fontWeight: 600, margin: 0 }}>Page not found</h1>
      <p style={{ color: 'var(--text-secondary)', margin: 0 }}>
        The page you were looking for doesn&apos;t exist.
      </p>
      <Link
        href="/"
        style={{
          marginTop: 12,
          padding: '10px 18px',
          borderRadius: 'var(--radius)',
          background: 'var(--accent)',
          color: '#fff',
          fontSize: 14,
        }}
      >
        Back to home
      </Link>
    </main>
  );
}
