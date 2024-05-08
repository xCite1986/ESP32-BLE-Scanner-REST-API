#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
const char* www_username = "admin";
const char* www_password = "admin";

WebServer server(80);
BLEScan* pBLEScan;
String scanResults;
unsigned long lastScanTime = 0;
int scanInterval = 5000; // Scan interval in milliseconds
String restEndpoint = "http://example.com/endpoint"; // REST API Endpoint
int sendInterval = 10000; // Send interval in milliseconds
unsigned long lastSendTime = 0;
String lastSentData = ""; // Last sent data
int lastHttpResponseCode = 0; // Last HTTP response code
String lastHttpResult = ""; // Last HTTP response content

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        char temp[512];
        String deviceName = "Unknown";
        if (advertisedDevice.haveName()) {
            deviceName = advertisedDevice.getName().c_str();
        }
        sprintf(temp, "<tr><td>%s</td><td>%s</td><td>%d</td><td>%.2f</td></tr>",
                advertisedDevice.getAddress().toString().c_str(),
                deviceName.c_str(),
                advertisedDevice.getRSSI(),
                calculateDistance(advertisedDevice.getRSSI()));
        scanResults += String(temp);
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
        scanResults = "<tr><th>Geräte-ID</th><th>Name</th><th>Empfangsstärke (RSSI)</th><th>Entfernung (m)</th></tr>";
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
    page += "<style>table {width: 100%; border-collapse: collapse;} th, td {border: 1px solid black; padding: 8px; text-align: left;} th {background-color: #f2f2f2;} #status {margin-top: 20px;}</style>";
    page += "</head><body>";
    page += "<h1>Scan Results</h1><table>" + scanResults + "</table>";
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
        String jsonData = "{\"data\": \"" + scanResults + "\"}";
        lastSentData = jsonData; // Store the last sent data
        int httpResponseCode = http.POST(jsonData);
        lastHttpResponseCode = httpResponseCode;
        lastHttpResult = http.getString();
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        http.end();
    }
}
