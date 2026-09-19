import type { Metadata } from 'next';
import { TopNav } from '@/components/TopNav';
import { Footer } from '@/components/Footer';
import { createMetadata } from '@/lib/seo';
import styles from './privacy.module.css';

export const metadata: Metadata = {
  ...createMetadata({
    title: 'Privacy Policy - Dao Browser',
    description:
      'How Dao Browser handles browsing data, optional AI features, website analytics, and your privacy choices across desktop, Android, and iOS.',
    path: '/privacy',
  }),
  robots: { index: false, follow: true },
};

export default function PrivacyPage() {
  return (
    <>
      <TopNav />
      <main className={styles.root}>
        <header className={styles.header}>
          <p className={styles.eyebrow}>Dao Browser</p>
          <h1>Privacy Policy</h1>
          <p className={styles.dates}>
            Draft updated <time dateTime="2026-09-20">September 20, 2026</time>
            <br />
            Effective date: pending finalization
          </p>
          <p>
            This notice explains how information is handled when you use Dao
            Browser for desktop, Android, or iOS, and the Dao website at
            dao.msgbyte.com. Features and controls differ by platform and version.
          </p>
        </header>

        <aside className={styles.notice} aria-labelledby="draft-status">
          <h2 id="draft-status">Draft — analytics retention not yet confirmed</h2>
          <p>
            This notice is not yet final. Retention periods for analytics data
            and server logs have not been confirmed. This draft does not promise
            a fixed server-side deletion schedule.
          </p>
        </aside>

        <nav className={styles.contents} aria-label="Privacy policy contents">
          <h2>Contents</h2>
          <ol>
            <li><a href="#scope">Scope and contact</a></li>
            <li><a href="#local-data">Information on your device</a></li>
            <li><a href="#network">Browsing and connected services</a></li>
            <li><a href="#ai">AI, Home, memory, Dream, and MCP</a></li>
            <li><a href="#analytics">Website and application analytics</a></li>
            <li><a href="#permissions">Permissions and private browsing</a></li>
            <li><a href="#retention">Recipients, locations, and retention</a></li>
            <li><a href="#choices">Your choices and privacy requests</a></li>
            <li><a href="#security">Security, children, and changes</a></li>
          </ol>
        </nav>

        <article className={styles.policy} aria-label="Privacy policy">
          <section id="scope">
            <h2>1. Scope and contact</h2>
            <p>
              Dao Browser is an open-source, client-side browser maintained by
              moonrailgun. The project does not provide a Dao account, cloud
              synchronization, or hosted storage for your browsing data. This
              notice covers Dao-provided features and the Dao website. It does
              not replace the policies of websites you visit, extensions you
              install, AI or translation providers you select, or your
              operating-system and app-store providers.
            </p>
            <p>
              Maintainer: <strong>moonrailgun</strong>.
              <br />
              Privacy contact:{' '}
              <a href="mailto:moonrailgun@gmail.com">moonrailgun@gmail.com</a>.
            </p>
            <p>
              Use this email address for private questions or requests about
              information handled by the project. Do not submit browsing history,
              passwords, API keys, or other sensitive information in public issue
              reports.
            </p>
          </section>

          <section id="local-data">
            <h2>2. Information on your device</h2>
            <p>
              Dao stores browser information locally to operate the features you
              use. Depending on your platform, this includes browsing history,
              bookmarks, saved passwords, reading-list links, open tabs and tab
              previews, session restoration data, preferences, and download
              records. Websites can store cookies, caches, and other site data
              through the browser engine. Downloaded files are saved to the
              location you choose or the platform download location.
            </p>
            <p>
              When you choose to import data from another browser on desktop,
              supported records are copied into your local Dao profile. Extension
              reinstallation can contact the relevant extension store to download
              packages.
            </p>
            <p>
              Desktop AI features also keep conversations, attachments, settings,
              workspace files, saved memories, and generated reports locally.
              Home project files, revisions, and source permissions are also
              stored locally. Information stored locally can still be sent to a
              service when you use a connected feature described below. Local
              storage does not mean every feature operates offline.
            </p>
            <p>
              Desktop regular profiles record foreground browsing time by domain,
              date, and time-of-day bucket. This separate local record contains
              aggregated durations, not full URLs, page titles, or page content.
              Recording operates independently of the Memory and Dream switches;
              it excludes Incognito and Guest profiles. Its retention window is
              the current local date and the preceding 370 dates.
            </p>
            <p>
              Device backups, files saved to cloud-backed locations, and copies
              you share with other apps are governed by those services and your
              settings.
            </p>
          </section>

          <section id="network">
            <h2>3. Browsing and connected services</h2>
            <p>
              Visiting a website sends requests to that website and the services
              it embeds. Searching sends your query to the selected search
              provider. Where search suggestions are enabled, the provider may
              receive text while you type. These recipients can receive your IP
              address, request headers, cookies applicable to their sites, and
              information you submit. Network and DNS providers also handle
              connection information needed to deliver requests.
            </p>
            <p>
              Update checks and downloads contact release infrastructure. Desktop
              releases use Dao update and download endpoints and GitHub; Android
              checks GitHub releases, including automatic foreground checks when
              enabled. iOS distribution and updates are handled through Apple.
              These services receive the requested resource and normal network
              metadata. Operating-system, browser-engine, and security services
              may make additional requests according to their configuration.
            </p>
            <p>
              On Android, opening or searching the extension catalog contacts
              Mozilla Add-ons; installing an extension downloads its package.
              Extensions may access site data according to their permissions.
              The bundled KISS Translator starts disabled in a new profile. If
              enabled and used, it sends text or page content to the translation
              service selected in the extension. Dao does not operate that
              translation service.
            </p>
          </section>

          <section id="ai">
            <h2>4. AI, Home, memory, Dream, and MCP</h2>
            <p>
              These features apply to desktop Dao. The current native Android
              and iOS apps do not include Dao Agent, Dream, or the local MCP
              server.
            </p>
            <h3>Agent and model providers</h3>
            <p>
              When you use Agent, requests go to the model endpoint you
              configure. Requests can include your prompt, relevant conversation
              history, attachments, selected text, captured page content or
              screenshots, tool results, and relevant saved memories. Browser
              tools can read and interact with pages, including signed-in pages,
              within their authorized scope. The information returned by those
              tools can become part of subsequent model requests.
            </p>
            <p>
              Web search and page-reading tools can also contact separate
              services, including provider search tools, Jina, DuckDuckGo, or the
              target website. A search service receives the query; a remote
              reader receives the requested URL. Your model provider is not
              necessarily the only recipient involved in an Agent task.
            </p>
            <p>
              Provider credentials are used to authenticate requests. Review the
              endpoint and its provider&apos;s terms before sending confidential
              information. Provider retention, training use, and deletion
              controls depend on that provider and your account; this notice
              does not promise that all providers exclude requests from training.
            </p>
            <h3>Home personalization</h3>
            <p>
              When you explicitly start Home personalization from browsing
              history, Dao prepares a limited summary of site origins and
              suggested sources for Agent and your configured model provider.
              This summary excludes raw page titles, visit counts, and full
              visited URLs. If you authorize Home to read a source, including a
              signed-in website, the returned page information can be included
              in model requests as described above.
            </p>
            <h3>Memory and Dream analysis</h3>
            <p>
              Memory and Dream analysis are off by default. When Memory is
              enabled, saved information can be retrieved into future Agent
              requests. Dream additionally requires its own setting. Once enabled,
              scheduled or manually requested analysis can send browsing
              summaries, domains, page titles, search keywords, time buckets,
              aggregate foreground durations, conversation excerpts, and
              feedback statistics to your configured model provider. Weekly
              analysis has a separate switch.
            </p>
            <p>
              Dream reports and suggested memories are stored locally. Turning
              a feature off stops future use of that feature; it does not erase
              existing records or retract information already sent to a provider.
              Turning Dream off also does not stop the separate local foreground
              activity record described in section 2.
            </p>
            <h3>External clients through MCP</h3>
            <p>
              The local MCP server is off by default. Enabling it lets compatible
              local clients request access, with approval required before a
              connection executes its first tool call. Approved clients can read
              page and browser information and perform actions in the authorized
              regular browser window. Incognito and Guest windows are excluded.
              A client may send returned information to its own remote service.
              Review that client&apos;s privacy practices; stopping its connection
              does not delete copies it has already received.
            </p>
          </section>

          <section id="analytics">
            <h2>5. Website and application analytics</h2>
            <p>
              The Dao website loads Tianji analytics, operated by msgbyte, from
              app.tianji.dev to measure website usage. This includes page visits
              and events for navigation, GitHub links, and downloads, with metadata such as the
              clicked location or selected download platform. Website analytics
              can process the page URL and referrer and browser or device
              information. The analytics endpoint also receives network metadata,
              including the IP address needed to establish a connection.
            </p>
            <p>
              Desktop Dao automatically sends a browser-open event and Agent
              message-send events to Tianji. The message event includes text
              length and attachment count. These Dao event payloads do not
              include message text, attachment contents, or browsing URLs. That
              limitation does not make the network request anonymous or apply to
              the separate model requests described in section 4.
            </p>
            <p>
              Desktop Dao currently has no in-app analytics switch. Blocking
              app.tianji.dev at the network level can prevent these requests.
              Webpage content blockers may not cover requests made by the
              browser process itself. The website also loads analytics without
              an in-page preference control. Clearing cookies alone should not
              be treated as an analytics opt-out.
            </p>
            <p>
              The current native iOS app does not integrate a Dao analytics,
              advertising, or third-party crash-reporting SDK. Opening the Dao
              website from the app is still a website visit and is subject to
              the website practices above. Apple may separately handle App Store,
              TestFlight, diagnostic, or feedback information under its own
              settings and terms.
            </p>
          </section>

          <section id="permissions">
            <h2>6. Permissions and private browsing</h2>
            <p>
              Permissions depend on the feature and platform. QR scanning uses
              the camera. Websites may request camera, microphone, or other
              supported capabilities. File selection, downloads, and sharing
              provide the selected data to the relevant destination. You can
              manage available site permissions in browser controls and app
              permissions in your operating-system settings.
            </p>
            <p>
              Private browsing limits local persistence; it does not hide you
              from websites, search providers, your employer or network operator,
              or services you use. Signing in to a website still identifies you
              to that website. Private browsing is not an analytics opt-out for
              websites you visit, including the Dao website.
            </p>
            <p>
              On iOS, private tabs use a nonpersistent WebKit data store and are
              excluded from automatic history and saved session restoration.
              Android also excludes private tabs from regular session recovery.
              Bookmarks you explicitly save, downloaded files, exported content,
              and copies shared with other apps can remain after private tabs
              are closed.
            </p>
          </section>

          <section id="retention">
            <h2>7. Recipients, locations, and retention</h2>
            <p>
              Recipients depend on the activity: visited websites and search
              providers receive browsing requests; hosting and download services
              deliver the website and releases; Tianji receives analytics;
              configured AI and translation providers process their feature
              inputs; and approved extensions, MCP clients, or sharing
              destinations receive the information needed for their actions.
              Data sent in a support request is also available to its recipients
              and the communication platform you choose. Public reports are
              visible to others.
            </p>
            <p>
              Local data remains subject to browser storage limits, cleanup
              behavior, and your deletion choices. The foreground activity
              retention window is described in section 2. Deleting a local
              conversation or report does not delete a provider&apos;s copy.
              Backups, downloaded files, and shared copies may require separate
              deletion.
            </p>
            <p>
              Remote services may process information outside your country.
              The Tianji analytics server operated by msgbyte is hosted in
              Frankfurt, Germany. Its endpoint uses Cloudflare&apos;s global proxy
              network, so connection information may also be processed outside
              Germany. This server location is not a promise that all processing
              takes place in Germany. Other recipients&apos; processing locations
              depend on the services you use.
            </p>
            <p>
              Retention periods for analytics data and server logs have not yet
              been confirmed. This draft does not promise a fixed deletion
              schedule for those records or for support correspondence. Contact
              the maintainer about information you have provided to the project;
              data held by independent providers is subject to their policies
              and available deletion procedures.
            </p>
          </section>

          <section id="choices">
            <h2>8. Your choices and privacy requests</h2>
            <ul>
              <li>
                Use the history, site-data, library, and download controls
                available on your platform. On iOS, clearing website data is
                separate from deleting history, bookmarks, reading-list entries,
                and downloaded files.
              </li>
              <li>
                Choose your search provider, review extensions and their
                permissions, and disable extensions you no longer use.
              </li>
              <li>
                Review your AI endpoint and context before sending a request.
                Manage saved memories and conversations separately; disable
                Memory, Dream, or MCP when you do not want those features to run.
              </li>
              <li>
                Revoke camera, microphone, and other permissions through the
                applicable browser or system settings. This affects future
                access, not information already shared.
              </li>
              <li>
                Delete downloaded or exported files and any cloud or backup
                copies separately. Uninstalling an app may leave those copies
                intact.
              </li>
            </ul>
            <p>
              Depending on applicable law, you may have rights to access,
              correct, delete, or obtain a copy of personal information, restrict
              or object to processing, withdraw consent where processing relies
              on consent, or complain to a data-protection authority. These
              rights have legal conditions and exceptions. Send requests about
              information received by the project to{' '}
              <a href="mailto:moonrailgun@gmail.com">moonrailgun@gmail.com</a>.
              The maintainer cannot remotely access or delete your local browser
              profile; use your device&apos;s controls for local data. Requests
              concerning an independent provider&apos;s own processing should
              also be directed to that provider.
            </p>
          </section>

          <section id="security">
            <h2>9. Security, children, and changes</h2>
            <p>
              Dao uses platform storage and permission mechanisms and HTTPS for
              its configured analytics endpoints. The security of a visited
              website or custom service also depends on that destination. No
              storage or transmission method eliminates all risk. Protect access
              to your device, review permissions, and keep the browser and
              operating system up to date.
            </p>
            <p>
              Dao is a general-purpose browser that can access the open web; it
              is not a child-directed service or a parental-control product.
              Parents and guardians should use appropriate device and account
              controls. Contact the maintainer with concerns about children&apos;s
              information submitted to the project.
            </p>
            <p>
              This page will identify the effective date of the final policy and
              the date of later revisions. Material changes may require
              additional notice or consent under applicable law. A policy update
              does not itself change your browser settings.
            </p>
          </section>
        </article>
      </main>
      <Footer />
    </>
  );
}
