#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include <helpers/BaseChatMesh.h>
#include "../MyMesh.h"
#include "target.h"

#ifdef USE_CARDKB
  #include <Wire.h>

// Select I2C bus for CardKB
#ifndef CARDKB_WIRE
  #ifdef USE_WIRE1_FOR_CARDKB
    #define CARDKB_WIRE Wire1
  #else
    #define CARDKB_WIRE Wire
  #endif
#endif

  static constexpr uint8_t CARDKB_ADDR = 0x5F;
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   1200

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif


#define PRESS_LABEL "long press"

#include "icons.h"

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[12];

public:
  SplashScreen(UITask* task) : _task(task) {
    // strip off dash and commit hash by changing dash to null terminator
    // e.g: v1.2.3-abcdef -> v1.2.3
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    int len = dash ? dash - ver : strlen(ver);
    if (len >= sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    // meshcore logo
    display.setColor(DisplayDriver::BLUE);
    int logoWidth = 128;
    display.drawXbm((display.width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // version info
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(2);
    display.drawTextCentered(display.width()/2, 22, _version_info);

    display.setTextSize(1);
    display.drawTextCentered(display.width()/2, 42, FIRMWARE_BUILD_DATE);

    return 1000;
  }

  void poll() override {
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
  }
};

class HomeScreen : public UIScreen {
  enum HomePage {
    FIRST,
    RECENT,
    RADIO,
    BLUETOOTH,
    ADVERT,
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
    SHUTDOWN,
    Count    // keep as last
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  bool _shutdown_init;
  AdvertPath recent[UI_RECENT_LIST_SIZE];


  void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
    // Convert millivolts to percentage
    const int minMilliVolts = 3000; // Minimum voltage (e.g., 3.0V)
    const int maxMilliVolts = 4200; // Maximum voltage (e.g., 4.2V)
    int batteryPercentage = ((batteryMilliVolts - minMilliVolts) * 100) / (maxMilliVolts - minMilliVolts);
    if (batteryPercentage < 0) batteryPercentage = 0; // Clamp to 0%
    if (batteryPercentage > 100) batteryPercentage = 100; // Clamp to 100%

    // battery icon
    int iconWidth = 24;
    int iconHeight = 10;
    int iconX = display.width() - iconWidth - 5; // Position the icon near the top-right corner
    int iconY = 0;
    display.setColor(DisplayDriver::GREEN);

    // battery outline
    display.drawRect(iconX, iconY, iconWidth, iconHeight);

    // battery "cap"
    display.fillRect(iconX + iconWidth, iconY + (iconHeight / 4), 3, iconHeight / 2);

    // fill the battery based on the percentage
    int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
    display.fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  bool sensors_scroll = false;
  int sensors_scroll_offset = 0;
  int next_sensors_refresh = 0;

  void refresh_sensors() {
    if (millis() > next_sensors_refresh) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      sensors.querySensors(0xFF, sensors_lpp);
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
      sensors_scroll = sensors_nb > UI_RECENT_LIST_SIZE;
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0), 
       _shutdown_init(false), sensors_lpp(200) {  }

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
    char tmp[80];
    // node name
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
    display.setCursor(0, 0);
    display.print(filtered_name);

    // battery voltage
    renderBatteryIndicator(display, _task->getBattMilliVolts());

    // curr page indicator
    int y = 14;
    int x = display.width() / 2 - 25;
    for (uint8_t i = 0; i < HomePage::Count; i++, x += 10) {
      if (i == _page) {
        display.fillRect(x-1, y-1, 3, 3);
      } else {
        display.fillRect(x, y, 1, 1);
      }
    }

    if (_page == HomePage::FIRST) {
      display.setColor(DisplayDriver::YELLOW);
      display.setTextSize(2);
      sprintf(tmp, "MSG: %d", _task->getMsgCount());
      display.drawTextCentered(display.width() / 2, 20, tmp);

      if (_task->hasConnection()) {
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 43, "< Connected >");
      } else if (the_mesh.getBLEPin() != 0) { // BT pin
        display.setColor(DisplayDriver::RED);
        display.setTextSize(2);
        sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, 43, tmp);
      }
    } else if (_page == HomePage::RECENT) {
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      display.setColor(DisplayDriver::GREEN);
      int y = 20;
      for (int i = 0; i < UI_RECENT_LIST_SIZE; i++, y += 11) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        int secs = _rtc->getCurrentTime() - a->recv_timestamp;
        if (secs < 60) {
          sprintf(tmp, "%ds", secs);
        } else if (secs < 60*60) {
          sprintf(tmp, "%dm", secs / 60);
        } else {
          sprintf(tmp, "%dh", secs / (60*60));
        }
        
        int timestamp_width = display.getTextWidth(tmp);
        int max_name_width = display.width() - timestamp_width - 1;
        
        char filtered_recent_name[sizeof(a->name)];
        display.translateUTF8ToBlocks(filtered_recent_name, a->name, sizeof(filtered_recent_name));
        display.drawTextEllipsized(0, y, max_name_width, filtered_recent_name);
        display.setCursor(display.width() - timestamp_width - 1, y);
        display.print(tmp);
      }
    } else if (_page == HomePage::RADIO) {
      display.setColor(DisplayDriver::YELLOW);
      display.setTextSize(1);
      // freq / sf
      display.setCursor(0, 20);
      sprintf(tmp, "FQ: %06.3f   SF: %d", _node_prefs->freq, _node_prefs->sf);
      display.print(tmp);

      display.setCursor(0, 31);
      sprintf(tmp, "BW: %03.2f     CR: %d", _node_prefs->bw, _node_prefs->cr);
      display.print(tmp);

      // tx power,  noise floor
      display.setCursor(0, 42);
      sprintf(tmp, "TX: %ddBm", _node_prefs->tx_power_dbm);
      display.print(tmp);
      display.setCursor(0, 53);
      sprintf(tmp, "Noise floor: %d", radio_driver.getNoiseFloor());
      display.print(tmp);
    } else if (_page == HomePage::BLUETOOTH) {
      display.setColor(DisplayDriver::GREEN);
      display.drawXbm((display.width() - 32) / 2, 18,
          _task->isSerialEnabled() ? bluetooth_on : bluetooth_off,
          32, 32);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 64 - 11, "toggle: " PRESS_LABEL);
    } else if (_page == HomePage::ADVERT) {
      display.setColor(DisplayDriver::GREEN);
      display.drawXbm((display.width() - 32) / 2, 18, advert_icon, 32, 32);
      display.drawTextCentered(display.width() / 2, 64 - 11, "advert: " PRESS_LABEL);
#if UI_SENSORS_PAGE == 1
    } else if (_page == HomePage::SENSORS) {
      int y = 18;
      refresh_sensors();
      char buf[30];
      char name[30];
      LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());

      for (int i = 0; i < sensors_scroll_offset; i++) {
        uint8_t channel, type;
        r.readHeader(channel, type);
        r.skipData(type);
      }

      for (int i = 0; i < (sensors_scroll?UI_RECENT_LIST_SIZE:sensors_nb); i++) {
        uint8_t channel, type;
        if (!r.readHeader(channel, type)) { // reached end, reset
          r.reset();
          r.readHeader(channel, type);
        }

        display.setCursor(0, y);
        float v;
        switch (type) {
          case LPP_GPS: // GPS
            float lat, lon, alt;
            r.readGPS(lat, lon, alt);
            strcpy(name, "gps"); sprintf(buf, "%.4f %.4f", lat, lon);
            break;
          case LPP_VOLTAGE:
            r.readVoltage(v);
            strcpy(name, "voltage"); sprintf(buf, "%6.2f", v);
            break;
          case LPP_CURRENT:
            r.readCurrent(v);
            strcpy(name, "current"); sprintf(buf, "%.3f", v);
            break;
          case LPP_TEMPERATURE:
            r.readTemperature(v);
            strcpy(name, "temperature"); sprintf(buf, "%.2f", v);
            break;
          case LPP_RELATIVE_HUMIDITY:
            r.readRelativeHumidity(v);
            strcpy(name, "humidity"); sprintf(buf, "%.2f", v);
            break;
          case LPP_BAROMETRIC_PRESSURE:
            r.readPressure(v);
            strcpy(name, "pressure"); sprintf(buf, "%.2f", v);
            break;
          case LPP_ALTITUDE:
            r.readAltitude(v);
            strcpy(name, "altitude"); sprintf(buf, "%.0f", v);
            break;
          case LPP_POWER:
            r.readPower(v);
            strcpy(name, "power"); sprintf(buf, "%6.2f", v);
            break;
          default:
            r.skipData(type);
            strcpy(name, "unk"); sprintf(buf, "");
        }
        display.setCursor(0, y);
        display.print(name);
        display.setCursor(
          display.width()-display.getTextWidth(buf)-1, y
        );
        display.print(buf);
        y = y + 12;
      }
      if (sensors_scroll) sensors_scroll_offset = (sensors_scroll_offset+1)%sensors_nb;
      else sensors_scroll_offset = 0;
#endif
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      if (_shutdown_init) {
        display.drawTextCentered(display.width() / 2, 34, "hibernating...");
      } else {
        display.drawXbm((display.width() - 32) / 2, 18, power_icon, 32, 32);
        display.drawTextCentered(display.width() / 2, 64 - 11, "hibernate: " PRESS_LABEL);
      }
    }
    return 5000;   // next render after 5000 ms
  }

  bool handleInput(char c) override {
  
     
     if (c == KEY_ENTER && _page == HomePage::FIRST) {
      _task->gotoComposeScreen();
      return true;
    }
    
    if (c == KEY_LEFT || c == KEY_PREV) {
      _page = (_page + HomePage::Count - 1) % HomePage::Count;
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _page = (_page + 1) % HomePage::Count;
      if (_page == HomePage::RECENT) {
        _task->showAlert("Recent adverts", 800);
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      if (_task->isSerialEnabled()) {  // toggle Bluetooth on/off
        _task->disableSerial();
      } else {
        _task->enableSerial();
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::ADVERT) {
      _task->notify(UIEventType::ack);
      if (the_mesh.advert()) {
        _task->showAlert("Advert sent!", 1000);
      } else {
        _task->showAlert("Advert failed..", 1000);
      }
      return true;
    }
#if UI_SENSORS_PAGE == 1
    if (c == KEY_ENTER && _page == HomePage::SENSORS) {
      _task->toggleGPS();
      next_sensors_refresh=0;
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::SHUTDOWN) {
      _shutdown_init = true;  // need to wait for button to be released
      return true;
    }
    
    /*
    if (c >= 32 && c <= 126) {
  _task->showAlert("KEY", 200); // visual ping on any printable char
  return true;
} 
*/

    return false;
  }
};

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;

  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[78];
  };
  #define MAX_UNREAD_MSGS   32
  int num_unread;
  MsgEntry unread[MAX_UNREAD_MSGS];

public:
  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) { num_unread = 0; }

  void addPreview(uint8_t path_len, const char* from_name, const char* msg) {
    if (num_unread >= MAX_UNREAD_MSGS) return;  // full

    auto p = &unread[num_unread++];
    p->timestamp = _rtc->getCurrentTime();
    if (path_len == 0xFF) {
      sprintf(p->origin, "(D) %s:", from_name);
    } else {
      sprintf(p->origin, "(%d) %s:", (uint32_t) path_len, from_name);
    }
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
  }

  int render(DisplayDriver& display) override {
    char tmp[16];
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    sprintf(tmp, "Unread: %d", num_unread);
    display.print(tmp);

    auto p = &unread[0];

    int secs = _rtc->getCurrentTime() - p->timestamp;
    if (secs < 60) {
      sprintf(tmp, "%ds", secs);
    } else if (secs < 60*60) {
      sprintf(tmp, "%dm", secs / 60);
    } else {
      sprintf(tmp, "%dh", secs / (60*60));
    }
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);  // horiz line

    display.setCursor(0, 14);
    display.setColor(DisplayDriver::YELLOW);
    char filtered_origin[sizeof(p->origin)];
    display.translateUTF8ToBlocks(filtered_origin, p->origin, sizeof(filtered_origin));
    display.print(filtered_origin);

    display.setCursor(0, 25);
    display.setColor(DisplayDriver::LIGHT);
    char filtered_msg[sizeof(p->msg)];
    display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
    display.printWordWrap(filtered_msg, display.width());

#if AUTO_OFF_MILLIS==0 // probably e-ink
    return 10000; // 10 s
#else
    return 1000;  // next render after 1000 ms
#endif
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      num_unread--;
      if (num_unread == 0) {
        _task->gotoHomeScreen();
      } else {
        // delete first/curr item from unread queue
        for (int i = 0; i < num_unread; i++) {
          unread[i] = unread[i + 1];
        }
      }
      return true;
    }
    if (c == KEY_ENTER) {
      num_unread = 0;  // clear unread queue
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

// ----------------- Compose Screen (drop-in) -----------------
class ComposeScreen : public UIScreen {
  UITask* _task;
  static constexpr int MAX_LEN = 240;
  char _buf[MAX_LEN + 1];
  int  _len = 0;
  int  _cursor = 0;
  unsigned long _sent_popup_until = 0;
  bool _dirty = true;
  
  // Recipient selection
  enum RecipientType {
    RECIPIENT_CONTACT,
    RECIPIENT_CHANNEL,
    RECIPIENT_BROADCAST
  };
  RecipientType _recipient_type = RECIPIENT_CHANNEL;
  int _recipient_index = 0;
  
  // Recent message senders (separate from just heard adverts)
  struct RecentSender {
    char name[32];
    uint8_t pubkey_prefix[7];
    uint32_t last_msg_time;
  };

  void insertChar(char ch) {
    if (_len >= MAX_LEN) return;
    // shift right from cursor
    for (int i = _len; i >= _cursor; --i) _buf[i + 1] = _buf[i];
    _buf[_cursor] = ch;
    _len++; _cursor++;
    _dirty = true;
  }

  void backspace() {
    if (_cursor <= 0) return;
    // shift left over cursor-1
    for (int i = _cursor - 1; i < _len; ++i) _buf[i] = _buf[i + 1];
    _cursor--; _len--;
    _dirty = true;
  }

  void moveLeft()  { if (_cursor > 0)    { _cursor--; _dirty = true; } }
  void moveRight() { if (_cursor < _len) { _cursor++; _dirty = true; } }

  void updateRecipientList() {
    // Get recently heard adverts
    _num_recent_contacts = the_mesh.getRecentlyHeard(_recent_contacts, UI_RECENT_LIST_SIZE);
    
    // Filter out any entries with all-zero pubkeys (invalid/empty entries)
    int valid_contacts = 0;
    for (int i = 0; i < _num_recent_contacts; i++) {
      bool is_valid = false;
      for (int j = 0; j < 7; j++) {
        if (_recent_contacts[i].pubkey_prefix[j] != 0) {
          is_valid = true;
          break;
        }
      }
      if (is_valid) {
        if (valid_contacts != i) {
          _recent_contacts[valid_contacts] = _recent_contacts[i];
        }
        valid_contacts++;
      }
    }
    _num_recent_contacts = valid_contacts;
    
    // Also add established mesh contacts (not just heard adverts)
    ContactInfo contact;
    int space_left = UI_RECENT_LIST_SIZE - _num_recent_contacts;
    
    for (int i = 0; i < the_mesh.getNumContacts() && space_left > 0; i++) {
      if (the_mesh.getContactByIdx(i, contact)) {
        // Check if this contact has a valid pubkey
        bool has_valid_pubkey = false;
        for (int k = 0; k < 7; k++) {
          if (contact.id.pub_key[k] != 0) {
            has_valid_pubkey = true;
            break;
          }
        }
        
        if (!has_valid_pubkey) continue; // Skip contacts with invalid pubkeys
        
        // Check if this contact is already in recent_contacts
        bool already_added = false;
        for (int j = 0; j < _num_recent_contacts; j++) {
          if (memcmp(_recent_contacts[j].pubkey_prefix, contact.id.pub_key, 7) == 0) {
            already_added = true;
            break;
          }
        }
        
        if (!already_added) {
          // Add this contact to the list
          AdvertPath* new_contact = &_recent_contacts[_num_recent_contacts];
          memcpy(new_contact->pubkey_prefix, contact.id.pub_key, 7);
          new_contact->path_len = contact.out_path_len > 0 ? contact.out_path_len : 0;
          if (contact.out_path_len > 0 && contact.out_path_len <= MAX_PATH_SIZE) {
            memcpy(new_contact->path, contact.out_path, contact.out_path_len);
          }
          new_contact->recv_timestamp = rtc_clock.getCurrentTime(); // Current time
          
          // Use contact name if available, otherwise it will show as hex ID
          if (contact.name[0]) {
            strncpy(new_contact->name, contact.name, sizeof(new_contact->name) - 1);
            new_contact->name[sizeof(new_contact->name) - 1] = 0;
          } else {
            new_contact->name[0] = 0; // Will show as hex ID
          }
          
          _num_recent_contacts++;
          space_left--;
        }
      }
    }
    
    // Ensure we have at least the default public channel
    bool found_public = false;
    for (int i = 0; i < 8; i++) { // Check first 8 channels
      ChannelDetails channel;
      if (the_mesh.getChannel(i, channel)) {
        if (strcmp(channel.name, "Public") == 0) {
          found_public = true;
          break;
        }
      } else {
        break; // No more channels
      }
    }
    
    if (!found_public) {
      // Add the default public channel
      the_mesh.addChannel("Public", "izOH6cXN6mrJ5e26oRXNcg==");
    }
    
    // Set default recipient to Public channel (channel 0)
    _recipient_type = RECIPIENT_CHANNEL;
    _recipient_index = 0;
    
    _dirty = true;
  }

  void nextRecipient() {
    if (_recipient_type == RECIPIENT_CONTACT) {
      _recipient_index++;
      // First check recent senders, then recent contacts
      int total_contacts = _num_recent_senders + _num_recent_contacts;
      if (_recipient_index >= total_contacts || total_contacts == 0) {
        _recipient_type = RECIPIENT_CHANNEL;
        _recipient_index = 0;
      }
    } else if (_recipient_type == RECIPIENT_CHANNEL) {
      _recipient_index++;
      ChannelDetails channel;
      if (!the_mesh.getChannel(_recipient_index, channel)) {
        _recipient_type = RECIPIENT_BROADCAST;
        _recipient_index = 0;
      }
    } else { // RECIPIENT_BROADCAST
      _recipient_type = RECIPIENT_CONTACT;
      _recipient_index = 0;
      // If no contacts, skip directly to channels
      if (_num_recent_senders == 0 && _num_recent_contacts == 0) {
        _recipient_type = RECIPIENT_CHANNEL;
      }
    }
    _dirty = true;
  }

  void prevRecipient() {
    if (_recipient_type == RECIPIENT_CONTACT) {
      if (_recipient_index > 0) {
        _recipient_index--;
      } else {
        // Go to broadcast
        _recipient_type = RECIPIENT_BROADCAST;
        _recipient_index = 0;
      }
    } else if (_recipient_type == RECIPIENT_CHANNEL) {
      if (_recipient_index > 0) {
        _recipient_index--;
      } else {
        // Go to last contact or broadcast if no contacts
        int total_contacts = _num_recent_senders + _num_recent_contacts;
        if (total_contacts > 0) {
          _recipient_type = RECIPIENT_CONTACT;
          _recipient_index = total_contacts - 1;
        } else {
          _recipient_type = RECIPIENT_BROADCAST;
          _recipient_index = 0;
        }
      }
    } else { // RECIPIENT_BROADCAST
      // Go to last channel
      _recipient_type = RECIPIENT_CHANNEL;
      _recipient_index = 0;
      // Find the last valid channel
      ChannelDetails channel;
      while (the_mesh.getChannel(_recipient_index + 1, channel)) {
        _recipient_index++;
      }
    }
    _dirty = true;
  }

  void formatContactId(char* dest, int max_len, const uint8_t* pubkey_prefix) {
    // Check if the pubkey is all zeros
    bool all_zeros = true;
    for (int i = 0; i < 6; i++) {
      if (pubkey_prefix[i] != 0) {
        all_zeros = false;
        break;
      }
    }
    
    if (all_zeros) {
      strcpy(dest, "NoKey");
    } else {
      // Show first 3 bytes (6 hex chars) of the public key
      snprintf(dest, max_len, "%02X%02X%02X", 
               pubkey_prefix[0], pubkey_prefix[1], pubkey_prefix[2]);
    }
  }

  void getCurrentRecipientName(char* dest, int max_len) {
    if (_recipient_type == RECIPIENT_CONTACT) {
      // First check recent senders, then recent contacts
      if (_recipient_index < _num_recent_senders) {
        if (_recent_senders[_recipient_index].name[0] && 
            strcmp(_recent_senders[_recipient_index].name, "Unknown") != 0) {
          snprintf(dest, max_len, "%s*", _recent_senders[_recipient_index].name);
        } else {
          char id_str[8];
          formatContactId(id_str, sizeof(id_str), _recent_senders[_recipient_index].pubkey_prefix);
          snprintf(dest, max_len, "%s*", id_str);
        }
      } else {
        int contact_idx = _recipient_index - _num_recent_senders;
        if (contact_idx < _num_recent_contacts) {
          if (_recent_contacts[contact_idx].name[0] && 
              strcmp(_recent_contacts[contact_idx].name, "Unknown") != 0) {
            snprintf(dest, max_len, "%s", _recent_contacts[contact_idx].name);
          } else {
            char id_str[8];
            formatContactId(id_str, sizeof(id_str), _recent_contacts[contact_idx].pubkey_prefix);
            snprintf(dest, max_len, "%s", id_str);
          }
        } else {
          strcpy(dest, "Unknown");
        }
      }
    } else if (_recipient_type == RECIPIENT_CHANNEL) {
      ChannelDetails channel;
      if (the_mesh.getChannel(_recipient_index, channel)) {
        if (channel.name[0]) {
          snprintf(dest, max_len, "#%s", channel.name);
        } else if (_recipient_index == 0) {
          // Channel 0 is typically the default public channel
          strcpy(dest, "#Public");
        } else {
          snprintf(dest, max_len, "#Ch%d", _recipient_index);
        }
      } else {
        snprintf(dest, max_len, "#NoChannel(%d)", _recipient_index);
      }
    } else {
      strcpy(dest, "Broadcast");
    }
  }

public:
  ComposeScreen(UITask* task) : _task(task) { 
    _buf[0] = '\0'; 
    updateRecipientList();
  }
  
  // Public access for UITask
  int _num_recent_senders = 0;
  RecentSender _recent_senders[UI_RECENT_LIST_SIZE];
  int _num_recent_contacts = 0;
  AdvertPath _recent_contacts[UI_RECENT_LIST_SIZE];
  
  void addRecentSender(const char* from_name) {
    // Look for existing sender
    for (int i = 0; i < _num_recent_senders; i++) {
      if (strcmp(_recent_senders[i].name, from_name) == 0) {
        // Update timestamp and move to front
        _recent_senders[i].last_msg_time = rtc_clock.getCurrentTime();
        RecentSender temp = _recent_senders[i];
        for (int j = i; j > 0; j--) {
          _recent_senders[j] = _recent_senders[j-1];
        }
        _recent_senders[0] = temp;
        return;
      }
    }
    
    // Add new sender at front
    if (_num_recent_senders < UI_RECENT_LIST_SIZE) {
      _num_recent_senders++;
    }
    
    // Shift existing entries down
    for (int i = _num_recent_senders - 1; i > 0; i--) {
      _recent_senders[i] = _recent_senders[i-1];
    }
    
    // Add new entry at front
    strncpy(_recent_senders[0].name, from_name, sizeof(_recent_senders[0].name) - 1);
    _recent_senders[0].name[sizeof(_recent_senders[0].name) - 1] = 0;
    _recent_senders[0].last_msg_time = rtc_clock.getCurrentTime();
    
    // Try to get pubkey from contacts
    ContactInfo* contact = the_mesh.searchContactsByPrefix(from_name);
    if (contact) {
      memcpy(_recent_senders[0].pubkey_prefix, contact->id.pub_key, 7);
    } else {
      memset(_recent_senders[0].pubkey_prefix, 0, 7);
    }
  }

  int render(DisplayDriver& display) override {
    display.setColor(DisplayDriver::YELLOW);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("To: ");
    
    char recipient_name[32];
    getCurrentRecipientName(recipient_name, sizeof(recipient_name));
    display.setColor(DisplayDriver::LIGHT);
    display.print(recipient_name);

    // Counter on same line
    char cnt[16];
    sprintf(cnt, " %d/%d", _len, MAX_LEN);
    display.setCursor(display.width() - display.getTextWidth(cnt) - 1, 0);
    display.print(cnt);

    // Divider
    display.drawRect(0, 10, display.width(), 1);

    // Text area
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, 14);
    char show[MAX_LEN + 1];
    memcpy(show, _buf, _len);
    show[_len] = 0;
    display.printWordWrap(show, display.width());

    // Footer help removed for cleaner interface

    // Sent popup
    if (millis() < _sent_popup_until) {
      display.setColor(DisplayDriver::DARK);
      int p = display.height() / 32;
      int y = display.height() / 3;
      display.fillRect(p, y, display.width() - 2 * p, y);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(p, y, display.width() - 2 * p, y);
      display.drawTextCentered(display.width() / 2, y + p * 3, "Sent!");
      return 600;  // refresh during popup
    }

#if AUTO_OFF_MILLIS==0
    return 2000;
#else
    return _dirty ? 100 : 1500;
#endif
  }

  bool handleInput(char c) override {
    // Text navigation keys (only when editing message)
    if (_len > 0 && c == KEY_LEFT)  { moveLeft();  return true; }
    if (_len > 0 && c == KEY_RIGHT) { moveRight(); return true; }
    
    // Recipient navigation keys (when not editing or message is empty)
    if (_len == 0 && c == KEY_LEFT) {
      prevRecipient();
      return true;
    }
    if (_len == 0 && c == KEY_RIGHT) {
      nextRecipient();
      return true;
    }

    // Up/Down for recipient navigation regardless of message length
    if (c == KEY_PREV) {
      // If message is empty, this acts as recipient navigation
      if (_len == 0) {
        prevRecipient();
        return true;
      } else {
        // If message exists, go back to home
        _task->gotoHomeScreen();
        return true;
      }
    }
    
    if (c == KEY_NEXT) {
      nextRecipient();
      return true;
    }

    // Tab to cycle recipients forward
    if (c == KEY_SELECT || c == '\t' || c == 0x09) {
      nextRecipient();
      return true;
    }

    // Send on Enter
    if (c == KEY_ENTER) {
      if (_len > 0) {
        if (_task->sendTextToRecipient(_buf, _recipient_type, _recipient_index, this)) {
          _sent_popup_until = millis() + 800;
          _len = 0; _cursor = 0; _buf[0] = '\0';
          _dirty = true;
          _task->notify(UIEventType::ack);
          _task->showAlert("Message sent", 800);
        } else {
          _task->showAlert("Send failed", 1000);
        }
      } else {
        _task->showAlert("Empty", 500);
      }
      return true;
    }

    // Backspace
    if (c == '\b') { backspace(); return true; }

    // Printable ASCII
    if (c >= 32 && c <= 126) { insertChar(c); return true; }

    return false;
  }
};
// --------------- end Compose Screen -----------------



void UITask::gotoComposeScreen() { setCurrScreen(compose); }


void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + AUTO_OFF_MILLIS;

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

  _node_prefs = node_prefs;
  if (_display != NULL) {
    _display->turnOn();
  }

#ifdef PIN_BUZZER
  buzzer.begin();
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  compose    = new ComposeScreen(this);  
  setCurrScreen(splash);

 #ifdef USE_CARDKB
  initCardKB();
  if (_cardkb_present) showAlert("CardKB: OK", 800);
  else                showAlert("CardKB: NOT FOUND", 1200);
#endif

}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif

#ifdef PIN_VIBRATION
  // Trigger vibration for all UI events except none
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}


void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0) {
    gotoHomeScreen();
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;

  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, from_name, text);
  
  // Add sender to recent senders list if compose screen exists
  if (compose) {
    ((ComposeScreen *) compose)->addRecentSender(from_name);
  }
  
  setCurrScreen(msg_preview);

  if (_display != NULL) {
    if (!_display->isOn()) _display->turnOn();
    _auto_off = millis() + AUTO_OFF_MILLIS;  // extend the auto-off timer
    _next_refresh = 100;  // trigger refresh
  }
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state);
  }
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
  curr = c;
  _next_refresh = 100;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _board->reboot();
  } else {
    _display->turnOff();
    _board->powerOff();
  }
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {

  static bool once = false;
//if (!once) { showAlert("LOOP!", 800); once = true; }

  char c = 0;
#if defined(PIN_USER_BTN)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#endif
#if defined(WIO_TRACKER_L1)
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  ev = analog_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#endif
#if defined(DISP_BACKLIGHT) && defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
    digitalWrite(DISP_BACKLIGHT, !touch_state);
    next_backlight_btn_check = millis() + 300;
  }
#endif

#ifdef USE_CARDKB
 //original
  if (c == 0) {
    char kc = pollCardKB();
    if (kc) {
      // Keep the exact same UX as button presses: wake/extend display via checkDisplayOn
      //char dbg[16]; sprintf(dbg, "kc:%02X", (uint8_t)kc);
      //showAlert(dbg, 200);
      
      c = checkDisplayOn(kc);
    }
  }
  /*
  // Always poll CardKB; only consume if we don't already have an input
  char kc2 = pollCardKB();
  if (kc2) {
    // show what is actually heading into the UI (raw UI code, not I2C byte)
    char dbg[16]; sprintf(dbg, "kc:%02X", (uint8_t)kc2);
    showAlert(dbg, 200);

    if (c == 0) {
      c = checkDisplayOn(kc2); // will consume if it just woke the display
    }
  }
  */
#endif

  if (c != 0 && curr) {
    curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
        _display->setTextSize(1);
        int y = _display->height() / 3;
        int p = _display->height() / 32;
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(p, y, _display->width() - p*2, y);
        _display->setColor(DisplayDriver::LIGHT);  // draw box border
        _display->drawRect(p, y, _display->width() - p*2, y);
        _display->drawTextCentered(_display->width() / 2, y + p*3, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
      _display->endFrame();
    }
#if AUTO_OFF_MILLIS > 0
    if (millis() > _auto_off) {
      _display->turnOff();
    }
#endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {

      // show low battery shutdown alert
      // we should only do this for eink displays, which will persist after power loss
      #if defined(THINKNODE_M1) || defined(LILYGO_TECHO)
      if (_display != NULL) {
        _display->startFrame();
        _display->setTextSize(2);
        _display->setColor(DisplayDriver::RED);
        _display->drawTextCentered(_display->width() / 2, 20, "Low Battery.");
        _display->drawTextCentered(_display->width() / 2, 40, "Shutting Down!");
        _display->endFrame();
      }
      #endif

      shutdown();

    }
    next_batt_chck = millis() + 8000;
  }
#endif
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      _display->turnOn();   // turn display on and consume event
      c = 0;
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 0;  // trigger refresh
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
    the_mesh.enterCLIRescue();
    c = 0;   // consume event
  }
  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double click triggered");
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
  checkDisplayOn(c);
  toggleBuzzer();
  c = 0;
  return c;
}

void UITask::toggleGPS() {
    if (_sensors != NULL) {
    // toggle GPS on/off
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        if (strcmp(_sensors->getSettingValue(i), "1") == 0) {
          _sensors->setSettingValue("gps", "0");
          notify(UIEventType::ack);
          showAlert("GPS: Disabled", 800);
        } else {
          _sensors->setSettingValue("gps", "1");
          notify(UIEventType::ack);
          showAlert("GPS: Enabled", 800);
        }
        _next_refresh = 0;
        break;
      }
    }
  }
}

bool UITask::sendTextToRecipient(const char* text, int recipient_type, int recipient_index, void* compose_screen) {
  if (!text || !*text) {
    showAlert("Empty message", 800);
    return false;
  }

  size_t msg_len = strnlen(text, 240);
  if (msg_len > MAX_TEXT_LEN) {
    showAlert("Message too long", 800);
    return false;
  }

  ComposeScreen* screen = (ComposeScreen*)compose_screen;
  uint32_t timestamp = rtc_clock.getCurrentTime();

  // Send to specific contact
  if (recipient_type == 0) { // RECIPIENT_CONTACT
    // Check if it's a recent sender first
    if (recipient_index < screen->_num_recent_senders) {
      // Try to find contact by name
      ContactInfo* mesh_contact = the_mesh.searchContactsByPrefix(screen->_recent_senders[recipient_index].name);
      if (mesh_contact) {
        uint32_t expected_ack, est_timeout;
        int result = the_mesh.sendMessage(*mesh_contact, timestamp, 0, text, expected_ack, est_timeout);
        
        if (result == MSG_SEND_SENT_FLOOD) {
          showAlert("Sent (flood)", 800);
          notify(UIEventType::ack);
          return true;
        } else if (result == MSG_SEND_SENT_DIRECT) {
          showAlert("Sent (direct)", 800);
          notify(UIEventType::ack);
          return true;
        }
      }
      showAlert("Contact not found", 800);
      return false;
    } else {
      // It's a recent contact (heard advert)
      int contact_idx = recipient_index - screen->_num_recent_senders;
      if (contact_idx < screen->_num_recent_contacts) {
        ContactInfo* mesh_contact = the_mesh.lookupContactByPubKey(screen->_recent_contacts[contact_idx].pubkey_prefix, 6);
        if (mesh_contact) {
          uint32_t expected_ack, est_timeout;
          int result = the_mesh.sendMessage(*mesh_contact, timestamp, 0, text, expected_ack, est_timeout);
          
          if (result == MSG_SEND_SENT_FLOOD) {
            showAlert("Sent (flood)", 800);
            notify(UIEventType::ack);
            return true;
          } else if (result == MSG_SEND_SENT_DIRECT) {
            showAlert("Sent (direct)", 800);
            notify(UIEventType::ack);
            return true;
          }
        }
      }
      showAlert("Contact not found", 800);
      return false;
    }
  }

  // Send to group channel
  if (recipient_type == 1) { // RECIPIENT_CHANNEL
    ChannelDetails channel;
    if (the_mesh.getChannel(recipient_index, channel)) {
      if (the_mesh.sendGroupMessage(timestamp, channel.channel, the_mesh.getNodeName(), text, msg_len)) {
        char alert_msg[32];
        snprintf(alert_msg, sizeof(alert_msg), "Sent to #%s", channel.name[0] ? channel.name : "Channel");
        showAlert(alert_msg, 800);
        notify(UIEventType::ack);
        return true;
      } else {
        showAlert("Group send failed", 800);
        return false;
      }
    }
    char alert_msg[32];
    snprintf(alert_msg, sizeof(alert_msg), "No channel %d", recipient_index);
    showAlert(alert_msg, 800);
    return false;
  }

  // Broadcast (fallback)
  if (recipient_type == 2) { // RECIPIENT_BROADCAST
    // Try sending to the first available channel as broadcast
    ChannelDetails channel;
    if (the_mesh.getChannel(0, channel)) {
      if (the_mesh.sendGroupMessage(timestamp, channel.channel, the_mesh.getNodeName(), text, msg_len)) {
        showAlert("Broadcast sent", 800);
        notify(UIEventType::ack);
        return true;
      }
    }
    showAlert("No broadcast method", 800);
    return false;
  }

  showAlert("Invalid recipient", 800);
  return false;
}

bool UITask::sendText(const char* text) {
  // Legacy method - try to send to first recent contact or channel
  AdvertPath recent[1];
  int n = the_mesh.getRecentlyHeard(recent, 1);
  
  if (n > 0) {
    return sendTextToRecipient(text, 0, 0, &recent[0]); // RECIPIENT_CONTACT
  } else {
    return sendTextToRecipient(text, 1, 0, nullptr); // RECIPIENT_CHANNEL
  }
}



void UITask::toggleBuzzer() {
    // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
      showAlert("Buzzer: ON", 800);
    } else {
      buzzer.quiet(true);
      showAlert("Buzzer: OFF", 800);
    }
    _next_refresh = 0;  // trigger refresh
  #endif
}

#ifdef USE_CARDKB
void UITask::initCardKB(int sdaPin, int sclPin) {
  // Adafruit nRF52 core: begin() has no sda/scl overload
  #if defined(ARDUINO_ARCH_NRF5) || defined(ARDUINO_ARCH_NRF52)
    (void)sdaPin; (void)sclPin;
    CARDKB_WIRE.begin();

  // ESP32/ESP8266: begin(sda, scl) is supported
  #elif defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    if (sdaPin >= 0 && sclPin >= 0) CARDKB_WIRE.begin(sdaPin, sclPin);
    else CARDKB_WIRE.begin();

  // RP2040 (Arduino-mbed): set pins then begin()
  #elif defined(ARDUINO_ARCH_RP2040)
    if (sdaPin >= 0) CARDKB_WIRE.setSDA(sdaPin);
    if (sclPin >= 0) CARDKB_WIRE.setSCL(sclPin);
    CARDKB_WIRE.begin();

  // Fallback: just begin()
  #else
    (void)sdaPin; (void)sclPin;
    CARDKB_WIRE.begin();
  #endif

  // Presence probe
  CARDKB_WIRE.beginTransmission(CARDKB_ADDR);
  _cardkb_present = (CARDKB_WIRE.endTransmission() == 0);
  _cardkb_next_poll = 0;
  _cardkb_esc_state = 0;
}
#endif

#ifdef USE_CARDKB

char UITask::pollCardKB() {
  if (!_cardkb_present) return 0;

  // Light throttle
  uint32_t now = millis();
  if (now < _cardkb_next_poll) return 0;
  _cardkb_next_poll = now + 5;

  // Read one byte (0 means "no key")
  CARDKB_WIRE.requestFrom((int)CARDKB_ADDR, 1);
  if (!CARDKB_WIRE.available()) return 0;

  uint8_t b = CARDKB_WIRE.read();
  
  /*
  if (b != 0) {
  static uint32_t last_dbg = 0;
  uint32_t now = millis();
  if (now - last_dbg > 200) { // rate-limit popups
    char dbg[16]; sprintf(dbg, "K:%02X", b);
    showAlert(dbg, 150);
    last_dbg = now;
  }
}
*/

/*
  if (b != 0) {
  static uint32_t last_dbg = 0;
  uint32_t now = millis();
  if (now - last_dbg > 150) { // rate limit
    char dbg[16]; sprintf(dbg, "K:%02X", b);
    showAlert(dbg, 120);
    last_dbg = now;
  }
}
*/

  if (b == 0) return 0;

  // Handle arrow escape sequences ESC [ A/B/C/D
  if (_cardkb_esc_state == 0) {
    if (b == 0x1B) {           // ESC
      _cardkb_esc_state = 1;
      return 0;
    }
  } else if (_cardkb_esc_state == 1) {
    if (b == '[') {
      _cardkb_esc_state = 2;
      return 0;
    } else {
      // Not an arrow sequence, reset and fall through to translation
      _cardkb_esc_state = 0;
      // (no early return)
    }
  } else if (_cardkb_esc_state == 2) {
    _cardkb_esc_state = 0;
    switch (b) {
      case 'A': return KEY_PREV;   // Up
      case 'B': return KEY_NEXT;   // Down
      case 'C': return KEY_RIGHT;  // Right
      case 'D': return KEY_LEFT;   // Left
      default: return 0;
    }
  }

  return translateCardKB(b);
}

char UITask::translateCardKB(uint8_t b) {
  // Printable ASCII
  if (b >= 32 && b <= 126) return (char)b;

  // Enter
  if (b == '\r' || b == '\n') return KEY_ENTER;

  // Backspace (if your composer expects a specific keycode, change this)
  //if (b == 0x08 || b == 0x7F) return '\b';
  if (b == 0x08) return '\b';

  // Tab → select (optional)
  if (b == '\t' || b == 0x09) return KEY_SELECT;

  // Lone ESC → back/prev
  //if (b == 0x1B) return KEY_PREV;
  if (b == 0x1B) return KEY_PREV;

  switch (b) {
    case 0xB5: return KEY_PREV;   // Up
    case 0xB6: return KEY_NEXT;   // Down
    case 0xB7: return KEY_RIGHT;  // Right
    case 0xB4: return KEY_LEFT;   // Left
  }
  
  return 0;
}

#endif // USE_CARDKB


