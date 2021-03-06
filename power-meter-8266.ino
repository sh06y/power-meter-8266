/**
	******************************************************************************
	* @File Name          : power-meter-8266.ino
	* @Author             : sh06y
	* @date               : 2022-04-17
	* @Description        : This file provides code for the firmware.
	* @Copyring           : Copyright (c) sh06y. All right reserved.
*/
const int FW_VERSION = 2;

/*
________________#########______________________
______________############_____________________
______________#############____________________
_____________##__###########___________________
____________###__######_#####__________________
____________###_#######___####_________________
___________###__##########_####________________
__________####__###########_####_______________
________#####___###########__#####_____________
_______######___###_########___#####___________
_______#####___###___########___######_________
______######___###__###########___######_______
_____######___####_##############__######______
____#######__#####################_#######_____
____#######__##############################____
___#######__######_#################_#######___
___#######__######_######_#########___######___
___#######____##__######___######_____######___
___#######________######____#####_____#####____
____######________#####_____#####_____####_____
_____#####________####______#####_____###______
______#####______;###________###______#________
________##_______####________####______________
***********************************************
***********************************************
*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "TM1637.h"
#include "smartconfig.h"
#include <ESPAsyncWebServer.h>
#include <ESPAsyncTCP.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

// MQTT Broker
const char *mqtt_broker = "192.168.0.12";
const char *state_topic = "meter/state";
const char *topic = "home/power";
// const char *mqtt_username = "emqx";
// const char *mqtt_password = "public";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);
AsyncWebServer server(80);

// port
#define powerledPin A0
#define resetButton 5
const int ledPin = 2;
const int buzzerPin = 16;
#define CLK 12
#define DIO 14

TM1637 tm1637(CLK,DIO);

// ??????
int powerledPin_rate = 800;// imp/kWh
int maxPower = 6500;
const int beep_delay = 400;


int light1;

int powerledPin_on, powerledPin_off, powerledPin_average;

unsigned long timeA;
unsigned long timeB;
unsigned long time_light;

unsigned long beepP = 0;

float power = 0;
bool beep = false;

char message[1000];

// HTML web page
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
	<title>????????????</title>
	<meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
	<meta name="viewport" content="width=device-width, initial-scale=1">
	<script>
		function submitMessage() {
			alert("?????????");
			// setTimeout(function(){ document.location.reload(false); }, 500);   
		}
		function submit_network() {
			alert("????????????????????????...");
			// setTimeout(function(){ document.location.reload(false); }, 500);   
		}
	</script></head>
	<body>
	<h1>????????????</h1>

	<p>???????????????%currentPower%W</p>

	<h2>WiFi??????</h2>
	<form action="/wifi" target="hidden-form">
		SSID: <input type="text" name="SSID" value=%SSID%><br>
		??????: <input type="text" name="wifi_passwd" value=%wifi_passwd%><br>
		<input type="submit" value="??????" onclick="submit_network()">
	</form><br>

	<h2>??????</h2>
	<form action="/setting" target="hidden-form">
		????????????(W): <input type="number" name="maxPower" value=%maxPower%><br>
		????????????(imp/kWh): <input type="number" name="powerledPin_rate" value=%powerledPin_rate%><br>
		<!-- ????????????(1-7): <input type="number" name="backlightLevel", value=%backlightLevel%> -->
		<br>
		<input type="submit" value="??????" onclick="submitMessage()">
	</form><br>
	<h2>??????</h2>
	<button onclick="document.location.href='/checkUpdate'">????????????</button>
	<!--
	<form action="/update" target="hidden-form">
		????????????????????????????????????????????????<input type="text" value=%update_server% onclick="submitMessage()">
	</form><br>
	-->
	<iframe name="hidden-form" style="display:none"></iframe>
</body></html>)rawliteral";

void notFound(AsyncWebServerRequest *request){
	request->send(404, "text/plain", "404 Not found!");
}

String processor(const String& var){
	if(var == "SSID"){
		return WiFi.SSID();
	}
	if(var == "wifi_passwd"){
		return WiFi.psk();
	}
	if(var == "maxPower"){
		return String(maxPower);
	}
	if(var == "currentPower"){
		return String(power);
	}
	if(var == "powerledPin_rate"){
		return String(powerledPin_rate);
	}
	// if(var == "update_server"){
	// 	return String(update_server);
	// }
	// if(var == "backlightLevel"){
	// 	return String(tm1637.getBacklight());
	// }
	return String();
}

void blinkled() {
	digitalWrite(ledPin, LOW);
	delay(200);
	digitalWrite(ledPin, HIGH);
	delay(200);
}

void resetWiFi(){
	if(digitalRead(resetButton) == 1){
		Serial.println("??????????????????");
		for(int i = 1; i <= 2; i++){
			digitalWrite(ledPin, LOW);
			digitalWrite(ledPin, LOW);
			delay(100);
			digitalWrite(ledPin, HIGH);
			digitalWrite(ledPin, HIGH);
			delay(100);
		}
		clearConfig();
		delay(1000);
	}
}

void saveParams(){
	// save params
	EEPROM.begin(2048);
	EEPROM.write(1025, maxPower / 50);
	EEPROM.write(1026, powerledPin_rate / 5);
	EEPROM.commit();

}

void loadParams(){
	// load params
	EEPROM.begin(2048);
	maxPower = EEPROM.read(1025) * 50;
	powerledPin_rate = EEPROM.read(1026) * 5;
	Serial.println("loadParams:");
	Serial.print("maxPower: ");
	Serial.println(maxPower);
	Serial.print("powerledPin_rate: ");
	Serial.println(powerledPin_rate);

}

void smartConfig() {
	// ??????flash?????????????????????
	WifiPwd* wifipwd = loadConfigs();
	if (strlen(wifipwd->pwd) > 0 ) {
		Serial.println("\n\ruse password connect wifi");
		// ??????????????????????????????,?????????????????????????????????wifi
		WiFi.mode(WIFI_STA);
		WiFi.begin(wifipwd->ssid, wifipwd->pwd);
		while (WiFi.status() != WL_CONNECTED) {
			// ??????wifi??????????????????
			blinkled();
			resetWiFi();
		}
		// ????????????
		delete wifipwd;
		// ??????????????????
		WiFi.setAutoConnect(true);
	} else {
		// ??????smartconfig
		Serial.println("wait smart config wifi");
		WiFi.beginSmartConfig();
		while(1) {
			// blinkled();
			yield();
			if (WiFi.smartConfigDone()) {
				WifiPwd *wifipwd  = new WifiPwd;
				strcpy(wifipwd->ssid, WiFi.SSID().c_str());
				strcpy(wifipwd->pwd, WiFi.psk().c_str());
				Serial.printf("SSID : %s\r\n", WiFi.SSID().c_str() );
				Serial.printf("PassWord : %s\r\n", WiFi.psk().c_str() );
				// ??????wifi????????????
				saveConfig(wifipwd);
				// ??????????????????
				// WiFi.setAutoConnect(true);
				break;
			}
		}
	}
	Serial.println("IP: ");
	Serial.println(WiFi.localIP());
}




void reconnect(){
	if(WiFi.status() != WL_CONNECTED) {
		// ??????wifi?????????????????????
		WiFi.reconnect();
		while(WiFi.status() != WL_CONNECTED) {
			delay(500);
			Serial.println("Connecting to WiFi..");
			blinkled();
			if(digitalRead(resetButton) == 1){
				clearConfig();
				Serial.println("??????????????????");
			}
		}
		Serial.println("Connected to the WiFi network");
	}
	
	while(!client.connected()) {
		String client_id = "esp8266-client-";
		client_id += String(WiFi.macAddress());
		Serial.printf("The client %s connects to the public mqtt broker\n", client_id.c_str());
		if(client.connect(client_id.c_str())) {
			Serial.println("Public emqx mqtt broker connected");
			// client.publish(topic, "hello world");
		}else{
			Serial.print("failed with state ");
			Serial.print(client.state());
			delay(2000);
		}
	}
}

void checkUpdate(){
	HTTPClient httpClient;
	httpClient.begin(espClient, "http://update.sy-blog.moe/meter/");
	int httpCode = httpClient.GET();
	if(httpCode == 200) {
		String newFWVersion = httpClient.getString();

		Serial.print("Current firmware version: ");
		Serial.println(FW_VERSION);
		Serial.print("Available firmware version: ");
		Serial.println(newFWVersion);

		int newVersion = newFWVersion.toInt();

		if(newVersion > FW_VERSION) {
			Serial.println("Preparing to update");
			String fwImageURL = "/meter/" + newFWVersion + ".bin";

			displayPower(9999);
			ESPhttpUpdate.rebootOnUpdate(true);
			t_httpUpdate_return ret = ESPhttpUpdate.update(espClient, "update.sy-blog.moe", 80, fwImageURL);
			
			switch(ret){
			    case HTTP_UPDATE_FAILED:
			        Serial.println("[update] Update failed.");
			        break;
			    case HTTP_UPDATE_NO_UPDATES:
			        Serial.println("[update] Update no Update.");
			        break;
			    case HTTP_UPDATE_OK:
			        Serial.println("[update] Update ok."); // may not be called since we reboot the ESP
			        break;
			}
		}
		else{
			Serial.println("Already on latest version");
		}
	}else{
		Serial.print("Firmware version check failed, got HTTP response code ");
		Serial.println(httpCode);
	}
	httpClient.end();
}

void displayPower(int power){
	int LCDprint[4];
	LCDprint[0] = power % 10;
	LCDprint[1] = power / 10 % 10;
	LCDprint[2] = power / 100 % 10;
	LCDprint[3] = power / 1000 % 10;
	
	tm1637.display(0, LCDprint[3]);
	tm1637.display(1, LCDprint[2]);
	tm1637.display(2, LCDprint[1]);
	tm1637.display(3, LCDprint[0]);
}

void setup() {
	Serial.begin(9600);
	Serial.println("Hello, World!");
	// I/O
	pinMode(buzzerPin, OUTPUT);
	digitalWrite(buzzerPin, LOW);
	pinMode(ledPin, OUTPUT);
	digitalWrite(ledPin, LOW);
	pinMode(5, INPUT);
	pinMode(resetButton, INPUT);
	// digitalWrite(sensor_VccPin, HIGH);

	loadParams();
	
	tm1637.init();
	tm1637.set(BRIGHT_TYPICAL);//BRIGHT_TYPICAL = 2,BRIGHT_DARKEST = 0,BRIGHTEST = 7;



///////////////////////////////////
	// Network INIT
	resetWiFi();
	smartConfig();


	checkUpdate();

///////////////////////////////////
	// MQTT INIT
	// ??????MQTT?????????
	client.setServer(mqtt_broker, mqtt_port);
	while (!client.connected()) {
		String client_id = "esp8266-client-";
		client_id += String(WiFi.macAddress());
		Serial.printf("The client %s connects to the public mqtt broker\n", client_id.c_str());
		if (client.connect(client_id.c_str())) {
			Serial.println("Public emqx mqtt broker connected");
		} else {
			Serial.print("failed with state ");
			Serial.print(client.state());
			delay(2000);
		}
	}



///////////////////////////////////
	//web page
	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
		request->send_P(200, "text/html", index_html, processor);
	});
	server.on("/setting", HTTP_GET, [](AsyncWebServerRequest *request){
		if(request->hasParam("maxPower")){
			maxPower = atoi(request->getParam("maxPower")->value().c_str());
			request->send(200, "text/plain", "success");
			Serial.println("set maxPower: " + String(maxPower));
		}
		if(request->hasParam("powerledPin_rate")){
			powerledPin_rate = atoi(request->getParam("powerledPin_rate")->value().c_str());
			request->send(200, "text/plain", "success");
			Serial.println("set powerledPin_rate: " + String(powerledPin_rate));
		}
		saveParams();
		
	});

	server.onNotFound(notFound);
	server.begin();

///////////////////////////////////
	// Auto Setting
	// lcd.print("setting...");
	Serial.println("setting");
	displayPower(0000);
	int light_a, light_b;
	while(1){
		light_a = analogRead(powerledPin);
		// Serial.println(light_a);
		delay(50);
		light_b = analogRead(powerledPin);
		if(light_b - light_a >= 50){
			powerledPin_on = light_b;
			powerledPin_off = light_a;
			powerledPin_average = (powerledPin_on + powerledPin_off) / 2;

			Serial.print("powerledPin_on is ");
			Serial.println(powerledPin_on);

			// lcd.setCursor(0,1);
			// lcd.print("success");
			delay(1000);
			// lcd.clear();

			// lcd.setCursor(0,0);
			// lcd.print("on:");
			// lcd.print(powerledPin_on);
			// lcd.setCursor(0,1);
			// lcd.print("off:");
			// lcd.print(powerledPin_off);
			
			for(int i = 1; i <= 5; i++){
				digitalWrite(ledPin, LOW);
				delay(100);
				digitalWrite(ledPin, HIGH);
				delay(100);
			}
			
			delay(1000);
			// lcd.clear();

			break;
		}
		resetWiFi();
	}

}




void loop(){
	
	light1 = analogRead(A0);
	if(light1 > powerledPin_average){
		// led on
		if(timeA == 0){
			timeA = micros();
		}else{
			timeB = micros();
			// ?????????????????????????????????
			time_light = timeB - timeA;
			power = 1. / powerledPin_rate * 1000 * 3600 * 1000000 / time_light;

			timeA = timeB;

			Serial.println(power);
			sprintf(message, "%.1f", power);
			reconnect();
			client.publish(topic, message);
			delay(200);

			displayPower(power);
			
			if(power >= maxPower){
				if(!beep){
					if(millis() - beepP >= beep_delay){
						beep = true;
						digitalWrite(buzzerPin, HIGH);
						beepP = millis();
					}
				}else{
					if(millis() - beepP >= beep_delay){
						beep = false;
						digitalWrite(buzzerPin, LOW);
					}
				}

			}else{
				beep = false;
				digitalWrite(buzzerPin, LOW);
			}
		}
		
	}

	resetWiFi();
	

	delay(10);

}
