#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "TM1637.h"
#include "smartconfig.h"
#include <ESPAsyncWebServer.h>
#include <ESPAsyncTCP.h>

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

// 变量
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
	<title>控制面板</title>
	<meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
	<meta name="viewport" content="width=device-width, initial-scale=1">
	<script>
		function submitMessage() {
			alert("已保存");
			// setTimeout(function(){ document.location.reload(false); }, 500);   
		}
		function submit_network() {
			alert("已保存，在重启后应用生效");
			// setTimeout(function(){ document.location.reload(false); }, 500);   
		}
	</script></head>
	<body>
	<h1>控制面板</h1>

	<p>当前功率：%currentPower%W</p>

	<h2>WiFi设置</h2>
	<form action="/wifi" target="hidden-form">
		SSID: <input type="text" name="SSID" value=%SSID%><br>
		密码: <input type="text" name="wifi_passwd" value=%wifi_passwd%><br>
		<input type="submit" value="保存" onclick="submit_network()">
	</form><br>

	<h2>设置</h2>
	<form action="/setting" target="hidden-form">
		报警阈值(W): <input type="number" name="maxPower" value=%maxPower%><br>
		脉冲频率(imp/kWh): <input type="number" name="powerledPin_rate" value=%powerledPin_rate%><br>
		<!-- 屏幕亮度(1-7): <input type="number" name="backlightLevel", value=%backlightLevel%> -->
		<br>
		<input type="submit" value="保存" onclick="submitMessage()">
	</form><br>
	

	<iframe style="display:none" name="hidden-form"></iframe>
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
		Serial.println("重置网络成功");
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
	// 先从flash中加载账号密码
	WifiPwd* wifipwd = loadConfigs();
	if (strlen(wifipwd->pwd) > 0 ) {
		Serial.println("\n\ruse password connect wifi");
		// 如果说有账号密码信息,那就直接用账号密码连接wifi
		WiFi.mode(WIFI_STA);
		WiFi.begin(wifipwd->ssid, wifipwd->pwd);
		while (WiFi.status() != WL_CONNECTED) {
		  // 直到wifi连接成功为止
			blinkled();
			resetWiFi();
		}
		// 删除内存
		delete wifipwd;
		// 设置自动连接
		WiFi.setAutoConnect(true);
	} else {
		// 进入smartconfig
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
				// 记住wifi账号密码
				saveConfig(wifipwd);
				// 设置自动连接
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
		// 如果wifi断开的话就重连
		WiFi.reconnect();
		while(WiFi.status() != WL_CONNECTED) {
			delay(500);
			Serial.println("Connecting to WiFi..");
			blinkled();
			if(digitalRead(resetButton) == 1){
				clearConfig();
				Serial.println("重置网络成功");
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

///////////////////////////////////
	// MQTT INIT
	// 连接MQTT服务器
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

	// ArduinoOTA.setHostname("power-meter");
	// // ArduinoOTA.setPassword("12345678");

	// ArduinoOTA.onStart([]() {
	// String type;
	// if (ArduinoOTA.getCommand() == U_FLASH) {
	// 	type = "sketch";
	// } else { // U_FS
	// 	type = "filesystem";
	// }

	// // NOTE: if updating FS this would be the place to unmount FS using FS.end()
	// Serial.println("Start updating " + type);
	// });
	// ArduinoOTA.onEnd([]() {
	// 	Serial.println("\nEnd");
	// });
	// ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
	// 	Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
	// });
	// 	ArduinoOTA.onError([](ota_error_t error) {
	// 	Serial.printf("Error[%u]: ", error);
	// 	if (error == OTA_AUTH_ERROR) {
	// 		Serial.println("Auth Failed");
	// 	} else if (error == OTA_BEGIN_ERROR) {
	// 		Serial.println("Begin Failed");
	// 	} else if (error == OTA_CONNECT_ERROR) {
	// 		Serial.println("Connect Failed");
	// 	} else if (error == OTA_RECEIVE_ERROR) {
	// 		Serial.println("Receive Failed");
	// 	} else if (error == OTA_END_ERROR) {
	// 		Serial.println("End Failed");
	// 	}
	// });
	// ArduinoOTA.begin();

///////////////////////////////////
	//web page
	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
		request->send_P(200, "text/html", index_html, processor);
	});
	server.on("/setting", HTTP_GET, [](AsyncWebServerRequest *request){
		if(request->hasParam("maxPower")){
			maxPower = atoi(request->getParam("maxPower")->value().c_str());
			Serial.println("set maxPower: " + String(maxPower));
		}
		if(request->hasParam("powerledPin_rate")){
			powerledPin_rate = atoi(request->getParam("powerledPin_rate")->value().c_str());
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
			// 计算两次亮灯之间的间隙
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
