#include <WiFi.h>
#include <LiquidCrystal.h>
#include <time.h>

// --- Definición de pines ---
#define ULTRA_TRIG_PIN   13
#define ULTRA_ECHO_PIN   12
LiquidCrystal lcd(2, 15, 18, 19, 21, 22);
#define LED_PIN2         5    // Rojo: PELIGRO
#define LED_PIN3         4    // Azul: PRECAUCIÓN
#define LED_PIN          14   // Verde: SEGURO
#define BUZZER_PIN       23
#define SENSOR_LLUVIA    32

// --- Variables globales ---
const char* ssid = "ISD-HOME";  
const char* password = "ISD2024.";  
WiFiServer server(80);

volatile float distance = 0.0;
String estadoLluvia = "SIN LLUVIA";
String mensajeLCD = "";
bool buzzerManualOff = false;

// Estructura para historial de datos
struct RegistroDatos {
  String fecha;
  float distancia;
  String lluvia;
  int valorLluvia;
};
RegistroDatos historial[10];
int indiceHistorial = 0;

// --- Task handles ---
TaskHandle_t TaskUltrasonidoHandle = NULL;
TaskHandle_t TaskSensoresLCDHandle = NULL;
TaskHandle_t TaskWebHandle = NULL;

// Función para agregar un nuevo registro al historial
void agregarRegistro(float dist, String lluviaEstado, int lluviaVal) {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  
  char fechaHora[20];
  strftime(fechaHora, sizeof(fechaHora), "%H:%M:%S", &timeinfo);
  
  historial[indiceHistorial] = {fechaHora, dist, lluviaEstado, lluviaVal};
  indiceHistorial = (indiceHistorial + 1) % 10;
}

// --- Task 1: Medición del sensor ultrasónico ---
void TaskUltrasonido(void *pvParameters) {
  while (true) {
    digitalWrite(ULTRA_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(ULTRA_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(ULTRA_TRIG_PIN, LOW);
    
    long dur = pulseIn(ULTRA_ECHO_PIN, HIGH, 30000);
    distance = (dur * 0.034) / 2;
    
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// --- Task 2: Lectura de sensores y LCD ---
void TaskSensoresLCD(void *pvParameters) {
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("Distancia: ");
  
  unsigned long ultimoRegistro = 0;
  
  while (true) {
    int valorLluvia = analogRead(SENSOR_LLUVIA);
    if (valorLluvia < 1500) estadoLluvia = "TORMENTA";
    else if (valorLluvia < 2400) estadoLluvia = "LLUVIOSO";
    else if (valorLluvia < 2850) estadoLluvia = "LLOVIZNA";
    else estadoLluvia = "SIN LLUVIA";
    
    // Registro cada 30 segundos
    if (millis() - ultimoRegistro >= 30000) {
      agregarRegistro(distance, estadoLluvia, valorLluvia);
      ultimoRegistro = millis();
    }
    
    // Control de LEDs y buzzer
    if (!buzzerManualOff) {
      if (distance < 5) {
        digitalWrite(LED_PIN2, LOW);
        digitalWrite(LED_PIN, HIGH);
        digitalWrite(LED_PIN3, LOW);
        digitalWrite(BUZZER_PIN, HIGH);
        mensajeLCD = "PELIGRO - " + estadoLluvia;
      } 
      else if (distance <= 6) {
        digitalWrite(LED_PIN3, HIGH);
        digitalWrite(LED_PIN2, LOW);
        digitalWrite(LED_PIN, LOW);
        digitalWrite(BUZZER_PIN, LOW);
        mensajeLCD = "PRECAUCION - " + estadoLluvia;
      } 
      else {
        digitalWrite(LED_PIN, LOW);
        digitalWrite(LED_PIN2, HIGH);
        digitalWrite(LED_PIN3, LOW);
        digitalWrite(BUZZER_PIN, LOW);
        mensajeLCD = "SEGURO - " + estadoLluvia;
      }
      
      if (valorLluvia < 400) digitalWrite(BUZZER_PIN, HIGH);
    }
    
    // Actualizar LCD
    lcd.setCursor(10, 0);
    lcd.print("    ");
    lcd.setCursor(10, 0);
    lcd.print(String(distance) + "cm");
    
    int len = mensajeLCD.length();
    if (len <= 16) {
      lcd.setCursor(0, 1);
      lcd.print(mensajeLCD + "                ");
    } else {
      for (int i = 0; i <= len - 16; i++) {
        lcd.setCursor(0, 1);
        lcd.print(mensajeLCD.substring(i, i + 16));
        delay(400);
      }
    }
    
    Serial.print("Distancia: ");
    Serial.print(distance);
    Serial.print(" cm | Lluvia: ");
    Serial.print(estadoLluvia);
    Serial.print(" (");
    Serial.print(valorLluvia);
    Serial.println(")");
    
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// --- Task 3: Servidor Web ---
void TaskWeb(void *pvParameters) {
  while (true) {
    WiFiClient client = server.available();
    if (!client) {
      vTaskDelay(1);
      continue;
    }
    
    // Esperar datos del cliente
    while(!client.available()) {
      vTaskDelay(1);
    }
    
    String request = client.readStringUntil('\r');
    client.flush();
    
    // Manejo de endpoints
    if (request.indexOf("GET /datos") != -1) {
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.print(distance);
    }
    else if (request.indexOf("GET /lluvia") != -1) {
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.print(estadoLluvia);
    }
    else if (request.indexOf("GET /historial") != -1) {
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: application/json");
      client.println("Connection: close");
      client.println();
      
      client.print("[");
      for (int i = 0; i < 10; i++) {
        int idx = (indiceHistorial + i) % 10;
        if (historial[idx].fecha != "") {
          if (i > 0) client.print(",");
          client.print("{\"fecha\":\"" + historial[idx].fecha + "\",");
          client.print("\"distancia\":" + String(historial[idx].distancia) + ",");
          client.print("\"lluvia\":\"" + historial[idx].lluvia + "\",");
          client.print("\"valorLluvia\":" + String(historial[idx].valorLluvia) + "}");
        }
      }
      client.print("]");
    }
    else if (request.indexOf("GET /buzzer/on") != -1) {
      buzzerManualOff = false;
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.print("Buzzer Reactivado");
    }
    else if (request.indexOf("GET /buzzer/off") != -1) {
      buzzerManualOff = true;
      digitalWrite(BUZZER_PIN, LOW);
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.print("Buzzer Desactivado");
    }
    else {
      // Página web principal
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println("Connection: close");
      client.println();
      
      client.print("<!DOCTYPE html><html lang=\"en\"><head>");
      client.print("<meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
      client.print("<title>Dashboard de Sensores ESP32</title>");
      client.print("<style>body{font-family:sans-serif;background-color:#f4f7f9;margin:0;padding:20px;}");
      client.print(".dashboard{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:20px;}");
      client.print(".widget{background-color:#fff;border-radius:8px;box-shadow:0 4px 8px rgba(0,0,0,0.1);padding:20px;text-align:center;}");
      client.print(".widget h2{margin-top:0;color:#333;}");
      client.print(".widget-value{font-size:2em;font-weight:bold;color:#007bff;margin-bottom:10px;}");
      client.print(".widget-graph{height:200px;width:100%;position:relative;}");
      client.print("#distancia-medidor{margin:20px auto;width:200px;height:160px;border-radius:10px;position:relative;}");
      client.print(".button-container{margin-top:20px;display:flex;justify-content:center;gap:10px;}");
      client.print("table{width:100%;border-collapse:collapse;margin-top:20px;}");
      client.print("th,td{padding:8px;text-align:left;border-bottom:1px solid #ddd;}");
      client.print("th{background-color:#f2f2f2;}");
      client.print(".alert{color:red;margin:10px 0;}</style>");
      client.print("<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>");
      client.print("<script src=\"https://cdnjs.cloudflare.com/ajax/libs/raphael/2.3.0/raphael.min.js\"></script>");
      client.print("<script src=\"https://cdn.jsdelivr.net/npm/justgage@1.6.1/justgage.min.js\"></script>");
      client.print("</head><body>");
      client.print("<div class=\"dashboard\">");
      client.print("<div class=\"widget\"><h2>Distancia</h2>");
      client.print("<div id=\"distancia-medidor\"></div>");
      client.print("<div id=\"distancia-alerta\" class=\"alert\"></div>");
      // Se elimina la inclusión fija de botones y se deja un contenedor vacío
      client.print("<div class=\"button-container\" id=\"buzzer-buttons\"></div>");
      client.print("<div class=\"widget-graph\"><h3>Historial de Distancia</h3>");
      client.print("<canvas id=\"distancia-grafica\"></canvas></div></div>");
      client.print("<div class=\"widget\"><h2>Lluvia</h2>");
      client.print("<div class=\"widget-value\" id=\"lluvia-valor\">Cargando...</div>");
      client.print("<div class=\"widget-graph\"><h3>Historial de Datos</h3>");
      client.print("<table id=\"historial-tabla\">");
      client.print("<thead><tr><th>Hora</th><th>Distancia (cm)</th><th>Estado Lluvia</th><th>Valor Lluvia</th></tr></thead>");
      client.print("<tbody id=\"historial-datos\"></tbody></table></div></div>");
      client.print("<script>");
      client.print("let distanciaMedidor;let chartDistancia;let buzzerActivo = !"+String(buzzerManualOff?"true":"false")+";");
      client.print("window.onload=function(){");
      client.print("distanciaMedidor=new JustGage({");
      // Se modifica la escala del medidor: max de 200 a 20
      client.print("id:\"distancia-medidor\",value:30,min:0,max:20,title:\"Distancia\",");
      client.print("label:\"cm\",gaugeColor:\"#e0e0e0\",levelColors:[\"#ff0000\",\"#f9d03f\",\"#00a65a\"],levelColorsGradient:false});");
      client.print("checkBuzzerState();setInterval(actualizarDatos,2000);");
      client.print("actualizarDatos();actualizarHistorial();};");
      // Se actualiza la función para mostrar solo el botón según el estado de la alarma
      client.print("function checkBuzzerState(){");
      client.print("  let container = document.getElementById('buzzer-buttons');");
      client.print("  container.innerHTML = '';");      
      client.print("  if(buzzerActivo){");
      client.print("    container.innerHTML = '<button onclick=\"buzzerOff()\">Apagar Alarma</button>';");
      client.print("  } else {");
      client.print("    container.innerHTML = '<button onclick=\"buzzerOn()\">Reactivar Alarma</button>';");
      client.print("  }");
      client.print("}");
      client.print("function actualizarDatos(){");
      client.print("  fetch('/datos').then(r=>r.text()).then(d=>{");
      client.print("    const dist=parseFloat(d);document.getElementById('distancia-alerta').innerText='';");
      client.print("    if(dist<10){document.getElementById('distancia-alerta').innerText='¡Alerta! Distancia baja.';");
      client.print("    document.getElementById('distancia-alerta').style.color='red';}");      
      client.print("    distanciaMedidor.refresh(dist);});");
      client.print("  fetch('/lluvia').then(r=>r.text()).then(d=>{");
      client.print("    document.getElementById('lluvia-valor').innerText=d;});}");
      // Se actualiza la función para que también actualice la gráfica de distancia
      client.print("function actualizarHistorial(){");
      client.print("  fetch('/historial').then(r=>r.json()).then(d=>{");
      client.print("    const tbody=document.getElementById('historial-datos');tbody.innerHTML='';");
      client.print("    let labels = []; let dataPoints = [];");
      client.print("    d.forEach(i=>{if(i.fecha){");
      client.print("      const r=document.createElement('tr');");
      client.print("      r.innerHTML=<td>${i.fecha}</td><td>${i.distancia.toFixed(1)}</td><td>${i.lluvia}</td><td>${i.valorLluvia}</td>;");
      client.print("      tbody.appendChild(r);");
      client.print("      labels.push(i.fecha); dataPoints.push(i.distancia);");
      client.print("    }});");
      client.print("    if(chartDistancia){");
      client.print("      chartDistancia.data.labels = labels;");
      client.print("      chartDistancia.data.datasets[0].data = dataPoints;");
      client.print("      chartDistancia.update();");
      client.print("    } else {");
      client.print("      const ctx = document.getElementById('distancia-grafica').getContext('2d');");
      client.print("      chartDistancia = new Chart(ctx, {");
      client.print("        type: 'line',");
      client.print("        data: { labels: labels, datasets: [{ label: 'Distancia (cm)', data: dataPoints, borderColor: 'blue', fill: false }] },");
      client.print("        options: { scales: { y: { beginAtZero: true, max: 20 } } }");
      client.print("      });");
      client.print("    }");
      client.print("  });");
      client.print("  setTimeout(actualizarHistorial,30000);}");
      client.print("function buzzerOn(){");
      client.print("  fetch('/buzzer/on').then(()=>{buzzerActivo=true;checkBuzzerState();alert('Alarma reactivada');});}");
      client.print("function buzzerOff(){");
      client.print("  if(confirm('¿Estás seguro de desactivar la alarma? Se mantendrá desactivada hasta que la reactives manualmente.')){");
      client.print("    fetch('/buzzer/off').then(()=>{buzzerActivo=false;checkBuzzerState();});}}");
      client.print("</script></body></html>");
    }
    
    delay(1);
    client.stop();
  }
}

void setup() {
  Serial.begin(9600);
  
  // Configuración de pines
  pinMode(ULTRA_TRIG_PIN, OUTPUT);
  pinMode(ULTRA_ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_PIN2, OUTPUT);
  pinMode(LED_PIN3, OUTPUT);
  pinMode(SENSOR_LLUVIA, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Inicializar historial
  for (int i = 0; i < 10; i++) {
    historial[i] = {"", 0.0, "", 0};
  }
  
  // Configurar hora
  configTime(0, 0, "pool.ntp.org");
  
  // Conexión WiFi
  Serial.print("Conectando a WiFi...");
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\n¡Conectado!");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());
  
  server.begin();
  
  // Crear tasks
  xTaskCreatePinnedToCore(TaskUltrasonido, "Ultrasonido", 2048, NULL, 1, &TaskUltrasonidoHandle, 0);
  xTaskCreatePinnedToCore(TaskSensoresLCD, "SensoresLCD", 4096, NULL, 1, &TaskSensoresLCDHandle, 1);
  xTaskCreatePinnedToCore(TaskWeb, "Web", 8192, NULL, 1, &TaskWebHandle, 1);
}

void loop() {
  delay(1000);
}
