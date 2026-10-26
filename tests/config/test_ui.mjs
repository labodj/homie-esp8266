import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const source = readFileSync(new URL('../../src/Homie/Boot/ConfigUi.hpp', import.meta.url), 'utf8');
const html = source.split('R"HOMIE_UI(')[1].split(')HOMIE_UI"')[0];
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
}

async function fixture({device = {}, failInfo = false, failScan = false, failSave = false, connectionFailure = null, testStatuses = [], stored = null} = {}) {
  const elements = new Map([...html.matchAll(/id="([^"]+)"/g)].map(([, id]) => [id, new Element()]));
  elements.get('fields').disabled = true;
  elements.get('port').value = '1883';
  elements.get('baseTopic').value = 'homie/';
  elements.get('ota').checked = true;
  elements.get('uniqueId').checked = true;
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
        data = ok ? {networks: [{ssid: '<script>unsafe</script>'}, {ssid: 'Home'}, {ssid: 'Home'}]}
          : {error: 'Initial Wi-Fi scan not finished yet'};
      } else if (path === '/config' && body === undefined) {
        data = {configured: stored !== null, config: stored || {}};
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
  assert.deepEqual(el('networks').children.map(option => option.value), ['<script>unsafe</script>', 'Home']);
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

test('clearing stored Wi-Fi password is explicit and disabling MQTT auth removes old credentials', async () => {
  const {el, calls, submit} = await fixture({stored: {device_id: 'j4', wifi: {ssid: 'Old'}, mqtt: {host: 'broker.invalid', auth: true}}});
  el('openWifi').checked = true; el('auth').checked = false;
  await submit();
  const body = calls.find(c => c.path === '/config/test').body;
  assert.equal(body.wifi.password, '');
  assert.equal(body.mqtt.username, null); assert.equal(body.mqtt.password, null);
});

test('the form enforces IDs compatible with LSH discovery', () => {
  const pattern = new RegExp('^(?:' + html.match(/id="deviceId"[^>]*pattern="([^"]+)"/)[1] + ')$');
  for (const id of ['j4', 'my-bridge', 'bridge2']) assert.ok(pattern.test(id));
  for (const id of ['Bad', 'a/b', 'a+b', 'a b', 'my--bridge', '-device', 'device-']) assert.equal(pattern.test(id), false);
});
