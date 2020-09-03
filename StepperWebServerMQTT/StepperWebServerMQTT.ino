/*
 *  Controller for Stepper Motor
 *  This sketch sets up a web server on port 80
 *  The server will control the Adafruit TB6612 Driver board depending on the request
 *  See http://server_ip/ for usage (or see docs for more info)
 *  The server_ip is the IP address of the ESP8266 module which will be 
 *  printed to Serial and the LED blinked when the module is connected.
 *
 *  Version 1.1 2015.10.01 R. Grokett
 *  - Added support for STBY pin 
 */

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <AccelStepper.h>
#include "Pass.h"

//// -- USER EDIT -- 
// enter wifi and MQTT credetials in the Pass.h file and place it into the scetch folder
//const char* ssid     = "xxx";    // YOUR WIFI SSID
//const char* password = "xxx";    // YOUR WIFI PASSWORD 
const char* mqttServer = "192.168.178.34";
//const int mqttPort = 1883;
//const char* mqttUser = "xxx";
//const char* mqttPassword = "xxx";


// change this to your motor if not NEMA-17 200 step
#define STEPS 20000  // Max steps for one revolution
#define MIN_POS 0  // Min abs position
#define MAX_POS 45000  // Max abs position

#define RPM 10000     // Max RPM
#define DELAY 1    // Delay to allow Wifi to work

#define MQTT_listen_topic "badezimmer/boiler"
#define MQTT_talk_topic "badezimmer/boiler/current_pos"


WiFiClient WIFIClient;
PubSubClient MQTTclient(WIFIClient);

// -- END --


int STBY = 0;     // D3 
int LED = 2;      // GPIO 0 (built-in LED)
int LIMIT_SWITCH = 15; // D8

int current_pos = 1; // current pos
String req = "";
String respMsg = "";
String MQTT_req = "";
bool MQTT_available = false;

// GPIO Pins for Motor Driver board
AccelStepper stepper(AccelStepper::DRIVER, 13, 12); //D6 and D7

// Create an instance of the server
// specify the port to listen on as an argument
WiFiServer server(80);

void setup() {
  Serial.begin(115200);
  delay(10);

  // prepare onboard LED
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);


  // prepare STBY GPIO and turn on Motors
  pinMode(STBY, OUTPUT);
  digitalWrite(STBY, LOW);

  
  // prepare LIMIT_switch
  pinMode(LIMIT_SWITCH, INPUT);
  
  // Set default speed to Max (doesn't move motor)
  stepper.setSpeed(RPM); //remove
  stepper.setMaxSpeed(40000);
  stepper.setAcceleration(4000);
  stepper.setSpeed(12000);
  
  // Connect to WiFi network
  setup_wifi();
  MQTTclient.setServer(mqttServer, mqttPort);
  MQTTclient.setCallback(callback);
  reconnectMQTT();

  // Blink onboard LED to signify its connected
  blink();
  blink();
  blink();
  blink();

  find_ref(true);
}

//void loop() {
//  WiFiClient WIFIclient = server.available();
//
//   if (!MQTTclient.connected()) {
//    reconnectMQTT();
//  }
//  MQTTclient.loop();
//  
//  if (!WIFIclient) {
//    return;
//  }
//  
//}

void loop() {

  WiFiClient WIFIclient = server.available();


   if (!MQTTclient.connected()) {
    reconnectMQTT();
  }

  MQTTclient.loop();

  if (!WIFIclient) {
    return;
  }

  String respMsg = "";    // HTTP Response Message
  
  // Wait until the WIFIclient sends some data
  Serial.println("new client");
  while(!WIFIclient.available()){
    delay(1);
    MQTTclient.loop();
  }

  String req = WIFIclient.readStringUntil('\r');
  Serial.println(req);
  WIFIclient.flush();
    
 
  // Read the first line of the request
  //String req = WIFIclient.readStringUntil('\r');
  Serial.println(req);
  Serial.println(req.indexOf("HTTP"));
  int start_index = 13; // "GET /stepper/"
  int end_index = req.indexOf("HTTP"); //also tace blank char
  Serial.println(end_index);
  
  req = req.substring(start_index, end_index); // remove useless chars from beginning (GET /stepper/) and end (GET /stepper/)
  
  process_req(req);
  
  WIFIclient.flush();
  if (MQTT_available) {
    // Read the first line of the request
    Serial.println("hier");
    req = MQTT_req;
  }
    Serial.println(req);
    Serial.println(req.indexOf("steps"));

    WIFIclient.flush();

  // Prepare the response
  String s = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n";
  if (respMsg.length() > 0)
    s += respMsg;
  else
    s += "OK";
   
  s += "\n";

  // Send the response to the client
  WIFIclient.print(s);
  delay(1);
  MQTT_available = false;

//  Serial.println("Client disconnected");

  // The client will actually be disconnected 
  // when the function returns and 'client' object is detroyed


}

int getValue(String req) {
  int val_start = req.indexOf('?');
  int val_end   = req.indexOf(' ', val_start + 1);
  if (val_start == -1 || val_end == -1) {
    Serial.print("Invalid request: ");
    Serial.println(req);
    return(0);
  }
  req = req.substring(val_start + 1, val_end);
  Serial.print("Request: ");
  Serial.println(req);
   
  return(req.toInt());
}

void process_req(String req){

  Serial.println("in der process methode");
  Serial.println(req);

  // Match the request 
  if (req.indexOf("stop") != -1 ) {
    digitalWrite(STBY, LOW);
    respMsg = "OK: MOTORS OFF";
  } 
  else if (req.indexOf("start") != -1) {
    digitalWrite(STBY, HIGH);
    blink();
    respMsg = "OK: MOTORS ON";
  } 
  else if (req.indexOf("rpm") != -1) {
    int rpm = getValue(req);
    if ((rpm < 1) || (rpm > RPM)) {
      respMsg = "ERROR: rpm out of range 1 to "+ String(RPM);
    } else {
      stepper.setSpeed(rpm);
      respMsg = "OK: RPM = "+String(rpm);
    }
  }
  // This is just a simplistic method of handling + or - number steps...
  else if (req.indexOf("steps") != -1) {
    int steps = getValue(req);
    if ((steps == 0) || (steps < 0 - STEPS) || ( steps > STEPS )) {
      respMsg = "ERROR: steps out of range ";
    } else {  
      digitalWrite(STBY, HIGH);       // Make sure motor is on
      respMsg = "OK: STEPS = "+String(steps);
      delay(DELAY); 
      move_steps(steps);
 //     for (int i=0;i<steps;i++) {   // This loop is needed to allow Wifi to not be blocked by step
   //     move_steps(1);
 //       delay(DELAY);   
  //    }
      
    }
  }
  else if (req.indexOf("pos") != -1) {
    int pos = getValue(req);
    if ((pos < MIN_POS) || ( pos > MAX_POS )) {
      respMsg = "ERROR: steps out of range ";
    } else {  
      digitalWrite(STBY, HIGH);       // Make sure motor is on
      respMsg = "OK: POS = "+String(pos);
      delay(DELAY); 
      move_abs_pos(pos);
    }
  }
  else if (req.indexOf("current_pos") != -1) {
    int current_pos = stepper.currentPosition();
    respMsg = "OK: CURRENT_POS = "+String(current_pos);
  }  
  
  else if (req.indexOf("find_ref") != -1) {
  find_ref(true);
  respMsg = "OK: REF FOUND";
  }  
  
  else {
    respMsg = printUsage();
  }
    

}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
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
  // Start the server
  server.begin();
  Serial.println("Server started");
  Serial.println("IP address: ");

  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  // Loop until we're reconnected
  while (!MQTTclient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    // if (client.connect("ESP8266Client")) {
    if (MQTTclient.connect("ESP8266Client", mqttUser, mqttPassword)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(MQTTclient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
    delay(100);
  }
  MQTTclient.subscribe(MQTT_listen_topic);
}

String printUsage() {
  // Prepare the usage response
  String s = "Stepper usage:\n";
  s += "http://{ip_address}/stepper/stop\n";
  s += "http://{ip_address}/stepper/start\n";
  s += "http://{ip_address}/stepper/rpm?[1 to " + String(RPM) + "]\n";
  s += "http://{ip_address}/stepper/steps?[-" + String(STEPS) + " to " + String(STEPS) +"]\n";
  return(s);
}
void blink() {
  digitalWrite(LED, LOW);
  delay(100); 
  digitalWrite(LED, HIGH);
  delay(100);
}

void move_steps(int steps){
  digitalWrite(STBY, HIGH);
  stepper.move(steps);
  stepper.run();
  while (stepper.distanceToGo() != 0){
    delay(1);
    stepper.run();
  }
  digitalWrite(STBY, LOW);
  current_pos+=steps;


}
void move_abs_pos(int pos){
  if (abs(current_pos-pos)>5){
    digitalWrite(STBY, HIGH);
    if (pos<5000){
      find_ref(false);
      move_steps(pos);  
      current_pos=pos;
    }
    else{
      int steps = pos - current_pos;
      move_steps(steps - 1000);  //drive to position always from same side
      move_steps(1000); 
    }
    digitalWrite(STBY, LOW);
    Serial.println("das ist er");
    Serial.println(String(current_pos));
    reconnectMQTT();
    MQTTclient.publish(MQTT_talk_topic, String(current_pos).c_str(), false); 
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
 
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  char msg[length+1];
      for (int i = 0; i < length; i++) {
          msg[i] = (char)payload[i];
      }
      Serial.println();
   
      msg[length] = ' ';
      Serial.println(msg); 

 
  Serial.println();
  Serial.println("-----------------------");
  
  MQTT_req = msg;
  MQTT_available = true;
  process_req(msg);
}

void find_ref(bool set_pos) {
  while(!digitalRead(LIMIT_SWITCH)) {
    move_steps(-100);
  }
  move_steps(2000);
  current_pos = 1;
  if (set_pos){
    reconnectMQTT();
    MQTTclient.publish(MQTT_talk_topic, String(current_pos).c_str(), false);    
  }

}
