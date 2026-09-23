/**
 * Software AI Engine for FLORA
 *
 * Runs all AI / Machine Learning inference, labeling, and condition handling
 * directly in software (web dashboard & server), offloading computational stress
 * and heat generation from the microcontroller hardware (ESP32 & ESP32-CAM).
 */

import { ConditionDetails, TelemetryRecord, ThresholdConfig } from '../types/dashboard';

// ============================================================================
// 1. NEURAL NETWORK WEIGHTS & PARAMETERS (Exact weights from trained model)
// Architecture: Input (3) -> Dense (16, ReLU) -> Dense (8, ReLU) -> Dense (3, Softmax)
// Classes: 0: High, 1: Low, 2: Moderate
// ============================================================================

export const AI_MODEL_CONFIG = {
  version: '2.0.0-software',
  inputNames: ['Temperature (°C)', 'Humidity (%)', 'Soil Moisture (%)'],
  classes: ['High', 'Low', 'Moderate'] as const,
  scaler: {
    mean: [28.62221905, 59.83464446, 67.15121274],
    std: [4.90833472, 14.30678582, 17.56037094],
  },
  weights: {
    w1: [
      [0.06882256269454956, -0.4267715513706207, -0.737812876701355],
      [-0.8693224787712097, -0.7673643827438354, 0.9594364762306213],
      [1.0567576885223389, 1.106550693511963, 0.018498126417398453],
      [-0.14441879093647003, 0.00937872938811779, -0.167218416929245],
      [-0.8071327209472656, -0.07039181143045425, -0.8527116179466248],
      [0.833522617816925, -0.004474660847336054, -0.870482861995697],
      [1.2302871942520142, -0.04089164733886719, -0.11043281108140945],
      [0.4346289038658142, -0.5216134190559387, 0.4670726954936981],
      [-0.3797735273838043, 0.44210222363471985, -0.466645747423172],
      [0.3798741400241852, 0.8371866345405579, 0.931255042552948],
      [0.27597203850746155, -1.0527435541152954, -1.10983407497406],
      [0.028266865760087967, 0.3196512460708618, -0.24809375405311584],
      [0.8012734055519104, -0.8134379982948303, 0.044027362018823624],
      [-0.8783088326454163, -0.028593210503458977, -1.149275541305542],
      [-0.015918029472231865, 1.098854899406433, -1.223756194114685],
      [-0.9087123274803162, 0.7294166684150696, 0.6827735900878906],
    ],
    b1: [
      -0.33383816480636597, -0.04227512702345848, 0.00598018616437912, 0.9734883308410645,
      0.0101948706433177, 0.08699293434619904, 0.15194343030452728, -0.039835698902606964,
      -0.1792176514863968, -0.1527959257364273, 0.050946008414030075, 1.1363394260406494,
      0.032514918595552444, 0.16508275270462036, 0.08269620686769485, -0.1381007581949234,
    ],
    w2: [
      [-0.08528643846511841, 0.9258883595466614, 0.9372411966323853, -0.2972812354564667, 0.28391730785369873, 0.6539251804351807, -0.45965415239334106, 0.39327797293663025, 0.7302142977714539, 0.6038991212844849, 0.27909255027770996, -0.6000090837478638, 0.8755719065666199, 0.27866849303245544, 0.4186272919178009, 0.5126122236251831],
      [0.38313302397727966, -0.2798565924167633, -0.6670504212379456, 0.7949616312980652, -0.5273675322532654, -0.2648628056049347, 0.8802730441093445, -0.4845865070819855, 0.02055532857775688, -0.3929181396961212, -0.2888396978378296, 1.393118977546692, -0.15848633646965027, -0.014853849075734615, -0.5457460284233093, -0.33138036727905273],
      [0.4096508324146271, 0.6907843351364136, 0.5744191408157349, -0.4921860992908478, 0.21714578568935394, 0.2794820964336395, 0.5689708590507507, 0.11954770237207413, -0.24762453138828278, 1.1214158535003662, 0.8481930494308472, -0.5134627819061279, 0.46792274713516235, 1.0409159660339355, 0.690727710723877, 1.0592621564865112],
      [0.6716789603233337, -0.433378666639328, -0.3417074680328369, 1.1884747743606567, -0.6467021107673645, -1.3498767614364624, 1.0179617404937744, -0.6889092326164246, -0.49025285243988037, -1.0054714679718018, -0.13654395937919617, 1.2754573822021484, -0.7012107372283936, -0.38324686884880066, -0.19201244413852692, -0.1864306926727295],
      [0.3110116124153137, 0.6469503045082092, 0.656973123550415, -0.42947953939437866, 1.0382357835769653, 0.7639718055725098, 0.09721807390451431, 0.9110994935035706, 0.3641708791255951, 0.7807572484016418, 1.0385931730270386, -0.6630306839942932, 0.9146028757095337, 0.6162042021751404, 1.0718811750411987, 1.0075286626815796],
      [-1.461543083190918, 0.7269726395606995, -0.40606456995010376, -0.08050477504730225, -0.5667373538017273, 0.1318613588809967, 0.591193675994873, 0.055499445647001266, 0.07901997119188309, -0.9624040722846985, -0.7183491587638855, -0.02602144330739975, -0.047028105705976486, 0.35150280594825745, 0.912333071231842, -0.6064872145652771],
      [-0.7923133373260498, 0.1591053456068039, 0.8603146076202393, -0.12581093609333038, 0.777137815952301, 0.3313423991203308, -0.10066390782594681, 0.38185828924179077, 0.3132287561893463, -0.3796441853046417, 0.3952942192554474, -0.6672437191009521, 0.29572203755378723, -0.173675075173378, 0.556067943572998, 0.5922378301620483],
      [0.08922634273767471, 0.893825888633728, 0.5647271871566772, -0.331464946269989, 0.1557532101869583, 0.5404926538467407, -0.03166302293539047, 0.49496689438819885, 0.9643188118934631, 0.5456828474998474, 0.10235010832548141, -0.50180584192276, 0.7161643505096436, 0.4410538375377655, -0.008739474229514599, 0.31059911847114563],
    ],
    b2: [
      -0.4448825716972351, 1.08997642993927, -0.07677268236875534, 1.2537405490875244,
      -0.4421117603778839, 0.11290084570646286, -0.42201805114746094, -0.41561034321784973],
    w3: [
      [1.6813514232635498, -1.5571410655975342, 0.32658571004867554, -2.547600746154785, 0.9485321640968323, -2.1808338165283203, 1.1100209951400757, 1.5548009872436523],
      [-1.6825470924377441, 1.2517266273498535, -1.506621241569519, 1.286407470703125, -1.9308793544769287, 0.43764057755470276, -0.5190735459327698, -2.007999897003174],
      [-0.14856992661952972, 0.2482733279466629, 1.1309523582458496, -0.5288796424865723, 0.34089553356170654, 1.4595993757247925, -1.7829900979995728, -0.24722546339035034],
    ],
    b3: [-0.6362723112106323, 0.5786270499229431, 0.16296890377998352],
  },
};

export interface SoftwareAiInferenceResult {
  sensor_risk: 'High' | 'Low' | 'Moderate';
  sensor_confidence: number;
  high_probability: number;
  low_probability: number;
  moderate_probability: number;
  inference_source: 'SOFTWARE_NEURAL_NETWORK';
}

/** Vector dot product and add bias helper */
function denseLayer(input: number[], weights: number[][], bias: number[]): number[] {
  const output = new Array(weights.length);
  for (let i = 0; i < weights.length; i++) {
    let sum = bias[i];
    const row = weights[i];
    for (let j = 0; j < input.length; j++) {
      sum += input[j] * row[j];
    }
    output[i] = sum;
  }
  return output;
}

/** ReLU activation function */
function relu(arr: number[]): number[] {
  return arr.map((x) => Math.max(0, x));
}

/** Softmax activation with numerical stability */
function softmax(arr: number[]): number[] {
  const max = Math.max(...arr);
  const exps = arr.map((x) => Math.exp(x - max));
  const sum = exps.reduce((a, b) => a + b, 0);
  return exps.map((e) => e / (sum || 1));
}

/**
 * Runs the Environmental Neural Network Model in Software.
 * Standardizes raw temperature, humidity, and soil moisture,
 * passes through MLP 3-layer architecture, and outputs risk probabilities.
 */
export function runSoftwareEnvironmentalAi(
  temperature: number,
  humidity: number,
  soilMoisture: number
): SoftwareAiInferenceResult {
  const { mean, std } = AI_MODEL_CONFIG.scaler;
  const { w1, b1, w2, b2, w3, b3 } = AI_MODEL_CONFIG.weights;

  // 1. Feature Standardization (Z-score normalization)
  const normTemp = (temperature - mean[0]) / std[0];
  const normHum = (humidity - mean[1]) / std[1];
  const normSoil = (soilMoisture - mean[2]) / std[2];
  const inputVec = [normTemp, normHum, normSoil];

  // 2. Layer 1: Dense (16) + ReLU
  const h1 = relu(denseLayer(inputVec, w1, b1));

  // 3. Layer 2: Dense (8) + ReLU
  const h2 = relu(denseLayer(h1, w2, b2));

  // 4. Layer 3: Dense (3) + Softmax
  const logits = denseLayer(h2, w3, b3);
  const probs = softmax(logits);

  const highProb = probs[0] * 100;
  const lowProb = probs[1] * 100;
  const modProb = probs[2] * 100;

  // Determine dominant predicted class
  let bestIndex = 0;
  if (probs[1] > probs[bestIndex]) bestIndex = 1;
  if (probs[2] > probs[bestIndex]) bestIndex = 2;

  const predictedClass = AI_MODEL_CONFIG.classes[bestIndex];
  const confidence = probs[bestIndex] * 100;

  return {
    sensor_risk: predictedClass,
    sensor_confidence: Number(confidence.toFixed(2)),
    high_probability: Number(highProb.toFixed(2)),
    low_probability: Number(lowProb.toFixed(2)),
    moderate_probability: Number(modProb.toFixed(2)),
    inference_source: 'SOFTWARE_NEURAL_NETWORK',
  };
}

// ============================================================================
// 2. SMART CONDITION HANDLING ENGINE ("CARA PENANGANAN SUATU KONDISI")
// Translates multi-factor sensor signals + AI predictions into actionable protocols
// ============================================================================

export interface SmartHandlingOutput {
  condition: ConditionDetails;
  watering_status: 'NO_WATERING' | 'TOO_WET' | 'WATERING_RECOMMENDED' | 'URGENT_CHECK' | 'MONITOR' | string;
  watering_priority: 'LOW' | 'MEDIUM' | 'HIGH';
  watering_description: string;
  next_check_minutes: number;
}

export function evaluateConditionAndHandling(
  telemetry: {
    temperature: number;
    humidity: number;
    soil_moisture: number;
    sensor_risk?: string;
    vision_prediction?: string;
    vision_healthy?: number;
    vision_powdery?: number;
    vision_rust?: number;
  },
  cfg: ThresholdConfig = {
    soilDry: 30,
    soilVeryDry: 20,
    soilWet: 80,
    tempHigh: 35,
    humidityLow: 45,
    humidityHigh: 80,
    consecutive: 3,
  }
): SmartHandlingOutput {
  const { temperature, humidity, soil_moisture, sensor_risk, vision_prediction } = telemetry;
  const visual = (vision_prediction || 'Healthy').toLowerCase();

  // Factors tracking
  const factors: string[] = [];
  const actions: string[] = [];

  let conditionTitle = 'Healthy & Stable Microclimate';
  let description = 'Kondisi lingkungan dan tanaman berada dalam parameter optimal. Pertumbuhan vegetatif normal.';
  let priority: 'LOW' | 'MEDIUM' | 'HIGH' = 'LOW';
  let wateringStatus: 'NO_WATERING' | 'TOO_WET' | 'WATERING_RECOMMENDED' | 'URGENT_CHECK' | 'MONITOR' = 'NO_WATERING';
  let wateringPriority: 'LOW' | 'MEDIUM' | 'HIGH' = 'LOW';
  let wateringDescription = 'Kadar air tanah optimal, tidak diperlukan penyiraman saat ini.';
  let nextCheckMinutes = 60;
  let recommendedInspection = 'Lakukan pemeriksaan visual rutin mingguan.';

  // 1. Evaluate Extreme Soil & Microclimate Drought (URGENT)
  if (soil_moisture < cfg.soilVeryDry) {
    wateringStatus = 'URGENT_CHECK';
    wateringPriority = 'HIGH';
    priority = 'HIGH';
    conditionTitle = 'Kekeringan Kritis & Dehidrasi Media Tanam';
    factors.push(`Kelembapan tanah sangat rendah (${soil_moisture.toFixed(1)}% < batas kritis ${cfg.soilVeryDry}%)`);

    if (temperature >= cfg.tempHigh) {
      factors.push(`Suhu udara ekstrem (${temperature.toFixed(1)}°C) mempercepat laju transpirasi`);
    }
    if (humidity <= cfg.humidityLow) {
      factors.push(`Kelembapan udara rendah (${humidity.toFixed(1)}%) memicu defisit tekanan uap air`);
    }

    description = 'Media tanam mengalami defisit air parah. Tanaman berisiko layu permanen dan kerusakan perakaran bila tidak segera ditangani.';
    actions.push('Lakukan penyiraman perlahan (bottom watering atau siram bertahap) agar tanah tidak menolak air (hidrofobik).');
    actions.push('Pindahkan tanaman sementara dari paparan sinar terik matahari langsung.');
    actions.push('Periksa elastisitas batang dan helai daun setelah 30 menit penyiraman.');
    recommendedInspection = 'Segera siram dan periksa perakaran media tanam dalam 15 menit ke depan.';
    nextCheckMinutes = 15;
  }
  // 2. Normal Drought / Watering Recommended
  else if (soil_moisture < cfg.soilDry) {
    wateringStatus = 'WATERING_RECOMMENDED';
    wateringPriority = 'MEDIUM';
    priority = 'MEDIUM';
    conditionTitle = 'Media Tanam Mengering';
    factors.push(`Kelembapan tanah (${soil_moisture.toFixed(1)}%) berada di bawah batas minimum optimal (${cfg.soilDry}%)`);
    description = 'Kandungan air pada media tanam mulai menipis. Tanaman memerlukan tambahan hidrasi untuk menjaga turgor sel.';
    actions.push('Lakukan penyiraman secukupnya hingga air mengalir keluar dari lubang drainase pot.');
    actions.push('Pastikan air tidak menggenang pada tatakan pot.');
    recommendedInspection = 'Siapkan penyiraman sebelum tanah mengering sepenuhnya.';
    nextCheckMinutes = 30;
  }
  // 3. Saturated / Over-watered Soil
  else if (soil_moisture > cfg.soilWet) {
    wateringStatus = 'TOO_WET';
    wateringPriority = 'MEDIUM';
    priority = 'MEDIUM';
    conditionTitle = 'Media Tanam Terlalu Basah / Jenuh Air';
    factors.push(`Kelembapan tanah tinggi (${soil_moisture.toFixed(1)}% > batas basah ${cfg.soilWet}%)`);
    factors.push('Porositas tanah berkurang, membatasi pasokan oksigen ke akar');
    description = 'Media tanam dalam kondisi sangat jenuh air. Kondisi anaerobik yang berlarut-larut memicu pembusukan akar (root rot) dan jamur tanah.';
    actions.push('Hentikan penyiraman sampai permukaan media tanam mengering hingga kisaran 40–50%.');
    actions.push('Periksa lubang drainase pot apakah tersumbat.');
    actions.push('Tingkatkan aerasi udara di sekitar dasar pot.');
    recommendedInspection = 'Periksa drainase pot dan hindari penambahan air.';
    nextCheckMinutes = 30;
  }

  // 4. Evaluate Disease Vision Inference (Rust or Powdery Mildew)
  if (visual.includes('rust')) {
    conditionTitle = 'Indikasi Penyakit Karat Daun (Leaf Rust)';
    priority = 'HIGH';
    factors.push('Pola bercak oranye/cokelat terdeteksi pada permukaan helai daun melalui AI Vision');
    if (humidity > 70) factors.push('Kelembapan udara mendukung perkecambahan spora Pucciniales');
    description = 'Sistem mendeteksi indikasi visual jamur karat daun. Penyakit ini dapat menyebar ke dedaunan sekitar melalui spora di udara atau percikan air.';
    actions.push('Gunting dan musnahkan daun yang terinfeksi parah (jangan dijadikan kompos).');
    actions.push('Hindari membasahi daun saat menyiram; siram langsung ke pangkal tanah.');
    actions.push('Aplikasikan fungisida protektif organik (berbasis tembaga atau belerang) pada bagian bawah daun.');
    actions.push('Pisahkan tanaman yang terindikasi agar tidak menular ke tanaman lainnya.');
    recommendedInspection = 'Lakukan isolasi tanaman dan pemeriksaan fisik daun terinfeksi segera.';
  } else if (visual.includes('powdery')) {
    conditionTitle = 'Indikasi Penyakit Embun Tepung (Powdery Mildew)';
    priority = 'HIGH';
    factors.push('Pola lapisan putih menyerupai tepung terdeteksi pada daun melalui AI Vision');
    if (humidity > cfg.humidityHigh) factors.push(`Kelembapan udara tinggi (${humidity.toFixed(1)}%) mempercepat penyebaran miselium jamur`);
    description = 'Sistem mendeteksi indikasi visual jamur embun tepung (Erysiphales). Penyakit ini menghambat fotosintesis dan membuat daun mengering melengkung.';
    actions.push('Isolasi tanaman dan perbaiki sirkulasi udara dengan menjaga jarak antar pot.');
    actions.push('Semprotkan larutan fungisida ramah lingkungan (1 sdt baking soda + beberapa tetes sabun cair per liter air atau minyak mimba/neem oil).');
    actions.push('Pangkas daun yang tertutup lapisan putih tebal untuk membuka akses cahaya.');
    recommendedInspection = 'Lakukan isolasi dan semprotkan larutan pelindung organik.';
  }

  // 5. High Environmental Risk from Software ML
  if (sensor_risk === 'High' && priority !== 'HIGH') {
    priority = 'HIGH';
    conditionTitle = 'Risiko Lingkungan Iklim Mikro Tinggi';
    factors.push('Model AI iklim mikro mengidentifikasi kombinasi suhu, kelembapan, dan tanah dalam zona risiko patogen tinggi');
    description = 'Meskipun gejala fisik belum dominan, kombinasi faktor mikroklimat saat ini sangat mendukung timbulnya patogen atau stres tanaman.';
    actions.push('Sesuaikan ventilasi udara dan kelembapan ruangan.');
    actions.push('Pantau perkembangan daun lebih sering.');
  }

  if (actions.length === 0) {
    actions.push('Pertahankan siklus pemeliharaan dan jadwal monitoring rutin.');
    actions.push('Pastikan pencahayaan cukup dan ventilasi terjaga baik.');
  }

  return {
    condition: {
      title: conditionTitle,
      description,
      factors,
      actions,
      priority,
      recommended_inspection: recommendedInspection,
    },
    watering_status: wateringStatus,
    watering_priority: wateringPriority,
    watering_description: wateringDescription,
    next_check_minutes: nextCheckMinutes,
  };
}

/**
 * Enriches a raw telemetry payload with all Software AI capabilities:
 * 1. Neural Network inference (high, low, moderate probabilities, risk, confidence)
 * 2. Visual disease classification verification
 * 3. Comprehensive condition evaluation and smart handling protocol
 */
export function enrichWithSoftwareAi(
  raw: Record<string, unknown>,
  cfg?: ThresholdConfig
): TelemetryRecord {
  const temp = Number(raw.temperature ?? 25);
  const hum = Number(raw.humidity ?? 60);
  const soil = Number(raw.soil_moisture ?? 50);

  // 1. Execute Software Neural Network Model
  const mlResult = runSoftwareEnvironmentalAi(temp, hum, soil);

  // 2. Vision properties
  const visionPred = String(raw.vision_prediction ?? raw.prediction ?? 'Healthy');
  const healthy = Number(raw.vision_healthy ?? (visionPred.toLowerCase() === 'healthy' ? 95 : 5));
  const powdery = Number(raw.vision_powdery ?? (visionPred.toLowerCase() === 'powdery' ? 90 : 5));
  const rust = Number(raw.vision_rust ?? (visionPred.toLowerCase() === 'rust' ? 92 : 3));

  // 3. Smart Handling & Protocols
  const handling = evaluateConditionAndHandling(
    {
      temperature: temp,
      humidity: hum,
      soil_moisture: soil,
      sensor_risk: mlResult.sensor_risk,
      vision_prediction: visionPred,
      vision_healthy: healthy,
      vision_powdery: powdery,
      vision_rust: rust,
    },
    cfg
  );

  const timestamp = typeof raw.timestamp === 'string' ? raw.timestamp : new Date().toISOString();

  return {
    ...raw,
    timestamp,
    temperature: Number(temp.toFixed(2)),
    humidity: Number(hum.toFixed(2)),
    soil_moisture: Number(soil.toFixed(2)),
    soil_raw: Number(raw.soil_raw ?? 0),

    // Software ML inference results
    sensor_risk: mlResult.sensor_risk,
    sensor_confidence: mlResult.sensor_confidence,
    high_probability: mlResult.high_probability,
    low_probability: mlResult.low_probability,
    moderate_probability: mlResult.moderate_probability,

    // Vision results
    vision_connected: Boolean(raw.vision_connected ?? true),
    vision_healthy: healthy,
    vision_powdery: powdery,
    vision_rust: rust,
    vision_prediction: visionPred,
    vision_confidence: Math.max(healthy, powdery, rust),

    // Threshold & Status flags
    temperature_status: temp >= (cfg?.tempHigh ?? 35) ? 'HIGH' : 'NORMAL',
    humidity_status: hum > (cfg?.humidityHigh ?? 80) ? 'HIGH' : hum < (cfg?.humidityLow ?? 45) ? 'LOW' : 'NORMAL',
    soil_status: soil < (cfg?.soilDry ?? 30) ? 'DRY' : soil > (cfg?.soilWet ?? 80) ? 'WET' : 'NORMAL',

    // Smart Watering & Condition Protocol
    dry_since: null,
    dry_duration_minutes: 0,
    consecutive_dry_readings: 0,
    watering_status: handling.watering_status,
    watering_priority: handling.watering_priority,
    watering_description: handling.watering_description,
    next_check_time: new Date(Date.now() + handling.next_check_minutes * 60_000).toISOString(),
    condition: handling.condition,
  } as TelemetryRecord;
}
