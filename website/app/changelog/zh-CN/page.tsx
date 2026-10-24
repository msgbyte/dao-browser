import { ChangelogPage, changelogMetadata } from '../ChangelogPage';

export const dynamic = 'error';
export const metadata = changelogMetadata('zh-CN');

export default function Page() {
  return <ChangelogPage locale="zh-CN" />;
}
