import { LucideIcon } from '../LucideIcon';
import styles from './AgentVisuals.module.css';

/* =====================================================================
 * Each export below is a self-contained illustration that fits in the
 * right column of a FeatureSection. They all share the same outer frame
 * (.frame) so the page reads as a single rhythm.
 * ===================================================================== */

function Frame({ children }: { children: React.ReactNode }) {
  return (
    <div className={styles.frame} aria-hidden="true">
      {children}
    </div>
  );
}

// ---------------------------------------------------------------------
// FocusContextVisual — current page + selected text feeds the agent
// ---------------------------------------------------------------------
export function FocusContextVisual() {
  return (
    <Frame>
      <div className={styles.focusRoot}>
        <div className={styles.focusPage}>
          <div className={styles.focusHost}>arxiv.org/abs/2402.14207</div>
          <h4 className={styles.focusTitle}>Learning to compress prompts</h4>
          <span className={styles.focusLine} />
          <span className={`${styles.focusLine} ${styles.short}`} />
          <span className={`${styles.focusLine} ${styles.highlight}`} />
          <span className={styles.focusLine} />
          <span className={`${styles.focusLine} ${styles.short}`} />
        </div>

        <div className={styles.focusArrow}>→</div>

        <div className={styles.focusAgent}>
          <div className={styles.focusAgentHeader}>
            <span className={styles.dot} />
            Dao Agent
          </div>
          <span className={styles.focusContextChip}>
            <LucideIcon name="scan-eye" size={11} />
            selection · 1 paragraph
          </span>
          <div className={styles.focusBubble}>Explain this in plain terms.</div>
          <div className={styles.focusReply}>
            It&apos;s a way to compress long prompts into shorter ones
            without losing meaning — like JPEG for text.
          </div>
        </div>
      </div>
    </Frame>
  );
}

// ---------------------------------------------------------------------
// MemoryTimelineVisual — long-term memory as cards on a timeline
// ---------------------------------------------------------------------
const MEMORIES: { date: string; kind: string; text: string }[] = [
  {
    date: '03 / 12',
    kind: 'user',
    text: 'Prefers Python for scripting, TypeScript for everything else.',
  },
  {
    date: '04 / 02',
    kind: 'project',
    text: 'Working on Dao Browser — vertical-tab Chromium fork.',
  },
  {
    date: '04 / 18',
    kind: 'feedback',
    text: 'No trailing summaries. Diffs are enough.',
  },
  {
    date: '05 / 06',
    kind: 'reference',
    text: 'Design tokens live in app/globals.css.',
  },
];

export function MemoryTimelineVisual() {
  return (
    <Frame>
      <div className={styles.memoryRoot}>
        {MEMORIES.map((m) => (
          <div key={m.text} className={styles.memoryRow}>
            <div className={styles.memoryDate}>{m.date}</div>
            <div className={styles.memoryCard}>
              <span className={styles.memoryKind}>{m.kind}</span>
              <div>{m.text}</div>
            </div>
          </div>
        ))}
      </div>
    </Frame>
  );
}

// ---------------------------------------------------------------------
// SoulVisual — a single long-running companion, shaped by you over time
// ---------------------------------------------------------------------
export function SoulVisual() {
  return (
    <Frame>
      <div className={styles.soulSingleCard}>
        <div className={styles.soulSingleHeader}>
          <div className={styles.soulAvatar}>
            <LucideIcon name="heart" size={18} />
          </div>
          <div className={styles.soulHeaderText}>
            <div className={styles.soulHeaderName}>Dao Agent</div>
            <div className={styles.soulHeaderSince}>
              with you since March 2026 · shaped by 47 reflections
            </div>
          </div>
        </div>

        <div className={styles.soulTagline}>
          &ldquo;Same secretary every morning. Sharper every month.&rdquo;
        </div>

        <div className={styles.soulFieldGroup}>
          <div className={styles.soulField}>
            <span className={styles.soulFieldLabel}>voice</span>
            <span className={styles.soulFieldValue}>
              warm · terse · skips greetings
            </span>
          </div>
          <div className={styles.soulField}>
            <span className={styles.soulFieldLabel}>language</span>
            <span className={styles.soulFieldValue}>German first, EN for code</span>
          </div>
          <div className={styles.soulField}>
            <span className={styles.soulFieldLabel}>cares about</span>
            <span className={styles.soulFieldValue}>
              shipping · design polish · your sleep
            </span>
          </div>
          <div className={styles.soulField}>
            <span className={styles.soulFieldLabel}>avoids</span>
            <span className={styles.soulFieldValue}>
              corporate tone · empty summaries
            </span>
          </div>
        </div>

        <div className={styles.soulFootnote}>
          <LucideIcon name="repeat" size={11} />
          last refined 3 days ago from a conversation about PR review
        </div>
      </div>
    </Frame>
  );
}

// ---------------------------------------------------------------------
// EvolutionVisual — feedback loop: chat → reflect → update → improve
// ---------------------------------------------------------------------
const EVOLUTION_NODES: {
  icon: 'sparkles' | 'eye' | 'brain' | 'refresh-cw';
  label: string;
  sub: string;
}[] = [
  { icon: 'sparkles', label: 'Use', sub: 'You chat, click, browse.' },
  { icon: 'eye', label: 'Reflect', sub: 'Agent watches the outcome.' },
  { icon: 'brain', label: 'Update', sub: 'Memory & soul edit themselves.' },
  { icon: 'refresh-cw', label: 'Improve', sub: 'Next answer is sharper.' },
];

export function EvolutionVisual() {
  return (
    <Frame>
      <div className={styles.evolutionRoot}>
        <div className={styles.evolutionTrack}>
          {EVOLUTION_NODES.map((n) => (
            <div key={n.label} className={styles.evolutionNode}>
              <div className={styles.evolutionNodeIcon}>
                <LucideIcon name={n.icon} size={16} />
              </div>
              <div className={styles.evolutionNodeLabel}>{n.label}</div>
              <div className={styles.evolutionNodeSub}>{n.sub}</div>
            </div>
          ))}
          <div className={styles.evolutionLoopHint}>
            <LucideIcon name="repeat" size={11} />
            self-evolving loop
          </div>
        </div>
      </div>
    </Frame>
  );
}

// ---------------------------------------------------------------------
// WorkspaceVisual — sandboxed file tree with quota + capabilities
// ---------------------------------------------------------------------
export function WorkspaceVisual() {
  return (
    <Frame>
      <div className={styles.workspaceRoot}>
        <div className={styles.workspaceTree}>
          <div className={styles.workspaceTreeHeader}>
            <LucideIcon name="folder" size={12} />
            agent workspace
          </div>
          <div className={styles.treeRow}>{`├─ notes/`}</div>
          <div className={`${styles.treeRow} ${styles.muted}`}>
            {`│  ├─ reading-list.md`}
          </div>
          <div className={`${styles.treeRow} ${styles.muted}`}>
            {`│  └─ inbox.md`}
          </div>
          <div className={styles.treeRow}>{`├─ scrape/`}</div>
          <div className={`${styles.treeRow} ${styles.accent}`}>
            {`│  └─ arxiv-2402.json`}
          </div>
          <div className={styles.treeRow}>{`├─ scripts/`}</div>
          <div className={`${styles.treeRow} ${styles.muted}`}>
            {`│  └─ summarize.py`}
          </div>
          <div className={styles.treeRow}>{`└─ .souls/`}</div>
          <div className={`${styles.treeRow} ${styles.muted}`}>
            {`   └─ reviewer.toml`}
          </div>
        </div>

        <div className={styles.workspaceMeta}>
          <div className={styles.workspaceMetaRow}>
            <span className={styles.workspaceMetaLabel}>Scope</span>
            <span className={styles.workspaceMetaValue}>
              sandboxed · path-normalized
            </span>
          </div>
          <div className={styles.workspaceQuota}>
            <span className={styles.workspaceMetaLabel}>Quota</span>
            <div className={styles.workspaceQuotaBar}>
              <div className={styles.workspaceQuotaFill} />
            </div>
            <span className={styles.workspaceQuotaLabel}>
              31.4 MB / 100 MB
            </span>
          </div>
          <ul className={styles.workspaceCapList}>
            <li>
              <span className={styles.workspaceCapBadge}>R</span>
              Read project files inside scope
            </li>
            <li>
              <span className={styles.workspaceCapBadge}>W</span>
              Patch &amp; write with V4A diffs
            </li>
            <li>
              <span className={styles.workspaceCapBadge}>LS</span>
              List, never escape root
            </li>
          </ul>
        </div>
      </div>
    </Frame>
  );
}

// ---------------------------------------------------------------------
// ModelSwapVisual — bring-your-own-model, vendor agnostic
// ---------------------------------------------------------------------
export function ModelSwapVisual() {
  return (
    <Frame>
      <div className={styles.modelRoot}>
        <div className={styles.modelRow}>
          <span className={`${styles.modelChip} ${styles.active}`}>
            <span className={styles.modelChipDot} /> Claude Fable 5
          </span>
          <span className={styles.modelChip}>
            <span className={styles.modelChipDot} /> GPT-5.5
          </span>
          <span className={styles.modelChip}>
            <span className={styles.modelChipDot} /> Gemini 3.5 Flash
          </span>
          <span className={styles.modelChip}>
            <span className={styles.modelChipDot} /> DeepSeek-V4-Pro
          </span>
          <span className={styles.modelChip}>
            <span className={styles.modelChipDot} /> Qwen3.5 (local)
          </span>
        </div>

        <div className={styles.modelSocket}>
          <span>plug any of them in</span>
          <div className={styles.modelSocketBox}>
            <LucideIcon name="cpu" size={16} />
            OpenAI-compatible API
          </div>
          <span className={styles.modelHint}>
            Your endpoint, your key, your data. Souls and memory stay portable
            across providers — switch any time, take everything with you.
          </span>
        </div>
      </div>
    </Frame>
  );
}
