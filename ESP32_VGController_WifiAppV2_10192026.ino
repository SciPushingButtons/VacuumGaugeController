#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Adafruit_ADS1X15.h>
#include <LCD_I2C.h>
#include <math.h>


// ============================================================
// WIFI CONFIGURATION
// ============================================================

// Enter your wifi name here
const char* WIFI_SSID =
  "XXXXXXXXXXXXX";

//Enter you wifi password here
const char* WIFI_PASSWORD =
  "XXXXXXXXXXXXXX";


// Friendly local address:
//
// http://vacuumgauge.local

const char* MDNS_HOSTNAME =
  "vacuumgauge";


// ------------------------------------------------------------
// Static IP configuration
//
// CHANGE THESE if your local network does not use
// the 192.168.1.x range.
// ------------------------------------------------------------

IPAddress local_IP(
  192, 168, 0, 20
);

IPAddress gateway(
  192, 168, 1, 1
);

IPAddress subnet(
  255, 255, 255, 0
);

IPAddress primaryDNS(
  8, 8, 8, 8
);

IPAddress secondaryDNS(
  1, 1, 1, 1
);


WebServer server(80);


// ============================================================
// I2C PIN CONFIGURATION
// ============================================================

// ------------------------------------------------------------
// LCD I2C bus
//
// SDA = GPIO32
// SCL = GPIO33
// ------------------------------------------------------------

#define LCD_SDA 32
#define LCD_SCL 33


// ------------------------------------------------------------
// ADS1115 I2C bus
//
// SDA = GPIO18
// SCL = GPIO19
// ------------------------------------------------------------

#define ADS_SDA 18
#define ADS_SCL 19


// Use ESP32's second I2C controller for the ADS1115.

TwoWire ADSWire = TwoWire(1);


// ============================================================
// DEVICES
// ============================================================

LCD_I2C lcd(0x27);

Adafruit_ADS1115 ads;


// ============================================================
// GAUGE CONFIGURATION
// ============================================================

// Hardware divider = 1/4.
//
// ADS1115 therefore sees:
//
//     Gauge signal / 4
//
// Multiplying the differential voltage by 4 reconstructs
// the actual gauge output voltage.

const float DIVIDER_MULTIPLIER = 4.0;


// PKR voltage -> Torr relationship:
//
// P(Torr) = 10^(1.667 * V - 11.46)

const float TORR_SLOPE = 1.667;

const float TORR_OFFSET = 11.46;


// ============================================================
// GAUGE STATUS
// ============================================================

enum GaugeStatus {

  GAUGE_OFF,

  GAUGE_UNDERRANGE,

  GAUGE_OK,

  GAUGE_OVERRANGE,

  GAUGE_PIRANI_ERROR
};


GaugeStatus gauge_status =
  GAUGE_OFF;


// ============================================================
// MEASUREMENT VARIABLES
// ============================================================

int16_t adc_raw = 0;

float differential_voltage = 0.0;

float gauge_voltage = 0.0;

float pressure_torr = NAN;

char pressure_display[20];


// ============================================================
// TIMING
// ============================================================

// Gauge ADC measurement

unsigned long previousMeasurement = 0;

const unsigned long MEASUREMENT_INTERVAL =
  250;


// LCD refresh

unsigned long previousLCDUpdate = 0;

const unsigned long LCD_INTERVAL =
  500;


// Serial diagnostics

unsigned long previousSerialUpdate = 0;

const unsigned long SERIAL_INTERVAL =
  1000;


// ============================================================
// DETERMINE GAUGE STATUS
// ============================================================

GaugeStatus determineGaugeStatus(
  float voltage
) {

  // Gauge off / no supply

  if (voltage < 0.5) {

    return GAUGE_OFF;
  }


  // Pressure below gauge measurement range

  else if (voltage < 1.8) {

    return GAUGE_UNDERRANGE;
  }


  // Normal pressure measurement range

  else if (voltage <= 8.6) {

    return GAUGE_OK;
  }


  // Pressure above measurement range

  else if (voltage <= 9.5) {

    return GAUGE_OVERRANGE;
  }


  // Pirani error, likely filament problem

  else {

    return GAUGE_PIRANI_ERROR;
  }
}


// ============================================================
// CONVERT STATUS TO TEXT
// ============================================================

const char* getStatusText() {

  switch (gauge_status) {

    case GAUGE_OFF:

      return "Gauge Off";


    case GAUGE_UNDERRANGE:

      return "Underrange";


    case GAUGE_OK:

      return "OK";


    case GAUGE_OVERRANGE:

      return "Overrange";


    case GAUGE_PIRANI_ERROR:

      return "Pirani Error";


    default:

      return "Unknown";
  }
}


// ============================================================
// READ GAUGE
// ============================================================

void updateGaugeMeasurement() {

  /*
      ADS1115 wiring:

      A1 = divided gauge signal
      A0 = gauge signal return


      Adafruit:

      readADC_Differential_0_1()

      measures:

      A0 - A1


      We want:

      A1 - A0


      Therefore the result is negated.
  */


  adc_raw =
    ads.readADC_Differential_0_1();


  differential_voltage =
    -ads.computeVolts(adc_raw);


  // Undo the 1/4 divider

  gauge_voltage =
    differential_voltage *
    DIVIDER_MULTIPLIER;


  // Determine meaning of gauge voltage

  gauge_status =
    determineGaugeStatus(
      gauge_voltage
    );


  // Only calculate pressure inside valid range.

  if (
    gauge_status ==
    GAUGE_OK
  ) {

    pressure_torr =
      pow(
        10.0,
        TORR_SLOPE *
        gauge_voltage -
        TORR_OFFSET
      );


    snprintf(
      pressure_display,
      sizeof(
        pressure_display
      ),
      "%.2E",
      pressure_torr
    );
  }

  else {

    pressure_torr = NAN;
  }
}


// ============================================================
// UPDATE LCD
// ============================================================

void updateLCD() {

  lcd.clear();


  // ----------------------------------------------------------
  // Line 1
  // ----------------------------------------------------------

  lcd.setCursor(
    0,
    0
  );

  lcd.print("V:");

  lcd.print(
    gauge_voltage,
    3
  );

  lcd.print(" V");


  // ----------------------------------------------------------
  // Line 2
  // ----------------------------------------------------------

  lcd.setCursor(
    0,
    1
  );


  switch (gauge_status) {

    case GAUGE_OFF:

      lcd.print(
        "GAUGE OFF"
      );

      break;


    case GAUGE_UNDERRANGE:

      lcd.print(
        "UR"
      );

      break;


    case GAUGE_OK:

      lcd.print(
        pressure_display
      );

      lcd.print(
        " Torr"
      );

      break;


    case GAUGE_OVERRANGE:

      lcd.print(
        "OR"
      );

      break;


    case GAUGE_PIRANI_ERROR:

      lcd.print(
        "PIRANI ERROR"
      );

      break;
  }
}


// ============================================================
// SERIAL DIAGNOSTICS
// ============================================================

void printSerialData() {

  Serial.print(
    "ADC Raw: "
  );

  Serial.println(
    adc_raw
  );


  Serial.print(
    "Differential A1-A0: "
  );

  Serial.print(
    differential_voltage,
    5
  );

  Serial.println(
    " V"
  );


  Serial.print(
    "Gauge Voltage: "
  );

  Serial.print(
    gauge_voltage,
    4
  );

  Serial.println(
    " V"
  );


  Serial.print(
    "Status: "
  );

  Serial.println(
    getStatusText()
  );


  if (
    gauge_status ==
    GAUGE_OK
  ) {

    Serial.print(
      "Pressure: "
    );

    Serial.print(
      pressure_display
    );

    Serial.println(
      " Torr"
    );
  }


  Serial.println(
    "----------------------------"
  );
}


// ============================================================
// WEB PAGE
// ============================================================

const char MAIN_page[] PROGMEM = R"rawliteral(

<!DOCTYPE html>

<html>

<head>

<meta charset="UTF-8">

<meta
  name="viewport"
  content="width=device-width, initial-scale=1"
>

<title>
Vacuum Gauge Controller
</title>


<style>

body {
  font-family: Arial, sans-serif;
  background: #111827;
  color: #f3f4f6;
  margin: 0;
  padding: 20px;
}

.container {
  max-width: 1100px;
  margin: auto;
}

h1 {
  margin-bottom: 4px;
}

.subtitle {
  color: #9ca3af;
  margin-bottom: 25px;
}

.cards {
  display: flex;
  flex-wrap: wrap;
  gap: 15px;
}

.card {
  background: #1f2937;
  border-radius: 10px;
  padding: 20px;
  flex: 1;
  min-width: 180px;
}

.label {
  font-size: 14px;
  color: #9ca3af;
}

.value {
  font-size: 27px;
  font-weight: bold;
  margin-top: 8px;
}

.graphBox {
  background: #1f2937;
  border-radius: 10px;
  padding: 20px;
  margin-top: 20px;
}

.graphControls {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  margin-bottom: 16px;
}

.graphControls label {
  display: flex;
  flex-direction: column;
  font-size: 13px;
  color: #9ca3af;
}

.graphControls select,
.graphControls input,
.graphControls button {

  margin-top: 5px;

  padding: 8px;

  border-radius: 5px;

  border:
    1px solid #4b5563;

  background: #111827;

  color: #f3f4f6;
}

.graphControls button {

  cursor: pointer;

  min-width: 90px;
}

.buttonGroup {

  display: flex;

  gap: 6px;

  align-items: flex-end;

  flex-wrap: wrap;
}

canvas {

  display: block;

  width: 100%;

  height: 430px;

  background: #111827;

  border-radius: 6px;
}

.note {

  margin-top: 10px;

  color: #9ca3af;

  font-size: 12px;
}

</style>

</head>


<body>

<div class="container">


<h1>
Vacuum Gauge Controller
</h1>


<div class="subtitle">
ESP32 / ADS1115 PKR Gauge Monitor
</div>


<div class="cards">


<div class="card">

<div class="label">
Pressure
</div>

<div
  class="value"
  id="pressure"
>
--
</div>

</div>


<div class="card">

<div class="label">
Gauge Voltage
</div>

<div
  class="value"
  id="voltage"
>
--
</div>

</div>


<div class="card">

<div class="label">
Gauge Status
</div>

<div
  class="value"
  id="status"
>
--
</div>

</div>


<div class="card">

<div class="label">
ADC Differential
</div>

<div
  class="value"
  id="differential"
>
--
</div>

</div>


</div>


<div class="graphBox">


<h2>
Pressure History
</h2>


<div class="graphControls">


<label>

Graph Mode

<select id="graphMode">

<option
  value="rolling"
  selected
>
Rolling Window
</option>

<option value="session">
Session History
</option>

</select>

</label>


<label>

Rolling Window

<select id="timeWindow">

<option value="60">
1 minute
</option>

<option
  value="300"
  selected
>
5 minutes
</option>

<option value="900">
15 minutes
</option>

<option value="3600">
1 hour
</option>

</select>

</label>


<label>

Pressure Scale

<select id="scaleType">

<option
  value="log"
  selected
>
Logarithmic
</option>

<option value="linear">
Linear
</option>

</select>

</label>


<label>

Y Axis

<select id="axisMode">

<option
  value="fixed"
  selected
>
Manual
</option>

<option value="auto">
Auto
</option>

</select>

</label>


<label>

Min exponent

<input
  id="minExponent"
  type="number"
  value="-10"
  min="-12"
  max="6"
  step="1"
>

</label>


<label>

Max exponent

<input
  id="maxExponent"
  type="number"
  value="3"
  min="-12"
  max="6"
  step="1"
>

</label>


<div class="buttonGroup">

<button id="zoomInButton">
Y Zoom In
</button>

<button id="zoomOutButton">
Y Zoom Out
</button>

<button id="panUpButton">
Y Up
</button>

<button id="panDownButton">
Y Down
</button>

<button id="resetYButton">
Reset Y
</button>

</div>


<div class="buttonGroup">

<button id="pauseButton">
Pause
</button>

<button id="clearButton">
Clear
</button>

</div>


</div>


<canvas id="graph"></canvas>


<div class="note">

Manual Y-axis limits use powers of ten.

For example, -8 to -3 corresponds to
1E-8 through 1E-3 Torr.

</div>


</div>

</div>


<script>


// ============================================================
// GRAPH VARIABLES
// ============================================================

const canvas =
  document.getElementById(
    "graph"
  );

const ctx =
  canvas.getContext(
    "2d"
  );


let pressureData = [];

let graphPaused = false;


// One browser sample per second.
//
// 7200 samples = approximately two hours.

const MAX_HISTORY_POINTS =
  7200;


const DEFAULT_MIN_EXP =
  -10;

const DEFAULT_MAX_EXP =
  3;


// ============================================================
// CANVAS RESIZE
// ============================================================

function resizeCanvas() {

  const ratio =
    window.devicePixelRatio ||
    1;


  const rect =
    canvas.getBoundingClientRect();


  canvas.width =
    Math.floor(
      rect.width *
      ratio
    );


  canvas.height =
    Math.floor(
      rect.height *
      ratio
    );


  ctx.setTransform(
    ratio,
    0,
    0,
    ratio,
    0,
    0
  );


  drawGraph();
}


window.addEventListener(
  "resize",
  resizeCanvas
);


// ============================================================
// PRESSURE DISPLAY FORMAT
// ============================================================

function formatPressure(p) {

  if (
    p === null ||
    !Number.isFinite(p) ||
    p <= 0
  ) {

    return "--";
  }


  return (
    p.toExponential(2) +
    " Torr"
  );
}


// ============================================================
// MANUAL Y RANGE
// ============================================================

function getManualMinExp() {

  return Number(
    document.getElementById(
      "minExponent"
    ).value
  );
}


function getManualMaxExp() {

  return Number(
    document.getElementById(
      "maxExponent"
    ).value
  );
}


function setManualRange(
  minExp,
  maxExp
) {

  minExp =
    Math.max(
      -12,
      Math.min(
        5,
        minExp
      )
    );


  maxExp =
    Math.max(
      -11,
      Math.min(
        6,
        maxExp
      )
    );


  if (
    maxExp <= minExp
  ) {

    maxExp =
      minExp + 1;
  }


  document.getElementById(
    "minExponent"
  ).value =
    minExp;


  document.getElementById(
    "maxExponent"
  ).value =
    maxExp;


  document.getElementById(
    "axisMode"
  ).value =
    "fixed";


  drawGraph();
}


// ============================================================
// ZOOM
// ============================================================

function zoomYIn() {

  let minExp =
    getManualMinExp();


  let maxExp =
    getManualMaxExp();


  const range =
    maxExp -
    minExp;


  if (
    range <= 2
  ) {

    return;
  }


  minExp += 1;

  maxExp -= 1;


  setManualRange(
    minExp,
    maxExp
  );
}


function zoomYOut() {

  let minExp =
    getManualMinExp();


  let maxExp =
    getManualMaxExp();


  minExp -= 1;

  maxExp += 1;


  setManualRange(
    minExp,
    maxExp
  );
}


// ============================================================
// Y PAN
// ============================================================

function panYUp() {

  setManualRange(
    getManualMinExp() + 1,
    getManualMaxExp() + 1
  );
}


function panYDown() {

  setManualRange(
    getManualMinExp() - 1,
    getManualMaxExp() - 1
  );
}


// ============================================================
// VISIBLE HISTORY
// ============================================================

function getVisibleData() {

  const graphMode =
    document.getElementById(
      "graphMode"
    ).value;


  if (
    graphMode ===
    "session"
  ) {

    return pressureData;
  }


  const seconds =
    Number(
      document.getElementById(
        "timeWindow"
      ).value
    );


  const cutoff =
    Date.now() -
    seconds * 1000;


  return pressureData.filter(
    point =>
      point.time >=
      cutoff
  );
}


// ============================================================
// DRAW GRAPH
// ============================================================

function drawGraph() {

  const rect =
    canvas.getBoundingClientRect();


  const width =
    rect.width;


  const height =
    rect.height;


  ctx.clearRect(
    0,
    0,
    width,
    height
  );


  const left = 78;

  const right = 22;

  const top = 20;

  const bottom = 45;


  const graphWidth =
    width -
    left -
    right;


  const graphHeight =
    height -
    top -
    bottom;


  if (
    graphWidth <= 0 ||
    graphHeight <= 0
  ) {

    return;
  }


  const scaleType =
    document.getElementById(
      "scaleType"
    ).value;


  const axisMode =
    document.getElementById(
      "axisMode"
    ).value;


  const graphMode =
    document.getElementById(
      "graphMode"
    ).value;


  const visibleData =
    getVisibleData();


  const validPoints =
    visibleData.filter(
      point =>
        point.pressure !== null &&
        Number.isFinite(
          point.pressure
        ) &&
        point.pressure > 0
    );


  // ----------------------------------------------------------
  // X RANGE
  // ----------------------------------------------------------

  let xStart;

  let xEnd;


  if (
    graphMode ===
    "rolling"
  ) {

    const seconds =
      Number(
        document.getElementById(
          "timeWindow"
        ).value
      );


    xEnd =
      Date.now();


    xStart =
      xEnd -
      seconds * 1000;
  }

  else {

    if (
      pressureData.length >
      1
    ) {

      xStart =
        pressureData[0].time;


      xEnd =
        pressureData[
          pressureData.length -
          1
        ].time;
    }

    else {

      xEnd =
        Date.now();


      xStart =
        xEnd -
        60000;
    }
  }


  if (
    xEnd <=
    xStart
  ) {

    xEnd =
      xStart +
      1000;
  }


  // ----------------------------------------------------------
  // Y RANGE
  // ----------------------------------------------------------

  let minExponent =
    getManualMinExp();


  let maxExponent =
    getManualMaxExp();


  if (
    axisMode ===
    "auto" &&
    validPoints.length >
    0
  ) {

    const pressures =
      validPoints.map(
        p =>
          p.pressure
      );


    const minPressure =
      Math.min(
        ...pressures
      );


    const maxPressure =
      Math.max(
        ...pressures
      );


    minExponent =
      Math.floor(
        Math.log10(
          minPressure
        )
      ) - 1;


    maxExponent =
      Math.ceil(
        Math.log10(
          maxPressure
        )
      ) + 1;


    if (
      minExponent ===
      maxExponent
    ) {

      minExponent -= 1;

      maxExponent += 1;
    }
  }


  if (
    minExponent >=
    maxExponent
  ) {

    maxExponent =
      minExponent + 1;
  }


  // ----------------------------------------------------------
  // LOG GRID
  // ----------------------------------------------------------

  ctx.lineWidth = 1;

  ctx.font =
    "12px Arial";


  if (
    scaleType ===
    "log"
  ) {

    for (
      let exponent =
        minExponent;

      exponent <=
        maxExponent;

      exponent++
    ) {

      const fraction =
        (
          maxExponent -
          exponent
        ) /
        (
          maxExponent -
          minExponent
        );


      const y =
        top +
        fraction *
        graphHeight;


      ctx.strokeStyle =
        "#374151";


      ctx.beginPath();


      ctx.moveTo(
        left,
        y
      );


      ctx.lineTo(
        left +
        graphWidth,
        y
      );


      ctx.stroke();


      ctx.fillStyle =
        "#9ca3af";


      ctx.fillText(
        "1E" +
        exponent,
        10,
        y + 4
      );
    }
  }


  // ----------------------------------------------------------
  // LINEAR RANGE
  // ----------------------------------------------------------

  let linearMin =
    Math.pow(
      10,
      minExponent
    );


  let linearMax =
    Math.pow(
      10,
      maxExponent
    );


  if (
    scaleType ===
      "linear" &&
    axisMode ===
      "auto" &&
    validPoints.length >
      0
  ) {

    const pressures =
      validPoints.map(
        p =>
          p.pressure
      );


    linearMin =
      Math.min(
        ...pressures
      );


    linearMax =
      Math.max(
        ...pressures
      );


    if (
      linearMin ===
      linearMax
    ) {

      linearMin *=
        0.9;


      linearMax *=
        1.1;
    }


    const padding =
      (
        linearMax -
        linearMin
      ) *
      0.1;


    linearMin =
      Math.max(
        0,
        linearMin -
        padding
      );


    linearMax +=
      padding;
  }


  // ----------------------------------------------------------
  // LINEAR GRID
  // ----------------------------------------------------------

  if (
    scaleType ===
    "linear"
  ) {

    const divisions =
      6;


    for (
      let i = 0;

      i <=
      divisions;

      i++
    ) {

      const fraction =
        i /
        divisions;


      const y =
        top +
        fraction *
        graphHeight;


      const value =
        linearMax -
        fraction *
        (
          linearMax -
          linearMin
        );


      ctx.strokeStyle =
        "#374151";


      ctx.beginPath();


      ctx.moveTo(
        left,
        y
      );


      ctx.lineTo(
        left +
        graphWidth,
        y
      );


      ctx.stroke();


      ctx.fillStyle =
        "#9ca3af";


      ctx.fillText(
        value.toExponential(
          1
        ),
        5,
        y + 4
      );
    }
  }


  // ----------------------------------------------------------
  // AXES
  // ----------------------------------------------------------

  ctx.strokeStyle =
    "#9ca3af";


  ctx.lineWidth =
    1;


  ctx.beginPath();


  ctx.moveTo(
    left,
    top
  );


  ctx.lineTo(
    left,
    top +
    graphHeight
  );


  ctx.lineTo(
    left +
    graphWidth,
    top +
    graphHeight
  );


  ctx.stroke();


  // ----------------------------------------------------------
  // X AXIS LABELS
  // ----------------------------------------------------------

  ctx.fillStyle =
    "#9ca3af";


  if (
    graphMode ===
    "rolling"
  ) {

    const seconds =
      Number(
        document.getElementById(
          "timeWindow"
        ).value
      );


    let startLabel;


    if (
      seconds <
      120
    ) {

      startLabel =
        "-" +
        seconds +
        " sec";
    }

    else {

      startLabel =
        "-" +
        Math.round(
          seconds /
          60
        ) +
        " min";
    }


    ctx.fillText(
      startLabel,
      left,
      height -
      10
    );


    ctx.fillText(
      "Now",
      left +
      graphWidth -
      25,
      height -
      10
    );
  }

  else {

    ctx.fillText(
      "Session start",
      left,
      height -
      10
    );


    ctx.fillText(
      "Latest",
      left +
      graphWidth -
      35,
      height -
      10
    );
  }


  // ----------------------------------------------------------
  // NO DATA
  // ----------------------------------------------------------

  if (
    visibleData.length <
    2
  ) {

    ctx.fillStyle =
      "#9ca3af";


    ctx.fillText(
      "Waiting for pressure data...",
      left + 20,
      top + 30
    );


    return;
  }


  // ----------------------------------------------------------
  // PRESSURE TRACE
  // ----------------------------------------------------------

  ctx.strokeStyle =
    "#60a5fa";


  ctx.lineWidth =
    2;


  ctx.beginPath();


  let drawing =
    false;


  for (
    const point of
    visibleData
  ) {

    if (
      point.pressure ===
        null ||
      !Number.isFinite(
        point.pressure
      ) ||
      point.pressure <=
        0
    ) {

      drawing =
        false;


      continue;
    }


    const xFraction =
      (
        point.time -
        xStart
      ) /
      (
        xEnd -
        xStart
      );


    if (
      xFraction <
        0 ||
      xFraction >
        1
    ) {

      continue;
    }


    const x =
      left +
      xFraction *
      graphWidth;


    let y;


    if (
      scaleType ===
      "log"
    ) {

      const logPressure =
        Math.log10(
          point.pressure
        );


      const fraction =
        (
          maxExponent -
          logPressure
        ) /
        (
          maxExponent -
          minExponent
        );


      const clipped =
        Math.max(
          0,
          Math.min(
            1,
            fraction
          )
        );


      y =
        top +
        clipped *
        graphHeight;
    }

    else {

      const fraction =
        (
          linearMax -
          point.pressure
        ) /
        (
          linearMax -
          linearMin
        );


      const clipped =
        Math.max(
          0,
          Math.min(
            1,
            fraction
          )
        );


      y =
        top +
        clipped *
        graphHeight;
    }


    if (
      !drawing
    ) {

      ctx.moveTo(
        x,
        y
      );


      drawing =
        true;
    }

    else {

      ctx.lineTo(
        x,
        y
      );
    }
  }


  ctx.stroke();
}


// ============================================================
// FETCH DATA FROM ESP32
// ============================================================

async function updateData() {

  try {

    const response =
      await fetch(
        "/data",
        {
          cache:
            "no-store"
        }
      );


    const data =
      await response.json();


    // --------------------------------------------------------
    // LIVE VALUE CARDS
    // --------------------------------------------------------

    document.getElementById(
      "voltage"
    ).textContent =
      data.voltage.toFixed(
        3
      ) +
      " V";


    document.getElementById(
      "differential"
    ).textContent =
      data.differential.toFixed(
        4
      ) +
      " V";


    document.getElementById(
      "status"
    ).textContent =
      data.status;


    if (
      data.validPressure
    ) {

      document.getElementById(
        "pressure"
      ).textContent =
        formatPressure(
          data.pressure
        );
    }

    else {

      document.getElementById(
        "pressure"
      ).textContent =
        data.status;
    }


    // --------------------------------------------------------
    // GRAPH HISTORY
    // --------------------------------------------------------

    if (
      !graphPaused
    ) {

      pressureData.push({

        time:
          Date.now(),

        pressure:
          data.validPressure
            ? data.pressure
            : null

      });


      if (
        pressureData.length >
        MAX_HISTORY_POINTS
      ) {

        pressureData.shift();
      }
    }


    drawGraph();
  }


  catch (
    error
  ) {

    console.log(
      "ESP32 connection error:",
      error
    );


    document.getElementById(
      "status"
    ).textContent =
      "Connection Lost";
  }
}


// ============================================================
// GRAPH CONTROL EVENTS
// ============================================================

document.getElementById(
  "pauseButton"
).addEventListener(
  "click",
  function() {

    graphPaused =
      !graphPaused;


    this.textContent =
      graphPaused
        ? "Resume"
        : "Pause";
  }
);


document.getElementById(
  "clearButton"
).addEventListener(
  "click",
  function() {

    pressureData =
      [];


    drawGraph();
  }
);


document.getElementById(
  "zoomInButton"
).addEventListener(
  "click",
  zoomYIn
);


document.getElementById(
  "zoomOutButton"
).addEventListener(
  "click",
  zoomYOut
);


document.getElementById(
  "panUpButton"
).addEventListener(
  "click",
  panYUp
);


document.getElementById(
  "panDownButton"
).addEventListener(
  "click",
  panYDown
);


document.getElementById(
  "resetYButton"
).addEventListener(
  "click",
  function() {

    setManualRange(
      DEFAULT_MIN_EXP,
      DEFAULT_MAX_EXP
    );
  }
);


document.getElementById(
  "graphMode"
).addEventListener(
  "change",
  drawGraph
);


document.getElementById(
  "timeWindow"
).addEventListener(
  "change",
  drawGraph
);


document.getElementById(
  "scaleType"
).addEventListener(
  "change",
  drawGraph
);


document.getElementById(
  "axisMode"
).addEventListener(
  "change",
  drawGraph
);


document.getElementById(
  "minExponent"
).addEventListener(
  "change",
  function() {

    document.getElementById(
      "axisMode"
    ).value =
      "fixed";


    drawGraph();
  }
);


document.getElementById(
  "maxExponent"
).addEventListener(
  "change",
  function() {

    document.getElementById(
      "axisMode"
    ).value =
      "fixed";


    drawGraph();
  }
);


// ============================================================
// START BROWSER APP
// ============================================================

resizeCanvas();

updateData();


setInterval(
  updateData,
  1000
);


</script>


</body>

</html>

)rawliteral";


// ============================================================
// WEBPAGE HANDLER
// ============================================================

void handleRoot() {

  server.send_P(
    200,
    "text/html",
    MAIN_page
  );
}


// ============================================================
// JSON DATA ENDPOINT
// ============================================================

void handleData() {

  String json;


  json.reserve(
    220
  );


  json =
    "{";


  json +=
    "\"voltage\":";


  json +=
    String(
      gauge_voltage,
      4
    );


  json +=
    ",";


  json +=
    "\"differential\":";


  json +=
    String(
      differential_voltage,
      5
    );


  json +=
    ",";


  json +=
    "\"status\":\"";


  json +=
    getStatusText();


  json +=
    "\"";


  json +=
    ",";


  if (
    gauge_status ==
    GAUGE_OK
  ) {

    json +=
      "\"pressure\":";


    json +=
      String(
        pressure_torr,
        10
      );


    json +=
      ",";


    json +=
      "\"validPressure\":true";
  }

  else {

    json +=
      "\"pressure\":null,";


    json +=
      "\"validPressure\":false";
  }


  json +=
    "}";


  server.sendHeader(
    "Cache-Control",
    "no-store"
  );


  server.send(
    200,
    "application/json",
    json
  );
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(
    115200
  );


  delay(
    500
  );


  Serial.println();


  Serial.println(
    "ESP32 Vacuum Gauge Controller"
  );


  Serial.println(
    "============================="
  );


  // ==========================================================
  // LCD I2C BUS
  // ==========================================================

  Wire.begin(
    LCD_SDA,
    LCD_SCL
  );


  lcd.begin();


  lcd.backlight();


  lcd.clear();


  lcd.setCursor(
    0,
    0
  );


  lcd.print(
    "Vacuum Gauge"
  );


  lcd.setCursor(
    0,
    1
  );


  lcd.print(
    "Starting..."
  );


  // ==========================================================
  // ADS1115 I2C BUS
  // ==========================================================

  ADSWire.begin(
    ADS_SDA,
    ADS_SCL
  );


  if (
    !ads.begin(
      0x48,
      &ADSWire
    )
  ) {

    Serial.println(
      "ERROR: ADS1115 not found."
    );


    lcd.clear();


    lcd.setCursor(
      0,
      0
    );


    lcd.print(
      "ADS1115 ERROR"
    );


    while (
      true
    ) {

      delay(
        1000
      );
    }
  }


  /*
      GAIN_ONE:

      ADS1115 full-scale range:

          +/- 4.096 V


      Maximum gauge signal:

          ~10.5 V


      After 1/4 divider:

          10.5 / 4
          = 2.625 V


      Therefore +/-4.096 V gives comfortable headroom.
  */


  ads.setGain(
    GAIN_ONE
  );


  Serial.println(
    "ADS1115 initialized."
  );


  // ==========================================================
  // WIFI SETUP
  // ==========================================================

  lcd.clear();


  lcd.setCursor(
    0,
    0
  );


  lcd.print(
    "Connecting WiFi"
  );


  WiFi.mode(
    WIFI_STA
  );


  // ----------------------------------------------------------
  // Configure static IP
  // ----------------------------------------------------------

  if (
    !WiFi.config(
      local_IP,
      gateway,
      subnet,
      primaryDNS,
      secondaryDNS
    )
  ) {

    Serial.println(
      "WARNING: Static IP configuration failed."
    );
  }


  // ----------------------------------------------------------
  // Connect to network
  // ----------------------------------------------------------

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );


  Serial.print(
    "Connecting to WiFi"
  );


  unsigned long wifiStart =
    millis();


  while (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    delay(
      500
    );


    Serial.print(
      "."
    );


    // Do not hang forever if Wi-Fi is unavailable.

    if (
      millis() -
      wifiStart >
      30000
    ) {

      break;
    }
  }


  Serial.println();


  // ==========================================================
  // WIFI SUCCESS
  // ==========================================================

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    Serial.println(
      "WiFi connected."
    );


    Serial.print(
      "IP address: "
    );


    Serial.println(
      WiFi.localIP()
    );


    // --------------------------------------------------------
    // MDNS
    // --------------------------------------------------------

    if (
      MDNS.begin(
        MDNS_HOSTNAME
      )
    ) {

      MDNS.addService(
        "http",
        "tcp",
        80
      );


      Serial.print(
        "mDNS started: http://"
      );


      Serial.print(
        MDNS_HOSTNAME
      );


      Serial.println(
        ".local"
      );
    }

    else {

      Serial.println(
        "WARNING: mDNS startup failed."
      );
    }


    // --------------------------------------------------------
    // WEB SERVER
    // --------------------------------------------------------

    server.on(
      "/",
      handleRoot
    );


    server.on(
      "/data",
      handleData
    );


    server.begin();


    Serial.println(
      "Web server started."
    );


    Serial.println();


    Serial.println(
      "Open the controller at:"
    );


    Serial.print(
      "http://"
    );


    Serial.println(
      WiFi.localIP()
    );


    Serial.print(
      "http://"
    );


    Serial.print(
      MDNS_HOSTNAME
    );


    Serial.println(
      ".local"
    );


    // --------------------------------------------------------
    // Show network info briefly on LCD
    // --------------------------------------------------------

    lcd.clear();


    lcd.setCursor(
      0,
      0
    );


    lcd.print(
      "WiFi Connected"
    );


    lcd.setCursor(
      0,
      1
    );


    lcd.print(
      WiFi.localIP()
    );


    delay(
      3000
    );
  }


  // ==========================================================
  // WIFI FAILURE
  // ==========================================================

  else {

    Serial.println(
      "WiFi connection failed."
    );


    Serial.println(
      "Gauge measurement will continue locally."
    );


    lcd.clear();


    lcd.setCursor(
      0,
      0
    );


    lcd.print(
      "WiFi Failed"
    );


    lcd.setCursor(
      0,
      1
    );


    lcd.print(
      "Gauge still OK"
    );


    delay(
      2000
    );
  }
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop() {

  // ----------------------------------------------------------
  // WEB SERVER
  // ----------------------------------------------------------

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    server.handleClient();
  }


  const unsigned long now =
    millis();


  // ----------------------------------------------------------
  // GAUGE MEASUREMENT
  // ----------------------------------------------------------

  if (
    now -
    previousMeasurement >=
    MEASUREMENT_INTERVAL
  ) {

    previousMeasurement =
      now;


    updateGaugeMeasurement();
  }


  // ----------------------------------------------------------
  // LCD
  // ----------------------------------------------------------

  if (
    now -
    previousLCDUpdate >=
    LCD_INTERVAL
  ) {

    previousLCDUpdate =
      now;


    updateLCD();
  }


  // ----------------------------------------------------------
  // SERIAL OUTPUT
  // ----------------------------------------------------------

  if (
    now -
    previousSerialUpdate >=
    SERIAL_INTERVAL
  ) {

    previousSerialUpdate =
      now;


    printSerialData();
  }
}