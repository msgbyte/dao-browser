import {readFileSync} from 'node:fs';
import {afterEach, beforeEach, expect, it, vi} from 'vitest';

// Execute the actual host scripts against a DOM, with explicit viewport
// geometry, wrapped with the shared helpers exactly as WithSnapshotHelpers() does.
const source = readFileSync('src/dao/browser/automation/dao_page_tools.cc', 'utf8');
const helpers = source.match(/kSnapshotHelpersScript\[\] = R"js\(([\s\S]*?)\)js";/)![1];
const withHelpers = (script: string) => `(function(){${helpers}return (${script})})()`;
const script = withHelpers(source.match(/kAccessibilityTreeScript\[\] = R"js\(([\s\S]*?)\)js";/)![1]);
const observe = () => JSON.parse((0, eval)(script)('compact', null, 'snapshot-test', false));
const query = (filters: Record<string, unknown> = {}) =>
  JSON.parse((0, eval)(script)('interactive', filters, 'snapshot-test', false));
const actionScript = withHelpers(source.match(/WithSnapshotHelpers\(R"js\(\n(\(function\(refId, snapshotId, preconditions, fill, text(?:, animate)?\)[\s\S]*?)\)js"\)/)![1]);
const click = (element: {ref_id: string; role: string; name: string; value?: string}, animate = false) =>
  JSON.parse((0, eval)(actionScript)(element.ref_id, 'snapshot-test',
    {role: element.role, name: element.name, value: element.value}, false, '', animate));

afterEach(() => { vi.restoreAllMocks(); document.body.innerHTML = ''; });

beforeEach(() => {
  vi.spyOn(HTMLElement.prototype, 'offsetWidth', 'get').mockReturnValue(100);
  vi.spyOn(HTMLElement.prototype, 'getBoundingClientRect').mockImplementation(function(this: HTMLElement) {
    return {top: this.id === 'offscreen' ? 10000 : 10, bottom: 40,
      left: 10, right: 110, width: 100, height: 30, x: 10, y: 10, toJSON() {}};
  });
});

it('prepares the cursor destination without clicking, then clicks once', () => {
  document.body.innerHTML = '<button>Continue</button>';
  const button = document.querySelector('button')!;
  const clicked = vi.fn();
  button.addEventListener('click', clicked);
  const element = observe().elements[0];

  expect(click(element, true)).toEqual({left: 10, top: 10, width: 100, height: 30});
  expect(clicked).not.toHaveBeenCalled();
  expect(click(element)).toEqual({clicked: true, ref_id: element.ref_id});
  expect(clicked).toHaveBeenCalledTimes(1);
});

it.each(['name', 'snapshot', 'removed'])('rejects a %s change during cursor movement', change => {
  document.body.innerHTML = '<button>Continue</button>';
  const button = document.querySelector('button')!;
  const clicked = vi.fn();
  button.addEventListener('click', clicked);
  const element = observe().elements[0];
  expect(click(element, true)).toEqual({left: 10, top: 10, width: 100, height: 30});

  if (change === 'name') button.textContent = 'Delete';
  if (change === 'snapshot') document.documentElement.setAttribute('data-dao-snapshot', 'new');
  if (change === 'removed') button.remove();

  expect(click(element)).toHaveProperty('error');
  expect(clicked).not.toHaveBeenCalled();
});

it('creates one bounded viewport snapshot and excludes sensitive field values', () => {
  document.body.innerHTML = `<h1>Search page</h1><label for="search">Search</label>
    <input id="search" value="Dao"><textarea aria-label="Notes">Known notes</textarea>
    <input type="password" value="password-secret">
    <input autocomplete="one-time-code" value="otp-secret">
    <input autocomplete="cc-number" value="payment-secret">
    <input aria-label="${'Long description '.repeat(8)} password" value="long-label-secret">
    <button id="offscreen">Outside</button><button>Continue</button>
    <a href="#reset">Forgot password?</a><input aria-label="Phone time" value="9:00">`;
  const page = observe();
  expect(page.elements.map((e: {name: string}) => e.name))
      .toEqual(['Search', 'Notes', 'Continue', 'Forgot password?', 'Phone time']);
  expect(page.elements.find((e: {name: string}) => e.name === 'Search')).not.toHaveProperty('checked');
  expect(page.elements[0]).toMatchObject({value: 'Dao', editable: true, ref_id: '1'});
  expect(document.querySelector('#search')?.getAttribute('data-dao-ref')).toBe('1');
  expect(document.documentElement.getAttribute('data-dao-snapshot')).toBe('snapshot-test');
  expect(JSON.stringify(page)).not.toContain('secret');
  expect(page.text).toContain('Search page');
  expect(page.text).not.toContain('Known notes');
});

it('retains exact field preconditions within a total value budget', () => {
  document.body.innerHTML = '<textarea aria-label="Notes"></textarea><input aria-label="Oversized">';
  const field = document.querySelector('textarea')!;
  field.value = 'x'.repeat(2001);
  document.querySelector('input')!.value = 'y'.repeat(10000);
  const page = observe();
  expect(page.elements).toHaveLength(1);
  expect(page.elements[0].value).toBe(field.value);
  expect(click(page.elements[0])).toMatchObject({clicked: true});
  field.value += 'changed';
  expect(click(page.elements[0])).toEqual({error: 'The value precondition failed.'});
});

it.each(['', 'true', 'plaintext-only', 'TRUE', 'PLAINTEXT-ONLY'])(
    'excludes contenteditable="%s" drafts and their nested controls', value => {
  document.body.innerHTML = `<div contenteditable="${value}">
    Private draft <span>Inherited draft</span><button>Private action</button>
    <span contenteditable="false">Embedded draft</span></div>
    <div contenteditable="false">Public content <button>Continue</button></div>`;
  const page = observe();
  expect(page.text).toContain('Public content');
  expect(JSON.stringify(page)).not.toMatch(/draft|Private/);
  expect(page.elements.map((e: {name: string}) => e.name)).toEqual(['Continue']);
  expect(document.querySelector('[contenteditable] button')?.hasAttribute('data-dao-ref')).toBe(false);
});

it('uses the same implicit role when observing and guarding interactive elements', () => {
  document.body.innerHTML = '<ul><li tabindex="0">Item</li></ul><img onclick="" alt="Photo">';
  const page = observe();
  expect(page.elements.map((e: {role: string}) => e.role)).toEqual(['listitem', 'image']);
  for (const element of page.elements) expect(click(element)).toMatchObject({clicked: true});
});

it('queries writable fields by their current value and associated label', () => {
  document.body.innerHTML = `<label for="title">Title</label><input id="title" value="old title">
    <textarea readonly>Locked</textarea><input disabled value="Disabled">`;
  const result = query({editable: true, text: 'old title', require_count: 1});
  expect(result.matches).toHaveLength(1);
  expect(result.matches[0]).toMatchObject({name: 'Title', value: 'old title', editable: true});
  expect(click(result.matches[0])).toMatchObject({clicked: true});
  expect(query({role: 'textbox', editable: false}).matches).toHaveLength(2);
});

it('bounds query values without exposing sensitive field text', () => {
  document.body.innerHTML = `<textarea aria-label="Notes"></textarea>
    <input type="password" value="password-secret">
    <textarea aria-label="Verification code">otp-secret</textarea>`;
  document.querySelector('textarea')!.value = 'x'.repeat(2100);
  const result = query({role: 'textbox'});
  expect(result.matches[0].value).toHaveLength(2000);
  expect(result.matches[0].value_truncated).toBe(true);
  expect(result.matches.slice(1).every((field: {value?: string; text: string}) =>
    field.value === undefined && field.text === '')).toBe(true);
  expect(JSON.stringify(result)).not.toContain('secret');
});

it('names native and ARIA buttons and rejects actions after their labels change', () => {
  document.body.innerHTML = `<input type="submit" value="Save">
    <input type="button" value="Cancel"><input type="reset" value="Reset">
    <div role="button">Continue</div><button>Back</button>
    <input type="submit" value="Fallback" aria-label="Confirm">`;
  const page = observe();
  expect(page.elements.map((e: {name: string}) => e.name))
      .toEqual(['Save', 'Cancel', 'Reset', 'Continue', 'Back', 'Confirm']);
  for (const element of page.elements) expect(click(element)).toMatchObject({clicked: true});
  document.querySelector('input')!.value = 'Delete';
  document.querySelector('[role="button"]')!.textContent = 'Delete';
  for (const index of [0, 3]) {
    expect(click(page.elements[index])).toEqual({error: 'The name precondition failed.'});
  }
});
