import styles from './BrowserFrameMockup.module.css';

export type MockupVariant = 'sidebar' | 'commandbar' | 'agent';

export interface BrowserFrameMockupProps {
  variant: MockupVariant;
}

/**
 * Photorealistic-feeling Dao Browser mockups, mirroring the actual layout
 * defined in src/dao/browser/ui/views:
 *
 *  - macOS traffic lights live at the TOP OF THE SIDEBAR (Dao removes the
 *    system titlebar entirely).
 *  - The address bar lives at the TOP OF THE PAGE CARD, not the sidebar.
 *    It contains nav buttons, a URL pill (host bold + path muted), and
 *    control-center buttons on the right.
 *  - The page card is a fully rounded (10px) elevated rectangle with a
 *    6-step soft shadow, inset 6px inside the window on three sides.
 *  - Pinned sites are a compact GRID of icon-only tiles at the top of
 *    the sidebar — never labelled tabs.
 *  - The agent variant has NO sidebar at all (kept tight on purpose).
 */

// ----- Reusable bits ---------------------------------------------------------

function TrafficLights() {
  return (
    <div className={styles.trafficLights}>
      <span className={`${styles.trafficLight} ${styles.trafficLightClose}`} />
      <span className={`${styles.trafficLight} ${styles.trafficLightMin}`} />
      <span className={`${styles.trafficLight} ${styles.trafficLightMax}`} />
    </div>
  );
}

function SidebarRail({
  active,
}: {
  active: 'docs' | 'github' | 'hn' | 'article';
}) {
  return (
    <aside className={styles.sidebar}>
      <TrafficLights />
      <div className={styles.sidebarInner}>
        <button type="button" className={styles.newTabButton}>
          <svg
            width="14"
            height="14"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="2"
            strokeLinecap="round"
            strokeLinejoin="round"
            aria-hidden="true"
          >
            <line x1="12" y1="5" x2="12" y2="19" />
            <line x1="5" y1="12" x2="19" y2="12" />
          </svg>
          <span>New Tab</span>
        </button>
        <div
          className={`${styles.tab} ${active === 'docs' ? styles.active : ''}`}
        >
          <span className={`${styles.favicon} ${styles.favDocs}`}>D</span>
          <span>Dao Browser docs</span>
        </div>
        <div
          className={`${styles.tab} ${active === 'github' ? styles.active : ''}`}
        >
          <span className={`${styles.favicon} ${styles.favGH}`}>G</span>
          <span>moonrailgun/dao-browser</span>
        </div>
        <div
          className={`${styles.tab} ${active === 'hn' ? styles.active : ''}`}
        >
          <span className={`${styles.favicon} ${styles.favHN}`}>Y</span>
          <span>Hacker News</span>
        </div>
        <div
          className={`${styles.tab} ${
            active === 'article' ? styles.active : ''
          }`}
        >
          <span className={`${styles.favicon} ${styles.favWiki}`}>W</span>
          <span>Why we still build browsers</span>
        </div>
      </div>
    </aside>
  );
}

function AddressBar({
  host,
  path,
}: {
  host: string;
  path?: string;
}) {
  return (
    <div className={styles.addressBar}>
      <div className={styles.navButtons}>
        <span className={`${styles.navButton} ${styles.disabled}`}>
          <ArrowIcon direction="left" />
        </span>
        <span className={styles.navButton}>
          <ArrowIcon direction="right" />
        </span>
        <span className={styles.navButton}>
          <RefreshIcon />
        </span>
      </div>

      <div className={styles.flexSpacer} />

      <div className={styles.urlPill}>
        <svg
          className={styles.lockIcon}
          width="11"
          height="11"
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          strokeWidth="2"
          strokeLinecap="round"
          strokeLinejoin="round"
        >
          <rect width="18" height="11" x="3" y="11" rx="2" ry="2" />
          <path d="M7 11V7a5 5 0 0 1 10 0v4" />
        </svg>
        <span className={styles.urlHost}>{host}</span>
        {path && <span className={styles.urlPath}>{path}</span>}
      </div>

      <div className={styles.flexSpacer} />

      <div className={styles.controlButtons}>
        <span className={styles.controlButton}>
          <ChatIcon />
        </span>
        <span className={styles.controlButton}>
          <ControlCenterIcon />
        </span>
      </div>
    </div>
  );
}

/* arrow-left / arrow-right (Lucide), used in nav buttons —
   matches LucideIcon::kArrowLeft / kArrowRight in the C++ source. */
function ArrowIcon({ direction }: { direction: 'left' | 'right' }) {
  return (
    <svg
      width="14"
      height="14"
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      {direction === 'left' ? (
        <>
          <path d="M5 12h14" />
          <path d="m12 5-7 7 7 7" />
        </>
      ) : (
        <>
          <path d="M5 12h14" />
          <path d="m12 5 7 7-7 7" />
        </>
      )}
    </svg>
  );
}

/* rotate-cw (Lucide), refresh button. */
function RefreshIcon() {
  return (
    <svg
      width="14"
      height="14"
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      <path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8" />
      <path d="M21 3v5h-5" />
    </svg>
  );
}

/* message-circle (Lucide), Dao chat / agent toggle. */
function ChatIcon() {
  return (
    <svg
      width="14"
      height="14"
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      <path d="M7.9 20A9 9 0 1 0 4 16.1L2 22Z" />
    </svg>
  );
}

/* sliders-horizontal (Lucide), Dao control center. */
function ControlCenterIcon() {
  return (
    <svg
      width="14"
      height="14"
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      <line x1="21" y1="4" x2="14" y2="4" />
      <line x1="10" y1="4" x2="3" y2="4" />
      <line x1="21" y1="12" x2="12" y2="12" />
      <line x1="8" y1="12" x2="3" y2="12" />
      <line x1="21" y1="20" x2="16" y2="20" />
      <line x1="12" y1="20" x2="3" y2="20" />
      <line x1="14" y1="2" x2="14" y2="6" />
      <line x1="8" y1="10" x2="8" y2="14" />
      <line x1="16" y1="18" x2="16" y2="22" />
    </svg>
  );
}

/* sparkles (Lucide) — only used in agent panel header. */
function Sparkle() {
  return (
    <svg
      width="13"
      height="13"
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      <path d="M11.017 2.814a1 1 0 0 1 1.966 0l1.051 5.558a2 2 0 0 0 1.594 1.594l5.558 1.051a1 1 0 0 1 0 1.966l-5.558 1.051a2 2 0 0 0-1.594 1.594l-1.051 5.558a1 1 0 0 1-1.966 0l-1.051-5.558a2 2 0 0 0-1.594-1.594l-5.558-1.051a1 1 0 0 1 0-1.966l5.558-1.051a2 2 0 0 0 1.594-1.594z" />
    </svg>
  );
}

function ArticlePage({
  host = 'dao.msgbyte.com',
  path = '/essay/why-we-still-build-browsers',
}: {
  host?: string;
  path?: string;
}) {
  return (
    <div className={styles.pageArea}>
      <div className={styles.page}>
        <AddressBar host={host} path={path} />
        <article className={styles.article}>
          <div className={styles.articleEyebrow}>ESSAY · 8 MIN READ</div>
          <h1 className={styles.articleTitle}>Why we still build browsers</h1>
          <div className={styles.articleByline}>by Lyon Chen · April 2026</div>
          <p className={styles.articleBody}>
            The browser has been with us for thirty years, and somehow it has never
            stopped feeling unfinished. Tabs sprawl. Sidebars clutter. Search bars
            become storefronts. We built Dao because the browser is too important
            to leave to whoever pays the most.
          </p>
          <p className={styles.articleQuote}>
            A browser is not a product. It is a place where you spend your day.
          </p>
        </article>
      </div>
    </div>
  );
}

// ----- Variants -------------------------------------------------------------

function SidebarMockup() {
  return (
    <>
      <SidebarRail active="article" />
      <ArticlePage />
    </>
  );
}

function CommandBarMockup() {
  return (
    <>
      <SidebarRail active="hn" />
      <ArticlePage host="news.ycombinator.com" path="" />
      <div className={styles.scrim}>
        <div className={styles.commandPanel}>
          <div className={styles.commandInputRow}>
            <svg
              className={styles.commandInputIcon}
              width="16"
              height="16"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
              strokeLinecap="round"
              strokeLinejoin="round"
            >
              <path d="m21 21-4.34-4.34" />
              <circle cx="11" cy="11" r="8" />
            </svg>
            <span className={styles.commandInputText}>vert</span>
            <span className={styles.commandCaret} aria-hidden="true" />
            <span className={styles.commandGhost}>ical tabs</span>
          </div>
          <div className={styles.commandSep} />
          <div className={`${styles.suggestion} ${styles.selected}`}>
            <svg
              className={styles.suggestionIcon}
              width="14"
              height="14"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
              strokeLinecap="round"
              strokeLinejoin="round"
            >
              <path d="m21 21-4.34-4.34" />
              <circle cx="11" cy="11" r="8" />
            </svg>
            <span className={styles.suggestionTitle}>
              Search for &quot;vertical tabs&quot;
            </span>
            <span className={styles.suggestionMeta}>↵</span>
          </div>
          <div className={styles.suggestion}>
            <svg
              className={styles.suggestionIcon}
              width="14"
              height="14"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
              strokeLinecap="round"
              strokeLinejoin="round"
            >
              <path d="M11.017 2.814a1 1 0 0 1 1.966 0l1.051 5.558a2 2 0 0 0 1.594 1.594l5.558 1.051a1 1 0 0 1 0 1.966l-5.558 1.051a2 2 0 0 0-1.594 1.594l-1.051 5.558a1 1 0 0 1-1.966 0l-1.051-5.558a2 2 0 0 0-1.594-1.594l-5.558-1.051a1 1 0 0 1 0-1.966l5.558-1.051a2 2 0 0 0 1.594-1.594z" />
            </svg>
            <span className={styles.suggestionTitle}>
              Ask AI: &quot;explain vertical tabs&quot;
            </span>
            <span className={styles.suggestionMeta}>⇧⏎</span>
          </div>
          <div className={styles.suggestion}>
            <svg
              className={styles.suggestionIcon}
              width="14"
              height="14"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
              strokeLinecap="round"
              strokeLinejoin="round"
            >
              <circle cx="12" cy="12" r="10" />
              <path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20" />
              <path d="M2 12h20" />
            </svg>
            <span className={styles.suggestionTitle}>
              dao.msgbyte.com/docs/sidebar
            </span>
            <span className={styles.suggestionMeta}>↗</span>
          </div>
        </div>
      </div>
    </>
  );
}

/** Agent variant: NO left rail. Page on the left, agent panel on the right.
 *  The traffic-light dots move into the agent panel header. */
function AgentMockup() {
  return (
    <div className={styles.agentRoot}>
      <div className={styles.agentPageArea}>
        <div className={styles.page}>
          <AddressBar host="dao.msgbyte.com" path="/essay/why-we-still-build-browsers" />
          <article className={styles.article}>
            <div className={styles.articleEyebrow}>ESSAY · 8 MIN READ</div>
            <h1 className={styles.articleTitle}>Why we still build browsers</h1>
            <div className={styles.articleByline}>by Lyon Chen · April 2026</div>
            <p className={styles.articleBody}>
              The browser has been with us for thirty years, and somehow it has never
              stopped feeling unfinished. Tabs sprawl. Sidebars clutter. Search bars
              become storefronts. We built Dao because the browser is too important
              to leave to whoever pays the most.
            </p>
            <p className={styles.articleQuote}>
              A browser is not a product. It is a place where you spend your day.
            </p>
          </article>
        </div>
      </div>
      <aside className={styles.agentPanel}>
        <div className={styles.agentTitlebar}>
          <Sparkle />
          <span className={styles.agentHeaderTitle}>Dao Agent</span>
          <span className={styles.agentHeaderModel}>Claude Fable 5</span>
        </div>
        <div className={styles.agentBody}>
          <div className={styles.agentLog}>
            <div className={styles.bubbleUser}>
              What is this article saying?
            </div>
            <div className={styles.bubbleAssistant}>
              The author argues that browsers are too important to be left to the
              highest bidder, and that Dao is built on the belief that this is a
              place you live in — not a product you visit.
              <div className={styles.toolCall}>
                <span className={styles.toolName}>read_page</span>{' '}
                dao.msgbyte.com/essay
              </div>
            </div>
            <div className={styles.bubbleUser}>Save the quote.</div>
          </div>
          <div className={styles.agentInput}>
            <svg
              width="12"
              height="12"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
              strokeLinecap="round"
              strokeLinejoin="round"
            >
              <path d="m22 2-7 20-4-9-9-4Z" />
              <path d="M22 2 11 13" />
            </svg>
            <span>Ask anything…</span>
          </div>
        </div>
      </aside>
    </div>
  );
}

export function BrowserFrameMockup({ variant }: BrowserFrameMockupProps) {
  if (variant === 'agent') {
    return <AgentMockup />;
  }
  return (
    <div className={styles.root}>
      {variant === 'sidebar' && <SidebarMockup />}
      {variant === 'commandbar' && <CommandBarMockup />}
    </div>
  );
}
