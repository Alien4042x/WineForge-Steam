import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';

const source = fs.readFileSync(new URL('../settings_panel.js', import.meta.url), 'utf8');
const config = { protocolVersion: 1, installationID: 'a'.repeat(64),
  token: 'b'.repeat(64), baseURL: 'http://127.0.0.1:31415/v1' };
const plain = value => JSON.parse(JSON.stringify(value));
function baseSnapshot() {
  return { protocolVersion: 1, installationID: config.installationID, appID: '730', revision: 'c'.repeat(64),
    overrides: { backend: 'inherit', upscaling: 'inherit', wfusync: null, metalHud: null, dxmtSpatial: 'inherit' },
    effective: { backend: 'd3dmetal', upscaling: 'default', wfusync: true, metalHud: false, dxmtSpatial: 'off' } };
}
function api(handler = () => { throw Error('Unexpected network request'); }, descriptor = config) {
  return vm.runInNewContext(source, { fetch: handler, AbortController, setTimeout, clearTimeout })(descriptor);
}
class Element {
  constructor(tag, document) { this.tag = tag; this.ownerDocument = document; this.children = []; this.events = {}; this.attributes = {}; }
  set textContent(value) { this.text = value; this.children = []; }
  get textContent() { return this.text || this.children.map(child => child.textContent).join(' '); }
  appendChild(child) { this.children.push(child); return child; }
  setAttribute(key, value) { this.attributes[key] = value; }
  addEventListener(key, listener) { this.events[key] = listener; }
  fire(key) { if (!this.disabled) this.events[key]?.(); }
  find(predicate) { return predicate(this) ? this : this.children.map(child => child.find(predicate)).find(Boolean); }
}
function root() { const doc = { createElement: tag => new Element(tag, doc) }; return doc.createElement('div'); }
const flush = () => new Promise(resolve => setImmediate(resolve));
function mockResponse(value, status = 200) { return { ok: status === 200, status, text: async () => JSON.stringify(value) }; }
function control(element, field) { return element.find(node => node.attributes['data-wf-setting'] === field); }
function button(element, name) { return element.find(node => node.tag === 'button' && node.textContent === name); }

const fixtures = JSON.parse(fs.readFileSync(new URL('./settings-migration-v1.json', import.meta.url), 'utf8'));
for (const fixture of fixtures) test(`migration: ${fixture.name}`, () => {
  const result = api().previewLegacyLaunchOptions(fixture.before);
  assert.equal(result.safe, fixture.safe);
  assert.deepEqual(plain(result.changes), fixture.changes);
  assert.equal(result.after, fixture.after);
  if (fixture.warning) assert.ok(result.warnings.join(' ').includes(fixture.warning));
});

test('mount reads canonical settings once and never auto-writes options', async () => {
  const calls = [], writes = [], element = root();
  const panel = api(async (url, options) => { calls.push({ url, options }); return mockResponse(baseSnapshot()); });
  const input = { appID: 730, getLaunchOptions: () => 'WINEMSYNC=1 %command% -custom', setLaunchOptions: value => writes.push(value) };
  panel.mount(element, input); await flush(); panel.mount(element, input); await flush();
  assert.equal(calls.length, 1); assert.equal(calls[0].options.method, 'GET');
  assert.equal(calls[0].options.headers.Authorization, `Bearer ${config.token}`);
  assert.deepEqual(writes, []);
  assert.deepEqual(control(element, 'backend').children.map(child => child.textContent), ['Automatic', 'D3DMetal', 'DXMT']);
  assert.deepEqual(control(element, 'upscaling').children.filter(child => !child.hidden).map(child => child.textContent), ['Use WineForge default', 'AMD FSR', 'NVIDIA MetalFX']);
  assert.ok(element.textContent.includes('WFUSync'));
  assert.equal(control(element, 'dxmtSpatial').disabled, true);
});

test('all setting controls send typed changes with revision, never individual ENV', async () => {
  for (const [field, uiValue, expected] of [['backend', 'dxmt', 'dxmt'], ['upscaling', 'nvidia', 'nvidia'],
    ['upscaling', 'inherit', 'inherit'], ['wfusync', 'off', false], ['wfusync', 'inherit', null],
    ['metalHud', 'on', true], ['metalHud', 'off', false], ['dxmtSpatial', '1.72', '1.72']]) {
    const current = baseSnapshot(); current.effective.backend = 'dxmt';
    const calls = [], element = root();
    const panel = api(async (url, options) => {
      calls.push({ url, options });
      if (options.method === 'PATCH') {
        const data = JSON.parse(options.body); Object.assign(current.overrides, data.changes); current.revision = 'd'.repeat(64);
      }
      return mockResponse(current);
    });
    panel.mount(element, { appID: '730', getLaunchOptions: () => '', setLaunchOptions: () => assert.fail('No options write') }); await flush();
    const choice = control(element, field); choice.value = uiValue; choice.fire('change'); await flush();
    assert.equal(calls.length, 2);
    assert.equal(calls[1].options.headers['If-Match'], 'c'.repeat(64));
    assert.deepEqual(JSON.parse(calls[1].options.body), { protocolVersion: 1, appID: '730', changes: { [field]: expected } });
    assert.equal(element._wineforgeSettingsV1.current.revision, 'd'.repeat(64));
  }
});

test('saved neutral profile stays readable until the user selects WineForge inheritance', async () => {
  const current = baseSnapshot(); current.overrides.upscaling = 'default';
  const calls = [], element = root();
  const panel = api(async (url, options) => {
    calls.push({ url, options });
    if (options.method === 'PATCH') {
      Object.assign(current.overrides, JSON.parse(options.body).changes);
      current.revision = 'd'.repeat(64);
    }
    return mockResponse(current);
  });
  panel.mount(element, { appID: '730', getLaunchOptions: () => '', setLaunchOptions: () => assert.fail('No options write') }); await flush();
  const choice = control(element, 'upscaling');
  assert.equal(choice.value, 'default');
  const previous = choice.children.find(option => option.value === 'default');
  assert.equal(previous.hidden, true); assert.equal(previous.disabled, true);
  assert.equal(calls.length, 1); assert.equal(calls[0].options.method, 'GET');
  choice.value = 'inherit'; choice.fire('change'); await flush();
  assert.deepEqual(JSON.parse(calls[1].options.body).changes, { upscaling: 'inherit' });
  assert.equal(choice.value, 'inherit');
});

test('409 refreshes without retrying the stale write or changing Steam options', async () => {
  const methods = [], element = root(), refreshed = baseSnapshot(); refreshed.revision = 'e'.repeat(64);
  const panel = api(async (url, options) => {
    methods.push(options.method); return options.method === 'PATCH' ? mockResponse({}, 409)
      : mockResponse(methods.length === 1 ? baseSnapshot() : refreshed);
  });
  panel.mount(element, { appID: '730', getLaunchOptions: () => '', setLaunchOptions: () => assert.fail('No options write') }); await flush();
  const hud = control(element, 'metalHud'); hud.value = 'on'; hud.fire('change'); await flush();
  assert.deepEqual(methods, ['GET', 'PATCH', 'GET']);
  assert.equal(element._wineforgeSettingsV1.current.revision, refreshed.revision);
  assert.ok(element.textContent.includes('changed elsewhere'));
});

test('missing descriptor makes no request and disables setting controls', async () => {
  const element = root(); api(() => assert.fail('No request without service'), null).mount(element,
    { appID: '730', getLaunchOptions: () => '', setLaunchOptions: () => assert.fail('No write') }); await flush();
  for (const field of ['backend', 'upscaling', 'wfusync', 'metalHud', 'dxmtSpatial']) assert.equal(control(element, field).disabled, true);
  assert.ok(element.textContent.includes('Open WineForge'));
});

test('foreign origin/nonces, unknown protocol and profile values fail closed', async () => {
  for (const bad of [{ protocolVersion: 2 }, { installationID: 'f'.repeat(64) }, { appID: '999' },
    { revision: 'old' }, { effective: { ...baseSnapshot().effective, backend: 'dxvk' } }]) {
    const element = root(); api(async () => mockResponse({ ...baseSnapshot(), ...bad })).mount(element,
      { appID: '730', getLaunchOptions: () => '', setLaunchOptions: () => assert.fail('No write') }); await flush();
    assert.equal(control(element, 'backend').disabled, true);
  }
  for (const bad of [{ ...config, baseURL: 'http://example.com/v1' }, { ...config, baseURL: 'http://127.0.0.1:65536/v1' },
    { ...config, protocolVersion: 2 }, { ...config, token: 'short' }]) {
    await assert.rejects(api(() => assert.fail('No request'), bad).request('730'));
  }
});

test('legacy cleanup runs only after canonical commit and matches exact original', async () => {
  const before = 'WINEWFUSYNC=0 MTL_HUD_ENABLED=1 USER_FLAG="a b" %command% --arg="x y"';
  let options = before; const order = [], element = root();
  const panel = api(async (url, request) => {
    if (request.method !== 'POST') return mockResponse(baseSnapshot());
    const body = JSON.parse(request.body); assert.deepEqual(body, { protocolVersion: 1, appID: '730', launchOptions: before });
    order.push('canonical-commit');
    return mockResponse({ ...baseSnapshot(), revision: 'd'.repeat(64), cleanup: { before, after: panel.previewLegacyLaunchOptions(before).after } });
  });
  panel.mount(element, { appID: '730', getLaunchOptions: () => options,
    setLaunchOptions: value => { order.push('steam-cleanup'); options = value; } }); await flush();
  assert.deepEqual(order, []); button(element, 'Import recognized legacy settings').fire('click'); await flush();
  assert.deepEqual(order, ['canonical-commit', 'steam-cleanup']);
  assert.ok(options.includes('USER_FLAG="a b" %command% --arg="x y"'));
});

test('concurrent Steam option changes and unverified cleanup are never overwritten', async () => {
  for (const concurrent of [true, false]) {
    const before = 'WINEWFUSYNC=0 %command% --custom'; let options = before; const element = root();
    const panel = api(async (url, request) => {
      if (request.method !== 'POST') return mockResponse(baseSnapshot());
      if (concurrent) options = '%command% --new-user-argument';
      return mockResponse({ ...baseSnapshot(), cleanup: { before, after: concurrent ? panel.previewLegacyLaunchOptions(before).after : '%command% --destroyed' } });
    });
    panel.mount(element, { appID: '730', getLaunchOptions: () => options, setLaunchOptions: () => assert.fail('Must preserve options') }); await flush();
    button(element, 'Import recognized legacy settings').fire('click'); await flush();
    assert.ok(element.textContent.includes('Nothing was removed'));
  }
});

test('failed import never clears launch options', async () => {
  const element = root(); const panel = api(async (url, request) => mockResponse(request.method === 'POST' ? {} : baseSnapshot(), request.method === 'POST' ? 422 : 200));
  panel.mount(element, { appID: '730', getLaunchOptions: () => 'WINEWFUSYNC=0 %command%', setLaunchOptions: () => assert.fail('Must preserve options') }); await flush();
  button(element, 'Import recognized legacy settings').fire('click'); await flush();
  assert.ok(element.textContent.includes('did not accept'));
});

test('failed Steam cleanup preserves the committed profile and reports pending cleanup', async () => {
  const before = 'WINEWFUSYNC=0 %command%', element = root();
  const panel = api(async (url, request) => request.method !== 'POST' ? mockResponse(baseSnapshot())
    : mockResponse({ ...baseSnapshot(), revision: 'd'.repeat(64), cleanup: { before, after: panel.previewLegacyLaunchOptions(before).after } }));
  panel.mount(element, { appID: '730', getLaunchOptions: () => before,
    setLaunchOptions: () => { throw Error('Steam refused cleanup'); } }); await flush();
  button(element, 'Import recognized legacy settings').fire('click'); await flush();
  assert.equal(element._wineforgeSettingsV1.current.revision, 'd'.repeat(64));
  assert.ok(element.textContent.includes('Profile saved; Steam launch-option cleanup failed'));
});

test('actual C-expanded Steam component wires AppID and typed settings requests', async () => {
  const componentPath = process.env.WINEFORGE_SETTINGS_COMPONENT || new URL('../build/settings-component-v1.js', import.meta.url);
  const native = fs.readFileSync(componentPath, 'utf8');
  const calls = [], writes = [];
  const context = vm.createContext({ fetch: async (url, options) => { calls.push({ url, options }); return mockResponse(baseSnapshot()); },
    AbortController, setTimeout, clearTimeout,
    i: { jsx: (type, props) => ({ type, props }), jsxs: (type, props) => ({ type, props }) },
    SteamClient: { Apps: { SetAppLaunchOptions: (id, value) => writes.push({ id, value }) } } });
  vm.runInContext(native, context);
  const details = { unAppID: 730, strCompatToolName: 'wineforge-proton', strLaunchOptions: 'WINEMSYNC=1 %command%', vecPlatforms: ['windows'] };
  const component = context.Component({ details }); assert.equal(component.props.className, 'WFSteamPanel');
  const element = root(); component.props.children[1].props.ref(element); await flush();
  assert.equal(calls.length, 1); assert.ok(calls[0].url.endsWith('/games/730/settings'));
  assert.deepEqual(control(element, 'upscaling').children.filter(child => !child.hidden).map(child => child.textContent), ['Use WineForge default', 'AMD FSR', 'NVIDIA MetalFX']);
  const hud = control(element, 'metalHud'); hud.value = 'on'; hud.fire('change'); await flush();
  assert.equal(calls[1].options.method, 'PATCH');
  assert.deepEqual(JSON.parse(calls[1].options.body).changes, { metalHud: true });
  assert.deepEqual(writes, []);
  assert.equal(context.Component({ details: { ...details, strCompatToolName: 'foreign-proton' } }), null);
  assert.equal(context.Component({ details: { ...details, strCompatToolName: '', vecPlatforms: ['osx'] } }), null);
});
