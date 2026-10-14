/* WineForge-Internal: native-steam/shared-game-settings-panel-v1.
 * Factory expression embedded in the native in-memory Steam UI rewrite.
 * No network, settings mutation or launch-options mutation before mount/action.
 */
(function createWineForgeSettingsPanel(config) {
  'use strict';
  const HEX = /^[0-9a-f]{64}$/;
  const SPATIAL = ['off', '1.5', '1.72', '2.0', '3.0'];
  const MANAGED = new Set(['CX_GRAPHICS_BACKEND', 'WINEMSYNC', 'WINEWFUSYNC',
    'MTL_HUD_ENABLED', 'D3DM_ENABLE_METALFX', 'DXMT_ENABLE_NVEXT',
    'DXMT_METALFX_SPATIAL_SWAPCHAIN', 'DXMT_CONFIG']);
  const appID = value => /^[1-9][0-9]*$/.test(String(value)) && Number(value) <= 4294967295;
  const configured = !!config && config.protocolVersion === 1 && HEX.test(config.installationID)
    && HEX.test(config.token) && /^http:\/\/127\.0\.0\.1:([1-9][0-9]{0,4})\/v1$/.test(config.baseURL)
    && Number(config.baseURL.split(':')[2].split('/')[0]) <= 65535;

  // A deliberately conservative literal lexer. It never evaluates shell text.
  function tokens(text) {
    const result = [];
    let i = 0;
    while (i < text.length) {
      if (' \t'.includes(text[i])) { i++; continue; }
      const start = i;
      let value = '', quote = '';
      while (i < text.length) {
        const c = text[i++];
        if (quote === "'") {
          if (c === "'") quote = ''; else value += c;
        } else if (c === '\\') {
          if (i >= text.length || text[i] === '\n') return null;
          const next = text[i++];
          if (quote && !'"\\$`'.includes(next)) value += '\\';
          value += next;
        } else if (c === '"') {
          quote = quote ? '' : '"';
        } else if (!quote && c === "'") {
          quote = "'";
        } else if (c === '$' || c === '`' || (!quote && '#;&|<>\n\r(){}'.includes(c))) {
          return null;
        } else if (!quote && ' \t'.includes(c)) {
          i--; break;
        } else value += c;
      }
      if (quote) return null;
      result.push({ start, end: i, value, raw: text.slice(start, i) });
    }
    return result;
  }

  const shellQuote = value => "'" + value.replace(/'/g, "'\\''") + "'";
  function previewLegacyLaunchOptions(text) {
    const warnings = [];
    const fail = reason => ({ before: text, after: text, changes: {}, warnings: [reason], safe: false });
    if (typeof text !== 'string' || text.length > 16384) return fail('Launch options are too large.');
    const words = tokens(text);
    if (!words) return fail('Complex or nonliteral shell options were preserved.');
    const command = words.filter(word => word.raw === '%command%');
    if (command.length !== 1) return fail('Import needs one standalone %command%; options were preserved.');
    const prefix = words.slice(0, words.indexOf(command[0]));
    if (prefix.some(word => !/^[A-Za-z_]\w*=/.test(word.raw)))
      return fail('A command wrapper precedes %command%; options were preserved.');
    const entries = prefix.map(word => ({ ...word, key: word.value.split('=')[0],
      value: word.value.slice(word.value.indexOf('=') + 1) }));
    const last = key => entries.filter(entry => entry.key === key).at(-1);
    const changes = {}, consumed = new Set(), replacements = new Map();
    const use = (key, field, value) => { changes[field] = value; consumed.add(key); };
    const backend = last('CX_GRAPHICS_BACKEND');
    if (backend) {
      if (['', 'automatic', 'd3dmetal', 'dxmt'].includes(backend.value))
        use(backend.key, 'backend', !backend.value || backend.value === 'automatic' ? 'inherit' : backend.value);
      else warnings.push('Unsupported legacy backend was preserved.');
    }
    for (const [key, field] of [['WINEWFUSYNC', 'wfusync'], ['MTL_HUD_ENABLED', 'metalHud']]) {
      const entry = last(key);
      if (!entry) continue;
      if (['0', '1'].includes(entry.value)) use(key, field, entry.value === '1');
      else warnings.push(`${key} has an unknown value and was preserved.`);
    }
    if (last('WINEMSYNC')) warnings.push('MSync is not WFUSync. WINEMSYNC was preserved; choose WFUSync explicitly.');
    const d3d = last('D3DM_ENABLE_METALFX'), dxmt = last('DXMT_ENABLE_NVEXT');
    if (d3d || dxmt) {
      if (d3d?.value === '1' && !dxmt && backend?.value === 'd3dmetal')
        use(d3d.key, 'upscaling', 'nvidia');
      else if (dxmt?.value === '1' && !d3d && backend?.value === 'dxmt')
        use(dxmt.key, 'upscaling', 'nvidia');
      else warnings.push('Legacy DLSS/profile flags are ambiguous and were preserved.');
    }
    const spatial = last('DXMT_METALFX_SPATIAL_SWAPCHAIN'), cfg = last('DXMT_CONFIG');
    if (spatial) {
      const parts = cfg ? cfg.value.split(';') : [];
      const isFactor = part => /^\s*d3d11\.metalSpatialUpscaleFactor\s*=/.test(part);
      const factors = parts.filter(isFactor).map(part => part.replace(/^[^=]*=/, '').trim());
      const factor = factors.at(-1) || '2';
      const normalized = { '1.5': '1.5', '1.72': '1.72', '2': '2.0', '2.0': '2.0', '3': '3.0', '3.0': '3.0' }[factor];
      if (!['0', '1'].includes(spatial.value) || (factors.length &&
        (!normalized || new Set(factors.map(Number)).size !== 1))) {
        warnings.push('Unknown/custom spatial settings were preserved.');
      } else {
        use(spatial.key, 'dxmtSpatial', spatial.value === '1' ? normalized : 'off');
        if (cfg && factors.length) {
          // Duplicated config assignments can carry separate user tuning; leave
          // them intact rather than flattening several configs into one.
          if (entries.filter(entry => entry.key === cfg.key).length > 1) {
            delete changes.dxmtSpatial; consumed.delete(spatial.key);
            warnings.push('Repeated DXMT_CONFIG assignments were preserved.');
          } else {
            consumed.add(cfg.key);
            const remaining = parts.filter(part => !isFactor(part)).join(';');
            if (remaining) replacements.set(cfg.start, `DXMT_CONFIG=${shellQuote(remaining)}`);
          }
        }
      }
    }
    let after = text;
    for (const entry of entries.slice().reverse()) {
      if (consumed.has(entry.key))
        after = after.slice(0, entry.start) + (replacements.get(entry.start) || '') + after.slice(entry.end);
    }
    return { before: text, after, changes, warnings, safe: true };
  }

  function snapshot(value, id) {
    if (!value || value.protocolVersion !== 1 || value.installationID !== config?.installationID
      || value.appID !== String(id) || !HEX.test(value.revision)) throw new Error('Invalid WineForge settings response.');
    const { overrides: o, effective: e } = value;
    const one = (v, values) => values.includes(v);
    const nullableBool = v => v === null || typeof v === 'boolean';
    if (!o || !e || !one(o.backend, ['inherit', 'd3dmetal', 'dxmt'])
      || !one(o.upscaling, ['inherit', 'default', 'amd', 'nvidia'])
      || !nullableBool(o.wfusync) || !nullableBool(o.metalHud)
      || !one(o.dxmtSpatial, ['inherit', ...SPATIAL])
      || !one(e.backend, ['d3dmetal', 'dxmt']) || !one(e.upscaling, ['default', 'amd', 'nvidia'])
      || typeof e.wfusync !== 'boolean' || typeof e.metalHud !== 'boolean' || !SPATIAL.includes(e.dxmtSpatial))
      throw new Error('Unsupported WineForge profile schema.');
    return value;
  }

  async function request(id, method = 'GET', data, revision, suffix = '') {
    if (!configured || !appID(id)) throw new Error('Open WineForge and restart Steam to connect game settings.');
    const controller = new AbortController(), timeout = setTimeout(() => controller.abort(), 5000);
    try {
      const response = await fetch(`${config.baseURL}/games/${id}/settings${suffix}`, {
        method, signal: controller.signal, credentials: 'omit', cache: 'no-store',
        headers: { Authorization: `Bearer ${config.token}`, ...(data ? { 'Content-Type': 'application/json' } : {}),
          ...(revision ? { 'If-Match': revision } : {}) },
        ...(data ? { body: JSON.stringify(data) } : {}),
      });
      if (!response.ok) {
        const error = new Error(response.status === 409 ? 'Profile changed elsewhere. Reloaded; choose the setting again.'
          : 'WineForge did not accept this change. Check the app connection and profile.');
        error.conflict = response.status === 409;
        throw error;
      }
      // The local service must also impose the documented body-size limit.
      const text = await response.text();
      if (text.length > 32768) throw new Error('WineForge response is too large.');
      return snapshot(JSON.parse(text), id);
    } finally { clearTimeout(timeout); }
  }

  function mount(root, input) {
    if (!root || !appID(input.appID)) return;
    if (root._wineforgeSettingsV1?.appID === String(input.appID) && !root._wineforgeSettingsV1.disposed) {
      root._wineforgeSettingsV1.input = input;
      return;
    }
    if (root._wineforgeSettingsV1) root._wineforgeSettingsV1.disposed = true;
    const state = { appID: String(input.appID), input, current: null, busy: false, disposed: false };
    root._wineforgeSettingsV1 = state;
    const document = root.ownerDocument;
    const node = (tag, text, parent = root) => {
      const element = document.createElement(tag);
      if (text !== undefined) element.textContent = text;
      parent.appendChild(element); return element;
    };
    root.textContent = '';
    const status = node('p', 'Loading WineForge game profile\u2026');
    status.setAttribute('aria-live', 'polite');
    const controls = [];
    function select(field, label, values) {
      const row = node('label'); row.className = 'WFSteamRow';
      node('span', label, row);
      const choice = node('select', undefined, row); choice.setAttribute('data-wf-setting', field);
      for (const [value, text] of values) { const option = node('option', text, choice); option.value = value; }
      if (field === 'upscaling') {
        // Preserve a saved neutral override without offering a second default.
        const previous = node('option', 'Existing neutral profile', choice);
        previous.value = 'default'; previous.hidden = true; previous.disabled = true;
      }
      choice.disabled = true; controls.push({ field, choice });
      choice.addEventListener('change', () => {
        const value = ['wfusync', 'metalHud'].includes(field)
          ? choice.value === 'inherit' ? null : choice.value === 'on' : choice.value;
        void save({ [field]: value });
      });
    }
    select('backend', 'Graphics backend', [['inherit', 'Automatic'], ['d3dmetal', 'D3DMetal'], ['dxmt', 'DXMT']]);
    select('upscaling', 'Upscaling profile', [['inherit', 'Use WineForge default'], ['amd', 'AMD FSR'], ['nvidia', 'NVIDIA MetalFX']]);
    select('wfusync', 'WFUSync', [['inherit', 'Automatic'], ['on', 'On'], ['off', 'Off']]);
    select('metalHud', 'Metal HUD (FPS)', [['inherit', 'Use WineForge default'], ['on', 'On'], ['off', 'Off']]);
    select('dxmtSpatial', 'DXMT MetalFX spatial upscaler', [['inherit', 'Use WineForge default'], ['off', 'Off'], ['1.5', '1.5x'], ['1.72', '1.72x'], ['2.0', '2x'], ['3.0', '3x']]);
    const effective = node('p');
    const legacy = node('p');
    const refresh = node('button', 'Reload profile'); refresh.type = 'button';
    const migrate = node('button', 'Import recognized legacy settings'); migrate.type = 'button'; migrate.disabled = true;
    function paint(message) {
      if (state.disposed) return;
      status.textContent = message || 'Saved in WineForge. Applies to future launches from either app.';
      const current = state.current;
      for (const { field, choice } of controls) {
        if (current) {
          const value = current.overrides[field];
          choice.value = ['wfusync', 'metalHud'].includes(field) ? value === null ? 'inherit' : value ? 'on' : 'off' : value;
        }
        choice.disabled = state.busy || !current || (field === 'dxmtSpatial' && current.effective.backend !== 'dxmt');
        if (field === 'upscaling') {
          for (const option of choice.children) option.disabled = option.hidden || (option.value === 'amd' && current?.effective.backend === 'dxmt');
        }
      }
      if (current) {
        const e = current.effective;
        effective.textContent = `Effective: ${e.backend === 'dxmt' ? 'DXMT' : 'D3DMetal'} \u00b7 ${e.upscaling} \u00b7 WFUSync ${e.wfusync ? 'on' : 'off'} \u00b7 HUD ${e.metalHud ? 'on' : 'off'}`;
      }
      const preview = previewLegacyLaunchOptions(state.input.getLaunchOptions());
      legacy.textContent = preview.warnings.join(' ');
      migrate.disabled = state.busy || !current || !Object.keys(preview.changes).length;
      refresh.disabled = state.busy;
    }
    async function load(message) {
      if (state.busy || state.disposed) return;
      state.busy = true; paint('Loading WineForge game profile\u2026');
      try { state.current = await request(state.appID); }
      catch (error) { state.current = null; message = error.message; }
      finally { state.busy = false; paint(message); }
    }
    async function save(changes) {
      if (state.busy || !state.current || state.disposed) return;
      state.busy = true; paint('Saving WineForge profile\u2026');
      let message, conflict = false;
      try { state.current = await request(state.appID, 'PATCH', { protocolVersion: 1, appID: state.appID, changes }, state.current.revision); }
      catch (error) { message = error.message; conflict = error.conflict; }
      finally { state.busy = false; paint(message); }
      if (conflict) await load(message);
    }
    async function importLegacy() {
      if (state.busy || !state.current || state.disposed) return;
      const before = state.input.getLaunchOptions(), preview = previewLegacyLaunchOptions(before);
      if (!Object.keys(preview.changes).length) return;
      state.busy = true; paint('Importing recognized legacy settings\u2026');
      let message, conflict = false, committed = false;
      try {
        const updated = await request(state.appID, 'POST', { protocolVersion: 1, appID: state.appID, launchOptions: before }, state.current.revision, '/migrate');
        state.current = updated;
        committed = true;
        const cleanup = updated.cleanup;
        if (!cleanup || cleanup.before !== before || cleanup.after !== preview.after
          || state.disposed || state.input.getLaunchOptions() !== before) {
          message = 'Profile saved; launch options changed or cleanup could not be verified. Nothing was removed.';
        } else {
          await state.input.setLaunchOptions(cleanup.after.trim() === '%command%' ? '' : cleanup.after);
          message = 'Legacy settings imported into the shared WineForge profile.';
        }
      } catch (error) {
        message = committed ? 'Profile saved; Steam launch-option cleanup failed. Retry import to finish cleanup.' : error.message;
        conflict = !committed && error.conflict;
      }
      finally { state.busy = false; paint(message); }
      if (conflict) await load(message);
    }
    refresh.addEventListener('click', () => { void load(); });
    migrate.addEventListener('click', () => { void importLegacy(); });
    // One GET, no automatic settings write and no automatic launch-options sweep.
    void load();
  }
  function unmount(root) { if (root?._wineforgeSettingsV1) root._wineforgeSettingsV1.disposed = true; }
  return { protocolVersion: 1, mount, unmount, previewLegacyLaunchOptions, request, snapshot,
    managedLegacyKeys: [...MANAGED] };
})
