/*
* By Freddy Alcarazo - Video: https://youtu.be/mxP9H63WJZQ
* Comentarios Importantes:
* Usar Esp32 v.3.1.3 Se probo con la versión 2.0.5 y a cada rato obtenia "HTTP Request Failed".
* Yo use ArduinoJson: v.6.15.2 La fuente uso v.7.3.1 no hubo problemas funciona. Autor: Benoit Blanchon
* La libreria esp32cam.h se obtuvo desde: https://github.com/yoursunny/esp32cam
*/
#include <esp32cam.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "time.h"

// WiFi Credentials
const char* WIFI_SSID = "XXXXXXXXXXXXXXXXXXXXXXX";
const char* WIFI_PASS = "XXXXXXXXXXXXXXXXXXXXXXX";

// Gemini AI API Key
const char* GEMINI_API_KEY = "XXXXXXXXXXXXXXXXXXXX"; //Generar api key en https://ai.google.dev/ go to -> Solutions -> Gemini Api -> Obtén una clave de API de Gemini.. 

//Buzzer
const int buzzerPin = 13;
unsigned long buzzerStartTime = 0;
bool buzzerActive = false;
bool buzzerState = false;


// NTP Server for Date and Time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

// Base64 Encoding Function
const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
String base64_encode(const uint8_t* data, size_t length) {
    String encoded = "";
    int i = 0;
    uint8_t array_3[3], array_4[4];
    
    while (length--) {
        array_3[i++] = *(data++);
        if (i == 3) {
            array_4[0] = (array_3[0] & 0xfc) >> 2;
            array_4[1] = ((array_3[0] & 0x03) << 4) + ((array_3[1] & 0xf0) >> 4);
            array_4[2] = ((array_3[1] & 0x0f) << 2) + ((array_3[2] & 0xc0) >> 6);
            array_4[3] = array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++)
                encoded += base64_table[array_4[i]];
            i = 0;
        }
    }
    return encoded;
}

// Get Current Date and Time
String getCurrentTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "Time Error";
    }
    char buffer[30];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
}


// Function to Detect Vehicle Number Plate
void detectEnd3dPrinting() {
    Serial.println("\n[+] Capturing Image...");
    auto frame = esp32cam::capture();
    if (frame == nullptr) {
        Serial.println("[-] Capture failed");
        return;
    }
    
    // Convert Image to Base64
    String base64Image = base64_encode(frame->data(), frame->size());
    
    // Send Image to Gemini AI
    HTTPClient http;
    String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=" + String(GEMINI_API_KEY);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String payload = "{\"contents\":[{";
    payload += "\"parts\":[";
    payload += "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"" + base64Image + "\"}}";
    payload += ",{\"text\":\"Detect the word 'Finished' in the image. If this word is present return 'Finished' if it is not present return 'No Finished'.\"}";
    payload += "]}]}";
    
    int httpCode = http.POST(payload);
    if (httpCode > 0) {
        String response = http.getString();
        Serial.println("[+] Gemini AI Response: " + response);
        
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, response);
        if (error) {
            Serial.println("[-] JSON Parse Error: " + String(error.c_str()));
            return;
        }
        
        const char* aiText = doc["candidates"][0]["content"]["parts"][0]["text"];
        String printingStatus = String(aiText);
        
        // Check if valid number plate is detected
        if (!aiText || printingStatus == "No Finished" || printingStatus.indexOf("I'm afraid") != -1 || 
            printingStatus.indexOf("unable to find") != -1 || printingStatus.indexOf("no plate") != -1 || 
            printingStatus.length() < 3) {
            Serial.println("[!] No finished yet the printing..");
            return; // Don't send to Firebase if no valid plate is detected
        }
        
        // If we reach here, we have a valid number plate
        String dateTime = getCurrentTime();
        Serial.println("\n======= My 3D Printer Status =======");
        Serial.println("Date & Time: " + dateTime);
        Serial.println("Satatus: " + printingStatus);
        Serial.println("====================================");
        
        printingStatus.trim(); //Eliminar cualquier espacio en blanco adicional. 
        
      if(printingStatus == "Finished"){
          // Encender el buzzer. 2 pitidos
          /*
          digitalWrite(buzzerPin, HIGH);
          delay(500);
          digitalWrite(buzzerPin, LOW);
          delay(500);
          digitalWrite(buzzerPin, HIGH);
          delay(500);
          digitalWrite(buzzerPin, LOW);
          */
          buzzerActive = true;
          buzzerStartTime = millis();
      }
      
    } else {
        Serial.println("[-] HTTP Request Failed: " + String(httpCode));
    }
    http.end();
}

// Setup Function
void setup() {
    Serial.begin(115200);
    Serial.println("\n[+] Starting ESP32-CAM...");
    
    //Configurar el pin del buzzer como salida
    pinMode(buzzerPin, OUTPUT);
    digitalWrite(buzzerPin, LOW);
  
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("[-] WiFi Failed!");
        delay(5000);
        ESP.restart();
    }
    Serial.println("[+] WiFi Connected: " + WiFi.localIP().toString());

    
    //Hacer un pitido indicando conexión wifi ok.
    digitalWrite(buzzerPin, HIGH);
    delay(500);
    digitalWrite(buzzerPin, LOW);

      
    // Initialize Camera
    using namespace esp32cam;
    Config cfg;
    cfg.setPins(pins::AiThinker);
    cfg.setResolution(Resolution::find(800, 600));
    cfg.setJpeg(80);
    
    if (!Camera.begin(cfg)) {
        Serial.println("[-] Camera Failed!");
        delay(5000);
        ESP.restart();
    }
    Serial.println("[+] Camera Started");
    
    // Initialize NTP Time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Start Plate Detection Task
    xTaskCreate([](void*) {
        while (1) {
            detectEnd3dPrinting();
            delay(15000); // Check every 15 seconds
        }
    }, "PlateTask", 8192, NULL, 1, NULL);
}

// Main Loop
void loop() {

  if(buzzerActive) {
      unsigned long currentTime = millis();
      unsigned long elapsed = currentTime - buzzerStartTime;
      
      // Sonar por 1 minuto (60000 ms)
      if(elapsed < 60000) {
          // Alternar el buzzer cada 500ms (pitido intermitente)
          if(elapsed % 1000 < 500) {
              if(!buzzerState) {
                  digitalWrite(buzzerPin, HIGH);
                  buzzerState = true;
              }
          } else {
              if(buzzerState) {
                  digitalWrite(buzzerPin, LOW);
                  buzzerState = false;
              }
          }
      } else {
          // Terminó el minuto
          digitalWrite(buzzerPin, LOW);
          buzzerActive = false;
      }
  }

    
}
