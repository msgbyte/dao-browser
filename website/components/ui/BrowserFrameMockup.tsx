import styles from './BrowserFrameMockup.module.css';

export type MockupVariant = 'sidebar' | 'commandbar' | 'agent';

export interface BrowserFrameMockupProps {
  variant: MockupVariant;
}

function SidebarMockup() {
  return (
    <>
      <aside className={styles.sidebar}>
        <div className={styles.urlPill} />
        <div className={`${styles.tab} ${styles.tabWide}`} />
        <div className={`${styles.tab} ${styles.tabMid}`} />
        <div className={styles.tabActive} />
        <div className={`${styles.tab} ${styles.tabWide}`} />
        <div className={`${styles.tab} ${styles.tabNarrow}`} />
        <div className={styles.sectionLabel}>Folders</div>
        <div className={`${styles.tab} ${styles.tabMid}`} />
        <div className={`${styles.tab} ${styles.tabNarrow}`} />
      </aside>
      <main className={styles.content}>
        <div className={styles.contentArt}>
          <div className={`${styles.contentBar} ${styles.contentBarLong}`} />
          <div className={`${styles.contentBar} ${styles.contentBarMid}`} />
          <div className={`${styles.contentBar} ${styles.contentBarShort}`} />
        </div>
      </main>
    </>
  );
}

function CommandBarMockup() {
  return (
    <>
      <SidebarMockup />
      <div className={styles.scrim}>
        <div className={styles.commandPanel}>
          <div className={styles.commandInput} />
          <div className={`${styles.suggestion} ${styles.suggestionSelected}`} />
          <div className={styles.suggestion} />
          <div className={styles.suggestion} />
        </div>
      </div>
    </>
  );
}

function AgentMockup() {
  return (
    <>
      <SidebarMockup />
      <aside className={styles.agentPanel}>
        <div className={styles.bubbleUser} />
        <div className={styles.bubbleAssistant} />
        <div className={styles.bubbleUser} style={{ width: '50%' }} />
        <div className={styles.bubbleAssistant} style={{ width: '70%' }} />
      </aside>
    </>
  );
}

export function BrowserFrameMockup({ variant }: BrowserFrameMockupProps) {
  return (
    <div className={styles.root}>
      {variant === 'sidebar' && <SidebarMockup />}
      {variant === 'commandbar' && <CommandBarMockup />}
      {variant === 'agent' && <AgentMockup />}
    </div>
  );
}
