#include "arduino_secrets.h"


/* 
  Sketch generated by the Arduino IoT Cloud Thing "Untitled 2"
  https://create.arduino.cc/cloud/things/c0ab3c3d-50e1-4cdb-a264-d98a57503763 

  Arduino IoT Cloud Variables description

  The following variables are automatically generated and updated when changes are made to the Thing

  float dissolved_Oxygen;
  float pH;
  float total_Dissolved_Solids;
  float turbidity;
  int loop_Delay_Minutes;
  bool comms_OK;
  bool sensors_Immersed;
  bool system_OK;
  CloudTemperature air_Temperature;
  CloudTemperature water_Temperature;

  Variables which are marked as READ/WRITE in the Cloud Thing will also have functions
  which are called when their values are changed from the Dashboard.
  These functions are generated with the Thing and added at the end of this sketch.
*/

#include "thingProperties.h"



/*********************************************************************
**                   Eva Cunningham, November 2024                  **
**                                                                  **
** Water Sensor Hub for Seawater Swimming Water Quality Measurement **
**                                                                  **
**********************************************************************/

// Include the low power library so we can put the processor to sleep between samples

#include <ArduinoLowPower.h>

// Include libraries for devices etc. 
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include "GravityTDS.h"
#include "DFRobot_PH.h"

/********************************/
/**** ADJUSTABLE PARAMETERS *****/
/********************************/

bool            diagnostics_On = true;                   // Diagnostics ON/OFF
const int       immersion_Sensor_In_Water_Range = 200;   // Any value less than this and the sensor is definitely under water ** Change after Calibration **
int             default_Loop_Delay_Minutes = 15;         // Polling time for sensors .. this can be changed from the dashboard
int             awake_For_Transmission_Milliseconds = 30000; 

const int       turbidity_Constant = 100;               // converts Turbidity sensor voltage to Jackson Turbidity Units (JTU) ** Change after calibration

// Dissolved Oxygen Sensor Calibration Values ** Change after Calibration
#define CAL1_V 1600 //mv
#define CAL1_T 25  //℃
#define CAL2_V 1300 //mv
#define CAL2_T 5   //℃

const uint16_t DO_Table[41] = {
    14460, 14220, 13820, 13440, 13090, 12740, 12420, 12110, 11810, 11530,
    11260, 11010, 10770, 10530, 10300, 10080, 9860, 9660, 9460, 9270,
    9080, 8900, 8730, 8570, 8410, 8250, 8110, 7960, 7820, 7690,
    7560, 7430, 7300, 7180, 7070, 6950, 6840, 6730, 6630, 6530, 6410};


// Sensor Pins
#define activate_Sensors_Pin 0
#define Temp_Pin 1
#define tds_Pin 2
#define turbidity_Pin 3
#define pH_Pin 4
#define dox_Pin 5
#define immersed_Pin 6
#define ref_Voltage 3.3
#define adc_Range 1024

// Other Variables

unsigned long milliseconds_in_a_second  = 1000L; 
unsigned long milliseconds_in_a_minute = milliseconds_in_a_second * 60;
/* cycle time of main  loop in ms */
int loop_Timer;
GravityTDS gravityTDS;
DFRobot_PH ph_Sensor;



// Code to initialise the temperature sensor bus
OneWire oneWire(Temp_Pin);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature temp_Sensors(&oneWire);
int numberOfDevices; // Number of temperature devices found when we poll

DeviceAddress tempDeviceAddress; // current temperature device address


void setup() {
  // Initialize serial and wait for port to open:
  Serial.begin(9600);
  // This delay gives the chance to wait for a Serial Monitor without blocking if none is found
  delay(1500); 
 
  // Defined in thingProperties.h
  initProperties();

  // Connect to Arduino IoT Cloud
  ArduinoCloud.begin(ArduinoIoTPreferredConnection, false); // 'false' Disables Arduino wake up between sleeps (cloud watchdog timer)
  
  /*
     The following function allows you to obtain more information
     related to the state of network and IoT Cloud connection and errors
     the higher number the more granular information you’ll get.
     The default is 0 (only errors).
     Maximum is 4
 */
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();

  // Main Setup 
  print_Diagnostics("Sensor Hub Starting Up");
  // Initialise the sensors
  setup_Sensors();
  
  loop_Delay_Minutes = default_Loop_Delay_Minutes;

}

void loop() {
  
  // MAIN LOOP 

  // Turn on onboard LED to show we are out of deep sleep
  digitalWrite(LED_BUILTIN, true);

  // Check the immersion sensor
  
  if ((analogRead(immersed_Pin)< immersion_Sensor_In_Water_Range ) | diagnostics_On) {
    sensors_Immersed = true;
  }
  else {
    sensors_Immersed = false;
  }
  
  //******** ALWAYS READ THE AIR TEMPERATURE
  air_Temperature = get_Air_Temp();
  print_Diagnostics("Air Temperature: "+ String(air_Temperature));
    
  
  if (sensors_Immersed) { // Read the other sensors if they are under water
    
    // TURN THE SENSORS ON
    activate_Sensors();
    
    //******** READ THE WATER TEMPERATURE
    water_Temperature = get_Water_Temp();
    print_Diagnostics("Water Temperature: "+ String(water_Temperature));

    //******** READ TDS
    // TDS probe needs the current water temperature to calibrate
    total_Dissolved_Solids = get_TDS(water_Temperature);
    print_Diagnostics("TDS: "+ String(total_Dissolved_Solids));
    
    //******** READ Turbidity
    turbidity = get_Turbidity();
    print_Diagnostics("Turbidity: "+ String(turbidity));

    //******** READ pH
    pH = get_pH (water_Temperature);
    print_Diagnostics("pH: "+ String(pH));

    //******** READ Dissolved Oxygen
    dissolved_Oxygen = get_DO2(water_Temperature);
    print_Diagnostics("Dissolved Oxygen: "+ String(dissolved_Oxygen));
    
    // TURN THE SENSORS OFF to save enetgy
    deactivate_Sensors();
    
  }
  else {
    // set sensors to NULL values 
    set_Sensors_to_Null();
  }
  
 // Update the Arduino Cloud 
  ArduinoCloud.update();
 
  // Do a regular sleep long enough for the Arduino Cloud Update to finish
  delay(awake_For_Transmission_Milliseconds); 
  print_Diagnostics("Exiting normal sleep"); 
  
 
  // Note the IoT cloud can change loop_Delay_Minutes so we reconvert it to ms here
  loop_Timer = milliseconds_in_a_minute * loop_Delay_Minutes; 
  // Now that the sensors are off we can put the Arduino into deep sleep
  digitalWrite(LED_BUILTIN, false); // Turn LED off to show we going into deep sleep
  LowPower.deepSleep(loop_Timer); 
    
  print_Diagnostics("Exiting deep sleep");
  
  
}



/************************/
/* SENSOR FUNCTIONS *****/
/************************/

void activate_Sensors(){

 digitalWrite(activate_Sensors_Pin, HIGH);
  
}

void deactivate_Sensors(){

 digitalWrite(activate_Sensors_Pin, LOW);
  
}
//******** AIR TEMP

float get_Air_Temp(){
  
  //set current temp for calibratrion
  float air_Temp;
  temp_Sensors.requestTemperatures(); // Send the command to get temperatures
  temp_Sensors.getAddress(tempDeviceAddress, 0);
  air_Temp = temp_Sensors.getTempC(tempDeviceAddress);
  return(air_Temp);
  
}

//******** WATER TEMP

float get_Water_Temp(){
  
  //set current temp for calibratrion
  float water_Temp;
  temp_Sensors.getAddress(tempDeviceAddress, 1);
  water_Temp = temp_Sensors.getTempC(tempDeviceAddress); 
  return(water_Temp);
  
}

//******** TDS
float get_TDS(float water_temp){
  
  //set current temp for calibratrion
  float tds_Value;
  gravityTDS.setTemperature(water_temp);  // set the temperature and execute temperature compensation
  gravityTDS.update();  //sample and calculate
  tds_Value = gravityTDS.getTdsValue();  // then get the value
  return(tds_Value);
  
}

//******** TURDIDITY
float get_Turbidity(){
  
  float turbidity_Value;
  int sensorValue = analogRead(turbidity_Pin);// read the input on analog pin
  float turbidity_Voltage = sensorValue * (ref_Voltage / adc_Range); // Convert the analog reading to  voltage
  turbidity_Value = turbidity_Voltage * turbidity_Constant; // convert voltage to turbidity
  return(turbidity_Value);
  
}

//******** pH
float get_pH(float water_temp){
  
  float pH_Value, sensor_Voltage;
  sensor_Voltage = analogRead(pH_Pin)*(ref_Voltage / adc_Range);  // read the voltage
  pH_Value = ph_Sensor.readPH(sensor_Voltage,water_temp);  // convert voltage to pH with temperature compensation
  ph_Sensor.calibration(sensor_Voltage,water_temp);
  return(pH_Value);
  
}

//******** Disssolved Oxygen
float get_DO2(float water_Temperature){
  
  float DO2_Value, sensor_Voltage, V_saturation;
  int water_temp_int;
  water_temp_int = (int)water_Temperature;
  
  // read sensor
  sensor_Voltage = analogRead(dox_Pin)*(ref_Voltage / adc_Range);  // read the voltage
  V_saturation = (water_Temperature - CAL2_T) * (CAL1_V - CAL2_V) / (CAL1_T - CAL2_T) + CAL2_V;
  DO2_Value = sensor_Voltage * DO_Table[water_temp_int] / V_saturation; // convert voltage to DO2 taking temp into account
  return(DO2_Value);
  
}

// ********* Misc Sensor Stuff

void setup_Sensors() {
  
  pinMode (activate_Sensors_Pin, OUTPUT);
  
  set_Sensors_to_Null();
  // Get the temperature probes set up
  temp_Sensors.begin();
  // Count number of temperature devices on the wire .. should be 2. This could be improved to check and handle error if no 2
  numberOfDevices = temp_Sensors.getDeviceCount();
  print_Diagnostics("Number of Temperature Probes: "+ String(numberOfDevices));

  // TDS Sensor Setup
  gravityTDS.setPin(tds_Pin);
  gravityTDS.setAref(ref_Voltage);  //reference voltage on ADC
  gravityTDS.setAdcRange(adc_Range);  //1024 for 10bit ADC;4096 for 12bit ADC
  gravityTDS.begin();  //initialization

  ph_Sensor.begin();
 
  
}

void set_Sensors_to_Null(){

  // Set sensors to NULL (or meaningless) values as they are out of the water
  // Note the air temperatrure is still valid
  
  dissolved_Oxygen =0;
  pH = 0;
  total_Dissolved_Solids=0;
  turbidity = 0;
  water_Temperature = -50;  
}

/************************/
/* Misc Functions *******/
/************************/

void print_Diagnostics(String diagnostic_Data){

  // This function prints diagnostic messages to the serial port if diagnostics ON
  if (diagnostics_On)
    Serial.println(diagnostic_Data);  
}

/*
  Since LoopDelayMinutes is READ_WRITE variable, onLoopDelayMinutesChange() is
  executed every time a new value is received from IoT Cloud.
*/
void onLoopDelayMinutesChange()  {
  // no code needed .. it'll be reflected next time around the main loop
}











