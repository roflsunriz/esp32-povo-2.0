#pragma once
namespace povo {
constexpr const char* setupPage = R"HTML(<!doctype html><html lang="ja"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>povo ログイン</title>
<style>body{font:18px system-ui,sans-serif;max-width:32rem;margin:3rem auto;padding:0 1rem;color:#222;background:#fffef5}input,button{font:inherit;padding:.8rem;box-sizing:border-box;max-width:100%}input{width:100%;margin:.5rem 0 1rem}button{background:#ffe600;border:1px solid #222;border-radius:.4rem;cursor:pointer}button:disabled{opacity:.5}p{line-height:1.6}#status{white-space:pre-wrap}label{display:block}small{display:block;margin-top:1rem}</style>
<h1>povo ログイン</h1><p>登録したメールアドレスへ認証コードを送ります。</p>
<form id="email-form"><label for="email">メールアドレス</label><input id="email" type="email" autocomplete="email" maxlength="254" required><button>コードを送る</button></form>
<form id="otp-form" hidden><label for="otp">今回届いた6桁のコード</label><input id="otp" inputmode="numeric" autocomplete="one-time-code" pattern="[0-9]{6}" maxlength="6" required><button>ログイン</button><small id="timer"></small></form>
<p id="status" role="status" aria-live="polite">{{INITIAL_MESSAGE}}</p><small>ログイン後は設定用Wi-Fiが終了します。通常のWi-Fiへ戻ってください。</small>
<script>
const status=document.getElementById('status'),emailForm=document.getElementById('email-form'),otpForm=document.getElementById('otp-form');let deadline=0,busy=false,complete=false;
async function submit(path,data){if(busy)return;busy=true;document.querySelectorAll('button').forEach(b=>b.disabled=true);status.textContent='通信中…';try{const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json','X-Setup-Token':'{{TOKEN}}'},body:JSON.stringify(data)});const result=await r.json();status.textContent=result.message;if(result.state==='otp'){deadline=Date.now()+Math.min(120,Math.max(0,result.ttl))*1000;otpForm.hidden=false;document.getElementById('otp').value='';document.getElementById('otp').focus();}if(result.state==='done'){complete=true;emailForm.hidden=true;otpForm.hidden=true;}}catch(e){status.textContent='接続できません。設定用Wi-Fiへの接続を確認してください。';}finally{busy=false;document.querySelectorAll('button').forEach(b=>b.disabled=complete);}}
emailForm.onsubmit=e=>{e.preventDefault();submit('/api/login',{email:document.getElementById('email').value});};otpForm.onsubmit=e=>{e.preventDefault();submit('/api/otp',{otp:document.getElementById('otp').value});};setInterval(()=>{if(!deadline||complete)return;const s=Math.max(0,Math.ceil((deadline-Date.now())/1000));document.getElementById('timer').textContent=s?'入力期限まで '+s+' 秒':'期限が切れました。「コードを送る」で新しいコードを取得してください。';},500);
</script></html>)HTML";
}
