#include <WiFi.h>
#include <esp_now.h>

typedef struct struct_message {
  char id;
  // int id;
  // int buffer[300];
} struct_message;

struct Timer {
  unsigned long time{0};
  bool is_running{false};
    
  void start() { time = millis(); is_running = true; }

  void reset() { is_running = false; }

  unsigned long get_elapsed() { return (is_running) ? millis()-time : 0; }
};

struct_message msg;
bool hit[2] = {false, false};
Timer timer_hit;

void on_recv(const uint8_t* mac, const uint8_t* incomingData, int len) 
{
  memcpy(&msg, incomingData, sizeof(msg));
  int id = msg.id - '0';
  if (!hit[id]) {
    hit[id] = true;
    if (!timer_hit.is_running) { timer_hit.start(); }
    Serial.print(msg.id);
  }
  // Serial.println(msg.id);
  // Serial.write((uint8_t*) &msg.buffer, sizeof(msg.buffer));
}

void setup() 
{
  Serial.begin(115200);
  delay(1000);
  
  btStop();
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(esp_now_recv_cb_t(on_recv));
}

void loop() 
{
  while (true) {
    if (timer_hit.get_elapsed() > 2000) {
      hit[0] = hit[1] = false;
    }
    delay(10);
  }
}