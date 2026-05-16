import styles from './HostSkillsVisual.module.css';

interface HostRow {
  host: string;
  skills: string[];
}

const HOSTS: HostRow[] = [
  {
    host: 'github.com',
    skills: ['Summarize PR', 'Review diff', 'Find related issues'],
  },
  {
    host: 'youtube.com',
    skills: ['Transcript', 'Chapter summary', 'Bookmark timestamp'],
  },
  {
    host: 'x.com',
    skills: ['Unroll thread', 'Save to memory'],
  },
  {
    host: 'arxiv.org',
    skills: ['Explain abstract', 'Cite as BibTeX'],
  },
];

export function HostSkillsVisual() {
  return (
    <div className={styles.root} aria-hidden="true">
      <ul className={styles.list}>
        {HOSTS.map((row) => (
          <li key={row.host} className={styles.item}>
            <span className={styles.host}>{row.host}</span>
            <span className={styles.arrow}>→</span>
            <span className={styles.skills}>
              {row.skills.map((s) => (
                <span key={s} className={styles.skill}>
                  {s}
                </span>
              ))}
            </span>
          </li>
        ))}
      </ul>
    </div>
  );
}
