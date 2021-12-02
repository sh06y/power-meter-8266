#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "TM1637.h"

// WiFi
const char *ssid = "HUAWEI-WiFi"; // Enter your WiFi name
const char *password = "shyh302088ypjss";  // Enter WiFi password

// MQTT Broker
const char *mqtt_broker = "192.168.0.12";
const char *state_topic = "meter/state";
const char *topic = "home/power";
// const char *mqtt_username = "emqx";
// const char *mqtt_password = "public";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);


// port
#define powerledPin A0
const int ledPin = 2;
const int buzzerPin = D6;
#define CLK D2
#define DIO D3

TM1637 tm1637(CLK,DIO);

// 变量
const int powerledPin_rate = 800;// imp/kWh
const int maxPower = 6500;
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

void reconnect(){
	while (!client.connected()) {
		String client_id = "esp8266-client-";
		client_id += String(WiFi.macAddress());
		Serial.printf("The client %s connects to the public mqtt broker\n", client_id.c_str());
		if (client.connect(client_id.c_str())) {
			Serial.println("Public emqx mqtt broker connected");
			// client.publish(topic, "hello world");
		} else {
			Serial.print("failed with state ");
			Serial.print(client.state());
			delay(2000);
		}
	}
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

	// I/O
	pinMode(buzzerPin, OUTPUT);
	digitalWrite(buzzerPin, LOW);
	pinMode(ledPin, OUTPUT);
	digitalWrite(ledPin, LOW);
	pinMode(5, OUTPUT);
	digitalWrite(5, HIGH);
	// pinMode(sensor_VccPin, OUTPUT);
	// digitalWrite(sensor_VccPin, HIGH);
	tm1637.init();
	tm1637.set(BRIGHT_TYPICAL);//BRIGHT_TYPICAL = 2,BRIGHT_DARKEST = 0,BRIGHTEST = 7;



///////////////////////////////////
	// Network INIT
	// connecting to a WiFi network
	WiFi.begin(ssid, password);
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.println("Connecting to WiFi..");
		digitalWrite(ledPin, HIGH);
		delay(100);
		digitalWrite(ledPin, LOW);
		delay(100);
	}
	digitalWrite(ledPin, HIGH);
	Serial.println("Connected to the WiFi network");
	//connecting to a mqtt broker
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

	// client.publish(topic, "hello world");

	// for(int i = 1; i <= 100; i++){
	//  // sprintf(a, "%d", i);
	//  sprintf(message, "%d", i);
	//  client.publish(topic, message);
		
	//  delay(500);
	// }


	ArduinoOTA.setHostname("power-meter");
	// ArduinoOTA.setPassword("12345678");

	ArduinoOTA.onStart([]() {
	String type;
	if (ArduinoOTA.getCommand() == U_FLASH) {
		type = "sketch";
	} else { // U_FS
		type = "filesystem";
	}

	// NOTE: if updating FS this would be the place to unmount FS using FS.end()
	Serial.println("Start updating " + type);
	});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nEnd");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
	});
		ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) {
			Serial.println("Auth Failed");
		} else if (error == OTA_BEGIN_ERROR) {
			Serial.println("Begin Failed");
		} else if (error == OTA_CONNECT_ERROR) {
			Serial.println("Connect Failed");
		} else if (error == OTA_RECEIVE_ERROR) {
			Serial.println("Receive Failed");
		} else if (error == OTA_END_ERROR) {
			Serial.println("End Failed");
		}
	});
	ArduinoOTA.begin();



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

	}

}




void loop(){
	
	// yield();
	while(1){
		ArduinoOTA.handle();

		light1 = analogRead(A0);
		if(light1 > powerledPin_average){
			// led on
			timeA = micros();
			// powerSupply_test();
			delay(300);
			break;
		}
		yield();
		client.loop();
	}
	

	// 计算两次亮灯之间的间隙
	time_light = timeA - timeB;

	power = 1. / powerledPin_rate * 1000 * 3600 * 1000000 / time_light;

	Serial.println(power);
	sprintf(message, "%.1f", power);
	reconnect();
	client.publish(topic, message);
	delay(200);
	// lcd.clear();
	// lcd.setCursor(0,0);
	// lcd.print(power);

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
	// Serial.println(beep);





	
	// yield();
	while(1){
		ArduinoOTA.handle();

		light1 = analogRead(A0);
		if(light1 > powerledPin_average){
			// led on
			timeB = micros();
			// powerSupply_test();
			delay(300);
			break;
		}
		yield();
		client.loop();
	}
	// client.loop();
	
	// 计算两次亮灯之间的间隙
	time_light = timeB - timeA;

	power = 1. / powerledPin_rate * 1000 * 3600 * 1000 * 1000 / time_light;

	Serial.println(power);
	sprintf(message, "%.1f", power);
	reconnect();
	client.publish(topic, message);
	delay(200);
	// lcd.clear();
	// lcd.setCursor(0,0);
	// lcd.print(power);

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
