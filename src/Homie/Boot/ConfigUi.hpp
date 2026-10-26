#pragma once

namespace HomieInternals {
// Kept in flash and served only by BootConfig; no filesystem upload is needed.
const char CONFIG_UI[] PROGMEM = R"HOMIE_UI(<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Device setup</title>
<style>
body{font:16px system-ui,sans-serif;background:#f2f5f8;color:#182838;margin:0}
main{max-width:36rem;margin:2rem auto;padding:1.5rem;background:white;border-radius:1rem}
h1{margin-top:0}fieldset{border:1px solid #ccd5df;border-radius:.5rem;margin:1rem 0;padding:1rem}
label{display:block;margin:.8rem 0}input,select,button{font:inherit;box-sizing:border-box;max-width:100%}
input:not([type=checkbox]),select{display:block;width:100%;padding:.6rem;margin-top:.3rem;border:1px solid #8090a0;border-radius:.3rem}
button{padding:.7rem 1rem;background:#125b99;color:white;border:0;border-radius:.4rem;cursor:pointer}
button:disabled{opacity:.5;cursor:wait}small{display:block;color:#4b5c6c;margin-top:.3rem}
[role=status]{white-space:pre-wrap}a{color:#125b99}:focus-visible{outline:3px solid #ed9b20;outline-offset:2px}
@media(max-width:40rem){main{margin:0;border-radius:0}}
</style>
<main>
<h1>Device setup</h1>
<p>Connect this device to your Wi-Fi and MQTT broker. No Internet connection is needed.</p>
<p id="info" role="status">Loading device information...</p>
<button id="retry" type="button" hidden>Retry loading</button>
<form id="setup" autocomplete="off">
<fieldset id="fields" disabled>
<legend>Configuration</legend>
<label>Device name<input id="name" maxlength="64" required></label>
<label>Device ID<input id="deviceId" maxlength="32" pattern="[a-z0-9]+(-[a-z0-9]+)*" required></label>
<small>When replacing a device, enter its existing ID (for example j4). Disconnect the old device before saving.</small>
<fieldset><legend>Wi-Fi</legend>
<label>Network name (SSID)<input id="ssid" list="networks" maxlength="32" required></label>
<datalist id="networks"></datalist>
<button id="scan" type="button">Refresh networks</button>
<small id="scanStatus" role="status">You can also enter a hidden network name manually.</small>
<label>Wi-Fi password<input id="wifiPassword" type="password" maxlength="64" autocomplete="new-password"></label>
<small id="wifiHelp">Leave the password empty for an open network.</small>
<label><input id="openWifi" type="checkbox"> Use an open network (clear any stored Wi-Fi password)</label>
</fieldset>
<fieldset><legend>MQTT</legend>
<label>Broker address<input id="host" maxlength="255" placeholder="192.168.1.10" required></label>
<label>Port<input id="port" type="number" min="1" max="65535" value="1883" required></label>
<label>Base topic<input id="baseTopic" maxlength="48" value="homie/" required></label>
<small>Use the same base topic as your existing devices; homie/ also works with Homie v5.</small>
<label><input id="auth" type="checkbox"> Broker requires authentication</label>
<fieldset id="credentials" disabled hidden><legend>Broker credentials</legend>
<label>Username<input id="username" maxlength="32" autocomplete="off"></label>
<label>MQTT password<input id="mqttPassword" type="password" maxlength="32" autocomplete="new-password"></label>
<small id="mqttHelp">Enter the broker credentials.</small>
</fieldset>
</fieldset>
<fieldset id="settingsGroup" hidden><legend>Firmware settings</legend><div id="settings"></div></fieldset>
<label><input id="ota" type="checkbox" checked> Allow firmware updates over MQTT</label>
<p>Wi-Fi and MQTT authentication are tested before saving. If a test fails, this hotspot stays available and nothing is replaced. After saving, the device restarts and closes setup. MQTT topic permissions and normal operation must be checked on your usual network.</p>
<label><input id="uniqueId" type="checkbox" required> No other online device uses this ID (switch off the previous device before replacing it).</label>
<button id="save" type="submit">Test, save and restart</button>
</fieldset>
</form>
<p id="result" role="status" aria-live="polite"></p>
<noscript>Enable JavaScript to configure this device.</noscript>
</main>
<script>
'use strict';
const el = id => document.getElementById(id);
let settings = [];
let stored = null;
async function api(path, body) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 15000);
  try {
    const response = await fetch(path, {
      method: body === undefined ? 'GET' : 'PUT',
      headers: body === undefined ? {} : {'Content-Type': 'application/json'},
      body: body === undefined ? undefined : JSON.stringify(body),
      cache: 'no-store', signal: controller.signal
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || 'Request failed (' + response.status + ')');
    return data;
  } finally {
    clearTimeout(timer);
  }
}
function showError(target, error) {
  el(target).textContent = error.name === 'AbortError'
    ? 'Device did not respond. Stay connected to its hotspot and try again.' : error.message;
}
async function loadDevice() {
  el('retry').hidden = true;
  try {
    const device = await api('/device-info');
    const saved = await api('/config');
    stored = saved.configured ? saved.config : null;
    settings = device.settings || [];
    el('settings').replaceChildren();
    for (const [index, setting] of settings.entries()) {
      const label = document.createElement('label');
      label.textContent = setting.description || setting.name;
      const input = document.createElement('input');
      input.id = 'setting-' + index;
      input.type = setting.type === 'bool' ? 'checkbox' :
        (['long', 'ulong', 'double'].includes(setting.type) ? 'number' : 'text');
      const value = stored && stored.settings && Object.prototype.hasOwnProperty.call(stored.settings, setting.name)
        ? stored.settings[setting.name] : setting.default;
      if (input.type === 'checkbox') input.checked = value === true;
      else {
        input.required = setting.required;
        input.value = value === undefined ? '' : value;
        if (input.type === 'number') input.step = setting.type === 'double' ? 'any' : '1';
        if (setting.type === 'ulong') input.min = '0';
      }
      label.append(input);
      el('settings').append(label);
    }
    el('settingsGroup').hidden = settings.length === 0;
    el('deviceId').value = (stored && stored.device_id) || device.device_id || device.hardware_device_id;
    if (stored) {
      el('name').value = stored.name || '';
      el('ssid').value = stored.wifi.ssid;
      el('host').value = stored.mqtt.host;
      el('port').value = stored.mqtt.port || 1883;
      el('baseTopic').value = stored.mqtt.base_topic || 'homie/';
      el('auth').checked = stored.mqtt.auth === true;
      el('auth').onchange();
      el('ota').checked = !!(stored.ota && stored.ota.enabled);
      el('wifiHelp').textContent = 'Leave empty to keep the stored password. For an open network, select the checkbox below.';
      el('mqttHelp').textContent = 'Leave credentials empty to keep the stored values. Enter only the values you want to replace.';
    }
    el('info').textContent = device.firmware.name + ' ' + device.firmware.version;
    el('fields').disabled = false;
    scanNetworks();
  } catch (error) {
    showError('info', error);
    el('retry').hidden = false;
  }
}
async function scanNetworks() {
  el('scan').disabled = true;
  try {
    const data = await api('/networks');
    el('networks').replaceChildren();
    for (const ssid of new Set(data.networks.map(network => network.ssid))) {
      const option = document.createElement('option');
      option.value = ssid;
      el('networks').append(option);
    }
    el('scanStatus').textContent = 'Network list updated. You can also enter a network name manually.';
  } catch (error) {
    showError('scanStatus', error);
  } finally {
    el('scan').disabled = false;
  }
}
el('auth').onchange = () => {
  el('credentials').hidden = el('credentials').disabled = !el('auth').checked;
};
el('scan').onclick = scanNetworks;
el('retry').onclick = loadDevice;
async function testConnections(config) {
  await api('/config/test', config);
  const deadline = Date.now() + 70000;
  while (Date.now() < deadline) {
    let status;
    try { status = await api('/config/status'); }
    catch (error) {
      // Joining a Wi-Fi network can change the AP channel and interrupt a phone.
      el('result').textContent = 'Reconnect to this hotspot. The connection test is still running; nothing has been saved.';
      await new Promise(resolve => setTimeout(resolve, 1000));
      continue;
    }
    if (status.status === 'ready') return;
    if (status.status === 'failed') throw new Error(status.error);
    if (status.status === 'saved') throw new Error('Configuration was already saved. Reconnect to your usual Wi-Fi and check the device.');
    el('result').textContent = status.status === 'mqtt'
      ? 'Wi-Fi connected. Testing MQTT authentication...'
      : 'Testing Wi-Fi (2.4 GHz). Stay connected to this hotspot...';
    await new Promise(resolve => setTimeout(resolve, 750));
  }
  throw new Error('Test confirmation was not received. Nothing was saved by this page. Stay on the hotspot and retry.');
}
el('setup').onsubmit = async event => {
  event.preventDefault();
  if (!el('uniqueId').checked) {
    el('result').textContent = 'Confirm that this device ID is unique or switch off the previous device first.';
    return;
  }
  const config = {
    name: el('name').value, device_id: el('deviceId').value,
    wifi: {ssid: el('ssid').value, password: el('wifiPassword').value},
    mqtt: {host: el('host').value, port: Number(el('port').value),
      base_topic: el('baseTopic').value, auth: el('auth').checked},
    ota: {enabled: el('ota').checked}, settings: Object.create(null)
  };
  if (stored && !el('wifiPassword').value && !el('openWifi').checked) delete config.wifi.password;
  if (el('openWifi').checked) config.wifi.password = '';
  if (config.mqtt.auth) {
    if (!stored || el('username').value) config.mqtt.username = el('username').value;
    if (!stored || el('mqttPassword').value) config.mqtt.password = el('mqttPassword').value;
  } else if (stored) {
    config.mqtt.username = null;
    config.mqtt.password = null;
  }
  for (const [index, setting] of settings.entries()) {
    const input = el('setting-' + index);
    if (input.type === 'checkbox') config.settings[setting.name] = input.checked;
    else if (input.value !== '') config.settings[setting.name] =
      input.type === 'number' ? Number(input.value) : input.value;
  }
  if (new TextEncoder().encode(JSON.stringify(config)).length >= 1000) {
    el('result').textContent = 'Configuration is too large. Shorten the names or firmware settings and try again.';
    return;
  }
  el('fields').disabled = true;
  el('result').textContent = 'Testing connection settings...';
  try {
    await testConnections(config);
  } catch (error) {
    showError('result', error);
    el('fields').disabled = false;
    return;
  }
  el('result').textContent = 'Saving verified configuration...';
  try {
    await api('/config', config);
    el('result').textContent = 'Configuration saved. The device is restarting and will close setup and its hotspot. Reconnect to your usual Wi-Fi.';
  } catch (error) {
    showError('result', error);
    el('result').textContent += ' If the hotspot closed, saving may already have succeeded: reconnect to your usual Wi-Fi and check the device before retrying.';
    el('fields').disabled = false;
  }
};
loadDevice();
</script>
</html>
)HOMIE_UI";
}  // namespace HomieInternals
