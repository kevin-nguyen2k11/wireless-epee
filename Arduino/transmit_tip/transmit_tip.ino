#include <WiFi.h>
#include <esp_now.h>
#include <Goertzel.h>
#include "driver/rtc_io.h"
#include "driver/ledc.h"
#include "esp_sleep.h"
// #include "esp32s3/rom/rtc.h"

#define ADC_ATTEN                    (ADC_11db)
#define ADC_PIN                    (GPIO_NUM_9)
#define BUTTON_PIN                 (GPIO_NUM_5)
#define BUTTON_GROUND              (GPIO_NUM_2)
#define PWM_PIN                    (GPIO_NUM_4)
#define TOUCH_PIN                          (T4)
#define BUTTON_PIN_BITMASK        (0x000000020)

#define PWM_FREQ                         (1000) // hz
#define SAMPLE_RATE                     (10000) // hz
#define SAMPLE_PERIOD                     (100) // microseconds. 100 is 10khz
#define NUM_SAMPLES                       (300) // 30ms sample time
#define SIGNAL_THRESHOLD                 (8000)
#define DEBOUNCE_TIME                      (50) // ms
#define TOUCH_THRESHOLD                 (50000)

#define LIGHT_IDLE_TIME                   (100) // ms
#define DEEP_IDLE_TIME               (60000000) // us, 1 min

uint8_t server_address[] = {0xa4, 0xcf, 0x12, 0x6c, 0x1a, 0xbc};
// char box_id = '0';
char box_id = '1';
esp_now_peer_info_t server_info;
esp_now_peer_info_t peer_info;

typedef struct struct_message {
  char id = box_id;
  // int id;
  // int buffer[NUM_SAMPLES];
} struct_message;

struct Timer {
  unsigned long time{0};
  bool is_running{false};
    
  void start() { time = millis(); is_running = true; }

  void reset() { is_running = false; }

  unsigned long get_elapsed() { return (is_running) ? millis()-time : 0; }
};

ledc_timer_config_t pwm_timer = {
  .speed_mode = LEDC_LOW_SPEED_MODE,
  .duty_resolution = LEDC_TIMER_8_BIT,
  .timer_num = LEDC_TIMER_0,
  .freq_hz = PWM_FREQ,
  .clk_cfg = LEDC_USE_RTC8M_CLK
};
ledc_channel_config_t pwm_channel = {
  .gpio_num = PWM_PIN,
  .speed_mode = LEDC_LOW_SPEED_MODE,
  .channel = LEDC_CHANNEL_0,
  .intr_type = LEDC_INTR_DISABLE,
  .timer_sel = LEDC_TIMER_0,
  .duty = 128,
  .hpoint = 0,
  .sleep_mode = LEDC_SLEEP_MODE_KEEP_ALIVE
};

unsigned long last_touch;
Timer timer_press_duration;
int buffer[NUM_SAMPLES];
Goertzel goertzel(PWM_FREQ, SAMPLE_RATE);
hw_timer_t* timer = NULL;
volatile SemaphoreHandle_t timer_semaphore;
struct_message msg;

void IRAM_ATTR on_timer()
{
  xSemaphoreGiveFromISR(timer_semaphore, NULL);
}

void set_button()
{
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUTTON_GROUND, OUTPUT);
  gpio_set_drive_capability(BUTTON_GROUND, GPIO_DRIVE_CAP_0);
  digitalWrite(BUTTON_GROUND, LOW);
}

void setup_espnow()
{
  if (esp_now_init() != ESP_OK) {
    // Serial.println("Error initializing ESP-NOW");
    return;
  }
  memcpy(server_info.peer_addr, server_address, 6);
  server_info.channel = 0;  
  server_info.encrypt = false;
  if (esp_now_add_peer(&server_info) != ESP_OK){
    // Serial.println("Failed to add server");
    return;
  }
}

void blink(int num_blinks)
{
  pinMode(LED_BUILTIN, OUTPUT);
  for (int i{0}; i<2*num_blinks; ++i) {
    if (i%2) {
      digitalWrite(LED_BUILTIN, LOW);
    }
    else {
      digitalWrite(LED_BUILTIN, HIGH);
    }
    delay(200);
  }
}

void light_sleep()
{
  // Serial.println("Entering light sleep");
  // rtc_gpio_init(BUTTON_PIN);
  // rtc_gpio_init(BUTTON_GROUND);
  gpio_hold_en(BUTTON_PIN);
  gpio_hold_en(BUTTON_GROUND);
  WiFi.mode(WIFI_OFF);
  esp_now_deinit();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK, ESP_EXT1_WAKEUP_ALL_LOW);
  esp_sleep_enable_timer_wakeup(DEEP_IDLE_TIME);
  Serial.flush();
  esp_light_sleep_start();
  Serial.begin(115200);

  // switch (esp_sleep_get_wakeup_cause())
  // {
  // case ESP_SLEEP_WAKEUP_UART:
  //     blink(1);
  //     break;
  // case ESP_SLEEP_WAKEUP_WIFI:
  //     blink(2);
  //     break;
  // case ESP_SLEEP_WAKEUP_COCPU:
  //     blink(3);
  //     break;
  // case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
  //     blink(4);
  //     break;
  // case ESP_SLEEP_WAKEUP_BT:
  //     blink(5);
  //     break;
  // case ESP_SLEEP_WAKEUP_VAD:
  //     blink(6);
  //     break;
  // case ESP_SLEEP_WAKEUP_UNDEFINED:
  //     blink(7);
  // case ESP_SLEEP_WAKEUP_EXT0:
  //     blink(8);
  //     break;
  // case ESP_SLEEP_WAKEUP_EXT1:
  //     blink(9);
  //     break;
  // case ESP_SLEEP_WAKEUP_TIMER:
  //     blink(10);
  //     break;
  // case ESP_SLEEP_WAKEUP_TOUCHPAD:
  //     blink(11);
  //     break;
  // case ESP_SLEEP_WAKEUP_ULP:
  //     blink(12);
  //     break;
  // case ESP_SLEEP_WAKEUP_GPIO:
  //     blink(13);
  //     break;
  // default:
  //     blink(14);
  //     break; // printf(""); //
  // }

  // blink(rtc_get_reset_reason(0));
  // delay(2000);
  while (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    if (touchRead(TOUCH_PIN) < TOUCH_THRESHOLD) { deep_sleep(); }
    else { esp_light_sleep_start(); }
  }
  gpio_hold_dis(BUTTON_PIN);
  gpio_hold_dis(BUTTON_GROUND);
  // rtc_gpio_deinit(BUTTON_PIN);
  // rtc_gpio_deinit(BUTTON_GROUND);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  setup_espnow();
  set_button();
  last_touch = millis();
}

void deep_sleep()
{
  // check if touch pin would just wake up right away
  // turn off pwm and deregister pwm channels
  // Serial.println("Entering deep sleep"); // disable wifi and other modules?
  // gpio_sleep_sel_en(LED_PIN);
  digitalWrite(LED_BUILTIN, HIGH);
  gpio_sleep_sel_en(PWM_PIN);
  gpio_hold_dis(BUTTON_PIN);
  gpio_hold_dis(BUTTON_GROUND);
  // ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
  // ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
  // ledc_timer_config_t stop_timer = {
  //   .speed_mode = LEDC_LOW_SPEED_MODE,
  //   .timer_num = LEDC_TIMER_0,
  //   .deconfigure = true
  // };
  // ledc_timer_config(&stop_timer);
  gpio_pullup_dis(BUTTON_PIN);
  pinMode(BUTTON_GROUND, INPUT);
  rtc_gpio_isolate(GPIO_NUM_5);
  rtc_gpio_isolate(GPIO_NUM_6);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  touchSleepWakeUpEnable(TOUCH_PIN, TOUCH_THRESHOLD);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);
  // client.stop();
  esp_deep_sleep_start();
}

void setup() 
{
  Serial.begin(115200);
  delay(1000);
  
  // if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TOUCHPAD) {
  //   pinMode(LED_BUILTIN, OUTPUT);
  //   for (int i{0}; i<100; ++i) {
  //     if (i%2) {
  //       digitalWrite(LED_BUILTIN, LOW);
  //     }
  //     else {
  //       digitalWrite(LED_BUILTIN, HIGH);
  //     }
  //     delay(1000);
  //   }
  // }

  // ledcAttachChannel(LED_PIN, 500, 8, 1);
  // ledcWrite(LED_PIN, 25);
  // gpio_sleep_sel_dis(LED_PIN);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_ON);
  gpio_sleep_sel_dis(PWM_PIN);
  ledc_timer_config(&pwm_timer);
  ledc_channel_config(&pwm_channel);

  btStop();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  // WiFi.onEvent(connected_to_ap, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  // WiFi.onEvent(got_ip_from_ap, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  // WiFi.onEvent(disconnected_from_ap, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  // WiFi.begin(wifi_address, wifi_password);
  // Serial.println("\nConnecting");
  setup_espnow();

  timer_semaphore = xSemaphoreCreateBinary();
  timer = timerBegin(1000000);
  timerStop(timer);
  timerAttachInterrupt(timer, &on_timer);
  timerAlarm(timer, SAMPLE_PERIOD, true, 0);
}

void loop() 
{  
  analogSetPinAttenuation(ADC_PIN, ADC_ATTEN);
  set_button();
  last_touch = millis();
  bool hold = false;
  while(true) {
    if (digitalRead(BUTTON_PIN) == 0) {
      if (millis()-last_touch > DEBOUNCE_TIME) {
        if (!timer_press_duration.is_running) { timer_press_duration.start(); }
        if (timer_press_duration.get_elapsed()>=2 && !hold) {
          // Serial.println("touch");
          // esp_now_send(broadcast_address, (uint8_t *) &msg, sizeof(msg));
          gpio_pullup_dis(BUTTON_PIN);
          pinMode(BUTTON_GROUND, INPUT);
          // delay(50);
          timerRestart(timer);
          timerStart(timer);
          ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
          int i = 0;
          while (i != NUM_SAMPLES) {
            if (xSemaphoreTake(timer_semaphore, 0) == pdTRUE) {
              buffer[i] = analogRead(ADC_PIN);
              i++;
            }
          }
          ledc_channel_config(&pwm_channel);
          timerStop(timer);
          set_button();
          // if (goertzel.Mag(buffer, NUM_SAMPLES) < SIGNAL_THRESHOLD) {
            // client.print("1");
          // float mag = goertzel.Mag(buffer, NUM_SAMPLES);
          // float whole, fractional;
          // fractional = std::modf(mag, &whole);
          // int integralWhole = static_cast<int>(whole);
          // buffer[0] = integralWhole;
          // // msg.id = integralWhole;
          // for (int j=0; j<NUM_SAMPLES; j++) {
          //   msg.buffer[j] = buffer[j];
          // }
          esp_now_send(server_address, (uint8_t *) &msg, sizeof(msg));
          // msg.id = 0;
          // }
          last_touch = millis();
          hold = true;
        }
      }
    }
    else { timer_press_duration.reset(); hold = false; }
    if (timer_press_duration.get_elapsed() > 5000) { 
      // if (touchRead(TOUCH_PIN) > TOUCH_THRESHOLD) { blink(200); ESP.restart(); }
      // else { delay(60000); }
      blink(2); ESP.restart();
    }
    if (millis()-last_touch>LIGHT_IDLE_TIME && !hold) { light_sleep(); }
  }
}