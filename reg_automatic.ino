#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <virtuabotixRTC.h>
#include <EEPROM.h>

#define button 14
#define led 4
#define relay 5
virtuabotixRTC myRTC(12, 15, 13);

// Units are hours
unsigned int start_hours_size = 2;
int start_hours[8] = {19,20,0,0,0,0,0,0};

// Initialize Wifi connection to the router
char ssid[] = "YOUR_WIFI_SSID";     // your network SSID (name)
char password[] = "YOUR_WIFI_PASSWORD"; // your network key
WiFiClientSecure client;

// System vars
int sw = 0;
int statusled = 0;
int is_on = 0;
int sw_last = 0;
int sw_last_last = 0;
unsigned int is_on_manual = 0;
unsigned int is_on_timer = 0;
unsigned int is_on_remote = 0;

/*
 * 
 * Telegram Bot section
 * 
 */
// Initialize Telegram BOT
#define BOTtoken "YOUR_TELGRAM_BOT_TOKEN" // your Bot Token (Get from Botfather)

UniversalTelegramBot bot(BOTtoken, client);
int Bot_mtbs = 10000; //mean time between scan messages
long Bot_lasttime;   //last time messages' scan has been done
bool Start = false;
String telegram_password = "YOUR_CUSTOM_AUTH_PASSWORD";
String allowed_chats[32];
unsigned int allowed_chats_index = 0;

void sendSignals(String text) {
  for (int i=0; i<allowed_chats_index; i++)
    bot.sendMessage(allowed_chats[i], text, "");
}

void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i=0; i<numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    String command = split(text,' ', 0);
    String param = split(text,' ', 1);

    String from_name = bot.messages[i].from_name;
    if (from_name == "") from_name = "Guest";

    bool allowed = false;
    for(int i=0; i<allowed_chats_index; i++) {
      if (allowed_chats[i] == chat_id) {
        Serial.println("Authentication correct: " + allowed_chats[i]);
        allowed = true;
        break;
      }
    }

    if (command == "/help" || command == "/start") {
      String welcome = "Welcome to Talaia reg controler v2, " + from_name + ".\n";
      welcome += "/auth <password> : Autentica amb password\n";
      welcome += "/hour : Hora actual\n";
      welcome += "/wifi : Wifi status\n";
      welcome += "/status : Estat del reg\n";
      welcome += "/reg_on : Activa reg\n";
      welcome += "/reg_off : Para reg\n";
      welcome += "/set_timer <h1,h2...> : Configura reg automatic\n";
      welcome += "/help : Mostra aquesta ajuda\n";
      bot.sendMessage(chat_id, welcome, "");
    }

    if (command == "/auth") {
      if (allowed)
        bot.sendMessage(chat_id, "You are already allowed", "");
      else if ( param == telegram_password ) {
        allowed_chats[allowed_chats_index] = String(chat_id);
        allowed_chats_index++;
        Serial.println("Add ChatID:"+chat_id+" to allowed chats");
        bot.sendMessage(chat_id, "Password correct, you are now allowed! ChatID:"+chat_id, "");
        allowed = true;
      } else bot.sendMessage(chat_id, "Password incorrect, go away!", "");
    }

    if (allowed) {
      
      if (command == "/wifi") {
        bot.sendMessage(chat_id, WifiStatus(), "");
      }
      
      if (command == "/hour") {
        setRTC();
        bot.sendMessage(chat_id, "Current time is " + humanTime(), "");
      }

      if (command == "/status") {
        String stext = "";
        stext += "Timer:" + String(is_on_timer);
        stext += " Manual:" + String(is_on_manual);
        stext += " Remote:" + String(is_on_remote);
        stext += " TimerHours:";
        for(int i=0; i<start_hours_size; i++)
          stext += " " + String(start_hours[i]);
        bot.sendMessage(chat_id, stext, "");
      }

      if (command == "/set_timer") {
          String h;
          start_hours_size = 0;
          for(int i=0;i<8;i++) {
            h = split(param,',',i);
            if( h == "") break;
            else {
              int hi = h.toInt();
              if ( hi > 0 ) {
                start_hours[i] = hi;
                start_hours_size++;
              }
            }
          }
          save_hours();
          bot.sendMessage(chat_id, "OK set timer", "");
      }
      
      if (command == "/reg_on") {
        if (is_on_remote || is_on_manual || is_on_timer)
          bot.sendMessage(chat_id, "Already on!", "");
        else {
          is_on_remote = true;
          bot.sendMessage(chat_id, "Activated! Do not forget to stop it please :/", "");
        }
      }
      
      if (command == "/reg_off") {
        is_on_remote = false;
        if (is_on_manual || is_on_timer)
          bot.sendMessage(chat_id, "Remote off but still enabled by other source!", "");
        else
          bot.sendMessage(chat_id, "Stoped! Thank you for saving water :)", "");
      }
      
    }
    else bot.sendMessage(chat_id, "You are not allowed, please authenticate with: /auth <password>\n["+command+","+param+","+chat_id+","+allowed+"]", "");
  }
}

/* 
 *  
 * NTP network time protocol section 
 * 
*/

IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "hora.rediris.es";
int timezone = 2; // GMT+2
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
unsigned int localPort = 2390; 
WiFiUDP udp;

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

// returns current time
String getTime() {
  String cdata = "";
  WiFi.hostByName(ntpServerName, timeServerIP);
  Serial.println("Starting NTP process with IP " + (String)timeServerIP);
  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  // wait to see if a reply is available
  delay(2000);
  
  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
  }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970.epoch % 60 In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);

    // print the hour, minute and second:
    Serial.print("The current time is ");
    Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
    cdata += (String)( ((epoch  % 86400L) / 3600) + timezone );
    cdata += ':';
    Serial.print(':');
    if ( ((epoch % 3600) / 60) < 10 ) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      cdata += '0';
      Serial.print('0');
    }
    Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
    cdata += (String)((epoch  % 3600) / 60);
    Serial.print(':');
    cdata += ':';
    if ( (epoch % 60) < 10 ) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
      cdata += '0';
    }
    Serial.println(epoch % 60); // print the second
    cdata += (String) (epoch % 60);
  }
  return cdata;
}

void setRTC() {
  String t = getTime();
  unsigned int h = split(t,':',0).toInt();
  unsigned int m = split(t,':',1).toInt();
  unsigned int s = split(t,':',2).toInt();
  myRTC.setDS1302Time(s, m, h, 5, 3, 8, 2018);
}

// OTA Over-The-Air section 
void set_ota() {
    ArduinoOTA.onStart([]() {
    Serial.println("Start OTA");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd OTA");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OTA Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// DATA_SIZE|VAL1|VAL2...
void save_hours() {
    EEPROM.write(0, start_hours_size);
  for(int addr=1; addr <= start_hours_size; addr++) {
    EEPROM.write(addr, start_hours[addr-1]);
  }
  EEPROM.commit();
}

void read_hours() {
   start_hours_size = EEPROM.read(0);
   for(int addr=1; addr <= start_hours_size; addr++) {
    start_hours[addr-1] = EEPROM.read(addr);
   }
}


// Water pump control
void pump_control() {
  sw_last_last = sw_last;
  sw_last = sw;
  sw = digitalRead(button);
  
  Serial.print("Switch: ");
  Serial.println(sw);
  Serial.print("Manual: ");
  Serial.println(is_on_manual);
  
  // If switch is OFF, do nothing
  if (sw == 0 && is_on_manual) {
    digitalWrite(relay,LOW);
    digitalWrite(led,HIGH);
    delay(100);
    digitalWrite(led,LOW);
  }
  
  // Manual swith ON/OFF
  if (sw == 1 && sw_last == 0 && sw_last_last == 1) is_on_manual = true;
  if (sw == 0 && sw_last == 0 && sw_last_last == 0) is_on_manual = false;
  
  // Normal Timer operation
  if (sw == 1) relayTimeControl();
  if (is_on_manual || is_on_remote) {
    digitalWrite(relay,HIGH);
    digitalWrite(led,HIGH);
  }
  else if (is_on == 0){
    digitalWrite(relay,LOW);
    if (statusled)
      digitalWrite(led,HIGH);
    else
     digitalWrite(led,LOW);
    statusled = !statusled;
  }
}

void relayTimeControl() {
  int timer_on = 0;
  int i;
  
  for(i=0; i<start_hours_size; i++ )
    if (start_hours[i] == int(myRTC.hours))
        timer_on = 1;
        
  if (timer_on) {
        digitalWrite(relay,HIGH);
        is_on_timer = true;
        if (is_on == 0) sendSignals("Reg timer ON at " + String(myRTC.hours));
        is_on = 1;
  } else {
      is_on_timer = false;
      if(!is_on_manual and !is_on_remote) {
        digitalWrite(relay,LOW);
        if( is_on == 1) sendSignals("Reg timer OFF at " + String(myRTC.hours));
        is_on = 0;
      }
    }
}

/*
 * 
 * Auxiliar Functions
 * 
 */
String split(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void printTime() {
  Serial.println(humanTime());
}

String humanTime() {
  //char t[9];
  String t = "";
  t += String(int(myRTC.hours)) + ":" + String(int(myRTC.minutes));
  //sprintf(t,"%u:%u\0",myRTC.hours,myRTC.minutes);
  return t;
}

String WifiStatus() {
  String wstatus = "";
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  wstatus += "SSID:" + WiFi.SSID();

  // print your WiFi shield's IP address:
  String ip = WiFi.localIP().toString();
  Serial.print("IP Address: ");
  Serial.println(ip);
  wstatus += " IP:" + ip;

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
  wstatus += " Signal:" + String(rssi);
  return wstatus;
}

/*
 * 
 * Main Arduino functions
 * 
 */

void setup() {
  Serial.begin(115200);  
  pinMode(relay, OUTPUT);
  pinMode(led, OUTPUT);
  pinMode(button, INPUT_PULLUP);
  
  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // attempt to connect to Wifi network:
  Serial.print("Connecting Wifi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("WiFi connected");

  // NTP
  Serial.println("Starting NTP/UDP service");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
  
  // FLASH for saving hours
  EEPROM.begin(9);
  read_hours();

  // OTA
  set_ota();

  // Set initial time
  setRTC();
}

void loop() {
  ArduinoOTA.handle();
  myRTC.updateTime();
  pump_control();
  
  if (millis() > Bot_lasttime + Bot_mtbs)  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while(numNewMessages) {
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    
    Bot_lasttime = millis();
  } 
  printTime();
  delay(5000);
}
