/*********************************************************************************
 *  MIT License
 *
 *  Copyright (c) 2022 Gregg E. Berman
 *
 *  https://github.com/HomeSpan/HomeSpan
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 ********************************************************************************/

/*
 *                ESP-WROOM-32 Utilized pins
 *              ╔═════════════════════════════╗
 *              ║┌─┬─┐  ┌──┐  ┌─┐             ║
 *              ║│ | └──┘  └──┘ |             ║
 *              ║│ |            |             ║
 *              ╠═════════════════════════════╣
 *          +++ ║GND                       GND║ +++
 *          +++ ║3.3V                     IO23║ USED_FOR_NOTHING
 *              ║                         IO22║
 *              ║IO36                      IO1║ TX
 *              ║IO39                      IO3║ RX
 *              ║IO34                     IO21║
 *              ║IO35                         ║ NC
 *      RED_LED ║IO32                     IO19║
 *              ║IO33                     IO18║ RELAY
 *              ║IO25                      IO5║
 *              ║IO26                     IO17║ NEOPIXEL_RGB
 *              ║IO27                     IO16║ NEOPIXEL_RGBW
 *              ║IO14                      IO4║
 *              ║IO12                      IO0║ +++, BUTTON
 *              ╚═════════════════════════════╝
 */

float angle = 0;

#define REQUIRED VERSION(1, 6, 0) // Required HomeSpan version

#include "HomeSpan.h"
#include "OTA.hpp"
#include "extras/Pixel.h"
#include <ElegantOTA.h>
#include <WebServer.h>
#include <WiFiClient.h>

#if defined(CONFIG_IDF_TARGET_ESP32)

	#if MODE == 0 || MODE == 2
	    // Data from gpio 16 is going through the level shifter to increase the control voltage to 5V
		#define NEOPIXEL_PIN 16
		#define RGBW         1
	#elif MODE == 1
	    // Data from gpio 17 is directly connected to the neopixel, fine for RGB strips
		#define NEOPIXEL_PIN 17
		#define RGBW         0
	#endif

#elif defined(CONFIG_IDF_TARGET_ESP32S2)

	#define NEOPIXEL_PIN 38

#elif defined(CONFIG_IDF_TARGET_ESP32C3)

	#define NEOPIXEL_PIN 2

#endif

// clang-format off
CUSTOM_CHAR(Toggle, 00000001-0001-0001-0001-46637266EA00, PR + PW + EV, BOOL, 0, 0, 1, false);
CUSTOM_CHAR(Toggle2, 00000002-0001-0001-0001-46637266EA00, PR + PW + EV, BOOL, 0, 0, 1, false);

// clang-format on

WebServer server(80);

void setupWeb();

char sNumber[18] = "11:11:11:11:11:11";

struct Pixel_Strand : Service::LightBulb { // Addressable RGBW Pixel Strand of nPixel Pixels

	struct SpecialEffect {
		Pixel_Strand *px;
		const char   *name;

		SpecialEffect(Pixel_Strand *px, const char *name) {
			this->px   = px;
			this->name = name;
			Serial.printf("Adding Effect %d: %s\n", px->Effects.size() + 1, name);
		}

		virtual void     init() {}
		virtual uint32_t update() { return (60000); }
		virtual int      requiredBuffer() { return (0); }
	};

	Characteristic::On         power{0, true};
	Characteristic::Hue        H{0, true};
	Characteristic::Saturation S{100, true};
	Characteristic::Brightness V{100, true};
	Characteristic::Toggle     rainbow{false, true};

	vector<SpecialEffect *> Effects;

	Pixel        *pixel;
	int           nPixels;
	Pixel::Color *colors;
	uint32_t      alarmTime;

	Pixel_Strand(int pin, int nPixels) : Service::LightBulb() {

		pixel         = new Pixel(pin, RGBW); // creates RGB/RGBW pixel LED on specified pin using default timing parameters suitable for most SK68xx LEDs
		this->nPixels = nPixels;              // store number of Pixels in Strand

		Effects.push_back(new ManualControl(this));
		Effects.push_back(new Rainbow(this));

		rainbow.setUnit(""); // configures custom "Toggle" characteristic for use with Eve HomeKit
		rainbow.setDescription("Rainbow Animation");

		V.setRange(5, 100, 1); // sets the range of the Brightness to be from a min of 5%, to a max of 100%, in steps of 1%

		int bufSize = 0;

		for (int i = 0; i < Effects.size(); i++)
			bufSize = Effects[i]->requiredBuffer() > bufSize ? Effects[i]->requiredBuffer() : bufSize;

		colors = (Pixel::Color *)calloc(bufSize, sizeof(Pixel::Color)); // storage for dynamic pixel pattern

		Serial.printf("\nConfigured Pixel_Strand on pin %d with %d pixels and %d effects.  Color buffer = %d pixels\n\n", pin, nPixels, Effects.size(), bufSize);

		update();
	}

	boolean update() override {

		if (!power.getNewVal()) {
			pixel->set(Pixel::Color().RGB(0, 0, 0, 0), this->nPixels);
		} else if (!rainbow.getNewVal()) {
			Effects[0]->init();
			if (rainbow.updated())
				Serial.printf("Effect changed to: %s\n", Effects[0]->name);
		} else {
			Effects[1]->init();
			alarmTime = millis() + Effects[1]->update();
			if (rainbow.updated())
				Serial.printf("Effect changed to: %s\n", Effects[1]->name);
		}
		return (true);
	}

	void loop() override {

		if (millis() > alarmTime && power.getVal())
			if (rainbow.getVal()) {
				alarmTime = millis() + Effects[1]->update();
			}
	}

	//////////////

	struct ManualControl : SpecialEffect {

		ManualControl(Pixel_Strand *px) : SpecialEffect{px, "Manual Control"} {}

		void init() override {

			float h = px->H.getNewVal();
			float s = px->S.getNewVal();
			float v = px->V.getNewVal();

#if MODE == 0
			if (h == 30) {
				px->pixel->set(
				    Pixel::Color().HSV(h, s, 0, v), px->nPixels);
			} else {
				px->pixel->set(
				    Pixel::Color().HSV(h, s, v), px->nPixels);
			}
#elif MODE == 1
			px->pixel->set(
			    Pixel::Color().HSV(h, s, v), px->nPixels);
#endif
		}
	};

	//////////////
	struct Rainbow : SpecialEffect {

		int8_t *dir;

		Rainbow(Pixel_Strand *px) : SpecialEffect{px, "Rainbow"} {
			dir = (int8_t *)calloc(px->nPixels, sizeof(int8_t));
		}

		void init() override {
			for (int i = 0; i < px->nPixels; i++) {
				px->colors[i].RGB(0, 0, 0, 0);
				dir[i] = 0;
			}
		}

		uint32_t update() override {
			float value = px->V.getNewVal<float>();
			for (int i = 0; i < px->nPixels; i++) {
				px->colors[i] = Pixel::Color().HSV(angle, 100, value);
			}
			px->pixel->set(px->colors, px->nPixels);
			angle++;
			if (angle == 360) angle = 0;
			return (100);
		}

		int requiredBuffer() override { return (px->nPixels); }
	};
};

struct NeoPixel_RGBW : Service::LightBulb { // Addressable single-wire RGBW LED Strand (e.g. NeoPixel)

	Characteristic::On         power{0, true};
	Characteristic::Brightness V{100, true};
	Characteristic::Toggle     goUp{false, true};
	Characteristic::Toggle2    goDown{false, true};
	Pixel                     *pixel;
	uint8_t                    nPixels;
	uint32_t                   alarmTime;
	uint8_t                    fadeTime = 2; // in minutes

	// TODO max brightness val characteristic
	// TODO fade time

	NeoPixel_RGBW(uint8_t pin, uint8_t nPixels) : Service::LightBulb() {

		goUp.setUnit("");
		goUp.setDescription("Start slow brightness up");

		goDown.setUnit("");
		goDown.setDescription("Start slow brightness down");

		V.setRange(1, 100, 1);                // sets the range of the Brightness to be from a min of 5%, to a max of 100%, in steps of 1%
		pixel         = new Pixel(pin, true); // creates Pixel RGBW LED (second parameter set to true for RGBW) on specified pin
		this->nPixels = nPixels;              // save number of Pixels in this LED Strand
		update();                             // manually call update() to set pixel with restored initial values
	}

	boolean update() override {

		int   p = power.getNewVal();
		float v = V.getNewVal<float>();                    // range = [0,100]
		pixel->set(pixel->HSV(0, 100, 0, v * p), nPixels); // sets all nPixels to the same HSV color (note use of static method pixel->HSV, instead of defining and setting Pixel::Color)

		if (p == 0 && goUp.getVal() == 1) {
			goUp.setVal(0);
		}

		if (p == 0 && goDown.getVal() == 1) {
			goDown.setVal(0);
		}

		// TODO case when power is off and goDown gets on

		if (goDown.getNewVal() == 1 && p == 1) {
			alarmTime = millis() + 2 * 1000;
		}

		if (goUp.getNewVal() == 1) {

			// if power is on continue from this level,
			// otherwise, start from 0
			if (power.getVal() == 0)
				V.setVal(0);

			alarmTime = millis() + 2 * 1000;
		}

		return (true);
	}

	void loop() override {

		if (millis() > alarmTime && goUp.getVal()) {

			// if power is off, turn it on
			if (power.getVal() == 0)
				power.setVal(1);

			int brightness = V.getVal();
			brightness++;
			pixel->set(pixel->HSV(0, 100, 0, brightness), nPixels);
			V.setVal(brightness);

			// when max val is reached turn off
			if (brightness == 100)
				goUp.setVal(0);

			LOG1("Brightness up to %d", brightness);
			LOG1("\n");
			alarmTime = millis() + 2 * 1000;
		}

		if (millis() > alarmTime && goDown.getVal() && power.getVal()) {

			int brightness = V.getVal();
			if (brightness > 0) {
				brightness--;
				pixel->set(pixel->HSV(0, 100, 0, brightness), nPixels);
				V.setVal(brightness);

				// when max val is reached turn off
				if (brightness == 1) {
					goDown.setVal(0);
					power.setVal(0);
					pixel->set(pixel->HSV(0, 100, 0, 0), nPixels);
				}

				LOG1("Brightness down to %d", brightness);
				LOG1("\n");
				alarmTime = millis() + 2 * 1000;
			}
		}
	}
};

NeoPixel_RGBW *STRIP;

///////////////////////////////

void setup() {

	Serial.begin(115200);

	Serial.print("Active firmware version: ");
	Serial.println(FirmwareVer);

	String mode;
#if MODE == 0
	mode = "-RGBW ";
#elif MODE == 1
	mode = "-RGB ";
#elif MODE == 2
	mode = "-WHITE ";
#endif

	String     temp           = FW_VERSION;
	const char compile_date[] = __DATE__ " " __TIME__;
	char      *fw_ver         = new char[temp.length() + 30];
	strcpy(fw_ver, temp.c_str());
	strcat(fw_ver, mode.c_str());
	strcat(fw_ver, " (");
	strcat(fw_ver, compile_date);
	strcat(fw_ver, ")");

	for (int i = 0; i < 17; ++i) // we will iterate through each character in WiFi.macAddress() and copy it to the global char sNumber[]
	{
		sNumber[i] = WiFi.macAddress()[i];
	}
	sNumber[17] = '\0'; // the last charater needs to be a null

	homeSpan.setSketchVersion("1.0.0");                             // set sketch version
	homeSpan.setLogLevel(1);                                        // set log level to 0 (no logs)
	homeSpan.setStatusPin(32);                                      // set the status pin to GPIO32
	homeSpan.setStatusAutoOff(10);                                  // disable led after 10 seconds
	homeSpan.setWifiCallback(setupWeb);                             // Set the callback function for wifi events
	homeSpan.reserveSocketConnections(5);                           // reserve 5 socket connections for Web Server
	homeSpan.setControlPin(0);                                      // set the control pin to GPIO0
	homeSpan.setPortNum(88);                                        // set the port number to 81
	homeSpan.enableAutoStartAP();                                   // enable auto start of AP
	homeSpan.enableWebLog(10, "pool.ntp.org", "UTC-2:00", "myLog"); // enable Web Log
	homeSpan.setSketchVersion(fw_ver);

	homeSpan.begin(Category::Lighting, "SOLVINDEN");

	new SpanAccessory(1);
	new Service::AccessoryInformation();
	new Characteristic::Name("SOLVINDEN");
	new Characteristic::Manufacturer("HomeSpan");
	new Characteristic::SerialNumber(sNumber);
#if MODE == 0
	new Characteristic::Model("NeoPixel RGBW LEDs");
#elif MODE == 1
	new Characteristic::Model("NeoPixel RGB LEDs");
#elif MODE == 2
	new Characteristic::Model("NeoPixel WHITE LEDs");
#endif
	new Characteristic::FirmwareRevision(temp.c_str());
	new Characteristic::Identify();

	new Service::HAPProtocolInformation();
	new Characteristic::Version("1.1.0");

#if MODE == 0 || MODE == 1
	new Pixel_Strand(NEOPIXEL_PIN, 9);
#elif MODE == 2
	STRIP = new NeoPixel_RGBW(NEOPIXEL_PIN, 9);

	// LOG0("Adding Accessory: Switch\n");

	// new SpanAccessory(2);
	// new Service::AccessoryInformation();
	// new Characteristic::Name("Switch");
	// new Characteristic::Identify();
	// SWITCH = new DEV_Switch();
#endif
}

///////////////////////////////

void loop() {
	homeSpan.poll();
	server.handleClient();
	repeatedCall();
}

///////////////////////////////

void setupWeb() {
	server.on("/metrics", HTTP_GET, []() {
		double uptime       = esp_timer_get_time() / (6 * 10e6);
		double heap         = esp_get_free_heap_size();
		String uptimeMetric = "# HELP uptime Solvinden uptime\nhomekit_uptime{device=\"solvinden\",location=\"home\"} " + String(int(uptime));
		String heapMetric   = "# HELP heap Available heap memory\nhomekit_heap{device=\"solvinden\",location=\"home\"} " + String(int(heap));

		Serial.println(uptimeMetric);
		Serial.println(heapMetric);
		server.send(200, "text/plain", uptimeMetric + "\n" + heapMetric);
	});

	server.on("/reboot", HTTP_GET, []() {
		String content = "<html><body>Rebooting!  Will return to configuration page in 10 seconds.<br><br>";
		content += "<meta http-equiv = \"refresh\" content = \"10; url = /\" />";
		server.send(200, "text/html", content);

		ESP.restart();
	});

	ElegantOTA.begin(&server); // Start ElegantOTA
	server.begin();
	LOG1("HTTP server started");
} // setupWeb
