import type { Metadata } from 'next';
import { TopNav } from '@/components/TopNav';
import { Footer } from '@/components/Footer';
import { FeatureSection } from '@/components/FeatureSection';
import { HostSkillsVisual } from '@/components/ui/HostSkillsVisual';
import { Button } from '@/components/ui/Button';
import { LucideIcon } from '@/components/ui/LucideIcon';
import {
  FocusContextVisual,
  MemoryTimelineVisual,
  SoulVisual,
  EvolutionVisual,
  WorkspaceVisual,
  ModelSwapVisual,
} from '@/components/ui/agent/AgentVisuals';
import { GITHUB_URL, SITE_URL } from '@/lib/version';
import styles from './page.module.css';

export const metadata: Metadata = {
  title: 'Dao Agent — A browser-native AI that lives next to your tabs.',
  description:
    'Dao Agent is the AI built into Dao Browser. Focus-aware context, long-term memory, switchable souls, host-based skills, sandboxed workspace, self-evolution, and no vendor lock-in.',
  alternates: {
    canonical: `${SITE_URL}/agent`,
  },
  openGraph: {
    title: 'Dao Agent — AI built into the browser.',
    description:
      'Focus-aware context, long-term memory, switchable souls, host-based skills, sandboxed workspace, self-evolution. Bring your own model.',
    url: `${SITE_URL}/agent`,
    siteName: 'Dao Browser',
    type: 'website',
  },
};

export default function AgentPage() {
  return (
    <>
      <TopNav />
      <main>
        {/* -------- Hero -------- */}
        <section id="top" className={styles.hero}>
          <div className={styles.eyebrow}>DAO AGENT</div>
          <h1 className={styles.h1}>
            A browser-native AI that learns you, on your terms.
          </h1>
          <p className={styles.subtitle}>
            Focus-aware context, long-term memory, switchable souls, per-host
            skills, a sandboxed workspace, self-evolution — and the model
            choice stays yours.
          </p>

          <div className={styles.heroPillRow}>
            <span className={styles.heroPill}>
              <LucideIcon name="scan-eye" size={13} /> Focus context
            </span>
            <span className={styles.heroPill}>
              <LucideIcon name="brain" size={13} /> Long-term memory
            </span>
            <span className={styles.heroPill}>
              <LucideIcon name="heart" size={13} /> A soul of its own
            </span>
            <span className={styles.heroPill}>
              <LucideIcon name="repeat" size={13} /> Self-evolution
            </span>
            <span className={styles.heroPill}>
              <LucideIcon name="globe" size={13} /> Host-based skills
            </span>
            <span className={styles.heroPill}>
              <LucideIcon name="folder" size={13} /> Workspace
            </span>
            <span className={styles.heroPill}>
              <LucideIcon name="lock-open" size={13} /> Your API · your data
            </span>
          </div>

          <nav className={styles.toc} aria-label="Page sections">
            <a className={styles.tocLink} href="#focus">Focus</a>
            <a className={styles.tocLink} href="#memory">Memory</a>
            <a className={styles.tocLink} href="#soul">Soul</a>
            <a className={styles.tocLink} href="#evolution">Evolution</a>
            <a className={styles.tocLink} href="#skills">Skills</a>
            <a className={styles.tocLink} href="#workspace">Workspace</a>
            <a className={styles.tocLink} href="#models">Models</a>
          </nav>
        </section>

        {/* -------- 01 Focus context -------- */}
        <FeatureSection
          id="focus"
          eyebrow="01 / FOCUS CONTEXT"
          heading="It already knows what you're looking at."
          body="Every prompt to Dao Agent carries the page you're on and what you've selected — automatically, no copy-paste. Ask a follow-up question and the agent answers about this paragraph, this code block, this video timestamp. The browser is the context."
          bullets={[
            { icon: 'scan-eye', label: 'Current page + DOM snapshot' },
            { icon: 'mouse-pointer-2', label: 'Highlighted selection auto-attached' },
            { icon: 'target', label: 'No context-pasting, no tab-juggling' },
          ]}
          customVisual={<FocusContextVisual />}
          mockupSide="right"
        />

        {/* -------- 02 Long-term memory -------- */}
        <FeatureSection
          id="memory"
          eyebrow="02 / LONG-TERM MEMORY"
          heading="Tell it once. It remembers."
          body="Facts you share — your role, your preferences, the projects you're shipping — become structured memory entries the agent loads on every turn. No prompt rewriting between sessions. The memory is local, inspectable, and yours to edit."
          bullets={[
            { icon: 'brain', label: 'Typed memory: user · project · feedback · reference' },
            { icon: 'file-text', label: 'Stored as readable Markdown' },
            { icon: 'shield-check', label: 'Lives on your machine, not in the cloud' },
          ]}
          customVisual={<MemoryTimelineVisual />}
          mockupSide="left"
        />

        {/* -------- 03 Soul -------- */}
        <FeatureSection
          id="soul"
          eyebrow="03 / SOUL"
          heading="One assistant. Yours. For the long run."
          body="Dao Agent isn't a row of role-bots you switch between — it's a single companion that grows into you. Its soul is the slow accumulation of how you speak, what you care about, what you can't stand. The same secretary every morning, sharper every month. You shape it; it stays."
          bullets={[
            { icon: 'heart', label: 'One identity, refined over months — not a costume rack' },
            { icon: 'sliders-horizontal', label: 'Voice, principles, taboos, the things it should remember to ask' },
            { icon: 'code', label: 'Soul is a plain file you can read, edit, version, back up' },
          ]}
          customVisual={<SoulVisual />}
          mockupSide="right"
        />

        {/* -------- 04 Self-evolution -------- */}
        <FeatureSection
          id="evolution"
          eyebrow="04 / SELF-EVOLUTION"
          heading="It gets better the more you use it."
          body="After meaningful interactions, the agent reflects on what worked and what didn't — and rewrites its own memory, prunes stale references, sharpens its soul. You can review every change before it lands. Nothing learns silently."
          bullets={[
            { icon: 'repeat', label: 'Reflect → propose → diff → accept' },
            { icon: 'refresh-cw', label: 'Memory & soul updates as readable diffs' },
            { icon: 'eye', label: 'Audit log: every learned fact has a source' },
          ]}
          customVisual={<EvolutionVisual />}
          mockupSide="left"
        />

        {/* -------- 05 Host-based skills -------- */}
        <FeatureSection
          id="skills"
          eyebrow="05 / HOST-BASED SKILLS"
          heading="Skills tuned to where you are — taught in plain conversation."
          body="“Next time I'm on GitHub, summarize the PR diff for me.” That's it — the agent turns the request into a reusable skill, scoped to that one host, and brings it back the next time you land on the domain. Skills are scoped, so you can keep hundreds without bloating the prompt: only the ones relevant to the page you're on get loaded into context."
          bullets={[
            { icon: 'sparkles', label: 'Create skills by asking — no scripting required' },
            { icon: 'globe', label: 'Per-host scope: only what matters here loads' },
            { icon: 'layers', label: 'Hundreds of skills, zero context bloat' },
            { icon: 'zap', label: 'Hot-loaded the moment you land on the domain' },
          ]}
          customVisual={<HostSkillsVisual />}
          mockupSide="right"
        />

        {/* -------- 06 Workspace -------- */}
        <FeatureSection
          id="workspace"
          eyebrow="06 / WORKSPACE"
          heading="A sandboxed scratch disk for the agent."
          body="The agent has a quota-bounded, path-normalized workspace of its own — for notes, scrapes, scripts, saved souls. Read, write, list. It cannot escape that root or touch the rest of your filesystem. V4A diffs make every file change reversible."
          bullets={[
            { icon: 'folder', label: 'Path-normalized · escape-proof' },
            { icon: 'file-text', label: 'Read / write / list, V4A patch format' },
            { icon: 'layers', label: 'Quota-bounded — no surprise disk usage' },
          ]}
          customVisual={<WorkspaceVisual />}
          mockupSide="left"
        />

        {/* -------- 07 Your API, your data -------- */}
        <FeatureSection
          id="models"
          eyebrow="07 / YOUR API, YOUR DATA"
          heading="Your model. Your key. Your data."
          body="Dao Agent talks to anything that speaks the OpenAI-compatible API: Claude, GPT, Gemini, DeepSeek, Qwen, your local Ollama. The endpoint and key live in your config — not in a Dao account. Every memory, every soul, every workspace byte is a file on your disk that you can read, edit, back up, or delete. No middleman, no telemetry, no lock-in."
          bullets={[
            { icon: 'cpu', label: 'OpenAI-compatible — plug in any vendor' },
            { icon: 'lock-open', label: 'Your API key stays on your machine' },
            { icon: 'shield-check', label: 'Memory & souls are local files you own' },
            { icon: 'repeat', label: 'Swap providers without rewriting anything' },
          ]}
          customVisual={<ModelSwapVisual />}
          mockupSide="right"
        />

        {/* -------- Closing CTA -------- */}
        <section className={styles.cta}>
          <h2 className={styles.ctaHeading}>Try Dao Agent.</h2>
          <p className={styles.ctaBody}>
            It ships inside Dao Browser — no separate install, no extra
            account. Open the sidebar, pick a soul, plug in a key.
          </p>
          <div className={styles.ctaActions}>
            <Button href="/download" variant="primary">
              <LucideIcon name="download" size={16} aria-hidden />
              Download
            </Button>
            <Button href={GITHUB_URL} variant="ghost" external>
              <LucideIcon name="star" size={16} aria-hidden />
              Star on GitHub
            </Button>
          </div>
        </section>
      </main>
      <Footer />
    </>
  );
}
