#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>

#define HAS_ROTARY false

const char *mqtt_server = "192.168.---.---";

const char *rotary_topic = "esp32/rotary/state";
const char *button_topic = "esp32/rotary_button";

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
char msg[50];
int value = 0;

#if HAS_ROTARY
bool rotary_has_update = false;

const int outputA = 34;
const int outputB = 35;

int counter = 0;
int aLastState;
#endif

void setup_wifi() {
    delay(10);

    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFiClass::status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void callback(char *topic, byte *message, unsigned int length) {
    Serial.print("Message arrived on topic: ");
    Serial.print(topic);
    Serial.print(". Message: ");
    String messageTemp;

    for (int i = 0; i < length; i++) {
        Serial.print((char) message[i]);
        messageTemp += (char) message[i];
    }
    Serial.println();
}

void registerEntities() {

#if HAS_ROTARY
    char rotary_payload[100];
    snprintf(rotary_payload, sizeof(rotary_payload),
             R"({"name":"ESP32 Rotary","state_topic":"%s", "unique_id": "esp32_rotary"})", rotary_topic);

    client.publish(
            "homeassistant/sensor/esp32_rotary/config",
            rotary_payload
    );
#endif

    for (int i = 1; i <= 9; i++) {
        delay(100);
        char button_payload[200];
        snprintf(button_payload, sizeof(button_payload),
                 R"({"name":"ESP32 Button %d","state_topic":"%s_%d/state","unique_id":"esp32_button_%d"})", i,
                 button_topic, i, i);

        char button_config_topic[100];
        snprintf(button_config_topic, sizeof(button_config_topic),
                 "homeassistant/binary_sensor/esp32_rotary_click_%d/config", i);

        client.publish(
                button_config_topic,
                button_payload
        );
    }
}

void reconnect() {

    const char *mqtt_username = "homeassistant";
    const char *mqtt_password = "<MQTT_BROKER_PASSWORD>";

    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect("core-mosquitto", mqtt_username, mqtt_password)) {
            Serial.println("connected");
            registerEntities();
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

byte rows[] = {18, 19, 21};
const int rowCount = sizeof(rows) / sizeof(rows[0]);

byte cols[] = {32, 33, 25};
const int colCount = sizeof(cols) / sizeof(cols[0]);

byte keys[colCount][rowCount];
byte lastKeys[colCount][rowCount];
bool hasPendingPublish[colCount][rowCount];

void setup_button_matrix() {
    for (unsigned char row : rows) {
        pinMode(row, INPUT);
    }

    for (unsigned char col : cols) {
        pinMode(col, INPUT_PULLUP);
    }

    for (int rowIndex = 0; rowIndex < rowCount; rowIndex++) {
        for (auto & colIndex : hasPendingPublish)
            colIndex[rowIndex] = false;

    }
}

void readMatrix() {
    for (int colIndex = 0; colIndex < colCount; colIndex++) {
        byte curCol = cols[colIndex];
        pinMode(curCol, OUTPUT);
        digitalWrite(curCol, LOW);

        for (int rowIndex = 0; rowIndex < rowCount; rowIndex++) {
            byte rowCol = rows[rowIndex];
            pinMode(rowCol, INPUT_PULLUP);
            keys[colIndex][rowIndex] = digitalRead(rowCol);
            pinMode(rowCol, INPUT);
        }
        pinMode(curCol, INPUT);
    }
}


void setup() {
    setup_button_matrix();
#if HAS_ROTARY
    pinMode(outputA, INPUT);
    pinMode(outputB, INPUT);

    aLastState = digitalRead(outputA);
#endif
    Serial.begin(115200);

    setup_wifi();
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
}

void publishButtonUpdate(int buttonNum) {
    char button_state_topic[100];
    snprintf(button_state_topic, sizeof(button_state_topic), "%s_%d/state", button_topic, buttonNum);

    client.publish(button_state_topic, "ON");
    client.publish(button_state_topic, "OFF");
}

#if HAS_ROTARY
void publishRotaryUpdate() {
    char buffer[3];
    itoa(counter, buffer, 10);
    client.publish(rotary_topic, buffer);
    rotary_has_update = false;
}

void checkEncoderState() {
    int aState = digitalRead(outputA);
    if (aState != aLastState) {
        if (digitalRead(outputB) != aState) {
            if (counter < 100) {
                counter++;
                rotary_has_update = true;
            }
        } else {
            if (counter > 1) {
                counter--;
                rotary_has_update = true;
            }
        }
    }
    aLastState = aState;
}

#endif

void printMatrix() {
    for (int rowIndex = 0; rowIndex < rowCount; rowIndex++) {
        for (int colIndex = 0; colIndex < colCount; colIndex++) {
            if (keys[colIndex][rowIndex] != lastKeys[colIndex][rowIndex]) {
                if (!keys[colIndex][rowIndex]) {
                    hasPendingPublish[colIndex][rowIndex] = true;
                } else {
                    // On Release
                }
            }
            lastKeys[colIndex][rowIndex] = keys[colIndex][rowIndex];
        }
    }
}

void loop() {
    if (!client.connected()) {
        reconnect();
    }
    client.loop();

    readMatrix();
    printMatrix();
#if HAS_ROTARY
    checkEncoderState();
#endif

    const unsigned long DELAY_TIME = 1000;

    unsigned long now = millis();
    if (now - lastMsg > DELAY_TIME) {
        lastMsg = now;
#if HAS_ROTARY
        if (rotary_has_update)
            publishRotaryUpdate();
#endif
        for (int rowIndex = 0; rowIndex < rowCount; rowIndex++) {
            for (int colIndex = 0; colIndex < colCount; colIndex++) {
                if (hasPendingPublish[colIndex][rowIndex]) {
                    publishButtonUpdate(3 * rowIndex + colIndex + 1);
                    hasPendingPublish[colIndex][rowIndex] = false;
                }
            }
        }
    }
}