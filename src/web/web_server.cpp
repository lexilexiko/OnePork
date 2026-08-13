// web/web_server.cpp
#include "web_server.h"
#include "../net/ap_sta.h"
#include "../storage/littlefs_ops.h"
#include "../cap/sniffer.h"
#include "../sync/sync_manager.h"
#include "../sync/wpasec.h"
#include "../sync/pwncrack.h"
#include "../sync/pot_parse.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <cctype>
#include <string.h>
#include <esp_heap_caps.h>

namespace Web {

static WebServer s_srv(80);

static const char* INDEX_HTML =
"<!doctype html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"<meta charset=\"utf-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"<title>0n3Pork W3b</title>\n"
"<style>\n"
"  :root{--bg:#0f1115;--fg:#e8e8e8;--dim:#9aa3ad;--ok:#3ecf6c;--warn:#f0b03a;--err:#e25c5c;--card:#1a1d23;--b:#2a2f38;}\n"
"  *{box-sizing:border-box}\n"
"  body{font-family:-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;margin:0;background:var(--bg);color:var(--fg)}\n"
"  header{padding:14px 18px;background:#15181f;border-bottom:1px solid var(--b);display:flex;align-items:center;gap:10px}\n"
"  header h1{font-size:16px;margin:0;font-weight:600}\n"
"  .pill{font-size:11px;padding:2px 8px;border-radius:10px;background:var(--card);color:var(--dim);border:1px solid var(--b)}\n"
"  main{max-width:760px;margin:0 auto;padding:14px}\n"
"  .card{background:var(--card);border:1px solid var(--b);border-radius:8px;padding:14px;margin-bottom:12px}\n"
"  .card h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--dim);margin:0 0 10px}\n"
"  .row{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-top:1px solid var(--b);gap:8px}\n"
"  .row:first-of-type{border-top:0}\n"
"  .k{color:var(--dim);font-size:13px}\n"
"  .v{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:13px;text-align:right;word-break:break-all}\n"
"  .ok{color:var(--ok)}.warn{color:var(--warn)}.err{color:var(--err)}\n"
"  .hint{font-size:12px;color:var(--dim);margin:8px 0 0;line-height:1.4}\n"
"  button{background:#2b6cff;color:#fff;border:0;padding:10px 14px;border-radius:6px;font-size:14px;cursor:pointer;width:100%;margin-top:6px}\n"
"  button.alt{background:#3a3f4a}\n"
"  button.danger{background:#a23a3a}\n"
"  button:disabled{opacity:.4;cursor:not-allowed}\n"
"  input{background:#0e1116;color:var(--fg);border:1px solid var(--b);padding:9px 10px;border-radius:6px;width:100%;font-size:14px;margin-top:4px}\n"
"  label{font-size:12px;color:var(--dim);display:block;margin-top:8px}\n"
"  .grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px}\n"
"  pre{background:#0a0c10;color:#d6e2f0;padding:10px;border-radius:6px;overflow:auto;font-size:12px;max-height:240px;white-space:pre-wrap}\n"
"  table.kt{width:100%;font-size:12px;border-collapse:collapse}\n"
"  table.kt th{text-align:left;padding:6px 4px;border-bottom:1px solid var(--b);color:var(--dim)}\n"
"  table.kt td{padding:6px 4px;border-bottom:1px solid var(--b);word-break:break-all;vertical-align:middle}\n"
"  button.mini{width:auto;padding:6px 10px;margin:0;font-size:12px}\n"
"  .empty{color:var(--dim);font-style:italic;padding:8px 0}\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<header>\n"
"  <h1>0n3Pork W3b</h1>\n"
"  <span class=\"pill\" id=\"ver\">v--</span>\n"
"  <span class=\"pill\" id=\"heap\">heap ?</span>\n"
"</header>\n"
"<main>\n"
"  <section class=\"card\">\n"
"    <h2>WiFi</h2>\n"
"    <div class=\"row\"><span class=\"k\">Mode</span><span class=\"v\" id=\"m-mode\">--</span></div>\n"
"    <div class=\"row\"><span class=\"k\">SSID</span><span class=\"v\" id=\"m-ssid\">--</span></div>\n"
"    <div class=\"row\"><span class=\"k\">Status</span><span class=\"v\" id=\"m-status\">--</span></div>\n"
"    <div class=\"row\"><span class=\"k\">IP</span><span class=\"v\" id=\"m-ip\">--</span></div>\n"
"    <div class=\"row\"><span class=\"k\">MAC</span><span class=\"v\" id=\"m-mac\">--</span></div>\n"
"    <div class=\"row\"><span class=\"k\">RSSI / clients</span><span class=\"v\" id=\"m-rssi\">--</span></div>\n"
"    <div class=\"row\"><span class=\"k\">Joined WiFi</span><span class=\"v\" id=\"m-sta\">--</span></div>\n"
"    <div class=\"row\"><span class=\"k\">Share</span><span class=\"v\" id=\"m-share\">--</span></div>\n"
"    <label>This AP name</label>\n"
"    <input id=\"ap-ssid\" placeholder=\"0n3Pork W3b\" autocomplete=\"off\">\n"
"    <label>This AP password (min 8 chars)</label>\n"
"    <input id=\"ap-pass\" placeholder=\"on3pork123\" autocomplete=\"off\">\n"
"    <button class=\"alt\" onclick=\"saveAp()\">Save AP name / password</button>\n"
"    <p class=\"hint\">After save, reconnect to the new name and type the password.</p>\n"
"    <label>Home / target WiFi SSID</label>\n"
"    <input id=\"sta-ssid\" placeholder=\"-- not saved --\" autocomplete=\"off\">\n"
"    <label>Home WiFi password</label>\n"
"    <input id=\"sta-pass\" placeholder=\"-- not saved --\" autocomplete=\"off\">\n"
"    <p class=\"hint\">Last saved home WiFi stays in these boxes after reboot.</p>\n"
"    <div class=\"grid2\">\n"
"      <button class=\"alt\" onclick=\"setMode('AP')\">AP only</button>\n"
"      <button onclick=\"setMode('APSTA')\">AP + STA</button>\n"
"    </div>\n"
"    <p class=\"hint\">AP only = 0n3Pork W3b. AP + STA = you stay on 0n3Pork W3b while the board joins the WiFi above. Sync keeps the UI.</p>\n"
"  </section>\n"
"  <section class=\"card\">\n"
"    <h2>Capture</h2>\n"
"    <div class=\"row\"><span class=\"k\">State</span><span class=\"v\" id=\"cap-state\">--</span></div>\n"
"    <div class=\"row\"><span class=\"k\">Channel</span><span class=\"v\" id=\"cap-ch\">--</span></div>\n"
"    <div class=\"row\"><span class=\"k\">EAPOL / written</span><span class=\"v\" id=\"cap-cnt\">--</span></div>\n"
"    <div class=\"row\"><span class=\"k\">Files in /handshakes/</span><span class=\"v\" id=\"cap-files\">--</span></div>\n"
"    <button id=\"cap-btn\" onclick=\"toggleCapture()\">--</button>\n"
"    <p class=\"hint\">Web START is light mode: this channel only, UI stays. Board button is aggressive: hops all channels, kicks nearby clients, SSID becomes 0n3Pork AGG. Stop aggressive with the same button, then reconnect to 0n3Pork W3b.</p>\n"
"    <h3 style=\"margin-top:12px;margin-bottom:8px;font-size:12px;color:var(--dim);text-transform:uppercase;letter-spacing:.06em\">Handshake files</h3>\n"
"    <table style=\"width:100%;font-size:12px;border-collapse:collapse\">\n"
"    <thead><tr style=\"border-bottom:1px solid var(--b)\"><th style=\"text-align:left;padding:6px 0\">File</th><th style=\"text-align:right;padding:6px 0\">Size</th><th style=\"text-align:right;padding:6px 0\">Action</th></tr></thead>\n"
"    <tbody id=\"handshakes-list\"><tr><td colspan=\"3\" style=\"text-align:center;color:var(--dim);padding:12px 0\">loading...</td></tr></tbody>\n"
"    </table>\n"
"  </section>\n"
"  <section class=\"card\">\n"
"    <h2>Sync (needs home WiFi)</h2>\n"
"    <label>WPA-Sec API key (32 hex)</label>\n"
"    <input id=\"key-wpasec\" maxlength=\"32\" placeholder=\"-- not saved --\" autocomplete=\"off\">\n"
"    <label>Pwncrack key</label>\n"
"    <input id=\"key-pwncrack\" placeholder=\"-- not saved --\" autocomplete=\"off\">\n"
"    <button class=\"alt\" onclick=\"saveKeys()\">Save keys</button>\n"
"    <div class=\"grid2\">\n"
"      <button onclick=\"diagnose()\">Test Connection</button>\n"
"      <button onclick=\"sync('wpasec')\">Sync WPA-Sec</button>\n"
"    </div>\n"
"    <button onclick=\"sync('pwncrack')\">Sync Pwncrack</button>\n"
"    <p class=\"hint\">Sync joins the saved WiFi as AP+STA so 0n3Pork W3b stays up.</p>\n"
"    <pre id=\"sync-log\">no sync yet</pre>\n"
"  </section>\n"
"  <section class=\"card\">\n"
"    <h2>Upload PCAP files</h2>\n"
"    <input type=\"file\" id=\"file-upload\" multiple accept=\".pcap,.pcapng,.hc22000,.22000,application/octet-stream\">\n"
"    <button onclick=\"uploadFiles()\">Upload to /handshakes/</button>\n"
"    <pre id=\"upload-log\"></pre>\n"
"  </section>\n"
"  <section class=\"card\">\n"
"    <h2>WPA-Sec keys</h2>\n"
"    <div id=\"res-wpasec\" class=\"empty\">empty</div>\n"
"  </section>\n"
"  <section class=\"card\">\n"
"    <h2>Pwncrack keys</h2>\n"
"    <div id=\"res-pwncrack\" class=\"empty\">empty</div>\n"
"    <p class=\"hint\">Join puts that SSID/password into the boxes and starts AP+STA. You stay on 0n3Pork W3b.</p>\n"
"  </section>\n"
"  <section class=\"card\">\n"
"    <h2>Storage</h2>\n"
"    <div class=\"row\"><span class=\"k\">Total / free</span><span class=\"v\" id=\"fs-stats\">--</span></div>\n"
"    <button class=\"danger\" onclick=\"if(confirm('Erase all handshakes and results?')) wipe()\">Wipe storage</button>\n"
"  </section>\n"
"</main>\n"
"<script>\n"
"const $=s=>document.getElementById(s);\n"
"let capRunning=false;\n"
"function fillSaved(id,val){\n"
"  const el=$(id);\n"
"  if(!el || el.dataset.touched) return;\n"
"  el.value = val||'';\n"
"  el.placeholder = val?'saved':'-- not saved --';\n"
"}\n"
"async function jget(p){return await (await fetch(p)).json();}\n"
"async function jpost(p,body){\n"
"  const o={method:'POST'};\n"
"  if(body!==undefined) o.body=JSON.stringify(body), o.headers={'Content-Type':'application/json'};\n"
"  return await (await fetch(p,o)).json();\n"
"}\n"
"async function refresh(){\n"
"  try{\n"
"    const s=await jget('/api/status');\n"
"    $('ver').textContent=s.version;\n"
"    $('heap').textContent='heap '+Math.round(s.freeHeap/1024)+'K';\n"
"    var modeLbl=s.mode==='APSTA'?'AP+STA':(s.mode||'--');\n"
"    $('m-mode').innerHTML='<span class=\"ok\">'+modeLbl+'</span>';\n"
"    $('m-ssid').textContent=s.apSsid||s.ssid||'--';\n"
"    $('m-status').innerHTML = s.connected?'<span class=\"ok\">up</span>':'<span class=\"warn\">down</span>';\n"
"    $('m-ip').textContent=s.apIp||s.ip||'--';\n"
"    $('m-mac').textContent=s.mac||'--';\n"
"    var rc='';\n"
"    if(s.rssi) rc+=s.rssi;\n"
"    if(s.apClients) rc+=(rc?' / ':'')+s.apClients+' clients';\n"
"    $('m-rssi').textContent=rc||'--';\n"
"    var staLine=(s.staSsid||'--');\n"
"    if(s.staConnected) staLine+='  '+(s.staIp||'');\n"
"    else if(s.mode==='APSTA'||s.mode==='STA') staLine+='  connecting';\n"
"    $('m-sta').textContent=staLine;\n"
"    $('m-share').innerHTML = s.mode==='APSTA'?(s.napt?'<span class=\"ok\">NAT on</span>':'<span class=\"warn\">AP+STA, no phone NAT</span>'):'off';\n"
"    var cm=s.capMode||'off';\n"
"    if(cm==='aggressive') $('cap-state').innerHTML='<span class=\"err\">aggressive</span>';\n"
"    else if(cm==='light') $('cap-state').innerHTML='<span class=\"ok\">light</span>';\n"
"    else $('cap-state').innerHTML='<span class=\"warn\">stopped</span>';\n"
"    $('cap-btn').textContent = (cm==='light'||cm==='aggressive')?'STOP light':'START light';\n"
"    $('cap-btn').disabled = (cm==='aggressive');\n"
"    capRunning = (cm==='light');\n"
"    $('cap-ch').textContent = s.capChannel||'--';\n"
"    $('cap-cnt').textContent = (s.capEapol||0)+' / '+(s.capWritten||0);\n"
"    $('cap-files').textContent=s.handshakeCount;\n"
"    fillSaved('ap-ssid', s.apName);\n"
"    fillSaved('ap-pass', s.apPass);\n"
"    fillSaved('sta-ssid', s.staSsid);\n"
"    fillSaved('sta-pass', s.staPass);\n"
"    if(s.keys){\n"
"      fillSaved('key-wpasec', s.keys.wpasec);\n"
"      fillSaved('key-pwncrack', s.keys.pwncrack);\n"
"    }\n"
"    const f=await jget('/api/fs');\n"
"    $('fs-stats').textContent = Math.round(f.total/1024)+'K total, '+Math.round(f.free/1024)+'K free';\n"
"    if(s.sync && s.sync.message){\n"
"      var extra = s.sync.running ? ' ('+s.sync.progress+'%)' : '';\n"
"      $('sync-log').textContent = s.sync.message + extra;\n"
"    }\n"
"  }catch(e){console.log(e);}\n"
"}\n"
"async function saveAp(){\n"
"  const ssid=$('ap-ssid').value.trim();\n"
"  const pass=$('ap-pass').value;\n"
"  if(!ssid){alert('enter AP name');return;}\n"
"  if(pass && pass.length>0 && pass.length<8){alert('AP password must be 8+ chars');return;}\n"
"  const r=await jpost('/api/wifi/ap',{ssid:ssid,pass:pass});\n"
"  $('ap-ssid').dataset.touched='';\n"
"  $('ap-pass').dataset.touched='';\n"
"  if(r && r.ok===false) alert(r.error||'AP save failed');\n"
"  else alert('AP saved. Reconnect to '+ssid+' with the new password');\n"
"  refresh();\n"
"}\n"
"async function setMode(m){\n"
"  if(m==='STA') m='APSTA';\n"
"  if(m==='APSTA'){\n"
"    const ssid=$('sta-ssid').value.trim();\n"
"    const pass=$('sta-pass').value;\n"
"    if(!ssid){alert('enter SSID first');return;}\n"
"    await jpost('/api/wifi/sta',{ssid:ssid,pass:pass});\n"
"  }\n"
"  const r=await jpost('/api/wifi/mode',{mode:m});\n"
"  if(r && r.ok===false) alert(r.error||'mode switch failed');\n"
"  refresh();\n"
"}\n"
"async function joinNet(ssid,pass){\n"
"  $('sta-ssid').value=ssid||'';\n"
"  $('sta-pass').value=pass||'';\n"
"  $('sta-ssid').dataset.touched='1';\n"
"  $('sta-pass').dataset.touched='1';\n"
"  await setMode('APSTA');\n"
"}\n"
"function joinFromBtn(btn){\n"
"  joinNet(decodeURIComponent(btn.dataset.s||''), decodeURIComponent(btn.dataset.p||''));\n"
"}\n"
"function esc(s){\n"
"  return String(s||'').replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));\n"
"}\n"
"function renderKeys(id, items){\n"
"  const el=$(id);\n"
"  if(!items||!items.length){el.innerHTML='<div class=\"empty\">empty</div>';return;}\n"
"  var h='<table class=\"kt\"><thead><tr><th>SSID</th><th>BSSID</th><th>Password</th><th></th></tr></thead><tbody>';\n"
"  items.forEach(e=>{\n"
"    h+='<tr><td>'+esc(e.ssid)+'</td><td>'+esc(e.bssid)+'</td><td>'+esc(e.password)+'</td>';\n"
"    h+='<td><button class=\"mini\" data-s=\"'+encodeURIComponent(e.ssid||'')+'\" data-p=\"'+encodeURIComponent(e.password||'')+'\" onclick=\"joinFromBtn(this)\">Join</button></td></tr>';\n"
"  });\n"
"  el.innerHTML=h+'</tbody></table>';\n"
"}\n"
"async function toggleCapture(){\n"
"  if(capRunning) await jpost('/api/capture/stop'); else await jpost('/api/capture/start');\n"
"  refresh();\n"
"}\n"
"async function saveKeys(){\n"
"  const body={};\n"
"  const w=$('key-wpasec').value.trim();\n"
"  const p=$('key-pwncrack').value.trim();\n"
"  if(w) body.wpasec=w;\n"
"  if(p) body.pwncrack=p;\n"
"  if(!w && !p){alert('enter a key first');return;}\n"
"  const r=await jpost('/api/keys',body);\n"
"  $('key-wpasec').dataset.touched='';\n"
"  $('key-pwncrack').dataset.touched='';\n"
"  $('sync-log').textContent = r.ok ? 'keys saved' : (r.error||'key save failed');\n"
"  refresh();\n"
"}\n"
"async function sync(which){\n"
"  const ssid=$('sta-ssid').value.trim();\n"
"  const pass=$('sta-pass').value;\n"
"  if(ssid) await jpost('/api/wifi/sta',{ssid:ssid,pass:pass});\n"
"  const keyField = (which==='wpasec')?$('key-wpasec'):$('key-pwncrack');\n"
"  const apiKey = keyField.value.trim();\n"
"  if(apiKey){\n"
"    const body={};\n"
"    if(which==='wpasec') body.wpasec=apiKey; else body.pwncrack=apiKey;\n"
"    await jpost('/api/keys',body);\n"
"  }\n"
"  const log=$('sync-log');\n"
"  log.textContent='starting '+which+' (0n3Pork W3b stays up)...';\n"
"  const r=await jpost('/api/sync',{target:which, apiKey:apiKey});\n"
"  log.textContent = JSON.stringify(r,null,2);\n"
"}\n"
"async function diagnose(){\n"
"  const log=$('sync-log');\n"
"  log.textContent='Testing...';\n"
"  try{\n"
"    const r=await jpost('/api/diagnose',{});\n"
"    log.textContent = JSON.stringify(r,null,2);\n"
"  }catch(e){ log.textContent='Error: '+e.message; }\n"
"}\n"
"async function uploadFiles(){\n"
"  const input=$('file-upload');\n"
"  const files=input.files;\n"
"  if(!files.length){alert('select files first');return;}\n"
"  const log=$('upload-log');\n"
"  log.textContent='Uploading '+files.length+' files...';\n"
"  for(let f of files){\n"
"    const formData=new FormData();\n"
"    formData.append('file',f,f.name);\n"
"    try{\n"
"      const resp=await fetch('/api/upload',{method:'POST',body:formData});\n"
"      const j=await resp.json();\n"
"      log.textContent+='\\n'+f.name+': '+(j.message||'')+' '+(j.size||'');\n"
"    }catch(e){ log.textContent+='\\n'+f.name+': '+e.message; }\n"
"  }\n"
"  loadHandshakes();\n"
"}\n"
"async function loadResults(){\n"
"  try{\n"
"    const r=await jget('/api/results');\n"
"    renderKeys('res-wpasec', r.wpasec);\n"
"    renderKeys('res-pwncrack', r.pwncrack);\n"
"  }catch(e){console.log(e);}\n"
"}\n"
"async function loadHandshakes(){\n"
"  try{\n"
"    const r=await jget('/api/handshakes');\n"
"    const list=$('handshakes-list');\n"
"    if(!r.files||!r.files.length){\n"
"      list.innerHTML='<tr><td colspan=\"3\" style=\"text-align:center;color:var(--dim);padding:12px 0\">no files yet</td></tr>';\n"
"      return;\n"
"    }\n"
"    list.innerHTML = r.files.map(f=>{\n"
"      var sz=f.size;\n"
"      var szStr=(sz>=1024?Math.round(sz/1024)+'K':sz+'B');\n"
"      return '<tr style=\"border-bottom:1px solid var(--b)\"><td style=\"padding:6px 0\">'+f.name+'</td><td style=\"text-align:right;padding:6px 0\">'+szStr+'</td><td style=\"text-align:right;padding:6px 0\"><a href=\"/api/handshakes/'+encodeURIComponent(f.name)+'\" style=\"color:#2b6cff;text-decoration:none\">download</a></td></tr>';\n"
"    }).join('');\n"
"  }catch(e){console.log(e);}\n"
"}\n"
"async function wipe(){ await jpost('/api/wipe'); refresh(); loadResults(); loadHandshakes(); }\n"
"['ap-ssid','ap-pass','sta-ssid','sta-pass','key-wpasec','key-pwncrack'].forEach(id=>{\n"
"  $(id).addEventListener('input',()=>{$(id).dataset.touched='1';});\n"
"});\n"
"refresh(); loadHandshakes(); loadResults();\n"
"setInterval(()=>{ refresh(); loadHandshakes(); loadResults(); },3000);\n"
"</script>\n"
"</body>\n"
"</html>\n";

static bool validName(const char* name) {
    if (!name || !name[0]) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    if (strstr(name, "..")) return false;
    size_t n = strlen(name);
    if (n >= Storage::FILE_NAME_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}

static bool containsI(const char* hay, const char* needle) {
    if (!needle || !needle[0]) return true;
    if (!hay) return false;
    size_t n = strlen(needle);
    for (const char* p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] &&
               toupper((unsigned char)p[i]) == toupper((unsigned char)needle[i])) {
            i++;
        }
        if (i == n) return true;
    }
    return false;
}

static void sendJson(const JsonDocument& doc, int code = 200) {
    String out;
    serializeJson(doc, out);
    s_srv.send(code, "application/json", out);
}

static bool parseJsonBody(JsonDocument& doc) {
    if (!s_srv.hasArg("plain")) return false;
    return !deserializeJson(doc, s_srv.arg("plain"));
}

static void handleRoot() {
    s_srv.send(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handleStatus() {
    Net::Status s = Net::status();
    JsonDocument doc;
    if (s.mode == Net::Mode::APSTA) doc["mode"] = "APSTA";
    else doc["mode"] = "AP";
    doc["ssid"] = s.ssid;
    doc["apSsid"] = s.apSsid;
    doc["apIp"] = s.apIp;
    doc["staSsid"] = s.staSsidShow;
    doc["staIp"] = s.staIp;
    doc["staConnected"] = s.staConnected;
    doc["napt"] = s.napt;
    doc["connected"] = s.connected;
    doc["ip"] = s.ip;
    doc["rssi"] = s.rssi;
    doc["mac"] = s.mac;
    doc["apClients"] = s.apClients;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["version"] = ON3PORK_VERSION;
    doc["capture"] = Cap::isRunning();
    if (Cap::runMode() == Cap::RunMode::Aggressive) doc["capMode"] = "aggressive";
    else if (Cap::runMode() == Cap::RunMode::Light) doc["capMode"] = "light";
    else doc["capMode"] = "off";
    const Cap::Counters& c = Cap::counters();
    doc["capChannel"] = c.currentChannel;
    doc["capEapol"] = c.framesEapol;
    doc["capWritten"] = c.framesWritten;
    doc["capDeauth"] = c.framesDeauth;
    Storage::Stats fs = Storage::stats();
    doc["handshakeCount"] = fs.handshakes;

    SyncManager::SyncState syncState = SyncManager::getStatus();
    JsonObject syncObj = doc["sync"].to<JsonObject>();
    syncObj["running"] = SyncManager::isRunning();
    syncObj["message"] = syncState.message;
    syncObj["progress"] = syncState.progress;

    const Net::Cfg& cfg = Net::cfg();
    doc["apName"] = cfg.apSsid;
    doc["apPass"] = cfg.apPass;
    doc["staSsid"] = cfg.staSsid;
    doc["staPass"] = cfg.staPass;
    JsonObject keys = doc["keys"].to<JsonObject>();
    keys["wpasec"] = cfg.wpaSecKey[0] ? cfg.wpaSecKey : "";
    keys["pwncrack"] = cfg.pwncrackKey[0] ? cfg.pwncrackKey : "";
    sendJson(doc);
}

static void handleFs() {
    Storage::Stats fs = Storage::stats();
    JsonDocument doc;
    doc["total"] = fs.total;
    doc["used"]  = fs.used;
    doc["free"]  = fs.free;
    doc["handshakes"] = fs.handshakes;
    doc["results"]    = fs.results;
    sendJson(doc);
}

static void handleWifiMode() {
    JsonDocument doc;
    if (!parseJsonBody(doc)) {
        s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
        return;
    }
    const char* m = doc["mode"] | "";
    Net::Mode target;
    if (strcmp(m, "AP") == 0) target = Net::Mode::AP;
    else if (strcmp(m, "STA") == 0 || strcmp(m, "APSTA") == 0) target = Net::Mode::APSTA;
    else {
        s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"mode must be AP or APSTA\"}");
        return;
    }
    if (Cap::isRunning()) Cap::stop();
    bool ok = Net::setMode(target);
    if (ok) s_srv.send(200, "application/json", "{\"ok\":true}");
    else    s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"no STA creds\"}");
}

static void handleWifiSta() {
    JsonDocument doc;
    if (!parseJsonBody(doc)) {
        s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
        return;
    }
    const char* ssid = doc["ssid"] | "";
    const char* pass = doc["pass"] | "";
    bool ok = Net::setSta(ssid, pass);
    if (ok) s_srv.send(200, "application/json", "{\"ok\":true}");
    else    s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"bad ssid\"}");
}

static void handleWifiAp() {
    JsonDocument doc;
    if (!parseJsonBody(doc)) {
        s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
        return;
    }
    const char* ssid = doc["ssid"] | "";
    const char* pass = doc["pass"] | "";
    uint8_t ch = Net::cfg().apChannel;
    bool ok = Net::setAp(ssid, pass, ch);
    if (ok) s_srv.send(200, "application/json", "{\"ok\":true}");
    else    s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"bad AP name or password (8+ chars)\"}");
}

static void handleCaptureStart() {
    if (Cap::runMode() == Cap::RunMode::Aggressive) {
        s_srv.send(409, "application/json",
                   "{\"ok\":false,\"error\":\"aggressive is button-only; press the board button to stop\"}");
        return;
    }
    Cap::startLight();
    s_srv.send(200, "application/json", "{\"ok\":true,\"mode\":\"light\"}");
}
static void handleCaptureStop() {
    Cap::stop();
    s_srv.send(200, "application/json", "{\"ok\":true}");
}

static void handleKeys() {
    JsonDocument doc;
    if (!parseJsonBody(doc)) {
        s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
        return;
    }
    bool any = false;
    bool ok = true;
    if (doc["wpasec"].is<const char*>()) {
        any = true;
        if (!Net::setWpaSecKey(doc["wpasec"] | "")) ok = false;
    }
    if (doc["pwncrack"].is<const char*>()) {
        any = true;
        if (!Net::setPwncrackKey(doc["pwncrack"] | "")) ok = false;
    }
    JsonDocument resp;
    resp["ok"] = ok && any;
    if (!any) resp["error"] = "no keys";
    else if (!ok) resp["error"] = "invalid key";
    sendJson(resp, (ok && any) ? 200 : 400);
}

static void handleSync() {
    JsonDocument doc;
    if (!parseJsonBody(doc)) {
        s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
        return;
    }
    const char* target = doc["target"] | "";
    SyncManager::SyncTarget syncTarget;
    if (strcmp(target, "wpasec") == 0) syncTarget = SyncManager::SYNC_WPASEC;
    else if (strcmp(target, "pwncrack") == 0) syncTarget = SyncManager::SYNC_PWNCRACK;
    else {
        s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid target\"}");
        return;
    }
    if (SyncManager::isRunning()) {
        s_srv.send(503, "application/json", "{\"ok\":false,\"error\":\"sync already running\"}");
        return;
    }
    if (!Net::hasStaCreds()) {
        s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"save home WiFi first\"}");
        return;
    }

    char keyBuf[65];
    const char* apiKey = doc["apiKey"] | "";
    if (!apiKey[0]) {
        if (syncTarget == SyncManager::SYNC_WPASEC) {
            strncpy(keyBuf, Net::cfg().wpaSecKey, sizeof(keyBuf) - 1);
        } else {
            strncpy(keyBuf, Net::cfg().pwncrackKey, sizeof(keyBuf) - 1);
        }
        keyBuf[sizeof(keyBuf) - 1] = '\0';
        apiKey = keyBuf;
    }
    if (!apiKey[0]) {
        s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"missing apiKey\"}");
        return;
    }
    if (syncTarget == SyncManager::SYNC_WPASEC && !WPASec::hasApiKey(apiKey)) {
        s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"wpa-sec key must be 32 hex\"}");
        return;
    }
    if (syncTarget == SyncManager::SYNC_PWNCRACK && !Pwncrack::hasApiKey(apiKey)) {
        s_srv.send(400, "application/json", "{\"ok\":false,\"error\":\"bad pwncrack key\"}");
        return;
    }

    if (Cap::isRunning()) Cap::stop();
    SyncManager::start(syncTarget, apiKey);

    JsonDocument resp;
    resp["ok"] = true;
    resp["message"] = "sync started, will auto-return to AP when done";
    sendJson(resp);
}

static bool tcpProbe(const char* host, uint16_t port, char* ipOut, size_t ipLen) {
    IPAddress ip;
    if (!WiFi.hostByName(host, ip)) return false;
    if (ipOut && ipLen) {
        snprintf(ipOut, ipLen, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    }
    WiFiClient c;
    c.setTimeout(5000);
    bool ok = c.connect(host, port, 5000);
    if (ok) c.stop();
    return ok;
}

static void handleDiagnose() {
    JsonDocument resp;
    Net::Status s = Net::status();
    if (s.mode == Net::Mode::APSTA) resp["wifi"]["mode"] = "APSTA";
    else resp["wifi"]["mode"] = "AP";
    resp["wifi"]["connected"] = s.staConnected;
    resp["wifi"]["ip"] = s.staConnected ? s.staIp : s.ip;
    resp["heap"]["free"] = ESP.getFreeHeap();
    resp["heap"]["largest"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    resp["keys"]["wpasec"] = WPASec::hasApiKey();
    resp["keys"]["pwncrack"] = Pwncrack::hasApiKey();

    if (!s.staConnected) {
        resp["wpasec"]["status"] = "need joined WiFi + internet";
        resp["pwncrack"]["status"] = "need joined WiFi + internet";
        sendJson(resp);
        return;
    }

    char ip[16];
    resp["wpasec"]["host"] = "wpa-sec.stanev.org";
    resp["wpasec"]["port"] = 443;
    bool w = tcpProbe("wpa-sec.stanev.org", 443, ip, sizeof(ip));
    resp["wpasec"]["ip"] = ip;
    resp["wpasec"]["status"] = w ? "tcp 443 ok" : "unreachable";
    if (w && WPASec::hasApiKey()) {
        uint16_t n = 0;
        if (WPASec::pullPotfile(Net::cfg().wpaSecKey, n)) {
            resp["wpasec"]["potfile"] = n;
            resp["wpasec"]["status"] = "potfile ok";
        } else {
            resp["wpasec"]["potfile"] = WPASec::getLastError();
        }
    }

    ip[0] = '\0';
    resp["pwncrack"]["host"] = "pwncrack.org";
    resp["pwncrack"]["port"] = 80;
    bool p = tcpProbe("pwncrack.org", 80, ip, sizeof(ip));
    resp["pwncrack"]["ip"] = ip;
    resp["pwncrack"]["status"] = p ? "tcp 80 ok" : "unreachable";
    sendJson(resp);
}

static File s_up;
static char s_upName[Storage::FILE_NAME_MAX];
static bool s_upOk;
static size_t s_upSize;

static void handleUploadFile() {
    HTTPUpload& u = s_srv.upload();
    if (u.status == UPLOAD_FILE_START) {
        s_upOk = false;
        s_upSize = 0;
        const char* raw = u.filename.c_str();
        const char* base = strrchr(raw, '/');
        if (!base) base = strrchr(raw, '\\');
        base = base ? base + 1 : raw;
        char clean[Storage::FILE_NAME_MAX];
        size_t j = 0;
        for (size_t i = 0; base[i] && j + 1 < sizeof(clean); i++) {
            char c = base[i];
            if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') clean[j++] = c;
        }
        clean[j] = '\0';
        if (!validName(clean)) {
            snprintf(s_upName, sizeof(s_upName), "up_%lu.pcap", (unsigned long)millis());
        } else {
            strncpy(s_upName, clean, sizeof(s_upName) - 1);
            s_upName[sizeof(s_upName) - 1] = '\0';
        }
        Storage::ensureDir(Storage::DIR_HANDSHAKES);
        char path[64];
        snprintf(path, sizeof(path), "%s/%s", Storage::DIR_HANDSHAKES, s_upName);
        if (s_up) s_up.close();
        s_up = LittleFS.open(path, "w");
        s_upOk = (bool)s_up;
    } else if (u.status == UPLOAD_FILE_WRITE) {
        if (s_upOk && s_up) {
            size_t w = s_up.write(u.buf, u.currentSize);
            if (w != u.currentSize) s_upOk = false;
            else s_upSize += w;
        }
    } else if (u.status == UPLOAD_FILE_END || u.status == UPLOAD_FILE_ABORTED) {
        if (s_up) {
            s_up.close();
        }
        if (u.status == UPLOAD_FILE_ABORTED) s_upOk = false;
    }
}

static void handleUploadDone() {
    JsonDocument resp;
    resp["ok"] = s_upOk;
    resp["filename"] = s_upName;
    resp["size"] = (unsigned)s_upSize;
    resp["message"] = s_upOk ? "uploaded" : "upload failed";
    sendJson(resp, s_upOk ? 200 : 500);
}

static void addResult(JsonArray& arr, uint16_t& count, const char* q,
                      const char* bssid, const char* ssid, const char* pass) {
    if (count >= 80) return;
    if (!containsI(bssid, q) && !containsI(ssid, q) && !containsI(pass, q)) return;
    JsonObject o = arr.add<JsonObject>();
    o["bssid"] = bssid ? bssid : "";
    o["ssid"] = ssid ? ssid : "";
    o["password"] = pass ? pass : "";
    count++;
}

static void handleResults() {
    const char* q = s_srv.hasArg("q") ? s_srv.arg("q").c_str() : "";
    JsonDocument doc;
    JsonArray wpa = doc["wpasec"].to<JsonArray>();
    JsonArray pwn = doc["pwncrack"].to<JsonArray>();
    uint16_t wcount = 0;
    uint16_t pcount = 0;

    if (Storage::fileExists(Storage::FILE_WPASEC_RESULTS)) {
        File f = LittleFS.open(Storage::FILE_WPASEC_RESULTS, "r");
        if (f) {
            char line[320];
            while (f.available() && wcount < 80) {
                size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
                line[n] = '\0';
                if (n && line[n - 1] == '\r') line[n - 1] = '\0';
                char bssid[18], ssid[33], pass[64];
                if (Pot::parseLine(line, bssid, ssid, pass)) {
                    addResult(wpa, wcount, q, bssid, ssid, pass);
                }
            }
            f.close();
        }
    }

    if (Storage::fileExists(Storage::FILE_PWNCRACK_RESULTS)) {
        File f = LittleFS.open(Storage::FILE_PWNCRACK_RESULTS, "r");
        if (f) {
            char line[160];
            while (f.available() && pcount < 80) {
                size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
                line[n] = '\0';
                if (n && line[n - 1] == '\r') line[n - 1] = '\0';
                if (n < 3) continue;
                char buf[160];
                strncpy(buf, line, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                char* parts[8];
                int pc = 0;
                char* p = buf;
                while (pc < 8) {
                    parts[pc++] = p;
                    char* c = strchr(p, ':');
                    if (!c) break;
                    *c = '\0';
                    p = c + 1;
                }
                if (pc >= 5) addResult(pwn, pcount, q, parts[0], parts[3], parts[4]);
                else if (pc >= 2) addResult(pwn, pcount, q, parts[0], parts[0], parts[pc - 1]);
            }
            f.close();
        }
    }

    doc["count"] = (unsigned)(wcount + pcount);
    sendJson(doc);
}

struct HsListCtx {
    JsonArray* files;
    uint16_t count;
};

static void hsListAdd(const char* name, size_t size, void* raw) {
    HsListCtx* ctx = (HsListCtx*)raw;
    if (ctx->count >= 80) return;
    JsonObject o = ctx->files->add<JsonObject>();
    o["name"] = name;
    o["size"] = (unsigned)size;
    ctx->count++;
}

static void handleHandshakesList() {
    JsonDocument doc;
    JsonArray files = doc["files"].to<JsonArray>();
    HsListCtx ctx{&files, 0};
    Storage::forEachHandshake(hsListAdd, &ctx);
    doc["count"] = ctx.count;
    sendJson(doc);
}

static void handleHandshakeDownload() {
    String path = s_srv.uri();
    const char* prefix = "/api/handshakes/";
    if (!path.startsWith(prefix)) {
        s_srv.send(404, "text/plain", "not found");
        return;
    }
    String filename = path.substring(strlen(prefix));
    if (!validName(filename.c_str())) {
        s_srv.send(400, "text/plain", "bad filename");
        return;
    }
    char fullPath[64];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", Storage::DIR_HANDSHAKES, filename.c_str());
    if (!Storage::fileExists(fullPath)) {
        s_srv.send(404, "text/plain", "not found");
        return;
    }
    File f = LittleFS.open(fullPath, "r");
    if (!f) {
        s_srv.send(500, "text/plain", "open failed");
        return;
    }
    s_srv.streamFile(f, "application/octet-stream");
    f.close();
}

static void handleWipe() {
    if (Cap::isRunning()) Cap::stop();
    bool ok = Storage::formatStorage();
    JsonDocument resp;
    resp["ok"] = ok;
    sendJson(resp, ok ? 200 : 500);
}

static void handleNotFound() {
    if (s_srv.method() == HTTP_GET && s_srv.uri().startsWith("/api/handshakes/")) {
        handleHandshakeDownload();
        return;
    }
    s_srv.send(404, "text/plain", "not found");
}

void begin() {
    s_srv.on("/", HTTP_GET, handleRoot);
    s_srv.on("/api/status", HTTP_GET, handleStatus);
    s_srv.on("/api/fs", HTTP_GET, handleFs);
    s_srv.on("/api/wifi/mode", HTTP_POST, handleWifiMode);
    s_srv.on("/api/wifi/sta", HTTP_POST, handleWifiSta);
    s_srv.on("/api/wifi/ap", HTTP_POST, handleWifiAp);
    s_srv.on("/api/capture/start", HTTP_POST, handleCaptureStart);
    s_srv.on("/api/capture/stop", HTTP_POST, handleCaptureStop);
    s_srv.on("/api/keys", HTTP_POST, handleKeys);
    s_srv.on("/api/sync", HTTP_POST, handleSync);
    s_srv.on("/api/diagnose", HTTP_POST, handleDiagnose);
    s_srv.on("/api/upload", HTTP_POST, handleUploadDone, handleUploadFile);
    s_srv.on("/api/results", HTTP_GET, handleResults);
    s_srv.on("/api/handshakes", HTTP_GET, handleHandshakesList);
    s_srv.on("/api/wipe", HTTP_POST, handleWipe);
    s_srv.onNotFound(handleNotFound);
    s_srv.begin();
    Serial.println("[WEB] listening on :80");
}

void loop() {
    s_srv.handleClient();
}

} // namespace Web
