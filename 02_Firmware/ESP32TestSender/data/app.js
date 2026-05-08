(function(){
const $=id=>document.getElementById(id);
let pollTimer=null;

const scenarios={
  steady:[{k:'rate_hz',l:'Rate',u:'Hz',d:10,min:0.1,max:200},{k:'payload_size',l:'Payload',u:'',d:'medium',opts:['short','medium','long','custom']},{k:'duration_sec',l:'Duration',u:'sec (0=inf)',d:86400,min:0,max:259200}],
  burst:[{k:'burst_lines',l:'Burst Lines',u:'',d:50,min:1,max:10000},{k:'burst_rate_hz',l:'Burst Rate',u:'Hz',d:100,min:1,max:500},{k:'pause_ms',l:'Pause',u:'ms',d:2000,min:100,max:60000},{k:'payload_size',l:'Payload',u:'',d:'medium',opts:['short','medium','long','custom']},{k:'duration_sec',l:'Duration',u:'sec (0=inf)',d:86400,min:0,max:259200}],
  ramp:[{k:'start_rate_hz',l:'Start Rate',u:'Hz',d:1,min:0.1,max:200},{k:'end_rate_hz',l:'End Rate',u:'Hz',d:100,min:0.1,max:200},{k:'ramp_duration_sec',l:'Ramp Duration',u:'sec',d:3600,min:1,max:259200},{k:'hold_at_max',l:'Hold at Max',u:'sec (0=stop)',d:0,min:0,max:259200},{k:'payload_size',l:'Payload',u:'',d:'medium',opts:['short','medium','long','custom']}],
  gap_inject:[{k:'rate_hz',l:'Rate',u:'Hz',d:10,min:0.1,max:200},{k:'gap_every_sec',l:'Gap Every',u:'sec',d:300,min:10,max:86400},{k:'gap_size',l:'Gap Size',u:'lines',d:5,min:1,max:1000},{k:'payload_size',l:'Payload',u:'',d:'medium',opts:['short','medium','long','custom']},{k:'duration_sec',l:'Duration',u:'sec (0=inf)',d:3600,min:0,max:259200}],
  endurance:[{k:'base_rate_hz',l:'Base Rate',u:'Hz',d:10,min:0.1,max:200},{k:'burst_every_sec',l:'Burst Every',u:'sec',d:600,min:1,max:86400},{k:'burst_lines',l:'Burst Lines',u:'',d:100,min:1,max:10000},{k:'burst_rate_hz',l:'Burst Rate',u:'Hz',d:50,min:1,max:500},{k:'payload_size',l:'Payload',u:'',d:'medium',opts:['short','medium','long','custom']},{k:'duration_sec',l:'Duration',u:'sec (0=inf)',d:86400,min:0,max:259200}]
};

function renderParams(){
  const sc=$('scenario').value;
  const defs=scenarios[sc];
  let html='';
  defs.forEach(p=>{
    html+=`<div class="field"><label>${p.l} <small>${p.u}</small></label>`;
    if(p.opts){
      html+=`<select id="p-${p.k}">${p.opts.map(o=>`<option value="${o}"${o===p.d?' selected':''}>${o}</option>`).join('')}</select>`;
    }else{
      html+=`<input type="number" id="p-${p.k}" value="${p.d}" step="any" min="${p.min}" max="${p.max}">`;
    }
    html+=`</div>`;
  });
  $('params').innerHTML=html;
}

function setConn(ok){$('conn-dot').className=ok?'on':'off';$('conn-text').textContent=ok?'Connected':'Disconnected';}

async function fetchStatus(){
  try{
    const r=await fetch('/api/status');
    if(!r.ok)throw new Error('HTTP '+r.status);
    const d=await r.json();
    updateStats(d);
    setConn(true);
  }catch(e){setConn(false);}
}

function updateStats(d){
  const st=d.state||'IDLE';
  $('st-state').textContent=st;
  $('st-state').className='val badge '+st.toLowerCase();
  $('st-uptime').textContent=fmtTime(d.uptime_sec||0);
  $('st-ip').textContent=d.ip||'-'
  $('ls-lines').textContent=d.total_lines_sent||0;
  $('ls-seq').textContent=d.current_seq||0;
  $('ls-rate').textContent=(d.actual_rate_hz||0).toFixed(1)+' Hz';
  $('ls-target').textContent=(d.target_rate_hz||0).toFixed(1)+' Hz';
  $('ls-elapsed').textContent=fmtTime(d.elapsed_sec||0);
  const rem=d.remaining_sec;
  $('ls-remaining').textContent=(rem===0xFFFFFFFF?'∞':fmtTime(rem||0));
  $('ls-gaps').textContent=d.injected_gaps_count||0;
  $('ls-heap').textContent=(d.free_heap||0).toLocaleString();

  const running=st==='RUNNING';
  const paused=st==='PAUSED';
  const idle=st==='IDLE'||st==='COMPLETED'||st==='ERROR';
  $('btn-start').disabled=!(idle);
  $('btn-pause').disabled=!(running);
  $('btn-resume').disabled=!(paused);
  $('btn-stop').disabled=!(running||paused);
  $('btn-reset').disabled=!(idle);
  $('scenario').disabled=!(idle);
}

function fmtTime(s){
  const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sec=s%60;
  return String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(sec).padStart(2,'0');
}

function showAlert(msg,ok){
  const a=$('alert');
  a.textContent=msg;
  a.className='alert '+(ok?'ok':'err');
  setTimeout(()=>{a.className='alert hidden';},4000);
}

async function api(path,body){
  try{
    const r=await fetch('/api'+path,{method:'POST',headers:{'Content-Type':'application/json'},body:body?JSON.stringify(body):undefined});
    const j=await r.json().catch(()=>({}));
    if(!r.ok){showAlert(j.error||('HTTP '+r.status),false);return false;}
    if(j.error){showAlert(j.error,false);return false;}
    showAlert('OK',true);
    await fetchStatus();
    return true;
  }catch(e){showAlert(e.message,false);return false;}
}

function gatherParams(){
  const sc=$('scenario').value;
  const defs=scenarios[sc];
  const params={};
  defs.forEach(p=>{
    const el=$('p-'+p.k);
    params[p.k]=p.opts?el.value:parseFloat(el.value);
  });
  return {scenario:sc,params};
}

$('scenario').addEventListener('change',renderParams);
$('btn-start').addEventListener('click',()=>api('/start',gatherParams()));
$('btn-pause').addEventListener('click',()=>api('/pause'));
$('btn-resume').addEventListener('click',()=>api('/resume'));
$('btn-stop').addEventListener('click',()=>api('/stop'));
$('btn-reset').addEventListener('click',()=>api('/reset'));

renderParams();
fetchStatus();
pollTimer=setInterval(fetchStatus,1000);
})();
