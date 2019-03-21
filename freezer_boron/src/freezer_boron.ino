
// With some code derived from:
// https://build.particle.io/shared_apps/5a1d9e5310c40e5c02001232

// Hardware list - BOM
// https://www.adafruit.com/product/938 Monochrome 1.3" 128x64 OLED graphic display
//		- Solder jumpers to put it in i2c mode
//
// https://www.adafruit.com/product/2652 BME280 I2C or SPI Temperature Humidity Pressure Sensor[ID:2652]
//
// https://www.adafruit.com/product/3263 Universal Thermocouple Amplifier MAX31856
//
// https://store.particle.io/products/boron-lte Paticle Boron


#include <Adafruit_MAX31856.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define ALARM_NO_PIN A0
#define ALARM_NC_PIN A1
#define SPI_SS_MAX31856 A2
#define SPI_SCLK A3
#define SPI_MISO A4
#define SPI_MOSI A5

#define I2C_SDA D0
#define I2C_SCL D1
#define DISPLAY_RESET D2
#define JUMPER_A D3
#define JUMPER_B D4
#define JUMPER_C D5
#define ONE_WIRE D6
#define LED_PIN D7

// public variables
const unsigned long UPDATE_PERIOD_MS = 5000;
unsigned long lastUpdate = 0;
int led_on_state = 0;

double temp_tc = 0;
double temp_tc_cj = 0;
double temp_amb = 0;
double humid_amb = 0;
bool equip_alarm = FALSE;
bool equip_alarm_last = FALSE;
FuelGauge fuel;
double batt_percent = 0;

PMIC pmic;
bool usb_power_last = FALSE;
bool usb_power = TRUE;

bool equip_spec = FALSE;
int equip_t_nom = -1000;
double alarm_temp_min = -1000;
double alarm_temp_max = 1000;
bool low_t_alarm = FALSE;
bool low_t_alarm_last = FALSE;
bool high_t_alarm = FALSE;
bool high_t_alarm_last = FALSE;

double alarm_ambient_t_max = 30.0;
double alarm_ambient_h_max = 75.0;
bool amb_t_alarm = FALSE;
bool amb_t_alarm_last = FALSE;
bool amb_h_alarm = FALSE;
bool amb_h_alarm_last = FALSE;

bool fault_bme = FALSE;
bool fault_thermocouple = FALSE;
bool BMEsensorReady = FALSE;


// Use software SPI: CS, DI, DO, CLK
// Pin labels are CS, SDI, SDO, SCK
// formal labels: SS, MISO, MOSI, SCLK
Adafruit_MAX31856 maxthermo = Adafruit_MAX31856(
	SPI_SS_MAX31856,
	SPI_MISO,
	SPI_MOSI,
	SPI_SCLK );

Adafruit_BME280 bme; // I2C

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET    -1 // Reset pin # (1 if sharing Arduino reset pin)
//Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT,OLED_RESET);
Adafruit_SSD1306 display(128, 64, DISPLAY_RESET);
//char buf[64];


void setup() {

	Particle.variable( "temp_tc", &temp_tc, DOUBLE );
	Particle.variable( "temp_tc_cj", &temp_tc_cj, DOUBLE );
	Particle.variable( "temp_amb", &temp_amb, DOUBLE );
	Particle.variable( "humid_amb", &humid_amb, DOUBLE );
	Particle.variable( "equip_alarm", &equip_alarm, BOOLEAN );
	Particle.variable( "batt_percent", &batt_percent, DOUBLE );
	Particle.variable( "usb_power", &usb_power, BOOLEAN );
	Particle.variable( "equip_spec", &equip_spec, BOOLEAN );
	Particle.variable( "equip_t_nom", &equip_t_nom, INT );
	Particle.variable( "low_t_alarm", &low_t_alarm, BOOLEAN );
	Particle.variable( "amb_t_alarm", &amb_t_alarm, BOOLEAN );
	Particle.variable( "amb_h_alarm", &amb_h_alarm, BOOLEAN );
	Particle.variable( "fault_bme", &fault_bme, BOOLEAN );
	Particle.variable( "fault_tc", &fault_thermocouple, BOOLEAN );

	pinMode( LED_PIN, OUTPUT );

	pinMode( ALARM_NO_PIN, INPUT_PULLUP );
	pinMode( ALARM_NC_PIN, INPUT_PULLUP );

	pinMode( JUMPER_A, INPUT_PULLUP );
	pinMode( JUMPER_B, INPUT_PULLUP );
	pinMode( JUMPER_C, INPUT_PULLUP );

	// Jumpers are used for settings. They are pulled HIGH, so adding a jumper
	// gives a low

	//											D3				D4				D5
	// Nominal temp					JUMPER_A	JUMPER_B	JUMPER_C
	// -70									HIGH			HIGH			HIGH
	// -20									LOW				HIGH			HIGH
	// 4										HIGH			LOW				HIGH
	// 16										LOW 			LOW				HIGH
	// No specification			NA				NA				LOW

	if ( digitalRead( JUMPER_C) == HIGH ){
		equip_spec = TRUE;
	}

	if ( digitalRead( JUMPER_A) == HIGH && digitalRead( JUMPER_B) == HIGH ){
		equip_t_nom = -70;
		alarm_temp_min = -90.0;
		alarm_temp_max = -60.0;
	} else if ( digitalRead( JUMPER_A) == LOW && digitalRead( JUMPER_B) == HIGH ){
		equip_t_nom = -20;
		alarm_temp_min = -27.0;
		alarm_temp_max = -13.0;
	} else if ( digitalRead( JUMPER_A) == HIGH && digitalRead( JUMPER_B) == LOW ){
		equip_t_nom = 4;
		alarm_temp_min = 2;
		alarm_temp_max = 8;
	} else if ( digitalRead( JUMPER_A) == LOW && digitalRead( JUMPER_B) == LOW ){
		equip_t_nom = 16;
		alarm_temp_min = 13;
		alarm_temp_max = 20;
	}

	Serial.begin( 9600 );

	// Start sensors
	pmic.begin();
	maxthermo.begin();
	maxthermo.setThermocoupleType(MAX31856_TCTYPE_K);
	BMEsensorReady = bme.begin();

	if ( ! BMEsensorReady ){
		Particle.publish("FAULT_BME", "BME sensor is not ready", PRIVATE);
		fault_bme = TRUE;
	}

	// Display
	display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
}

void loop() {
	if (millis() - lastUpdate >= UPDATE_PERIOD_MS) {
		// alternate the LED_PIN between high and low
		// to show that we're still alive
		digitalWrite(LED_PIN, (led_on_state) ? HIGH : LOW);
		led_on_state = !led_on_state;


		// Check equipment alarms
		if( (digitalRead(ALARM_NC_PIN) == HIGH) or (digitalRead(ALARM_NO_PIN) == LOW) )
		{
			equip_alarm = TRUE;
		}
		else
		{
			equip_alarm = FALSE;
		}

		if( equip_alarm != equip_alarm_last ){
			equip_alarm_last = equip_alarm;
			if ( equip_alarm ){
				Particle.publish("ALARM_EQIP", "ALARM: Equipment in alarm!!!", PRIVATE);
			}
			else
			{
				Particle.publish("ALARM_EQIP", "CLEAR: Equipment alarm stopped.", PRIVATE);
			}

		}

		// Particle.publish("equip_alarm", String(equip_alarm), PRIVATE);

		// Power state
		batt_percent = fuel.getSoC();
		// Particle.publish("batt_percent", String(batt_percent), PRIVATE);

		usb_power = isUsbPowered();
		//Particle.publish("usb_power", String(usb_power), PRIVATE);
		//Particle.publish("byte_power", String(pmic.getSystemStatus()), PRIVATE);

		if (usb_power != usb_power_last) {
			if (usb_power){
				Particle.publish("ALARM_POWER", "CLEAR: Monitor external power restored.", PRIVATE);
			}
			else
			{
				Particle.publish("ALARM_POWER", "ALARM: No monitor external power!!!", PRIVATE);
			}
			usb_power_last = usb_power;
		}


		// Read Ambient data
		// Reading temperature or humidity takes about 250 milliseconds

		temp_amb = bme.readTemperature(); // degrees C
		humid_amb = bme.readHumidity(); // %


		// Check if any reads failed
		fault_bme = FALSE;
		if (isnan(temp_amb)) {
			Particle.publish("FAULT_BME", "Failed to read from DHT sensor temperature.", PRIVATE);
			fault_bme = TRUE;
		}

		if (isnan(humid_amb)) {
			Particle.publish("FAULT_BME", "Failed to read from DHT sensor humidity.", PRIVATE);
			fault_bme = TRUE;
		}

		// Read thermocouple data
		temp_tc = maxthermo.readThermocoupleTemperature();
		temp_tc_cj = maxthermo.readCJTemperature();

		// Check and print any faults
		fault_thermocouple = FALSE;
		uint8_t fault = maxthermo.readFault();
		if (fault) {
			fault_thermocouple = TRUE;
			if (fault & MAX31856_FAULT_CJRANGE) Particle.publish("FAULT_Thermo", "Cold Junction Range Fault", PRIVATE);
			if (fault & MAX31856_FAULT_TCRANGE) Particle.publish("FAULT_Thermo", "Thermocouple Range Fault", PRIVATE);
			if (fault & MAX31856_FAULT_CJHIGH)  Particle.publish("FAULT_Thermo", "Cold Junction High Fault", PRIVATE);
			if (fault & MAX31856_FAULT_CJLOW)   Particle.publish("FAULT_Thermo", "Cold Junction Low Fault", PRIVATE);
			if (fault & MAX31856_FAULT_TCHIGH)  Particle.publish("FAULT_Thermo", "Thermocouple High Fault", PRIVATE);
			if (fault & MAX31856_FAULT_TCLOW)   Particle.publish("FAULT_Thermo", "Thermocouple Low Fault", PRIVATE);
			if (fault & MAX31856_FAULT_OVUV)    Particle.publish("FAULT_Thermo", "Over/Under Voltage Fault", PRIVATE);
			if (fault & MAX31856_FAULT_OPEN)    Particle.publish("FAULT_Thermo", "Thermocouple Open Fault", PRIVATE);
		}

		// Update sensor alarms
		if ( (temp_tc < alarm_temp_min) && equip_spec && ! fault_thermocouple ){
			low_t_alarm = TRUE;
		} else{
			low_t_alarm = FALSE;
		}

		if ( low_t_alarm != low_t_alarm_last ){
			if (low_t_alarm){
				Particle.publish("ALARM_TEMP", "ALARM: Internal temperature below minimum.", PRIVATE);
			}
			else
			{
				Particle.publish("ALARM_TEMP", "CLEAR: Internal temperature no longer below minimum.", PRIVATE);
			}
			low_t_alarm_last = low_t_alarm;
		}


		if ( (temp_tc > alarm_temp_max) && equip_spec && ! fault_thermocouple ){
			high_t_alarm = TRUE;
		} else{
			high_t_alarm = FALSE;
		}

		if ( high_t_alarm != high_t_alarm_last ){
			if (high_t_alarm){
				Particle.publish("ALARM_TEMP", "ALARM: Internal temperature above maximum.", PRIVATE);
			}
			else
			{
				Particle.publish("ALARM_TEMP", "CLEAR: Internal temperature no longer above maximum.", PRIVATE);
			}
			high_t_alarm_last = high_t_alarm;
		}


		if ( (temp_amb > alarm_ambient_t_max) && ! fault_bme ){
			amb_t_alarm = TRUE;
		} else{
			amb_t_alarm = FALSE;
		}

		if ( amb_t_alarm != amb_t_alarm_last ){
			if (amb_t_alarm){
				Particle.publish("ALARM_AMB", "ALARM: Ambient temperature above maximum.", PRIVATE);
			}
			else
			{
				Particle.publish("ALARM_AMB", "CLEAR: Ambient temperature no longer above maximum.", PRIVATE);
			}
			amb_t_alarm_last = amb_t_alarm;
		}


		if ( (humid_amb > alarm_ambient_h_max && ! fault_bme ) ){
			amb_h_alarm = TRUE;
		} else{
			amb_h_alarm = FALSE;
		}

		if ( amb_h_alarm != amb_h_alarm_last ){
			if (amb_t_alarm){
				Particle.publish("ALARM_AMB", "ALARM: Ambient humidity above maximum.", PRIVATE);
			}
			else
			{
				Particle.publish("ALARM_AMB", "CLEAR: Ambient humidity no longer above maximum.", PRIVATE);
			}
			amb_h_alarm_last = amb_h_alarm;
		}

		// Create a status summary for display. Shows only one status at a time,
		// put higher priority items later in sequence so they overwrite
		// lower priority items.
		String monitor_status = "System is nominal";

		if (fault_bme){
			monitor_status =          "FAULT: Ambient sensor";
		}

		if (fault_thermocouple){
			monitor_status =          "FAULT: Thermocouple";
		}

		if ( abs( temp_tc_cj - temp_amb  ) > 3 ){
			monitor_status =          "WARN: Temp mismatch";
		}

		if ( amb_h_alarm ){
			monitor_status =          "ALARM: Ambient humid";
		}

		if ( amb_t_alarm ){
			monitor_status =          "ALARM: Ambient temp";
		}

		if ( ! usb_power ){
			monitor_status =          "FAULT: No power";
		}

		if ( equip_alarm ){
			monitor_status =          "ALARM: Equip. alarm";
		}

		if ( low_t_alarm ){
			monitor_status =          "ALARM: Low temp";
		}

		if ( high_t_alarm ){
			monitor_status =          "ALARM: High temp";
		}

		// Update display
		display.clearDisplay();
		// Size 1 has xx characters per row
		display.setTextSize(1);
		display.setTextColor(WHITE);
		display.setCursor(0,0);
		display.println("Dunn Lab Monitor");
		display.println(monitor_status);
		display.println(String::format("Internal Temp: %.1f C", temp_tc));
		display.println(String::format(" Ambient Temp: %.1f C", temp_amb));
		display.println(String::format("Ambient Humid: %.0f %%", humid_amb));
		display.println("");
		display.display();

	}
}




bool isUsbPowered() {
	byte systemStatus = pmic.getSystemStatus();
	// observed states when charging include 36, 52, and 180, all of which have
	// bits 32 and 4 high. That corresponds to hex 0x24
	return ((systemStatus & 0x24) == 0x24);
}
