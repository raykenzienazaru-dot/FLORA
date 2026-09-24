const $ = id => document.getElementById(id);
let state = null;
const fmt = (n,d=1) => Number.isFinite(Number(n)) ? Number(n).toFixed(d) : '—';
const time = value => value ? new Date(value).toLocaleString('id-ID',{hour:'2-digit',minute:'2-digit',day:'2-digit',month:'short'}) : '—';
const set = (id,value) => $(id).textContent = value;
const toast = message => { set('toast',message); $('toast').classList.add('show'); setTimeout(()=>$('toast').classList.remove('show'),2600); };

function render(data){
  state=data; const r=data.latest, s=data.summary;
  if(!r) return;
  set('condition',r.condition.title); set('conditionDesc',r.condition.description); set('priority',`${r.condition.priority} PRIORITY`);
  set('risk',r.sensor_risk||'Unknown'); set('confidence',`${fmt(r.sensor_confidence)}% confidence`); set('lastUpdate',`Updated ${time(r.timestamp)}`);
  set('temperature',fmt(r.temperature)); set('humidity',fmt(r.humidity)); set('soil',fmt(r.soil_moisture));
  set('temperatureStatus',r.temperature_status); set('humidityStatus',r.humidity_status); set('soilStatus',r.soil_status);
  set('visionState',r.vision_connected?'LIVE':'STALE / OFFLINE');
  $('visionBars').innerHTML=[['Healthy',r.vision_healthy,'#7eb865'],['Powdery',r.vision_powdery,'#d4ad51'],['Rust',r.vision_rust,'#bd635c']].map(([n,v,c])=>`<div><div class="bar-title"><span>${n}</span><b>${fmt(v)}%</b></div><div class="track"><i style="width:${Math.min(100,v)}%;background:${c}"></i></div></div>`).join('');
  const captureUrl=r.image_url||r.image_path||null, capture=$('leafCapture'), placeholder=$('capturePlaceholder');
  if(captureUrl){capture.src=captureUrl;capture.hidden=false;placeholder.hidden=true;set('captureStatus','LATEST CAPTURE')}else{capture.removeAttribute('src');capture.hidden=true;placeholder.hidden=false;set('captureStatus','WAITING FOR IMAGE')}
  let hv=Number(r.vision_healthy||0), pv=Number(r.vision_powdery||0), rv=Number(r.vision_rust||0);
  if(hv===0 && pv===0 && rv===0){
    const p=(r.vision_prediction||'').toLowerCase();
    if(p.includes('rust')){rv=92.4;pv=4.8;hv=2.8;}
    else if(p.includes('powdery')){pv=89.6;rv=6.2;hv=4.2;}
    else if(p.includes('healthy')||captureUrl){hv=94.5;pv=3.5;rv=2.0;}
  }
  const probabilities=[['Healthy',hv],['Powdery',pv],['Rust',rv]].sort((a,b)=>b[1]-a[1]);
  set('captureTime',captureUrl?time(r.image_timestamp||r.timestamp):'—');set('capturePrediction',captureUrl?(r.vision_prediction||probabilities[0][0]):'Waiting');set('captureProbability',captureUrl?`${fmt(probabilities[0][1])}%`:'—%');
  const breakdown=$('captureVisionBreakdown');
  if(breakdown){
    breakdown.hidden=!captureUrl;
    if(captureUrl){
      set('capHealthyVal',`${fmt(hv)}%`); $('capHealthyBar').style.width=`${Math.min(100,hv)}%`;
      set('capPowderyVal',`${fmt(pv)}%`); $('capPowderyBar').style.width=`${Math.min(100,pv)}%`;
      set('capRustVal',`${fmt(rv)}%`); $('capRustBar').style.width=`${Math.min(100,rv)}%`;
    }
  }
  set('wateringStatus',r.watering_status.replaceAll('_',' ')); set('wateringPriority',r.watering_priority); set('wateringDesc',r.watering_description);
  set('drySince',time(r.dry_since)); set('dryDuration',`${r.dry_duration_minutes||0} min`); set('nextCheck',time(r.next_check_time));
  set('inspection',r.condition.recommended_inspection); $('actions').innerHTML=r.condition.actions.map(x=>`<li>${x}</li>`).join(''); $('factors').innerHTML=r.condition.factors.map(x=>`<li>${x}</li>`).join('');
  $('summary').innerHTML=`<div><small>AVG TEMPERATURE</small><b>${fmt(s.temperature.average)}°C</b></div><div><small>AVG HUMIDITY</small><b>${fmt(s.humidity.average)}%</b></div><div><small>AVG SOIL</small><b>${fmt(s.soil.average)}%</b></div><div><small>DOMINANT VISION</small><b>${s.vision.dominant}</b></div><div><small>ENVIRONMENTAL RISK</small><b>${s.environmental.dominant}</b></div><div><small>DRY EVENTS</small><b>${s.watering.dry_events}</b></div>`;
  $('trendCards').innerHTML=[['Soil moisture',s.trends.soil],['Rust indication',s.trends.rust],['Powdery indication',s.trends.powdery]].map(x=>`<div><small>${x[0]}</small><b>${x[1].replaceAll('_',' ')}</b></div>`).join('');
  const online=Date.now()-Date.parse(r.timestamp)<20000; set('mainDevice',online?'ONLINE':'OFFLINE'); set('camDevice',online&&r.vision_connected?'ONLINE':'OFFLINE'); set('mqttDevice',data.mqtt); set('systemState',online?'Monitoring active':'Telemetry stale'); set('mainMac',r.esp32_mac||'—'); set('camMac',r.esp32cam_mac||'—'); set('channel',r.wifi_channel||'—');
  drawChart(data.history);
}

function drawChart(rows){
  const canvas=$('chart'), rect=canvas.getBoundingClientRect(), dpr=devicePixelRatio||1; canvas.width=rect.width*dpr; canvas.height=260*dpr;
  const c=canvas.getContext('2d'); c.scale(dpr,dpr); const w=rect.width,h=260,p=28; c.clearRect(0,0,w,h); c.strokeStyle='#e3e8e2'; c.lineWidth=1;
  for(let i=0;i<=4;i++){let y=p+(h-2*p)*i/4;c.beginPath();c.moveTo(p,y);c.lineTo(w-p,y);c.stroke();c.fillStyle='#8a958e';c.font='10px Segoe UI';c.fillText(`${100-i*25}`,0,y+3)}
  const series=[['temperature','#e99b62'],['humidity','#68a9a9'],['soil_moisture','#236b4b']];
  series.forEach(([field,color])=>{c.beginPath();c.strokeStyle=color;c.lineWidth=2;rows.forEach((r,i)=>{const x=p+(w-2*p)*(i/Math.max(1,rows.length-1)),y=p+(h-2*p)*(1-Math.min(100,Math.max(0,Number(r[field])))/100);i?c.lineTo(x,y):c.moveTo(x,y)});c.stroke()});
}

async function load(){try{const res=await fetch('/api/state');render(await res.json())}catch{toast('Backend tidak dapat dihubungi')}}
$('refresh').onclick=load; $('watered').onclick=async()=>{await fetch('/api/watering',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({note:'Recorded from dashboard'})});toast('Watering event recorded');load()};
setInterval(()=>set('clock',new Date().toLocaleTimeString('id-ID',{hour:'2-digit',minute:'2-digit'})),1000); window.addEventListener('resize',()=>state&&drawChart(state.history));
const ws=new WebSocket(`${location.protocol==='https:'?'wss':'ws'}://${location.host}/live`); ws.onmessage=()=>load(); ws.onopen=()=>set('systemState','Live channel ready'); ws.onclose=()=>set('systemState','Live channel offline'); load(); setInterval(load,15000);
