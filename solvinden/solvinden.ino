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
 *              ║IO26                     IO17║ NEOPIXEL
 *              ║IO27                     IO16║
 *              ║IO14                      IO4║
 *              ║IO12                      IO0║ +++, BUTTON
 *              ╚═════════════════════════════╝
 */

float angle = 0;

#define REQUIRED VERSION(1, 5, 0) // Required HomeSpan version
#define RGBW	 true			  // true = RGBW, false = RGB

#include "HomeSpan.h"
#include "extras/Pixel.h"
#include <AsyncElegantOTA.h>
#include <ESPAsyncWebServer.h>

#if defined(CONFIG_IDF_TARGET_ESP32)

#if RGBW
// Data from gpio 16 is going through the level shifter to increase the control voltage to 5V
#define NEOPIXEL_PIN 16
#else
// Data from gpio 17 is directly connected to the neopixel, fine for RGB strips
#define NEOPIXEL_PIN 17
#endif

#elif defined(CONFIG_IDF_TARGET_ESP32S2)

#define NEOPIXEL_PIN 38

#elif defined(CONFIG_IDF_TARGET_ESP32C3)

#define NEOPIXEL_PIN 2

#endif

// clang-format off
CUSTOM_CHAR(RainbowEnabled, 00000001-0001-0001-0001-46637266EA00, PR + PW + EV, BOOL, 0, 0, 1, false);
// clang-format on

AsyncWebServer server(80);

struct Pixel_Strand : Service::LightBulb { // Addressable RGBW Pixel Strand of nPixel Pixels

	struct SpecialEffect {
		Pixel_Strand *px;
		const char	 *name;

		SpecialEffect(Pixel_Strand *px, const char *name) {
			this->px   = px;
			this->name = name;
			Serial.printf("Adding Effect %d: %s\n", px->Effects.size() + 1, name);
		}

		virtual void	 init() {}
		virtual uint32_t update() { return (60000); }
		virtual int		 requiredBuffer() { return (0); }
	};

	Characteristic::On			   power{0, true};
	Characteristic::Hue			   H{0, true};
	Characteristic::Saturation	   S{100, true};
	Characteristic::Brightness	   V{100, true};
	Characteristic::RainbowEnabled rainbow{false, true};

	vector<SpecialEffect *> Effects;

	Pixel		 *pixel;
	int			  nPixels;
	Pixel::Color *colors;
	uint32_t	  alarmTime;

	Pixel_Strand(int pin, int nPixels) : Service::LightBulb() {

		pixel		  = new Pixel(pin, RGBW); // creates RGB/RGBW pixel LED on specified pin using default timing parameters suitable for most SK68xx LEDs
		this->nPixels = nPixels;			  // store number of Pixels in Strand

		Effects.push_back(new ManualControl(this));
		Effects.push_back(new Rainbow(this));

		rainbow.setUnit(""); // configures custom "RainbowEnabled" characteristic for use with Eve HomeKit
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

	void
	loop() override {

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

#if RGBW
			if (h == 30) {
				px->pixel->set(
					Pixel::Color().HSV(h, s, 0, v), px->nPixels);
			} else {
				px->pixel->set(
					Pixel::Color().HSV(h, s, v), px->nPixels);
			}
#else
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

struct DEV_Switch : Service::Switch {

	int					ledPin; // relay pin
	SpanCharacteristic *power;

	// Constructor
	DEV_Switch(int ledPin) : Service::Switch() {
		power		 = new Characteristic::On(0, true);
		this->ledPin = ledPin;
		pinMode(ledPin, OUTPUT);

		digitalWrite(ledPin, power->getVal());
	}

	// Override update method
	boolean update() {
		digitalWrite(ledPin, power->getNewVal());

		return (true);
	}
};

///////////////////////////////

void setup() {

	Serial.begin(115200);

	homeSpan.setSketchVersion("1.0.0");	  // set sketch version
	homeSpan.setLogLevel(0);			  // set log level to 0 (no logs)
	homeSpan.setStatusPin(32);			  // set the status pin to GPIO32
	homeSpan.setStatusAutoOff(10);		  // disable led after 10 seconds
	homeSpan.setWifiCallback(setupWeb);	  // Set the callback function for wifi events
	homeSpan.reserveSocketConnections(5); // reserve 5 socket connections for Web Server
	homeSpan.setControlPin(0);			  // set the control pin to GPIO0
	homeSpan.setPortNum(81);			  // set the port number to 81
	homeSpan.enableAutoStartAP();		  // enable auto start of AP

	homeSpan.begin(Category::Lighting, "SOLVINDEN");

	new SpanAccessory(1);
	new Service::AccessoryInformation();
	new Characteristic::Name("SOLVINDEN");
	new Characteristic::Manufacturer("HomeSpan");
	new Characteristic::SerialNumber("123-ABC");
	new Characteristic::Model("NeoPixel RGB LEDs");
	new Characteristic::FirmwareRevision("1.0");
	new Characteristic::Identify();

	new Service::HAPProtocolInformation();
	new Characteristic::Version("1.1.0");

	new Pixel_Strand(NEOPIXEL_PIN, 9);
}

///////////////////////////////

void loop() {
	homeSpan.poll();
}

///////////////////////////////

void setupWeb() {
	server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
		String content = "<html><body>Rebooting!  Will return to configuration page in 10 seconds.<br><br>";
		content += "<meta http-equiv = \"refresh\" content = \"10; url = /\" />";
		request->send(200, "text/html", content);

		ESP.restart();
	});

	AsyncElegantOTA.begin(&server); // Start AsyncElegantOTA
	server.begin();
	LOG1("HTTP server started");
} // setupWeb
