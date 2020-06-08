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

// -- USER EDIT -- 
const char* ssid     = "FRITZ!Box 7530 NP";    // YOUR WIFI SSID
const char* password = "49058267345139386298";    // YOUR WIFI PASSWORD 
const char* mqttServer = "hassio.local";
const int mqttPort = 1883;
const char* mqttUser = "florian";
const char* mqttPassword = "discus2bt";

// change this to your motor if not NEMA-17 200 step
#define STEPS 20000  // Max steps for one revolution
#define MIN_POS 0  // Min abs position
#define MAX_POS 50000  // Max abs position

#define RPM 10000     // Max RPM
#define DELAY 1    // Delay to allow Wifi to work

#define MQTT_topic "badezimmer/boiler"

WiFiClient WIFIClient;
PubSubClient MQTTclient(WIFIClient);

// -- END --


int STBY = 5;     // GPIO 5 TB6612 Standby
int LED = 2;      // GPIO 0 (built-in LED)

int current_pos = 0; // current pos
String req = "";

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
  digitalWrite(STBY, HIGH);
  
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
//}

void loop() {
  // Check if a client has connected

   if (!MQTTclient.connected()) {
    reconnectMQTT();
  }

  MQTTclient.loop();
  
  WiFiClient WIFIclient = server.available();

//  if (!WIFIclient) {
//    return;
//  }
  
  String respMsg = "";    // HTTP Response Message
  
  // Wait until the WIFIclient sends some data
  //Serial.println("new client");
  while(!WIFIclient.available()){
    delay(1);
  }
  
  // Read the first line of the request
  //String req = WIFIclient.readStringUntil('\r');
  Serial.println(req);
  WIFIclient.flush();
  
  // Match the request 
  if (req.indexOf("/stepper/stop") != -1 ) {
    digitalWrite(STBY, LOW);
    respMsg = "OK: MOTORS OFF";
  } 
  else if (req.indexOf("/stepper/start") != -1) {
    digitalWrite(STBY, HIGH);
    blink();
    respMsg = "OK: MOTORS ON";
  } 
  else if (req.indexOf("/stepper/rpm") != -1) {
    int rpm = getValue(req);
    if ((rpm < 1) || (rpm > RPM)) {
      respMsg = "ERROR: rpm out of range 1 to "+ String(RPM);
    } else {
      stepper.setSpeed(rpm);
      respMsg = "OK: RPM = "+String(rpm);
    }
  }
  // This is just a simplistic method of handling + or - number steps...
  else if (req.indexOf("/stepper/steps") != -1) {
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
  else if (req.indexOf("/stepper/pos") != -1) {
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
  else if (req.indexOf("/stepper/current_pos") != -1) {
    int current_pos = stepper.currentPosition();
    respMsg = "OK: CURRENT_POS = "+String(current_pos);
  }   
  else {
    respMsg = printUsage();
  }
    
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
  }
  MQTTclient.subscribe(MQTT_topic);
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
  stepper.move(steps);
  stepper.run();
  while (stepper.distanceToGo() != 0){
    delay(1);
    stepper.run();
  }
}
void move_abs_pos(int pos){
  if (stepper.distanceToGo() == 0){
    stepper.moveTo(pos);
    //stepper.setSpeed(12000);

    stepper.run();
    while (stepper.distanceToGo() != 0){
      delay(1);
      stepper.run();
    }
  }
  MQTTclient.publish(MQTT_topic, String(pos).c_str(), false);
}

void callback(char* topic, byte* payload, unsigned int length) {
 
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  char msg[length+1];
      for (int i = 0; i < length; i++) {
          msg[i] = (char)payload[i];
      }
      Serial.println();
   
      msg[length] = '\0';
      Serial.println(msg); 

 
  Serial.println();
  Serial.println("-----------------------");
  req = msg;

}
