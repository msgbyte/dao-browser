import { ChangelogPage, changelogMetadata } from './ChangelogPage';

export const dynamic = 'error';
export const metadata = changelogMetadata('en');

export default function Page() {
  return <ChangelogPage locale="en" />;
}
