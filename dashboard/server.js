const express = require('express');
const mqtt = require('mqtt');
const { WebSocketServer } = require('ws');
const fs = require('fs');
const path = require('path');
require('dotenv').config();

const app = express();
const port = Number(process.env.PORT || 3000);
const dataDir = path.join(__dirname, 'data');
const historyFile = path.join(dataDir, 'history.json');
const wateringFile = path.join(dataDir, 'watering-events.json');
fs.mkdirSync(dataDir, { recursive: true });

const readJson = (file, fallback = []) => {
  try { return JSON.parse(fs.readFileSync(file, 'utf8')); } catch { return fallback; }
};
const writeJson = (file, value) => fs.writeFileSync(file, JSON.stringify(value, null, 2));
let history = readJson(historyFile).slice(-5000);
let wateringEvents = readJson(wateringFile);
let mqttState = 'DISCONNECTED';
let mqttClient = null;
let lastTelemetryAt = history.at(-1)?.timestamp || null;

const cfg = {
  soilDry: Number(process.env.SOIL_DRY || 30),
  soilVeryDry: Number(process.env.SOIL_VERY_DRY || 20),
  soilWet: Number(process.env.SOIL_WET || 80),
  tempHigh: Number(process.env.TEMP_HIGH || 35),
  humidityLow: Number(process.env.HUMIDITY_LOW || 45),
  humidityHigh: Number(process.env.HUMIDITY_HIGH || 80),
  consecutive: Number(process.env.CONSECUTIVE_REQUIRED || 3)
};

const round = (n, d = 2) => Number(Number(n || 0).toFixed(d));
const validNumber = (n) => Number.isFinite(Number(n));
const dominant = (row) => {
  const choices = [['Healthy', row.vision_healthy], ['Powdery', row.vision_powdery], ['Rust', row.vision_rust]];
  return choices.sort((a, b) => Number(b[1] || 0) - Number(a[1] || 0))[0][0];
};
const trend = (rows, field) => {
  const values = rows.map(r => Number(r[field])).filter(Number.isFinite);
  if (values.length < 3) return 'INSUFFICIENT_DATA';
  const slope = (values.at(-1) - values[0]) / (values.length - 1);
  if (Math.abs(slope) < 0.4) return 'STABLE';
  return slope > 0 ? 'INCREASING' : 'DECREASING';
};

const AI_WEIGHTS = {
  mean: [28.62221905, 59.83464446, 67.15121274],
  std: [4.90833472, 14.30678582, 17.56037094],
  classes: ['High', 'Low', 'Moderate'],
  w1: [[0.06882256269454956,-0.4267715513706207,-0.737812876701355],[-0.8693224787712097,-0.7673643827438354,0.9594364762306213],[1.0567576885223389,1.106550693511963,0.018498126417398453],[-0.14441879093647003,0.00937872938811779,-0.167218416929245],[-0.8071327209472656,-0.07039181143045425,-0.8527116179466248],[0.833522617816925,-0.004474660847336054,-0.870482861995697],[1.2302871942520142,-0.04089164733886719,-0.11043281108140945],[0.4346289038658142,-0.5216134190559387,0.4670726954936981],[-0.3797735273838043,0.44210222363471985,-0.466645747423172],[0.3798741400241852,0.8371866345405579,0.931255042552948],[0.27597203850746155,-1.0527435541152954,-1.10983407497406],[0.028266865760087967,0.3196512460708618,-0.24809375405311584],[0.8012734055519104,-0.8134379982948303,0.044027362018823624],[-0.8783088326454163,-0.028593210503458977,-1.149275541305542],[-0.015918029472231865,1.098854899406433,-1.223756194114685],[-0.9087123274803162,0.7294166684150696,0.6827735900878906]],
  b1: [-0.33383816480636597,-0.04227512702345848,0.00598018616437912,0.9734883308410645,0.0101948706433177,0.08699293434619904,0.15194343030452728,-0.039835698902606964,-0.1792176514863968,-0.1527959257364273,0.050946008414030075,1.1363394260406494,0.032514918595552444,0.16508275270462036,0.08269620686769485,-0.1381007581949234],
  w2: [[-0.08528643846511841,0.9258883595466614,0.9372411966323853,-0.2972812354564667,0.28391730785369873,0.6539251804351807,-0.45965415239334106,0.39327797293663025,0.7302142977714539,0.6038991212844849,0.27909255027770996,-0.6000090837478638,0.8755719065666199,0.27866849303245544,0.4186272919178009,0.5126122236251831],[0.38313302397727966,-0.2798565924167633,-0.6670504212379456,0.7949616312980652,-0.5273675322532654,-0.2648628056049347,0.8802730441093445,-0.4845865070819855,0.02055532857775688,-0.3929181396961212,-0.2888396978378296,1.393118977546692,-0.15848633646965027,-0.014853849075734615,-0.5457460284233093,-0.33138036727905273],[0.4096508324146271,0.6907843351364136,0.5744191408157349,-0.4921860992908478,0.21714578568935394,0.2794820964336395,0.5689708590507507,0.11954770237207413,-0.24762453138828278,1.1214158535003662,0.8481930494308472,-0.5134627819061279,0.46792274713516235,1.0409159660339355,0.690727710723877,1.0592621564865112],[0.6716789603233337,-0.433378666639328,-0.3417074680328369,1.1884747743606567,-0.6467021107673645,-1.3498767614364624,1.0179617404937744,-0.6889092326164246,-0.49025285243988037,-1.0054714679718018,-0.13654395937919617,1.2754573822021484,-0.7012107372283936,-0.38324686884880066,-0.19201244413852692,-0.1864306926727295],[0.3110116124153137,0.6469503045082092,0.656973123550415,-0.42947953939437866,1.0382357835769653,0.7639718055725098,0.09721807390451431,0.9110994935035706,0.3641708791255951,0.7807572484016418,1.0385931730270386,-0.6630306839942932,0.9146028757095337,0.6162042021751404,1.0718811750411987,1.0075286626815796],[-1.461543083190918,0.7269726395606995,-0.40606456995010376,-0.08050477504730225,-0.5667373538017273,0.1318613588809967,0.591193675994873,0.055499445647001266,0.07901997119188309,-0.9624040722846985,-0.7183491587638855,-0.02602144330739975,-0.047028105705976486,0.35150280594825745,0.912333071231842,-0.6064872145652771],[-0.7923133373260498,0.1591053456068039,0.8603146076202393,-0.12581093609333038,0.777137815952301,0.3313423991203308,-0.10066390782594681,0.38185828924179077,0.3132287561893463,-0.3796441853046417,0.3952942192554474,-0.6672437191009521,0.29572203755378723,-0.173675075173378,0.556067943572998,0.5922378301620483],[0.08922634273767471,0.893825888633728,0.5647271871566772,-0.331464946269989,0.1557532101869583,0.5404926538467407,-0.03166302293539047,0.49496689438819885,0.9643188118934631,0.5456828474998474,0.10235010832548141,-0.50180584192276,0.7161643505096436,0.4410538375377655,-0.008739474229514599,0.31059911847114563]],
  b2: [-0.4448825716972351,1.08997642993927,-0.07677268236875534,1.2537405490875244,-0.4421117603778839,0.11290084570646286,-0.42201805114746094,-0.41561034321784973],
  w3: [[1.6813514232635498,-1.5571410655975342,0.32658571004867554,-2.547600746154785,0.9485321640968323,-2.1808338165283203,1.1100209951400757,1.5548009872436523],[-1.6825470924377441,1.2517266273498535,-1.506621241569519,1.286407470703125,-1.9308793544769287,0.43764057755470276,-0.5190735459327698,-2.007999897003174],[-0.14856992661952972,0.2482733279466629,1.1309523582458496,-0.5288796424865723,0.34089553356170654,1.4595993757247925,-1.7829900979995728,-0.24722546339035034]],
  b3: [-0.6362723112106323,0.5786270499229431,0.16296890377998352]
};

function runSoftwareAi(temp, hum, soil) {
  const norm = [
    (temp - AI_WEIGHTS.mean[0]) / AI_WEIGHTS.std[0],
    (hum - AI_WEIGHTS.mean[1]) / AI_WEIGHTS.std[1],
    (soil - AI_WEIGHTS.mean[2]) / AI_WEIGHTS.std[2]
  ];
  const dense = (inp, w, b) => w.map((row, i) => row.reduce((sum, val, j) => sum + val * inp[j], b[i]));
  const relu = arr => arr.map(x => Math.max(0, x));
  const softmax = arr => {
    const max = Math.max(...arr);
    const exps = arr.map(x => Math.exp(x - max));
    const sum = exps.reduce((a, b) => a + b, 0);
    return exps.map(e => e / (sum || 1));
  };
  const h1 = relu(dense(norm, AI_WEIGHTS.w1, AI_WEIGHTS.b1));
  const h2 = relu(dense(h1, AI_WEIGHTS.w2, AI_WEIGHTS.b2));
  const probs = softmax(dense(h2, AI_WEIGHTS.w3, AI_WEIGHTS.b3));
  let best = 0;
  if (probs[1] > probs[best]) best = 1;
  if (probs[2] > probs[best]) best = 2;
  return {
    risk: AI_WEIGHTS.classes[best],
    confidence: round(probs[best] * 100),
    high: round(probs[0] * 100),
    low: round(probs[1] * 100),
    moderate: round(probs[2] * 100)
  };
}

function derive(record, rows) {
  const temp = Number(record.temperature);
  const hum = Number(record.humidity);
  const soil = Number(record.soil_moisture);

  // Compute Software Neural Network ML inference
  const ai = runSoftwareAi(temp, hum, soil);
  record.sensor_risk = record.sensor_risk || ai.risk;
  record.sensor_confidence = record.sensor_confidence !== undefined ? record.sensor_confidence : ai.confidence;
  record.high_probability = record.high_probability !== undefined ? record.high_probability : ai.high;
  record.low_probability = record.low_probability !== undefined ? record.low_probability : ai.low;
  record.moderate_probability = record.moderate_probability !== undefined ? record.moderate_probability : ai.moderate;

  const recent = [...rows.slice(-(cfg.consecutive - 1)), record];
  const dryRows = recent.filter(r => Number(r.soil_moisture) < cfg.soilDry);
  const wetRows = recent.filter(r => Number(r.soil_moisture) > cfg.soilWet);
  const consecutiveDry = dryRows.length === recent.length ? dryRows.length : 0;
  let drySince = null;
  if (soil < cfg.soilDry) {
    for (let i = rows.length - 1; i >= 0; i--) {
      if (Number(rows[i].soil_moisture) >= cfg.soilDry) break;
      drySince = rows[i].timestamp;
    }
    drySince ||= record.timestamp;
  }
  const dryMinutes = drySince ? Math.max(0, Math.round((Date.parse(record.timestamp) - Date.parse(drySince)) / 60000)) : 0;

  let wateringStatus = 'NO_WATERING';
  let wateringPriority = 'LOW';
  let wateringDescription = 'Kadar air tanah optimal, tidak diperlukan penyiraman saat ini.';
  let nextCheckMinutes = 60;
  if (wetRows.length === recent.length && recent.length >= cfg.consecutive) {
    wateringStatus = 'TOO_WET';
    wateringPriority = 'MEDIUM';
    wateringDescription = 'Media tanam terdeteksi sangat lembap secara konsisten. Tunda penyiraman dan periksa drainase pot.';
    nextCheckMinutes = 30;
  } else if (consecutiveDry >= cfg.consecutive) {
    wateringStatus = 'WATERING_RECOMMENDED';
    wateringPriority = 'MEDIUM';
    wateringDescription = 'Kelembapan tanah rendah secara konsisten. Periksa tanaman dan lakukan penyiraman secukupnya.';
    nextCheckMinutes = 30;
    if (soil < cfg.soilVeryDry && temp >= cfg.tempHigh && hum <= cfg.humidityLow) {
      wateringStatus = 'URGENT_CHECK';
      wateringPriority = 'HIGH';
      wateringDescription = 'Kekeringan kritis! Tanah sangat kering disertai suhu tinggi dan kelembapan rendah. Segera siram dan beri naungan.';
      nextCheckMinutes = 15;
    }
  } else if (soil < cfg.soilDry) {
    wateringStatus = 'MONITOR';
    wateringDescription = 'Satu pembacaan tanah rendah terdeteksi. Tunggu pembacaan berikutnya untuk memastikan konsistensi.';
    nextCheckMinutes = 15;
  }

  const visual = dominant(record);
  const persistentVisual = recent.length >= cfg.consecutive && recent.every(r => dominant(r) === visual);
  let condition = 'Healthy & Stable Microclimate';
  let description = 'Parameter iklim mikro dan kelembapan optimal. Tidak ada indikasi stres atau patogen.';
  let factors = ['Parameter suhu, kelembapan, dan tanah berada di rentang ideal'];
  let actions = ['Pertahankan siklus pemeliharaan rutin', 'Pastikan pencahayaan dan sirkulasi udara optimal'];
  let priority = 'LOW';
  let recommendedInspection = 'Lakukan pemeriksaan visual rutin mingguan.';

  if (visual === 'Rust') {
    condition = 'Indikasi Penyakit Karat Daun (Rust)';
    description = 'AI Vision mendeteksi pola jamur karat daun. Berisiko menular cepat pada kanopi lembap.';
    factors = ['Spora jamur Pucciniales pada permukaan daun', 'Kelembapan udara mendukung perkecambahan jamur'];
    actions = ['Pangkas daun terinfeksi dan musnahkan', 'Hindari membasahi daun saat menyiram', 'Semprotkan fungisida protektif berbahan tembaga'];
    priority = persistentVisual ? 'HIGH' : 'MEDIUM';
    recommendedInspection = 'Segera isolasi tanaman dan periksa bagian bawah daun.';
  } else if (visual === 'Powdery') {
    condition = 'Indikasi Embun Tepung (Powdery Mildew)';
    description = 'AI Vision mendeteksi lapisan putih tepung jamur. Menghambat fotosintesis dan membuat daun layu.';
    factors = ['Lapisan miselium jamur pada daun', 'Sirkulasi udara kanopi kurang lancar'];
    actions = ['Isolasi tanaman dan perbaiki sirkulasi udara', 'Semprotkan larutan baking soda / minyak mimba', 'Beri jarak antar pot'];
    priority = persistentVisual ? 'HIGH' : 'MEDIUM';
    recommendedInspection = 'Lakukan penanganan fungisida organik dan buka sirkulasi kanopi.';
  } else if (soil < cfg.soilVeryDry) {
    condition = 'Kekeringan Kritis & Dehidrasi';
    description = 'Media tanam kehabisan cadangan air. Tanaman terancam dehidrasi permanen.';
    factors = [`Kelembapan tanah sangat rendah (${soil.toFixed(1)}%)`];
    actions = ['Lakukan penyiraman bertahap', 'Pindahkan dari terik matahari langsung', 'Periksa turgor sel daun'];
    priority = 'HIGH';
    recommendedInspection = 'Segera lakukan penyiraman darurat.';
  }

  if (persistentVisual && record.sensor_risk === 'High') priority = 'HIGH';

  return {
    ...record,
    temperature_status: temp >= cfg.tempHigh ? 'HIGH' : 'NORMAL',
    humidity_status: hum >= cfg.humidityHigh ? 'HIGH' : hum <= cfg.humidityLow ? 'LOW' : 'NORMAL',
    soil_status: soil < cfg.soilDry ? 'DRY' : soil > cfg.soilWet ? 'WET' : 'NORMAL',
    dry_since: drySince,
    dry_duration_minutes: dryMinutes,
    consecutive_dry_readings: consecutiveDry,
    watering_status: wateringStatus,
    watering_priority: wateringPriority,
    watering_description: wateringDescription,
    next_check_time: new Date(Date.parse(record.timestamp) + nextCheckMinutes * 60000).toISOString(),
    condition: { title: condition, description, factors, actions, priority, recommended_inspection: recommendedInspection }
  };
}

function normalize(payload) {
  if (!validNumber(payload.temperature) || !validNumber(payload.humidity) || !validNumber(payload.soil_moisture)) throw new Error('Telemetry sensor tidak valid');
  return {
    ...payload,
    timestamp: payload.timestamp && !Number.isNaN(Date.parse(payload.timestamp)) ? new Date(payload.timestamp).toISOString() : new Date().toISOString(),
    temperature: Number(payload.temperature), humidity: Number(payload.humidity), soil_moisture: Number(payload.soil_moisture),
    vision_healthy: Number(payload.vision_healthy || 0), vision_powdery: Number(payload.vision_powdery || 0), vision_rust: Number(payload.vision_rust || 0)
  };
}

function ingest(payload) {
  const record = derive(normalize(payload), history);
  history.push(record);
  history = history.slice(-5000);
  lastTelemetryAt = record.timestamp;
  writeJson(historyFile, history);
  broadcast({ type: 'telemetry', data: record });
  return record;
}

function summary(rows) {
  const values = field => rows.map(r => Number(r[field])).filter(Number.isFinite);
  const stats = field => { const v = values(field); return { average: round(v.reduce((a,b)=>a+b,0) / (v.length || 1)), minimum: v.length ? Math.min(...v) : 0, maximum: v.length ? Math.max(...v) : 0 }; };
  const count = (field, value) => rows.filter(r => String(r[field]).toLowerCase() === value.toLowerCase()).length;
  const visionCounts = { Healthy: rows.filter(r=>dominant(r)==='Healthy').length, Powdery: rows.filter(r=>dominant(r)==='Powdery').length, Rust: rows.filter(r=>dominant(r)==='Rust').length };
  const dominantVision = Object.entries(visionCounts).sort((a,b)=>b[1]-a[1])[0]?.[0] || 'Unknown';
  const riskCounts = { Low: count('sensor_risk','Low'), Moderate: count('sensor_risk','Moderate'), High: count('sensor_risk','High') };
  return {
    readings: rows.length, temperature: stats('temperature'), humidity: stats('humidity'), soil: stats('soil_moisture'),
    vision: { healthy: stats('vision_healthy'), powdery: stats('vision_powdery'), rust: stats('vision_rust'), counts: visionCounts, dominant: dominantVision },
    environmental: { counts: riskCounts, dominant: Object.entries(riskCounts).sort((a,b)=>b[1]-a[1])[0]?.[0] || 'Unknown' },
    watering: { dry_events: rows.filter((r,i)=>r.soil_status==='DRY' && rows[i-1]?.soil_status!=='DRY').length, wet_events: rows.filter((r,i)=>r.soil_status==='WET' && rows[i-1]?.soil_status!=='WET').length, recommendations: rows.filter(r=>['WATERING_RECOMMENDED','URGENT_CHECK'].includes(r.watering_status)).length, events: wateringEvents.length },
    trends: { soil: trend(rows,'soil_moisture'), rust: trend(rows,'vision_rust'), powdery: trend(rows,'vision_powdery') }
  };
}

app.use(express.json());
const staticDir = fs.existsSync(path.join(__dirname, 'dist')) ? path.join(__dirname, 'dist') : path.join(__dirname, 'public');
app.use(express.static(staticDir));
app.get('/api/state', (req, res) => {
  const cutoff = Date.now() - 24 * 3600000;
  const daily = history.filter(r => Date.parse(r.timestamp) >= cutoff);
  res.json({ latest: history.at(-1) || null, history: history.slice(-288), summary: summary(daily), mqtt: mqttState, lastTelemetryAt, config: cfg, wateringEvents: wateringEvents.slice(-20) });
});
app.post('/api/watering', (req, res) => {
  const latest = history.at(-1);
  const event = { id: Date.now(), timestamp: new Date().toISOString(), soil_before: latest?.soil_moisture ?? null, note: String(req.body.note || '').slice(0, 200) };
  wateringEvents.push(event); writeJson(wateringFile, wateringEvents); broadcast({type:'watering', data:event}); res.status(201).json(event);
});
let dummyFrames = [];
try {
  dummyFrames = require('./src/assets/dummyFrames.json');
} catch (e) {
  console.log('Dummy frames asset not loaded:', e.message);
}
let currentDummyIdx = 0;

function triggerSoftwareVision() {
  if (!dummyFrames || !dummyFrames.length) return null;
  const frame = dummyFrames[currentDummyIdx % dummyFrames.length];
  currentDummyIdx++;
  const imgTs = new Date().toISOString();

  if (history.length) {
    const latest = history[history.length - 1];
    latest.image_url = frame.imageUrl;
    latest.image_timestamp = imgTs;
    latest.vision_prediction = frame.prediction;
    latest.vision_healthy = frame.healthy;
    latest.vision_powdery = frame.powdery;
    latest.vision_rust = frame.rust;
    latest.vision_connected = true;
    latest.vision_confidence = Math.max(frame.healthy, frame.powdery, frame.rust);
  }

  broadcast({ type: 'image', data: { image_url: frame.imageUrl, image_timestamp: imgTs } });
  broadcast({ 
    type: 'vision', 
    data: {
      vision_prediction: frame.prediction,
      vision_healthy: frame.healthy,
      vision_powdery: frame.powdery,
      vision_rust: frame.rust,
      vision_connected: true
    } 
  });
  if (history.length) {
    broadcast({ type: 'telemetry', data: history.at(-1) });
  }

  if (mqttClient && mqttState === 'CONNECTED') {
    const visionTopic = process.env.MQTT_VISION_TOPIC || 'grenvis/vision/data';
    mqttClient.publish(visionTopic, JSON.stringify({
      prediction: frame.prediction,
      healthy: frame.healthy,
      powdery: frame.powdery,
      rust: frame.rust,
      confidence: Math.max(frame.healthy, frame.powdery, frame.rust)
    }));
  }

  return frame;
}

app.post('/api/control', (req, res) => {
  const { command } = req.body || {};
  if (!command || !['L', 'R', 'S', 'C'].includes(command)) {
    return res.status(400).json({ success: false, error: 'Invalid command. Allowed commands: L, R, S, C' });
  }

  const controlTopic = process.env.MQTT_CONTROL_TOPIC || 'grenvis/device/control';
  const messages = {
    'L': 'Left command sent',
    'R': 'Right command sent',
    'S': 'Stop command sent',
    'C': 'Camera capture request sent'
  };
  const message = messages[command];

  if (command === 'C') {
    triggerSoftwareVision();
  }

  if (mqttClient && mqttState === 'CONNECTED') {
    mqttClient.publish(controlTopic, command, { qos: 1 }, (err) => {
      if (err) {
        console.error('MQTT publish command error:', err.message);
        return res.status(500).json({ success: false, error: 'Failed to publish command to MQTT broker' });
      }
      res.json({ success: true, command, message });
    });
  } else {
    console.log(`[Device Control] ${command} command processed (MQTT state: ${mqttState})`);
    res.json({ success: true, command, message, mqtt: mqttState });
  }
});
app.post('/api/demo', (req, res) => res.status(201).json(ingest(req.body)));

const server = app.listen(port, () => console.log(`FLORA dashboard: http://localhost:${port}`));
const wss = new WebSocketServer({ server, path: '/live' });
wss.on('connection', (client) => {
  client.send(JSON.stringify({ type: 'mqtt', data: mqttState }));
  if (history.length) {
    client.send(JSON.stringify({ type: 'telemetry', data: history.at(-1) }));
  }
});
function broadcast(message) { const text = JSON.stringify(message); for (const client of wss.clients) if (client.readyState === 1) client.send(text); }

const mqttUrl = process.env.MQTT_URL || 'wss://m2da914a.ala.eu-central-1.emqxsl.com:8084/mqtt';
if (mqttUrl) {
  mqttClient = mqtt.connect(mqttUrl, {
    username: process.env.MQTT_USERNAME || 'grenvis_esp32',
    password: process.env.MQTT_PASSWORD || 'grenvis123',
    rejectUnauthorized: String(process.env.MQTT_REJECT_UNAUTHORIZED || 'true') === 'true',
    reconnectPeriod: 5000
  });
  const client = mqttClient;
  client.on('connect', () => {
    mqttState = 'CONNECTED';
    client.subscribe([
      process.env.MQTT_TOPIC || 'grenvis/sensor/data',
      process.env.MQTT_STATUS_TOPIC || 'grenvis/sensor/status',
      process.env.MQTT_VISION_TOPIC || 'grenvis/vision/data',
      process.env.MQTT_IMAGE_TOPIC || 'grenvis/vision/image',
      process.env.MQTT_CONTROL_TOPIC || 'grenvis/device/control',
    ]);
    broadcast({ type: 'mqtt', data: mqttState });
  });
  client.on('reconnect', () => { mqttState = 'RECONNECTING'; });
  client.on('offline', () => { mqttState = 'DISCONNECTED'; broadcast({ type: 'mqtt', data: mqttState }); });
  client.on('error', err => console.error('MQTT:', err.message));
  client.on('message', (topic, buffer) => {
    if (topic === (process.env.MQTT_STATUS_TOPIC || 'grenvis/sensor/status')) return;

    if (topic === (process.env.MQTT_CONTROL_TOPIC || 'grenvis/device/control')) {
      const cmd = buffer.toString().trim();
      if (cmd === 'C' || cmd === 'c') {
        triggerSoftwareVision();
      }
      return;
    }

    if (topic === (process.env.MQTT_IMAGE_TOPIC || 'grenvis/vision/image')) {
      const base64 = buffer.toString('base64');
      const dataUrl = `data:image/jpeg;base64,${base64}`;
      const imgTs = new Date().toISOString();
      if (history.length) {
        history[history.length - 1].image_url = dataUrl;
        history[history.length - 1].image_timestamp = imgTs;
      }
      broadcast({ type: 'image', data: { image_url: dataUrl, image_timestamp: imgTs } });
      return;
    }

    if (topic === (process.env.MQTT_VISION_TOPIC || 'grenvis/vision/data')) {
      try {
        const raw = JSON.parse(buffer.toString());
        const visionData = {
          vision_scan: Number(raw.vision_scan ?? raw.scan ?? 0),
          vision_prediction: String(raw.vision_prediction ?? raw.prediction ?? 'Unknown'),
          vision_confidence: Number(raw.vision_confidence ?? raw.confidence ?? 0),
          vision_healthy: Number(raw.vision_healthy ?? raw.healthy ?? 0),
          vision_powdery: Number(raw.vision_powdery ?? raw.powdery ?? 0),
          vision_rust: Number(raw.vision_rust ?? raw.rust ?? 0),
          vision_connected: true,
        };
        if (history.length) {
          Object.assign(history[history.length - 1], visionData);
        }
        broadcast({ type: 'vision', data: visionData });
      } catch (err) {
        console.error('Vision data parse error:', err.message);
      }
      return;
    }

    try { ingest(JSON.parse(buffer.toString())); } catch (err) { console.error('Telemetry ditolak:', err.message); }
  });
} else {
  console.log('MQTT nonaktif: isi MQTT_URL di dashboard/.env untuk menghubungkan broker.');
}

if (String(process.env.DEMO_MODE || 'true') === 'true' && !history.length) {
  const samples = 36;
  for (let i = samples; i > 0; i--) {
    const x = samples - i;
    ingest({ timestamp: new Date(Date.now() - i * 5 * 60000).toISOString(), temperature: 29 + x * .08 + Math.sin(x)*.5, humidity: 72 - x*.2, soil_moisture: 68 - x*1.15, sensor_risk: x > 27 ? 'High' : 'Moderate', sensor_confidence: 94.2, high_probability: x > 27 ? 91 : 3, low_probability: 2, moderate_probability: x > 27 ? 7 : 95, vision_connected: true, vision_healthy: Math.max(8, 42-x*.7), vision_powdery: 12+x*.15, vision_rust: 46+x*.55, vision_prediction: 'Rust', esp32_mac:'AA:BB:CC:11:22:33', esp32cam_mac:'DD:EE:FF:44:55:66', wifi_channel:6, uptime_seconds:x*300 });
  }
}
