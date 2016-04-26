#include <ESP8266WiFi.h>
#include <virtuabotixRTC.h>

// NodeMCU Pins definition
#define button 14
#define led 4
#define relay 5

// Units are hours
#define start_hours_size 2
int start_hours[] = {8,20}; // update start_hours_size if adding more hours

// WiFi settings
const char* ssid = "WIFI_ESSID";
const char* password = "WIFI_PASSWORD";

virtuabotixRTC myRTC(12, 15, 13); // RTC pins

WiFiServer server(80);

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("."); 
    }
    
  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  server.begin();
  
  pinMode(relay, OUTPUT);
  pinMode(led, OUTPUT);
  pinMode(button, INPUT_PULLUP);

// To set the initial hour of the RTC, use next line
// myRTC.setDS1302Time(00, 12, 11, 5, 15, 4, 2016);
}

int sw = 0;
int statusled = 0;
int printwstatus = 0;
int is_on = 0;

void loop() {
  myRTC.updateTime();
  relayTimeControl();
  weblisten();
  sw = digitalRead(button);
  Serial.println(sw);
  if (sw == 1) {
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
  printTime();
  
  if (printwstatus > 9) {
    printWifiStatus();
    printwstatus = 0;
  }
  else printwstatus++;
  delay(1000);
}

void relayTimeControl() {
  int start = 0;
  int i;
  for(i=0; i<start_hours_size; i++ ) {
    //Serial.println(start_hours[i]);
    if (start_hours[i] == int(myRTC.hours)) {
        digitalWrite(relay,HIGH);
        start = 1;
        is_on = 1;
        break;
    }
  }
  if (start == 0 && sw == 0) {
    digitalWrite(relay,LOW);
    is_on = 0;
  }
}

void weblisten() {
  // listen for incoming clients
  WiFiClient client = server.available();
  if (client) {
    Serial.println("new web client");
    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");  // the connection will be closed after completion of the response
          client.println("Refresh: 5");  // refresh the page automatically every 5 sec
          client.println();
          client.println("<!DOCTYPE HTML>");
          client.println("<html>");
          // output the value of each analog input pin
          client.println("<h3>Talaia reg automatic<h3>");
          client.println("<hr/>");
          client.print("Hora: ");
          client.println(humanTime());
          client.println("<br/>");
          client.print("Status: ");
          client.println(is_on || sw);
          client.println("</html>");
           break;
        }
        if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
        }
        else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);
   
    // close the connection:
    client.stop();
    Serial.println("client disonnected");
  }
}

void printTime() {
  Serial.println(humanTime());
}

void noprintTime() {
  Serial.print("Current Date / Time: "); 
  Serial.print(myRTC.dayofmonth); 
  Serial.print("/"); 
  Serial.print(myRTC.month); 
  Serial.print("/");
  Serial.print(myRTC.year);
  Serial.print(" ");
  Serial.print(myRTC.hours);
  Serial.print(":");
  Serial.print(myRTC.minutes);
  Serial.print(":");
  Serial.println(myRTC.seconds);
}

char* humanTime() {
  char t[9];
  sprintf(t,"%u:%u\0",myRTC.hours,myRTC.minutes);
  //puts(t);
  return t;
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}
