// Node 22+ and Chrome. Runs the actual embedded page with mocked HTTP responses.
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {spawn,spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import assert from 'node:assert/strict';

const root=resolve('build');await mkdir(root,{recursive:true});
const profile=resolve(root,`portal-chrome-${process.pid}`);
const source=await readFile('include/setup-page.h','utf8');
const html=source.match(/R"HTML\(([\s\S]*)\)HTML"/)[1].replace('{{TOKEN}}','test-token').replace('{{INITIAL_MESSAGE}}','');
const page=resolve(root,'portal-test.html');await writeFile(page,html);
const executable=process.env.CHROME_PATH||(process.platform==='win32'?'C:/Program Files/Google/Chrome/Application/chrome.exe':'google-chrome');
const chrome=spawn(executable,['--headless=new','--no-first-run','--no-default-browser-check','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'],{stdio:'ignore',windowsHide:true});
const pause=ms=>new Promise(r=>setTimeout(r,ms));
let socket;
try {
  let port;
  for(let i=0;i<100;i++){try{port=(await readFile(resolve(profile,'DevToolsActivePort'),'utf8')).split('\n')[0];break;}catch{await pause(100);}}
  assert(port,'Chrome did not start');
  const tabs=await(await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  socket=new WebSocket(tabs.find(t=>t.type==='page').webSocketDebuggerUrl);
  await new Promise((r,j)=>{socket.onopen=r;socket.onerror=j;});
  let id=0;const pending=new Map();
  socket.onmessage=e=>{const m=JSON.parse(e.data);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(m.error):p.resolve(m.result);}};
  const call=(method,params={})=>new Promise((resolve,reject)=>{const key=++id;pending.set(key,{resolve,reject});socket.send(JSON.stringify({id:key,method,params}));});
  const js=async expression=>{const r=await call('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});assert(!r.exceptionDetails,JSON.stringify(r.exceptionDetails));return r.result.value;};
  await call('Page.enable');
  async function load(){await call('Page.navigate',{url:pathToFileURL(page).href});for(let i=0;i<100;i++){if(await js("!!document.getElementById('email-form')"))break;await pause(30);}await js("window.calls=[];window.reply={state:'otp',message:'コードを送信しました',ttl:115};window.fetch=async(p,o)=>{calls.push({path:p,...o});return {json:async()=>reply};}");}
  await load();
  await js("document.querySelector('#email-form button').click()");
  assert.equal(await js('calls.length'),0,'empty email must not submit');
  await js("document.getElementById('email').value='test@example.invalid';document.querySelector('#email-form button').click()");
  await pause(50);
  assert.equal(await js('calls.length'),1);
  assert.equal(await js("calls[0].headers['X-Setup-Token']"),'test-token');
  assert.equal(await js("document.getElementById('otp-form').hidden"),false);
  assert((await js('deadline-Date.now()'))<=115000,'server TTL must be respected');
  for(const [width,height] of [[320,568],[360,780],[1920,1080]]){
    await call('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:width<500});
    assert(await js('document.documentElement.scrollWidth<=innerWidth'),'horizontal overflow');
    if(width===360){const shot=await call('Page.captureScreenshot',{format:'png'});await writeFile(resolve(root,'portal-mobile.png'),Buffer.from(shot.data,'base64'));}
  }
  await js("reply={state:'error',message:'再送は30秒後にできます'};window.beforeDeadline=deadline;document.querySelector('#email-form button').click()");await pause(50);
  assert.equal(await js('deadline'),await js('beforeDeadline'),'failed resend must not reset TTL');
  await js('deadline=Date.now()-1000');await pause(600);
  assert(await js("document.getElementById('timer').textContent.includes('期限が切れました')"));
  await js("reply={state:'done',message:'ログインしました'};document.getElementById('otp').value='123456';document.querySelector('#otp-form button').click()");await pause(50);
  assert(await js("document.getElementById('email-form').hidden&&document.getElementById('otp-form').hidden"));
  assert(await js("[...document.querySelectorAll('button')].every(b=>b.disabled)"));
  await load();
  await js("fetch=async()=>{throw Error('offline')};document.getElementById('email').value='test@example.invalid';document.querySelector('#email-form button').click()");await pause(50);
  assert(await js("document.getElementById('status').textContent.includes('接続できません')"));
  assert(await js("[...document.querySelectorAll('button')].every(b=>!b.disabled)"));
  console.log('Portal validation, OTP, TTL, resend failure, success, network recovery and 3 viewports passed');
} finally {
  socket?.close();
  if(process.platform==='win32')spawnSync('taskkill',['/PID',String(chrome.pid),'/T','/F'],{stdio:'ignore',windowsHide:true});else chrome.kill();
}
