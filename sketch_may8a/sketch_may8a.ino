#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <ArduinoJson.h>  // Einschließen der ArduinoJson-Bibliothek

const char* ssid = "ssid";
const char* password = "pass";
const char* www_username = "admin";
const char* www_password = "pass";

WebServer server(80);
BLEScan* pBLEScan;

// Verwenden eines DynamicJsonDocument anstelle eines String für scanResults
DynamicJsonDocument doc(2048);
JsonArray scanResults = doc.to<JsonArray>();

unsigned long lastScanTime = 0;
int scanInterval = 10000; // Scan interval in milliseconds
String restEndpoint = "http://example.com/endpoint"; // REST API Endpoint
int sendInterval = 10000; // Send interval in milliseconds
unsigned long lastSendTime = 0;
String lastSentData = ""; // Last sent data
int lastHttpResponseCode = 0; // Last HTTP response code
String lastHttpResult = ""; // Last HTTP response content

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        JsonObject obj = scanResults.createNestedObject();
        obj["device"] = advertisedDevice.getAddress().toString();
        obj["name"] = advertisedDevice.haveName() ? advertisedDevice.getName().c_str() : "Unknown";
        obj["rssi"] = advertisedDevice.getRSSI();
        obj["distance"] = calculateDistance(advertisedDevice.getRSSI());
    }

    static float calculateDistance(int rssi) {
        int txPower = -69; // TX Power of BLE device at 1 meter
        if (rssi == 0) return -1.0;
        float ratio = rssi * 1.0 / txPower;
        if (ratio < 1.0) {
            return pow(ratio, 10);
        } else {
            return (0.89976) * pow(ratio, 7.7095) + 0.111;
        }
    }
};

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWLAN verbunden.");
    Serial.println("IP-Adresse: " + WiFi.localIP().toString());

    server.on("/", HTTP_GET, []() {
        if (!server.authenticate(www_username, www_password)) {
            return server.requestAuthentication();
        }
        server.send(200, "text/html", generateWebPage());
    });

    server.on("/config", HTTP_GET, []() {
        if (!server.authenticate(www_username, www_password)) {
            return server.requestAuthentication();
        }
        server.send(200, "text/html", generateConfigPage());
    });

    server.on("/config", HTTP_POST, []() {
        if (!server.authenticate(www_username, www_password)) {
            return server.requestAuthentication();
        }
        if (server.hasArg("interval")) scanInterval = server.arg("interval").toInt();
        if (server.hasArg("sendInterval")) sendInterval = server.arg("sendInterval").toInt();
        if (server.hasArg("endpoint")) restEndpoint = server.arg("endpoint");
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
    });

    server.begin();
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
}

void loop() {
    if (millis() - lastScanTime > scanInterval) {
        scanResults.clear(); // Lösche alte Ergebnisse vor einem neuen Scan
        BLEScanResults foundDevices = pBLEScan->start(5, false);
        lastScanTime = millis();
    }

    if (restEndpoint.length() > 0 && millis() - lastSendTime > sendInterval) {
        sendBLEDataToEndpoint();
        lastSendTime = millis();
    }

    server.handleClient();
}

String generateWebPage() {
    String page = "<html><head><title>BLE Scanner</title>";
    page += "<style>table {width: 80%; border-collapse: collapse;} th, td {border: 1px solid black; padding: 8px; text-align: left;} th {background-color: #f2f2f2;} #status {margin-top: 20px;}</style>";
    page += "<meta http-equiv='content-type' content='text/html; charset=utf-8'>";
    page += "</head><body>";
    page += "<h1>Scan Results</h1><table>";
    for (JsonObject obj : scanResults) {
        page += "<tr><td>" + String((const char*)obj["device"]) + "</td><td>" + String((const char*)obj["name"]) + "</td><td>" + String(obj["rssi"].as<int>()) + "</td><td>" + String(obj["distance"].as<float>(), 2) + "</td></tr>";
    }
    page += "</table>";
    page += "<div id='status'><h2>REST Send Status</h2>";
    page += "Letzter Sendestatus: " + String(lastHttpResponseCode) + "<br>";
    page += "Letzte gesendete Daten: <pre>" + lastSentData + "</pre></div>";
    page += "<a href='/config'>Configure Scanner</a>";
    page += "</body></html>";
    return page;
}

String generateConfigPage() {
    String page = "<html><head><title>BLE Scanner Configuration</title></head><body>";
    page += "<h1>Configure Scanner</h1>";
    page += "<form method='post' action='/config'>";
    page += "Scan Interval: <input type='number' name='interval' value='" + String(scanInterval) + "' /> ms<br>";
    page += "REST Endpoint: <input type='text' name='endpoint' value='" + restEndpoint + "' /><br>";
    page += "Send Interval: <input type='number' name='sendInterval' value='" + String(sendInterval) + "' /> ms<br>";
    page += "<input type='submit' value='Save Settings' />";
    page += "</form></body></html>";
    return page;
}

void sendBLEDataToEndpoint() {
    if (restEndpoint.length() > 0) {
        HTTPClient http;
        http.begin(restEndpoint);
        http.addHeader("Content-Type", "application/json");
        String jsonData;
        serializeJson(doc, jsonData); // Konvertiere das JsonArray zu einem String
        lastSentData = jsonData; // Speichere die zuletzt gesendeten Daten
        int httpResponseCode = http.POST(jsonData);
        lastHttpResponseCode = httpResponseCode;
        lastHttpResult = http.getString();
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        http.end();
    }
}
