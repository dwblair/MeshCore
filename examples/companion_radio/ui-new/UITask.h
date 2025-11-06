#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

struct AdvertPath; // forward declaration

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  NodePrefs* _node_prefs;
  char _alert[80];
  unsigned long _alert_expiry;
  int _msgcount;
  unsigned long ui_started_at, next_batt_chck;
  int next_backlight_btn_check = 0;
#ifdef PIN_STATUS_LED
  int led_state = 0;
  int next_led_change = 0;
  int last_led_increment = 0;
#endif

// ---- CardKB (I2C: 0x5F) ---------------------------------
#ifdef USE_CARDKB
public:
  // If you want to call it from begin(), make it public
  void initCardKB(int sdaPin = -1, int sclPin = -1);

private:
  bool     _cardkb_present = false;
  uint32_t _cardkb_next_poll = 0;
  uint8_t  _cardkb_esc_state = 0; // 0=none,1=ESC,2='['

  char     pollCardKB();               // returns 0 if no key
  char     translateCardKB(uint8_t b); // ASCII + special keys
#endif
// ----------------------------------------------------------


  UIScreen* splash;
  UIScreen* home;
  UIScreen* msg_preview;
  UIScreen* compose; 
  UIScreen* curr;

  void userLedHandler();
  
  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);

  void setCurrScreen(UIScreen* c);

public:

  UITask(mesh::MainBoard* board, BaseSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    curr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void gotoHomeScreen() { setCurrScreen(home); }
  void showAlert(const char* text, int duration_millis);
  int  getMsgCount() const { return _msgcount; }
  bool hasDisplay() const { return _display != NULL; }
  bool isButtonPressed() const;

  void toggleBuzzer();
  void toggleGPS();

  void gotoComposeScreen();
  bool sendText(const char* text); // returns true if sent OK
  bool sendTextToRecipient(const char* text, int recipient_type, int recipient_index, void* compose_screen); // returns true if sent OK
  
  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;
  
  // Additional methods for chat interface
  void newDirectMessage(const char* from_name, const char* text, int msgcount);
  void newChannelMessage(const char* from_name, const char* text, int msgcount);

  void shutdown(bool restart = false);
};
