import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const html = readFileSync(new URL('../../src/Homie/Boot/ConfigUi.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];

// Minimal DOM for exercising the actual inline script without browser dependencies.
class Element {
  constructor() {
    this.value = '';
    this.checked = false;
    this.disabled = false;
    this.hidden = false;
    this.children = [];
    this.textContent = '';
  }
  append(child) { this.children.push(child); }
  replaceChildren() { this.children = []; }
  setAttribute(key, value) { this[key] = value; }
  addEventListener(event, handler) { this['on' + event] = handler; }
}

async function fixture({device = {}, failInfo = false, failStorage = false, failScan = false, networks = [{ssid: '<script>unsafe</script>'}, {ssid: 'Home'}, {ssid: 'Home'}], failSave = false, connectionFailure = null, testStatuses = [], stored = null} = {}) {
  const elements = new Map([...html.matchAll(/id="([^"]+)"/g)].map(([, id]) => [id, new Element()]));
  elements.get('fields').disabled = true;
  elements.get('port').value = '1883';
  elements.get('baseTopic').value = 'homie/';
  elements.get('ota').checked = true;
  elements.get('uniqueId').checked = true;
  elements.get('wifiPassword').type = 'password';
  elements.get('mqttPassword').type = 'password';
  const calls = [];
  const context = vm.createContext({
    document: {
      getElementById(id) {
        return elements.get(id) || elements.get('settings').children.flatMap(label => label.children)
          .find(input => input.id === id);
      },
      createElement: () => new Element()
    },
    AbortController, TextEncoder, setTimeout, clearTimeout,
    async fetch(path, options) {
      assert.ok(path.startsWith('/'), 'onboarding must not depend on Internet access');
      const body = options.body === undefined ? undefined : JSON.parse(options.body);
      calls.push({path, options, body});
      let data;
      let ok = true;
      if (path === '/device-info') {
        ok = !failInfo;
        data = ok ? {hardware_device_id: '68ee8fe34dd8', firmware: {name: 'bridge', version: 'test'},
          settings: [], ...device} : {error: 'Device unavailable'};
      } else if (path === '/networks') {
        ok = !failScan;
        data = ok ? {networks}
          : {error: 'Initial Wi-Fi scan not finished yet'};
      } else if (path === '/config' && body === undefined) {
        ok = !failStorage;
        data = ok ? {configured: stored !== null, config: stored || {}} : {error: 'Cannot mount storage. Existing data was preserved.'};
      } else if (path === '/config/test') {
        data = {success: true};
      } else if (path === '/config/status') {
        data = testStatuses.shift() || (connectionFailure ? {status: 'failed', error: connectionFailure} : {status: 'ready'});
      } else if (path === '/config') {
        ok = !failSave;
        data = ok ? {success: true} : {error: 'Configuration rejected'};
      } else assert.fail('Unexpected endpoint: ' + path);
      return {ok, status: ok ? 200 : 400, json: async () => data};
    }
  });
  vm.runInContext(script, context);
  await new Promise(resolve => setImmediate(resolve));
  const el = id => context.document.getElementById(id);
  return {el, calls, submit: () => el('setup').onsubmit({preventDefault() {}})};
}

test('unreadable storage blocks setup instead of offering a replacement hardware ID', async () => {
  const {el, calls} = await fixture({failStorage: true});
  assert.equal(el('fields').disabled, true);
  assert.equal(el('setup').hidden, true);
  assert.equal(el('deviceId').value, '');
  assert.equal(el('retry').hidden, false);
  assert.match(el('info').textContent, /Existing data was preserved/);
  assert.equal(calls.some(c => c.body !== undefined), false);
  el('fields').disabled = false;
  await el('retry').onclick();
  assert.equal(el('fields').disabled, true);
});

test('blank device works offline and allows explicit replacement ID and authenticated MQTT', async () => {
  const {el, calls, submit} = await fixture();
  assert.equal(el('fields').disabled, false);
  assert.equal(el('deviceId').value, '68ee8fe34dd8');
  el('deviceId').value = 'j4';
  el('name').value = 'Bridge j4';
  el('ssid').value = 'Home';
  el('wifiPassword').value = 'wifi-password';
  el('host').value = '192.0.2.20';
  el('baseTopic').value = 'homie/5/';
  el('auth').checked = true;
  el('auth').onchange();
  assert.equal(el('credentials').hidden, false);
  assert.equal(el('credentials').disabled, false);
  el('username').value = 'homie';
  el('mqttPassword').value = 'mqtt-password';
  await submit();
  assert.deepEqual(calls.find(call => call.path === '/config' && call.body !== undefined).body, {
    name: 'Bridge j4', device_id: 'j4', wifi: {ssid: 'Home', password: 'wifi-password'},
    mqtt: {host: '192.0.2.20', port: 1883, base_topic: 'homie/5/', auth: true,
      username: 'homie', password: 'mqtt-password'}, ota: {enabled: true}, settings: {}
  });
  assert.equal(el('fields').disabled, true, 'prevent duplicate saves after success');
  assert.match(el('result').textContent, /Configuration saved/);
});

test('explicit configuration mode preserves logical ID instead of hardware ID', async () => {
  const {el} = await fixture({device: {device_id: 'j4'}});
  assert.equal(el('deviceId').value, 'j4');
});

test('open networks and unauthenticated MQTT do not leak disabled credentials', async () => {
  const {el, calls, submit} = await fixture();
  el('username').value = 'stale-user';
  el('mqttPassword').value = 'stale-password';
  el('ota').checked = false;
  await submit();
  const {body, options} = calls.find(call => call.path === '/config' && call.body !== undefined);
  assert.equal(body.wifi.password, '');
  assert.equal(body.mqtt.auth, false);
  assert.equal('username' in body.mqtt, false);
  assert.equal('password' in body.mqtt, false);
  assert.equal(body.ota.enabled, false);
  assert.equal(options.method, 'PUT');
  assert.equal(options.cache, 'no-store');
});

test('firmware settings retain JSON types, optional fields can be omitted', async () => {
  const settings = [
    {name: 'enabled', type: 'bool', required: true, default: true},
    {name: 'count', type: 'long', required: true},
    {name: 'factor', type: 'double', required: true},
    {name: 'text', type: 'string', required: true},
    {name: 'optional', type: 'string', required: false},
    {name: '__proto__', type: 'string', required: true}
  ];
  const {el, calls, submit} = await fixture({device: {settings}});
  el('setting-1').value = '4';
  el('setting-2').value = '1.5';
  el('setting-3').value = 'hello';
  el('setting-5').value = 'safe';
  await submit();
  const result = calls.find(call => call.path === '/config' && call.body !== undefined).body.settings;
  assert.deepEqual(result, JSON.parse('{"enabled":true,"count":4,"factor":1.5,"text":"hello","__proto__":"safe"}'));
});

test('required strings preserve empty values on new and configured devices', async () => {
  const settings = [{name: 'text', type: 'string', required: true},
    {name: 'count', type: 'long', required: true},
    {name: 'factor', type: 'double', required: true}];
  for (const stored of [null, {device_id: 'sensor', wifi: {ssid: 'Home'},
    mqtt: {host: 'broker'}, settings: {text: '', count: 4, factor: 1.5}}]) {
    const {el, calls, submit} = await fixture({stored, device: {settings}});
    assert.equal(el('setting-0').value, '');
    assert.equal(el('setting-0').required, false, 'required strings may be empty');
    assert.equal(el('setting-1').required, true);
    assert.equal(el('setting-2').required, true);
    el('setting-1').value = '4';
    el('setting-2').value = '1.5';
    await submit();
    const tested = calls.find(c => c.path === '/config/test').body;
    assert.deepEqual(tested.settings, {text: '', count: 4, factor: 1.5});
    assert.deepEqual(tested, calls.find(c => c.path === '/config' && c.body !== undefined).body);
  }
});

test('clearing saved optional firmware settings restores defaults without dropping unknown settings', async () => {
  const stored = {device_id: 'sensor', wifi: {ssid: 'Home'}, mqtt: {host: 'broker'},
    settings: {label: 'old', count: 12, other: 'keep'}};
  const settings = [{name: 'label', type: 'string', required: false, default: 'default'},
    {name: 'count', type: 'long', required: false, default: 2}];
  const {el, calls, submit} = await fixture({stored, device: {settings}});
  el('setting-0').value = ''; el('setting-1').value = '';
  await submit();
  const body = calls.find(c => c.path === '/config/test').body;
  assert.deepEqual(body.settings, {label: null, count: null});
  assert.equal(body.device_id, 'sensor');
  assert.deepEqual(body, calls.find(c => c.path === '/config' && c.body !== undefined).body);
});

test('device-info failure blocks submission and offers retry', async () => {
  const {el} = await fixture({failInfo: true});
  assert.equal(el('fields').disabled, true);
  assert.equal(el('retry').hidden, false);
  assert.equal(el('info').textContent, 'Device unavailable');
});

test('scan errors keep manual Wi-Fi entry available', async () => {
  const {el} = await fixture({failScan: true});
  assert.equal(el('fields').disabled, false);
  assert.equal(el('scan').disabled, false);
  assert.equal(el('scanStatus').textContent, 'Initial Wi-Fi scan not finished yet');
});

test('network names are data, not HTML, and duplicates are removed', async () => {
  const {el} = await fixture();
  assert.match(html, /<select\b[^>]*id="networks"/);
  assert.doesNotMatch(html, /<datalist\b|\blist="networks"/);
  const options = el('networks').children.slice(1);
  assert.deepEqual(options.map(option => option.value), ['<script>unsafe</script>', 'Home']);
  assert.deepEqual(options.map(option => option.textContent), ['<script>unsafe</script>', 'Home']);
  assert.equal(el('networks').disabled, false);
});

test('selecting a nearby network updates SSID and clears a previous AP binding', async () => {
  const stored = {device_id: 'sensor', wifi: {ssid: 'Old', bssid: 'aa:bb:cc:dd:ee:ff', channel: 6}, mqtt: {host: 'broker'}};
  const {el} = await fixture({stored});
  el('networks').value = 'Home';
  el('networks').onchange();
  assert.equal(el('ssid').value, 'Home');
  assert.equal(el('lockAp').checked, false);
  assert.equal(el('bssid').value, '');
  assert.equal(el('channel').value, '0');
  assert.equal(el('deviceId').value, 'sensor');
  assert.equal(el('networks').value, 'Home');
  el('ssid').value = 'Hidden';
  el('ssid').oninput();
  assert.equal(el('networks').value, '');
});

test('refresh does not replace a manually entered SSID and empty scans stay usable', async () => {
  const {el} = await fixture({networks: [{ssid: ''}]});
  assert.equal(el('networks').disabled, true);
  assert.match(el('scanStatus').textContent, /No networks found/);
  el('ssid').value = 'Hidden';
  await el('scan').onclick();
  assert.equal(el('ssid').value, 'Hidden');
  assert.equal(el('scan').disabled, false);
  assert.equal(el('fields').disabled, false);
});

test('validation failures show the server reason and allow correction', async () => {
  const {el, submit} = await fixture({failSave: true});
  await submit();
  assert.match(el('result').textContent, /Configuration rejected/);
  assert.equal(el('fields').disabled, false);
});

test('oversized UTF-8 configuration is rejected before saving', async () => {
  const {el, calls, submit} = await fixture({device: {
    settings: [{name: 'text', type: 'string', required: true}]
  }});
  el('setting-0').value = '\u00e8'.repeat(500);
  await submit();
  assert.equal(calls.some(call => call.path === '/config' && call.body !== undefined), false);
  assert.match(el('result').textContent, /too large/);
  assert.equal(el('fields').disabled, false);
});

for (const reason of ['Wi-Fi password rejected', 'MQTT authentication rejected', 'Broker unreachable']) {
  test(reason + ' keeps form, hotspot workflow and previous config intact', async () => {
    const {el, calls, submit} = await fixture({connectionFailure: reason});
    el('deviceId').value = 'j4'; el('wifiPassword').value = 'keep-my-input';
    await submit();
    assert.equal(el('result').textContent, reason);
    assert.equal(el('fields').disabled, false);
    assert.equal(el('deviceId').value, 'j4');
    assert.equal(el('wifiPassword').value, 'keep-my-input');
    assert.equal(calls.some(call => call.path === '/config' && call.body !== undefined), false);
  });
}

test('Wi-Fi and MQTT checks finish before saving the same payload', async () => {
  const {calls, submit} = await fixture({testStatuses: [{status: 'wifi'}, {status: 'mqtt'}, {status: 'ready'}]});
  await submit();
  const paths = calls.map(call => call.path);
  assert.ok(paths.indexOf('/config/test') < calls.findIndex(c => c.path === '/config' && c.body !== undefined));
  assert.equal(paths.filter(p => p === '/config/status').length, 3);
  assert.deepEqual(calls.find(c => c.path === '/config/test').body, calls.find(c => c.path === '/config' && c.body !== undefined).body);
});

test('operator must confirm the ID is not used by another online bridge', async () => {
  const {el, calls, submit} = await fixture();
  el('uniqueId').checked = false;
  await submit();
  assert.equal(calls.some(c => c.path === '/config/test' || c.path === '/config' && c.body !== undefined), false);
  assert.match(el('result').textContent, /switch off the previous device/);
});

test('reconfiguration preloads stored values, preserves ID and omits unchanged secrets', async () => {
  const stored = {name: 'Existing', device_id: 'j4', wifi: {ssid: 'Old network'},
    mqtt: {host: 'broker.invalid', port: 1884, base_topic: 'homie/5/', auth: true},
    ota: {enabled: false}, settings: {level: 12}};
  const {el, calls, submit} = await fixture({stored, device: {settings: [{name: 'level', type: 'long', default: 2}]}});
  assert.equal(el('deviceId').value, 'j4'); assert.equal(el('ssid').value, 'Old network');
  assert.equal(el('name').value, 'Existing'); assert.equal(el('port').value, 1884);
  assert.equal(el('auth').checked, true); assert.equal(el('ota').checked, false);
  assert.equal(el('setting-0').value, 12);
  await submit();
  const body = calls.find(c => c.path === '/config/test').body;
  assert.equal('password' in body.wifi, false);
  assert.equal('username' in body.mqtt, false); assert.equal('password' in body.mqtt, false);
});

test('changing SSID clears the obsolete BSSID and channel, preserving logical ID', async () => {
  const stored = {device_id: 'j4', wifi: {ssid: 'Old', bssid: 'aa:bb:cc:dd:ee:ff', channel: 6}, mqtt: {host: 'broker'}};
  const {el, calls, submit} = await fixture({stored});
  el('ssid').value = 'New';
  await submit();
  const body = calls.find(c => c.path === '/config/test').body;
  assert.equal(body.wifi.bssid, null); assert.equal(body.wifi.channel, null);
  assert.equal(body.device_id, 'j4');
});

test('password visibility controls expose their state without changing values', async () => {
  const {el} = await fixture();
  el('wifiPassword').value = 'private';
  el('showWifi').onclick(); assert.equal(el('wifiPassword').type, 'text');
  assert.equal(el('showWifi')['aria-pressed'], 'true');
  el('showWifi').onclick(); assert.equal(el('wifiPassword').type, 'password');
  assert.equal(el('wifiPassword').value, 'private');
  el('openWifi').checked = true; el('openWifi').onchange();
  assert.equal(el('wifiPassword').disabled, true);
});

test('clearing stored Wi-Fi password is explicit and disabling MQTT auth removes old credentials', async () => {
  const {el, calls, submit} = await fixture({stored: {device_id: 'j4', wifi: {ssid: 'Old'}, mqtt: {host: 'broker.invalid', auth: true}}});
  el('openWifi').checked = true; el('auth').checked = false;
  await submit();
  const body = calls.find(c => c.path === '/config/test').body;
  assert.equal(body.wifi.password, '');
  assert.equal(body.mqtt.username, null); assert.equal(body.mqtt.password, null);
});

test('the generic UI follows Homie v3/v4/v5 ID rules without application-specific constraints', async () => {
  for (const version of [3, 4, 5]) {
    const {el} = await fixture({device: {homie_convention_version: version}});
    const pattern = new RegExp('^(?:' + el('deviceId').pattern + ')$');
    for (const id of ['sensor', 'my--sensor', 'temperature2']) assert.ok(pattern.test(id));
    for (const id of ['Bad', 'a/b', 'a+b', 'a b', '']) assert.equal(pattern.test(id), false);
    for (const id of ['-sensor', 'sensor-', '-']) assert.equal(pattern.test(id), version === 5);
  }
});

const advancedStored = {
  device_id: 'sensor', device_stats_interval: 300,
  wifi: {ssid: 'Home', ip: '192.0.2.10', mask: '255.255.255.0', gw: '192.0.2.1',
    dns1: '192.0.2.2', dns2: '192.0.2.3', bssid: 'aa:bb:cc:dd:ee:ff', channel: 6},
  mqtt: {host: 'broker.example'}
};

test('advanced values preload and remain effective with the panel closed', async () => {
  const {el, calls, submit} = await fixture({stored: advancedStored});
  assert.equal(Boolean(el('advanced').open), false);
  assert.equal(el('useStatic').checked, true); assert.equal(el('staticFields').disabled, false);
  assert.equal(el('lockAp').checked, true); assert.equal(el('pinFields').disabled, false);
  assert.equal(el('ip').value, '192.0.2.10'); assert.equal(el('dns2').value, '192.0.2.3');
  assert.equal(el('statsInterval').value, '300');
  await submit();
  const body = calls.find(c => c.path === '/config/test').body;
  for (const field of ['ip', 'mask', 'gw', 'dns1', 'dns2', 'bssid', 'channel']) {
    assert.equal(body.wifi[field], advancedStored.wifi[field]);
  }
  assert.equal('device_stats_interval' in body, false, 'unchanged interval stays in stored configuration');
  assert.equal(body.device_id, 'sensor');
});

test('DHCP and roaming explicitly remove all previous static and binding values', async () => {
  const {el, calls, submit} = await fixture({stored: advancedStored});
  el('useStatic').checked = false; el('useStatic').onchange();
  el('lockAp').checked = false; el('lockAp').onchange();
  assert.equal(el('staticFields').disabled, true); assert.equal(el('pinFields').disabled, true);
  assert.equal(el('dns1').required, false);
  await submit();
  const body = calls.find(c => c.path === '/config/test').body;
  for (const field of ['ip', 'mask', 'gw', 'dns1', 'dns2', 'bssid', 'channel']) assert.equal(body.wifi[field], null);
});

test('new static address, optional DNS and zero automatic channel use typed JSON', async () => {
  const {el, calls, submit} = await fixture();
  el('ssid').value = 'Home';
  el('useStatic').checked = true; el('useStatic').onchange();
  el('ip').value = '192.0.2.10'; el('mask').value = '255.255.255.0'; el('gw').value = '192.0.2.1';
  el('dns1').value = '192.0.2.2';
  el('lockAp').checked = true; el('lockAp').onchange(); el('bssid').value = 'aa:bb:cc:dd:ee:ff';
  el('statsInterval').value = '120';
  await submit();
  const tested = calls.find(c => c.path === '/config/test').body;
  assert.equal(tested.device_stats_interval, 120); assert.equal(tested.wifi.channel, 0);
  assert.equal(tested.wifi.dns2, null); assert.equal(tested.wifi.ip, '192.0.2.10');
  assert.deepEqual(tested, calls.find(c => c.path === '/config' && c.body !== undefined).body);
});

test('SSID edits clear old binding but allow explicitly selecting a new access point', async () => {
  const {el, calls, submit} = await fixture({stored: advancedStored});
  el('ssid').value = 'New'; el('ssid').oninput();
  assert.equal(el('lockAp').checked, false); assert.equal(el('pinFields').disabled, true);
  assert.equal(el('bssid').value, '');
  el('bssid').value = '11:22:33:44:55:66'; el('channel').value = '11';
  el('lockAp').checked = true; el('lockAp').onchange();
  await submit();
  const body = calls.find(c => c.path === '/config/test').body;
  assert.equal(body.wifi.bssid, '11:22:33:44:55:66'); assert.equal(body.wifi.channel, 11);
});

test('clearing any stored interval restores the default and zero remains an explicit value', async () => {
  for (const interval of [0, 60, 300]) {
    const first = await fixture({stored: {...advancedStored, device_stats_interval: interval}});
    first.el('statsInterval').value = '';
    await first.submit(); assert.equal(first.calls.find(c => c.path === '/config/test').body.device_stats_interval, null);
  }
  const second = await fixture(); second.el('statsInterval').value = '0';
  await second.submit(); assert.equal(second.calls.find(c => c.path === '/config/test').body.device_stats_interval, 0);
});

test('advanced validation opens the panel, validates IPv4 and requires a primary DNS', async () => {
  const {el} = await fixture();
  const ip = new RegExp('^(?:' + el('ip').pattern + ')$');
  for (const address of ['0.0.0.0', '192.168.1.10', '255.255.255.255']) assert.ok(ip.test(address));
  for (const address of ['256.1.2.3', '1.2.3', '1.2.3.4.5', 'not-an-ip']) assert.equal(ip.test(address), false);
  el('useStatic').checked = true; el('useStatic').onchange();
  el('dns2').value = '192.0.2.3'; el('dns2').oninput(); assert.equal(el('dns1').required, true);
  el('dns2').value = ''; el('dns2').oninput(); assert.equal(el('dns1').required, false);
  el('advanced').oninvalid(); assert.equal(el('advanced').open, true);
});
