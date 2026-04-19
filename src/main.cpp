#include <list>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>
#include <TinyGPSPlus.h>
#include "esp_sleep.h"

#include "ICM_20948.h"

#define LED_PIN D8
#define LED_COUNT 24

#define MOS_PIN D2
#define BUTTON_PIN D1

#define GPS_RX D7  
#define GPS_TX D6  
#define GPS_BAUD 9600

#define MAX_ENTRIES 32
#define EEPROM_SIZE MAX_ENTRIES*sizeof(struct loc_struct) + sizeof(int)

// Global variables

const char* ssid = "potato";
const char* pass = "12345678";

struct loc_struct {
  char name[10];
  double loc[2];
  int ptr;
  int ptro;
  int con;
  byte color[3];
};

std::list<struct loc_struct> locations;

Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
ICM_20948_I2C myICM;
TinyGPSPlus gps;
HardwareSerial gpsSerial(1); 
WebServer server(80);

// State variables
bool gps_init = false;
bool wifistatus = 0;

// Global variables

void initEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  int cnt;
  EEPROM.get(0, cnt);
  if (cnt == -1)  {
    EEPROM.put(0, 0);
    EEPROM.commit();
  }
}

void storeEEPROM() {
    int idx = 0;

    for (auto it = locations.begin(); it != locations.end(); ++it) {
        if (idx >= MAX_ENTRIES) break;

        int addr = sizeof(int) + idx * sizeof(loc_struct);
        EEPROM.put(addr, *it);
        idx++;
    }

    EEPROM.put(0, idx);     // store count LAST
    EEPROM.commit();

    Serial.printf("Stored %d locations\n", idx);
}

void loadEEPROM() {
    locations.clear();

    int cnt;
    EEPROM.get(0, cnt);

    // Validate count
    if (cnt < 0 || cnt > MAX_ENTRIES) {
        Serial.println("EEPROM count invalid, resetting");
        cnt = 0;
        EEPROM.put(0, cnt);
        EEPROM.commit();
        return;
    }

    for (int i = 0; i < cnt; i++) {
        int addr = sizeof(int) + i * sizeof(loc_struct);
        loc_struct loc;
        EEPROM.get(addr, loc);
        locations.push_back(loc);

        Serial.printf("Recovered location %d: %s\n", i, loc.name);
    }

    Serial.println("All locations reloaded from EEPROM");
}

void handleRoot() {
    // Load persistent data
    loadEEPROM();

    // Start HTML page
    String page = "<h2>Locations</h2>";

    // Table header
    page += "<table border='1'><tr>"
            "<th>#</th>"
            "<th>Name</th>"
            "<th>Latitude</th>"
            "<th>Longitude</th>"
            "<th>Color (R,G,B)</th>"
            "<th>Delete</th>"
            "</tr>";

    int idx = 0;
    for (auto it = locations.begin(); it != locations.end(); ++it) {
        page += "<tr>";
        page += "<td>"; page += idx; page += "</td>";             // index
        page += "<td>"; page += it->name; page += "</td>";       // name
        page += "<td>"; page += it->loc[0]; page += "</td>";     // latitude
        page += "<td>"; page += it->loc[1]; page += "</td>";     // longitude
        page += "<td>"; 
        page += it->color[0]; page += ",";
        page += it->color[1]; page += ",";
        page += it->color[2]; 
        page += "</td>";

        // Delete button form
        page += "<td>";
        page += "<form action='/delete' method='get'>";
        page += "<input type='hidden' name='idx' value='" + String(idx) + "'>";
        page += "<input type='submit' value='Delete'>";
        page += "</form>";
        page += "</td>";

        page += "</tr>";
        idx++;
    }
    page += "</table><br>";

    // Form to add a new location (unchanged)
    page += "<h2>Add Location</h2>";
    page += "<form action='/add' method='get'>";
    page += "<label for='name'>Name:</label>";
    page += "<input type='text' id='name' name='name' maxlength='10'><br>";
    page += "<label for='lat'>Latitude:</label>";
    page += "<input type='text' id='lat' name='lat'><br>";
    page += "<label for='lon'>Longitude:</label>";
    page += "<input type='text' id='lon' name='lon'><br>";
    page += "<label for='r'>Color R:</label>";
    page += "<input type='number' id='r' name='r' min='0' max='255'><br>";
    page += "<label for='g'>Color G:</label>";
    page += "<input type='number' id='g' name='g' min='0' max='255'><br>";
    page += "<label for='b'>Color B:</label>";
    page += "<input type='number' id='b' name='b' min='0' max='255'><br>";
    page += "<input type='submit' value='Add'>";
    page += "</form>";

    server.send(200, "text/html", page);
}

void handleAdd() {
    if (server.hasArg("name") && server.hasArg("lat") && server.hasArg("lon") &&
        server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {

        loc_struct loc;

        // Name
        String tempName = server.arg("name");
        tempName.toCharArray(loc.name, 10);

        // Latitude / Longitude
        loc.loc[0] = server.arg("lat").toDouble();
        loc.loc[1] = server.arg("lon").toDouble();

        // Color
        loc.color[0] = server.arg("r").toInt();
        loc.color[1] = server.arg("g").toInt();
        loc.color[2] = server.arg("b").toInt();

        // Default ptr, ptro, con
        loc.ptr = 0;
        loc.ptro = 0;
        loc.con = 0;

        locations.push_back(loc);
        storeEEPROM();  // save the updated list
    }

    // Redirect back to main page
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleDelete() {
    if (server.hasArg("idx")) {
        int idx = server.arg("idx").toInt();

        if (idx >= 0 && idx < locations.size()) {
            auto it = locations.begin();
            std::advance(it, idx);
            ring.setPixelColor(it->ptr, ring.Color(0, 0, 0));
            ring.show();
            locations.erase(it);   // remove element at index
            storeEEPROM();         // save changes
            Serial.printf("Deleted location at index %d\n", idx);
        }
    }

    // Redirect back to main page
    server.sendHeader("Location", "/");
    server.send(303);
}

void setupSensors() {

  // GPS
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);

  // LED ring
  ring.begin();   // initialize NeoPixel library
  ring.show();    // turn all LEDs off

  // 9DOF
  Wire.begin();
  Wire.setClock(400000);

  bool initialized = false;
  while (!initialized)
  {
    myICM.begin(Wire, 1);

    if (myICM.status != ICM_20948_Stat_Ok)
    {
      delay(500);
    }
    else
    {
      initialized = true;
    }
  }

  bool success = true; // Use success to show if the DMP configuration was successful

  // Initialize the DMP. initializeDMP is a weak function. You can overwrite it if you want to e.g. to change the sample rate
  success &= (myICM.initializeDMP() == ICM_20948_Stat_Ok);

  // Enable the DMP orientation sensor
  success &= (myICM.enableDMPSensor(INV_ICM20948_SENSOR_ORIENTATION) == ICM_20948_Stat_Ok);

  success &= (myICM.setDMPODRrate(DMP_ODR_Reg_Quat9, 0) == ICM_20948_Stat_Ok); // Set to the maximum

  // Enable the FIFO
  success &= (myICM.enableFIFO() == ICM_20948_Stat_Ok);

  // Enable the DMP
  success &= (myICM.enableDMP() == ICM_20948_Stat_Ok);

  // Reset DMP
  success &= (myICM.resetDMP() == ICM_20948_Stat_Ok);

  // Reset FIFO
  success &= (myICM.resetFIFO() == ICM_20948_Stat_Ok);

  // Check success
  if (success)
  {
    Serial.println(F("DMP enabled!"));
  }
  else
  {
    Serial.println(F("Enable DMP failed!"));
    Serial.println(F("Please check that you have uncommented line 29 (#define ICM_20948_USE_DMP) in ICM_20948_C.h..."));
    while (1)
      ; // Do nothing more
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(MOS_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  initEEPROM();
  loadEEPROM();

  digitalWrite(MOS_PIN, HIGH);

  setupSensors();

  // Turn on LED 0 to white
  ring.setPixelColor(0, ring.Color(255, 255, 255));
  ring.show();

  delay(2000);
  Serial.println("End of setup");

}


double roll, pitch, yaw;
int32_t acc;
double lo, la;

void getReadings() {
  icm_20948_DMP_data_t data;
  myICM.readDMPdataFromFIFO(&data);

  if ((myICM.status == ICM_20948_Stat_Ok) || (myICM.status == ICM_20948_Stat_FIFOMoreDataAvail)) // Was valid data available?
  {

    if ((data.header & DMP_header_bitmap_Quat9) > 0) // We have asked for orientation data so we should receive Quat9
    {
      double q1 = ((double)data.Quat9.Data.Q1) / 1073741824.0; // Convert to double. Divide by 2^30
      double q2 = ((double)data.Quat9.Data.Q2) / 1073741824.0; // Convert to double. Divide by 2^30
      double q3 = ((double)data.Quat9.Data.Q3) / 1073741824.0; // Convert to double. Divide by 2^30
      double q0 = sqrt(1.0 - ((q1 * q1) + (q2 * q2) + (q3 * q3)));

      double qw = q0;
      double qx = q1;
      double qy = q2;
      double qz = q3;


      double ysqr = qy * qy;  

      // roll (x-axis rotation)
      double t0 = +2.0 * (qw * qx + qy * qz);
      double t1 = +1.0 - 2.0 * (qx * qx + ysqr);
      roll = atan2(t0, t1);

      // pitch (y-axis rotation)
      double t2 = +2.0 * (qw * qy - qz * qx);
      t2 = t2 > 1.0 ? 1.0 : t2;
      t2 = t2 < -1.0 ? -1.0 : t2;
      pitch = asin(t2);

      // yaw (z-axis rotation)
      double t3 = +2.0 * (qw * qz + qx * qy);
      double t4 = +1.0 - 2.0 * (ysqr + qz * qz);
      yaw = -atan2(t3, t4);

      // Convert radians to degrees
      yaw *= 180.0 / PI;
      pitch *= 180.0 / PI;
      roll *= 180.0 / PI;


      acc = data.Quat9.Data.Accuracy;

      // Normalize yaw to [0, 360)
      if (yaw < 0) yaw += 360.0;
    }
  }

  if (myICM.status != ICM_20948_Stat_FIFOMoreDataAvail)
  {
    delay(10);
  }


  // GPS

  while (gpsSerial.available() > 0) {
    char c = gpsSerial.read();
    // Serial.write(c);
    gps.encode(c);
  }

  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 2000) {
    lastStatus = millis();

    if (gps.satellites.isValid()) {
      Serial.print("Satellites: ");
      Serial.println(gps.satellites.value());
    } else {
      Serial.println("Satellites: ---");
    }

    if (gps.location.isValid()) {
      Serial.print("Lat: "); Serial.println(gps.location.lat(), 6);
      Serial.print("Lng: "); Serial.println(gps.location.lng(), 6);
      lo = gps.location.lat();
      la = gps.location.lng();
      gps_init = true;
    } else {
      Serial.println("Location: waiting for fix...");
      gps_init = false;
    }
  }
}

int fixAngle(int angle) {
  while (angle < 0) {
    angle += 360;
  }
  angle %= 360;

  return angle;
}

void calcLoc() {
  double xdiff, ydiff;
  int p;

  for (auto it = locations.begin(); it != locations.end(); ++it) {
    xdiff = it->loc[0] - lo;
    ydiff = it->loc[1] - la;

    p = fixAngle((int)(atan2(ydiff, xdiff) * 180.0/3.142));
    p = fixAngle(-((int)p - (int)yaw) - 90);

    it->ptr = ((p * 24)/360);
  }
}

void activateWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, pass);

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/add",handleAdd);
  server.on("/delete", handleDelete);

  server.begin();
}

void activateSleep() {
  Serial.println("Button pressed, going to sleep...");

  digitalWrite(MOS_PIN, LOW);

  // Correct Arduino C3 wakeup macro
  esp_deep_sleep_enable_gpio_wakeup((1ULL << BUTTON_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);

  delay(2000); // debounce
  esp_deep_sleep_start();
}

bool btnstate = 1;
unsigned long presstim = 2000;
bool updated = 0;
void checkbtn() {
  int btn = digitalRead(BUTTON_PIN);

  // Serial.println(presstim);
  if (btnstate != btn && btn == 0) {
    presstim = millis();
    btnstate = 0;
  }
  else if (btnstate == btn && btn == 0) {
    if (millis() - presstim >= 2000 && !updated) {
      wifistatus = !wifistatus;
      Serial.println(wifistatus);
      if (wifistatus == 1)  {
        Serial.println("Activating wifi1");
        updated = 1;
        activateWiFi();
      }
      else  {
        Serial.println("Deactivating wifi1");
        updated = 1;
        // WiFi.softAPdisconnect(true);
      }
    }
  }
  else if (btnstate != btn && btn == 1) {
    btnstate = 1;
    Serial.println(millis() - presstim);
    if (millis() - presstim >= 2000 && !updated) {
      // Do nothing ig
    }
    else if (!updated){
      // Serial.println("Sleeping");
      activateSleep();
    }
  }
  else {
    presstim = millis();
    btnstate = 1;
    updated = 0;
  }
}


int ctr;
int nr;
int nro = -1;
void loop() {

  checkbtn();
  getReadings();

  if (wifistatus) {
    server.handleClient();
  }
  if (gps_init) {
  // Serial.printf("Yaw: %f, acc: %d\n", yaw, acc);

    int displayNorth = fixAngle(yaw-90);
    calcLoc();

    // ring.clear();

    // for (int i = 0; i < acc/100; i++) {
    //   ring.setPixelColor(i, ring.Color(255, 255, 255));
    // }

    // Serial.printf("Yaw: %f, ptr: %d\n", yaw, ptr[0]);

    byte r, g, b;
    r = 0;
    g = 255;
    b = 0;
    nr = (displayNorth*24)/360;
    
    if (nr != nro) {
      ring.setPixelColor(nro, ring.Color(0, 0, 0));
      if (wifistatus) ring.setPixelColor(nr, ring.Color(255, 255, 255));
      else  ring.setPixelColor(nr, ring.Color(255, 0, 0));
      nro = nr;
    }
    // ring.setPixelColor(((ptr[0] * 24)/360), ring.Color(r, g, b));

    for (auto it = locations.begin(); it != locations.end(); ++it) {
      if (it->ptr != it->ptro)
      {
        ring.setPixelColor(it->ptro, ring.Color(0, 0, 0));
        ring.setPixelColor(it->ptr, ring.Color(it->color[0], it->color[1], it->color[2]));
        it->ptro = it->ptr;
      }
    }
    delay(10);
    ring.show();
  }
  else {
    ctr++;
    if (ctr >= 24) {
      ctr = 0;
    }
    ring.setPixelColor(ctr, ring.Color(255, 0, 0));
    ring.show();
    delay(100);
    ring.clear();
  }

}
