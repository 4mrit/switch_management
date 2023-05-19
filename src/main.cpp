#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ctime>
#include <time.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

WiFiManager wifi;
Preferences preferences;

//---------Static IP-----------//
uint8_t custom_ip = 222;
//-----------------------------//

bool lightStatus();
void startWebServer_ACTIVE();
void startWebServer_EXPIRED();
void saveSchedulesToEEPROM();
void loadSchedulesFromEEPROM();
void handleRoot_GET();
void handleRoot_POST();
void handleDeleteSchedule_POST();
void handleDeleteAllSchedule_POST();
void handleSettings_GET();
void handleSettings_POST();
void deleteSchedule(int);
void deleteAllSchedules();
void establishNetwork();
void setCustomIPandSaveNetwork();
uint16_t getCurrentTimeInMinutes();
int getExpiryTime();
bool subscriptionStatus();
bool syncTime();
void reconnectNetworkWithCustomIP();
void saveCredentialsSTATION();


//--test/debug functions (meant to be removed on production)--//
void TEST_schedules_variable_data();
//-----------------------------//

bool defaultLEDState = HIGH;
const uint8_t LED_PIN = D1;
// char *ssid_STATION = "1011001";
// char *password_STATION = "dr0wss@p";
String ssid_STATION = "1011001";
String password_STATION = "dr0wss@p";
String ssid_AP = "Smart_Switch";
String password_AP = "dr0wss@p";
bool isModeStation = true;
const int timeOffsetHour = 5;
const int timeOffsetMin = 45;

struct Schedule
{
  int start_time_hour;
  int start_time_min;
  int duration;
  bool isDeleted = true;
};

const int MAX_SCHEDULES = 10;
Schedule schedules[MAX_SCHEDULES];
int num_schedules = 0;

ESP8266WebServer server(80);

void setup()
{
  Serial.begin(9600);
  preferences.begin("my-app", false);
  establishNetwork();
  pinMode(LED_PIN, OUTPUT);
  syncTime();
  loadSchedulesFromEEPROM();
  if (subscriptionStatus() == true)
  {
    startWebServer_ACTIVE();
  }
  else
  {
    startWebServer_EXPIRED();
  }
}

void loop()
{
  digitalWrite(LED_PIN, lightStatus());
  // dnsServer.processNextRequest();
  server.handleClient();
  delay(1000);
}

bool lightStatus()
{
  uint16_t current_time = getCurrentTimeInMinutes(); // returns in form of minutes eg: 5:1 pm = 1021m

  for (int i = 0; i < num_schedules; i++)
  {
    if (schedules[i].isDeleted)
      continue;
    int start_time = schedules[i].start_time_hour * 60 + schedules[i].start_time_min;
    int duration = schedules[i].duration;

    if (start_time <= current_time && current_time < start_time + duration)
    {
      Serial.printf("bulb D0 : ON (%d)\n",!defaultLEDState);
      return !defaultLEDState;
    }
  }
  Serial.printf("bulb D0 : OFF (%d)",defaultLEDState);
  return defaultLEDState;
}

void establishNetwork()
{
  ssid_STATION = preferences.getString("ssid_STATION",ssid_STATION);
  password_STATION = preferences.getString("password_STATION",password_STATION);
  ssid_AP = preferences.getString("ssid_AP",ssid_AP);
  password_AP = preferences.getString("password_AP",password_AP);

  
  WiFi.mode(WIFI_AP_STA);
  //starts hotspot
  
  WiFi.begin(ssid_STATION,password_STATION);// start connection
  //WAIT 10 SEC FOR CONNECTION
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.println("Connecting to wifi ..");
  }

  if(WiFi.status() == WL_CONNECTED)
  {
    Serial.print("Station Connection Successful!\nIP : ");
    Serial.println(WiFi.localIP());
    reconnectNetworkWithCustomIP();
    saveCredentialsSTATION();
    Serial.println(WiFi.localIP());
  }else {
    Serial.println("Station Connection Unsuccessful!to\nSSID : "+ssid_STATION + "\nPWD : "+password_STATION);
  }
  if(WiFi.softAP(ssid_AP,password_AP)){
    Serial.print("Soft ap successful \n IP : ");
    Serial.println(WiFi.softAPIP());
  }else
  {
    Serial.println("Soft AP unsuccessful!");
  }

}
void saveCredentialsSTATION(){
    ssid_STATION = WiFi.SSID();
    password_STATION = WiFi.psk();
    preferences.putString("ssid_STATION", ssid_STATION);
    preferences.putString("password_STATION", password_STATION);
}

void reconnectNetworkWithCustomIP(){

  IPAddress dnsServer1(8, 8, 8, 8);      // Google DNS server
  IPAddress dnsServer2(8, 8, 4, 4);      // Google DNS server
   if (WiFi.status() == WL_CONNECTED)
      {
        IPAddress IP = WiFi.gatewayIP();
        IP[3] = custom_ip;
        WiFi.config(  IP,
                      WiFi.gatewayIP(),
                      WiFi.subnetMask(),
                      dnsServer1,
                      dnsServer2
                    );
      }
}


bool syncTime()
{
  configTime(timeOffsetHour * 3600 + timeOffsetMin * 60, 0,
             "pool.ntp.org",
             "time.google.com");
  if (time(nullptr))
  {
    Serial.printf("Time Syncronization Successful \n");
    return true;
  }
  else
  {
    Serial.printf("Time Syncronization Unsuccessful \n");
    return false;
  }
}

uint16_t getCurrentTimeInMinutes()
{
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);

  // Print the date and time
  Serial.printf("Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                timeinfo->tm_year + 1900,
                timeinfo->tm_mon + 1,
                timeinfo->tm_mday,
                timeinfo->tm_hour,
                timeinfo->tm_min,
                timeinfo->tm_sec);

  return timeinfo->tm_hour * 60 + timeinfo->tm_min;
}

void startWebServer_ACTIVE()
{
  server.on("/", HTTP_GET, handleRoot_GET);
  server.on("/", HTTP_POST, handleRoot_POST);
  server.on("/delete-schedule", HTTP_POST, handleDeleteSchedule_POST);
  server.on("/delete-all-schedule", HTTP_POST, handleDeleteAllSchedule_POST);
  server.on("/settings", HTTP_GET, handleSettings_GET);
  server.on("/settings", HTTP_POST, handleSettings_POST);
  server.begin();
  Serial.println("light Scheduling HTTP Server started");
}

void startWebServer_EXPIRED()
{
  server.on("/", HTTP_GET, []()
            { server.send(200, "text/html", "<html><head><meta charset='UTF-8'></head><body><h1>Subscription Expired!! </h1><form method='GET' action='/settings'><button type='submit'>⚙️ Settings</button></form></body></html>"); });
  server.on("/settings", HTTP_GET, handleSettings_GET);
  server.on("/settings", HTTP_POST, handleSettings_POST);
  server.begin();
  Serial.println("Subscription Expired Server");
}

void handleRoot_GET()
{
  char *format = "";
  int start_time_hour;
  Serial.println("handling / get request");
  TEST_schedules_variable_data();

  String html = R"(
    <html>
      <head>
        <meta charset='UTF-8'><title>Light Scheduler</title>
      </head>
      <body>
        <h1>
          Light Scheduler 
          <form style='display:inline-block' method='GET' action='/settings'>
            <button type='submit'>⚙️ Settings</button>
          </form>
        </h1>
        <table>
          <tr>
            <th>Start Time</th>
            <th>Duration</th>
          </tr>
  )";

  for (int i = 0; i < num_schedules; i++)
  {
    if (schedules[i].isDeleted)
      continue;
    schedules[i].start_time_hour < 12 ? format = "AM" : format = "PM";
    if (schedules[i].start_time_hour > 12)
      start_time_hour = schedules[i].start_time_hour - 12;
    else
      start_time_hour = schedules[i].start_time_hour;

    html += R"(
      <tr>
        <td>)"+ String(start_time_hour) +":" + String(schedules[i].start_time_min) + " " + format + R"(</td>
        <td>)" + String(schedules[i].duration) + R"( min</td>
        <td>
          <form method='POST' action='/delete-schedule'>
            <input type='hidden' name='index' value=")" + i + R"(">
            <button type='submit'>Delete</button>
          </form>
        </td>
      </tr>
    )"; 
  }

  html += R"(
        </table><br><br>
        <form method='POST'>
          Start Time: <input type='time' name='start_time'><br><br>
          Duration : &nbsp&nbsp<input type='text' name='duration' placeholder='minutes'><br><br>
          <input type='submit' value='Save'>
        </form>
        <form method='POST' action='/delete-all-schedule'>
          <button type='submit'>Delete All Schedules</button>
        </form>
      </body>
    </html>
  )";
  server.send(200, "text/html", html);
}

void handleRoot_POST()
{
  Serial.println("handling / post request");
  TEST_schedules_variable_data();
  String start_time_hour = "";
  String start_time_min = "";
  String duration = "";

  for (int i = 0; i < MAX_SCHEDULES; i++)
  {

    if (!server.hasArg("start_time"))
    {
      break;
    }
    // Serial.println(server.arg("plain"));
    start_time_hour = server.arg("start_time").substring(0, 2);
    start_time_min = server.arg("start_time").substring(3);
    duration = server.arg("duration");

    if (start_time_hour != "" && duration != "" && schedules[i].isDeleted)
    {
      schedules[i].start_time_hour = start_time_hour.toInt();
      schedules[i].start_time_min = start_time_min.toInt();
      schedules[i].duration = duration.toInt();
      schedules[i].isDeleted = false;
      break;
    }
  }

  num_schedules++;
  saveSchedulesToEEPROM();
  TEST_schedules_variable_data();
  server.sendHeader("Location", "/");
  server.send(303);
}
void handleDeleteSchedule_POST()
{
  String index = server.arg("index");
  Serial.printf("handling deletion of index : %s\n",index);
  deleteSchedule(index.toInt());
  server.sendHeader("Location", "/");
  server.send(303);
}
void handleDeleteAllSchedule_POST()
{
  deleteAllSchedules();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSettings_GET()
{
  String html = R"(
  <html>
    <head>
      <meta charset='UTF-8'>
      <title>⚙️ Settings</title>
    </head>
  <body>
    <h1>Configuration Page</h1>
    <form method='POST' action="/settings">
        <h3>Network Mode:</h3>
        <input type='radio' name='network_mode' value = 'ap' onclick='changeLabel(this.value)'> Hotspot ( AP )<br/>
        <input type='radio' value="station" name='network_mode' onclick='changeLabel(this.value)'> Wi-Fi ( station )
        <h3>IP Address</h3>192.168.1. 
        <input type="number" name="ip" min="10" max="250" placeholder=")" +
                String(custom_ip) + R"(" style="width:4em"/><br/>
        <h3 id="cred_label" >Network Credentials (WiFi)</h3>
        SSID  : <input name="ssid" style="margin-left: 25px"/><br/>
        Password : <input name="password"/><br/><br/><br>
        <button type="submit">Save Changes</button>
    </form>
    <script>
      function changeLabel(value) {
        var label = document.getElementById('cred_label');
        if (value === 'ap') {
          label.innerHTML = 'Network Credentials (Hotspot)';
          
        } else {
          label.innerHTML = 'Network Credentials (WiFi)';
        }
      }
    </script>
  </body>
  </html>
  )";
  server.send(200, "text/html", html);
}

void handleSettings_POST()
{

  Serial.print("setting saved");
  String network_mode = server.arg("network_mode");
  String wifi_ssid = server.arg("ssid");
  String wifi_password = server.arg("password");
  String ip_address = server.arg("ip");

  if (network_mode != "")
  {
    isModeStation = network_mode == "station";
    Serial.printf("Network is Station : %d\n", isModeStation);
  }

  if (ip_address != "")
  {
    custom_ip = ip_address.toInt();
    Serial.printf("IP set to : %d\n", custom_ip);
  }

  if (wifi_ssid != "")
  {
    if (network_mode == "ap")
    {
      ssid_AP = wifi_ssid;
      preferences.putString("ssid_AP",ssid_AP);
      Serial.printf("AP SSID set to : %s\n", ssid_AP);
    }
    else
    {
      ssid_STATION = wifi_ssid;
      preferences.putString("ssid_STATION",ssid_STATION);
      Serial.printf("STATION SSID set to : %s\n", ssid_STATION);
    }
  }

  if (wifi_password != "")
  {
    if (network_mode == "ap")
    {
      password_AP = wifi_password;
      preferences.putString("password_AP",password_AP);
      Serial.printf("AP Password set to : %s\n", password_AP);
    }
    else
    {
      password_STATION = wifi_password;
      preferences.putString("password_STATION",password_STATION);
      Serial.printf("STATION Password set to : %s\n", password_STATION);
    }
  }

  server.send(200, "text/html", "saved");
  // flush any pending data to the client
  server.client().flush();

  // server.stop();
  // delay(10);
  establishNetwork();
}

void saveSchedulesToEEPROM()
{
  EEPROM.begin(512);
  int address = 0;

  EEPROM.write(address, num_schedules);

  address += sizeof(num_schedules);

  for (int i = 0; i < num_schedules; i++)
  {
    EEPROM.put(address, schedules[i]);
    address += sizeof(schedules[i]);
  }
  EEPROM.end();
}

void loadSchedulesFromEEPROM()
{
  EEPROM.begin(512);
  int address = 0;
  EEPROM.get(address, num_schedules);

  if (num_schedules > MAX_SCHEDULES)
  {
    num_schedules = MAX_SCHEDULES;
  }

  address += sizeof(num_schedules);

  for (int i = 0; i < MAX_SCHEDULES; i++)
  {
    EEPROM.get(address, schedules[i]);
    address += sizeof(schedules[i]);
  }
  EEPROM.end();
}

void deleteSchedule(int index)
{
  schedules[index].isDeleted = true;
  saveSchedulesToEEPROM();
  TEST_schedules_variable_data();
  digitalWrite(LED_PIN, lightStatus());
}

void deleteAllSchedules()
{
  num_schedules = 0;
  EEPROM.begin(512);
  EEPROM.write(0, num_schedules);

  for (int i = 0; i < MAX_SCHEDULES; i++)
    schedules[i].isDeleted = true;

  digitalWrite(LED_PIN, defaultLEDState);
  saveSchedulesToEEPROM();
  EEPROM.end();
}

bool subscriptionStatus()
{
  HTTPClient http;
  WiFiClient wifiClient;
  bool status;
  bool expired;
  StaticJsonDocument<256> response;
  StaticJsonDocument<48> request;
  const char *mac_address;
  int expiry_date_year;
  int expiry_date_month;
  int expiry_date_day;
  bool isExpiredLocal = true;
  String postData;

  http.begin(wifiClient, "http://202.79.43.18:1080/get_expiry_date.php");
  http.addHeader("Content-Type", "application/json");

  //----Format_of_request-------------//
  //    {
  //      "client_mac_address": "40:F5:20:23:01:2E";
  //    }
  //----------------------------------//
  request["client_mac_address"] = WiFi.macAddress();
  serializeJson(request, postData);

  int httpResponseCode = http.POST(postData);
  Serial.println(WiFi.macAddress());

  // Check for successful POST request
  if (httpResponseCode > 0)
  {
    Serial.printf("HTTP POST request success, response code: %d\n", httpResponseCode);
    DeserializationError error = deserializeJson(response, http.getStream());

    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return false;
    }
    status = response["status"];                          // true
    expired = response["expired"];                        // false
    mac_address = response["mac_address"];                // "40:F5:20:23:01:2E"
    expiry_date_year = response["expiry_date"]["year"];   // 2023
    expiry_date_month = response["expiry_date"]["month"]; // 4
    expiry_date_day = response["expiry_date"]["day"];     // 23

    //-----format of response---------//
    // {
    // "status": true,
    // "expired": true,
    // "mac_address": "40:F5:20:23:01:2E",
    // "expiry_date": {
    //     "year": 2023,
    //     "month": 4,
    //     "day": 23
    // }
    //---------------------------------//
  }
  else
  {
    Serial.printf("HTTP POST request failed, error code: %d\n", httpResponseCode);
  }
  http.end();

  Serial.printf("Status: %d\n", status);
  Serial.printf("Expired: %d \n", expired);
  Serial.printf("Mac Address: %s\n", mac_address);
  Serial.printf("Expiry date year: %d\n", expiry_date_year);
  Serial.printf("Expiry date month: %d\n", expiry_date_month);
  Serial.printf("Expiry date day: %d\n", expiry_date_day);

  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);

  bool isActiveLocal = (expiry_date_year > timeinfo->tm_year ||
                        (expiry_date_year == timeinfo->tm_year && expiry_date_month > timeinfo->tm_mon) ||
                        (expiry_date_year == timeinfo->tm_year && expiry_date_month == timeinfo->tm_mon && expiry_date_day > timeinfo->tm_mday));

  if (status == true &&
      expired == false &&
      WiFi.macAddress().equals(mac_address) &&
      isActiveLocal == true)
  {
    Serial.println("Subscription Active");
    // return true;
  }
  else
  {
    Serial.println("Subscription Expired");
    // return false;
  }
  http.end();
  // http.begin(wifiClient, "172.217.164.110");
  http.begin(wifiClient, "http://www.google.com/");
  httpResponseCode = http.GET();
  // Check for successful POST request
  if (httpResponseCode > 0)
  {
    Serial.printf("Google Working, response code: %d\n", httpResponseCode);
  }else {
    Serial.println("no google");
  }return true;
}

// test function-----------------//
//------------------------------//
void TEST_schedules_variable_data()
{
  Serial.println("-------TESTING SCHEDULES DATA------------");
  Serial.printf("Index | hour | tMinutes | Duration | Deleted?\n");
  for (int i = 0; i < MAX_SCHEDULES; i++)  {
    Serial.printf("%d     |  %d  |   %d     |   %d   | %d\n", i, schedules[i].start_time_hour, schedules[i].start_time_min, schedules[i].duration, schedules[i].isDeleted);
  }
  Serial.println("-----------------------------------------");
}