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
#include <chrono>

WiFiManager wifi;
Preferences preferences;

//---------Urgent Ring Time-----//
uint16_t UrgentOnTime = 10000 ; //10000 ms -> 10sec
//------------------------------//

//---------Static IP-----------//
uint8_t custom_ip = 222;

//---------Switch Pin-----------//
const uint8_t SWITCH_PIN = D5;
//-----------------------------//
//-----------------------------//

bool lightStatus();
void startWebServer();
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
void handleToogleSwitchState_POST();
void handleSyncTime_POST();
void handleRecheckSubscription_POST();
void handleUrgentOn_POST();
void deleteSchedule(int);
void deleteAllSchedules();
void establishNetwork(bool);
void setCustomIPandSaveNetwork();
uint16_t getCurrentTimeInSeconds();
int getExpiryTime();
bool subscriptionStatus();
bool syncTime();
void reconnectNetworkWithCustomIP();
void saveCredentialsSTATION();
bool isSavedSubscriptionActive();
String macAddressinDecimal();
//--test/debug functions (meant to be removed on production)--//
void TEST_schedules_variable_data();
//-----------------------------//

bool defaultSwitchState;
const char* pinNames[] = {
    "D3", "D10", "D4", "D9", "D2", "D1", "-", "-", "-", "D11", "D12", "-", "D6", "D7", "D5", "D8", "D0"
};



String ssid_STATION = "1011001";
String password_STATION = "dr0wss@p";
String ssid_AP = "Smart_Switch";
String password_AP = "dr0wss@p";
bool isModeStation = true;
bool isActive = false;
const int timeOffsetHour = 5;
const int timeOffsetMin = 45;
// const String serverLocation = "http://202.79.43.18:1080/";
// const String serverLocation = "http://192.168.43.111/modular_switch_control_system/";
const String serverLocation = "http://192.168.1.111/modular_switch_control_system/";

struct Schedule
{
  int start_time_hour;
  int start_time_min;
  int duration_minutes;
  int duration_seconds;
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
  defaultSwitchState = preferences.getBool("defaultSwitchState", HIGH);
  establishNetwork(0);
  pinMode(SWITCH_PIN, OUTPUT);
  syncTime();
  loadSchedulesFromEEPROM();
  startWebServer();
}

void loop()
{
  if(isSavedSubscriptionActive()){
    digitalWrite(SWITCH_PIN, lightStatus());
  }


  server.handleClient();

  //------------------------time sync(1day)----------------------//
  unsigned long syncInterval = 24 * 60 * 60 * 1000;  // 24 hours in milliseconds
  static unsigned long lastSyncTime = 0;
  if (millis() - lastSyncTime >= syncInterval) {
    syncTime();
    lastSyncTime = millis();
  }
  //-------------------------------------------------------------------------//

  //------------------------wifi connection (1min)----------------------//
  unsigned long wifiConnectionTimeout = 60000;
  static unsigned long lastConnectionTime = 0;
  if(WiFi.status() != WL_CONNECTED && millis()-lastConnectionTime >= wifiConnectionTimeout ) {
    lastConnectionTime = millis();
    Serial.println("wifi disconnected attempting reconnection");
    establishNetwork(true);
  }
  //-----------------------------------------------------------------------//


  if(isSavedSubscriptionActive() != isActive){

    Serial.println("changes detected starting server");
    Serial.printf("saved : %d  , isActive : %d",isSavedSubscriptionActive(),isActive );
    startWebServer();
  }
  delay(500);
}

bool lightStatus()
{
  uint16_t current_time = getCurrentTimeInSeconds(); // returns in form of minutes eg: 5:1 pm = 1021m
  Serial.print(pinNames[SWITCH_PIN]);

  for (int i = 0; i < num_schedules; i++)
  {
    if (schedules[i].isDeleted)
      continue;
    int start_time = schedules[i].start_time_hour * 3600 + schedules[i].start_time_min *60 ;
    int duration = schedules[i].duration_minutes *60 + schedules[i].duration_seconds;

    if (start_time <= current_time && current_time < start_time + duration)
    {
      Serial.printf(" : ON (%d)",!defaultSwitchState);
      return !defaultSwitchState;
    }
  }
  Serial.printf(" : OFF (%d)",defaultSwitchState);
  return defaultSwitchState;
}

void establishNetwork(bool reconnect_WiFi)
{
  ssid_STATION = preferences.getString("ssid_STATION",ssid_STATION);
  ssid_AP = preferences.getString("ssid_AP",ssid_AP);
  password_AP = preferences.getString("password_AP",password_AP);

  WiFi.mode(WIFI_AP_STA);
  //starts hotspot

  
  WiFi.config(IPAddress(),IPAddress(),IPAddress());
  WiFi.begin(ssid_STATION,password_STATION);// start connection
  //WAIT 10 SEC FOR CONNECTION
  if(reconnect_WiFi == false){
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
      Serial.println("Connecting to wifi ..");
    }
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
  if(reconnect_WiFi == false){
    if(WiFi.softAP(ssid_AP,password_AP )){
      Serial.print("Soft ap successful \n IP : ");
      Serial.println(WiFi.softAPIP());
    }else
    {
      Serial.println("Soft AP unsuccessful!");
    }
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
  if (!WiFi.isConnected())
  {
    Serial.printf("No network connection available. Time synchronization failed. \n");
    return false;
  }

  configTime(timeOffsetHour * 3600 + timeOffsetMin * 60, 0,
             "pool.ntp.org",
             "time.google.com"
             );
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

uint16_t getCurrentTimeInSeconds()
{
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);

  // Print the date and time
  Serial.printf(" | Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                timeinfo->tm_year + 1900,
                timeinfo->tm_mon + 1,
                timeinfo->tm_mday,
                timeinfo->tm_hour,
                timeinfo->tm_min,
                timeinfo->tm_sec);

  return timeinfo->tm_hour * 3600 + timeinfo->tm_min * 60 + timeinfo->tm_sec;
}
void startWebServer(){
  bool expiry_date_set = preferences.getBool("expiry_date_set");
  if (expiry_date_set && isSavedSubscriptionActive() || subscriptionStatus() == true){
    isActive = true;
    startWebServer_ACTIVE();
  }else {
    isActive = false;
    startWebServer_EXPIRED();
  }
}

void startWebServer_ACTIVE()
{
  server.on("/", HTTP_GET, handleRoot_GET);
  server.on("/", HTTP_POST, handleRoot_POST);
  server.on("/delete-schedule", HTTP_POST, handleDeleteSchedule_POST);
  server.on("/delete-all-schedule", HTTP_POST, handleDeleteAllSchedule_POST);
  server.on("/settings", HTTP_GET, handleSettings_GET);
  server.on("/settings", HTTP_POST, handleSettings_POST);
  server.on("/toogle-switch-state", HTTP_POST, handleToogleSwitchState_POST);
  server.on("/sync-time",HTTP_POST, handleSyncTime_POST);
  server.on("/urgent-on",HTTP_POST, handleUrgentOn_POST);
  server.begin();
  Serial.println("light Scheduling HTTP Server started");
}

void startWebServer_EXPIRED()
{
  server.on("/", HTTP_GET, []()
            { server.send(200, "text/html", R"(
              <html>
              <head><meta charset='UTF-8'></head>
              <body>
                <h1>Subscription Expired!! </h1>
                <form method='GET' action='/settings'>
                  <button type='submit'>⚙️ Settings</button>
                </form>
                <form method='POST' action='/recheck-subscription'>
                  <button type='submit'>Check Subscription</button>
                </form>
              </body>
              </html>
            )"); });
              
  server.on("/settings", HTTP_GET, handleSettings_GET);
  server.on("/settings", HTTP_POST, handleSettings_POST);
  server.on("/recheck-subscription",HTTP_POST, handleRecheckSubscription_POST);
  server.on("/toogle-switch-state", HTTP_POST, handleToogleSwitchState_POST);
  server.on("/sync-time",HTTP_POST, handleSyncTime_POST);
  server.begin();
  Serial.println("Subscription Expired Server");
}

void handleRoot_GET()
{
  char *format = "";
  uint8_t start_time_hour;
  uint16_t expiry_date_year = preferences.getUShort("expiry_date_year");
  uint8_t expiry_date_month = preferences.getUShort("expiry_date_month");
  uint8_t expiry_date_day = preferences.getUShort("expiry_date_day");

  Serial.println("handling / get request");
  TEST_schedules_variable_data();

  String html = R"(
    <html>
      <head>
        <meta charset='UTF-8'><title>Switch Scheduler</title>
        <style>
          form{
            display:inline;
          }
        </style>
      </head>
      <body>
        <h1 style="margin:0px">
          Switch Scheduler
          <form style='display:inline' method='GET' action='/settings'>
            <button type='submit'>⚙️ Settings</button>
          </form>
        </h1>
        <i>( Expires on: &nbsp; )" +String(expiry_date_year) + R"( / )" + String(expiry_date_month) + R"( / )" + String(expiry_date_day) + R"( )</i><br><br><br>
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
        <td>)" + String(schedules[i].duration_minutes) + " min :" + String(schedules[i].duration_seconds) + R"( sec</td>
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
          Duration : &nbsp&nbsp
          <input type='number' name='duration_minutes' placeholder='min' min="0" max="1439" size="5" >
          <input type='number' name='duration_seconds' placeholder='sec' min="0" max="59" size="5" >
          <br><br>
          <input type='submit' value='Save'>
        </form>
          &nbsp;&nbsp;&nbsp;
        <form method='POST' action='/urgent-on'>
          <button name="urgent-on" type='submit'>Urgent ()"+String(UrgentOnTime / 1000) + R"(s)</button>
        </form><br><br>
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
  String duration_minutes = "";
  String duration_seconds = "";

  for (int i = 0; i < MAX_SCHEDULES; i++)
  {

    if (!server.hasArg("start_time"))
    {
      break;
    }
    // Serial.println(server.arg("plain"));
    start_time_hour = server.arg("start_time").substring(0, 2);
    start_time_min = server.arg("start_time").substring(3);
    duration_minutes = server.arg("duration_minutes");
    duration_seconds = server.arg("duration_seconds");

    if (start_time_hour != "" && duration_minutes != "" && duration_seconds != "" && schedules[i].isDeleted)
    {
      Serial.printf("Writing Schedule to Index : %d\n",i);
      schedules[i].start_time_hour = start_time_hour.toInt();
      schedules[i].start_time_min = start_time_min.toInt();
      schedules[i].duration_minutes = duration_minutes.toInt();
      schedules[i].duration_seconds = duration_seconds.toInt();
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
  String macAddress = macAddressinDecimal();

  String sync_result = syncTime() ? 
              "<span class='green'>Successful</span>" :
              "<span class='red'>Unsuccessful</span>";

  String wifi_state = WiFi.status() == WL_CONNECTED ? 
              "<span class='green'>Connected</span>" : 
              "<span class='red'>Disconnected</span>";

  String html = R"(
  <html>
    <head>
      <meta charset='UTF-8'>
      <title>⚙️ Settings</title>
      <style>
        form,h4 {
          display:inline;
        }
        .red{color:red;}
        .green{color:green;}
      </style>
    </head>
  <body>
    <h1>Configuration Page</h1>
    <form  method='POST' action="/settings">
        <h4> Time Syncronization :</h4> )"+ sync_result+ R"(<br><br>
        <h4> Network Status :</h4> )"+ wifi_state+ R"(<br><br>
        <h4> Serial No :</h4> )"+ macAddress + R"(<br><br>
        <h4> IP Address :</h4>192.168.1. 
        <input disabled type="number" name="ip" min="10" max="250" value=")" +
                String(custom_ip) + R"(" style="width:4em"/><br/><br/><br>
        <h3 id="cred_label" >Network Credentials (WiFi)</h3>
        SSID  : <input name="ssid" style="margin-left: 25px"/><br/><br>
        Password : <input name="password"/><br/><br/><br>
        <button type="submit">Save Changes</button>
    </form>
    <form method='POST' action='/sync-time'>
      <button name="sync-time" type='submit'>Sync Time</button>
    </form>
    <form method='POST' action='/toogle-switch-state'>
      <button name="toogle-switch-state" type='submit'>Toggle Switch State</button>
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
  establishNetwork(0);
}

void handleToogleSwitchState_POST(){
  Serial.println("Toggling default switch state");
  defaultSwitchState = !defaultSwitchState;
  preferences.putBool("defaultSwitchState",defaultSwitchState);
  Serial.printf("default state = %s",defaultSwitchState?"HIGH":"LOW");
  server.sendHeader("Location", "/settings");
  server.send(303);
}

void handleSyncTime_POST(){
  Serial.println("Syncing Time With NTP Server");
  syncTime();
  server.sendHeader("Location", "/settings");
  server.send(303);
}
void handleUrgentOn_POST(){
  digitalWrite(SWITCH_PIN,!defaultSwitchState);

  server.sendHeader("Location", "/");
  server.send(303);

  delay(UrgentOnTime);
  if(isSavedSubscriptionActive()){
    digitalWrite(SWITCH_PIN,lightStatus());
  }else{
    digitalWrite(SWITCH_PIN,defaultSwitchState);
  }
}
void handleRecheckSubscription_POST(){
  if(subscriptionStatus() == true){
    Serial.println("subscription activated on recheck");
    server.send(200,"text/html","Subscription Activated! Opening Appopriate Server");
    ESP.restart();
    // startWebServer();
  }else {
    Serial.println("subscription expired on recheck");
    server.sendHeader("Location", "/");
    server.send(303);
  }
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
  digitalWrite(SWITCH_PIN, lightStatus());
}

void deleteAllSchedules()
{
  num_schedules = 0;
  EEPROM.begin(512);
  EEPROM.write(0, num_schedules);

  for (int i = 0; i < MAX_SCHEDULES; i++)
    schedules[i].isDeleted = true;

  digitalWrite(SWITCH_PIN, defaultSwitchState);
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
  const char *mac_address = "";
  int expiry_date_year;
  int expiry_date_month;
  int expiry_date_day;
  bool isExpiredLocal = true;
  String postData;
  time_t now = time(nullptr);
  struct tm* currentDate = localtime(&now);
  tm expiry_date = {};

  // Serial.printf("\nFUNCTION-IS_SAVED_SUBSCRIPTION_ACTIVE\nExpiration Year : %d\nExpiration Month : %d\nExpiration Day: %d\n",expiry_date_year,expiry_date_month,expiry_date_day);
  // Serial.printf("------------\n Year : %d\n Month : %d\n Day: %d",timeinfo->tm_year+1900,timeinfo->tm_mon+ 1,timeinfo->tm_mday);

    

  http.begin(wifiClient, serverLocation + "get_expiry_date.php");
  http.addHeader("Content-Type", "application/json");

  //----Format_of_request-------------//
  //    {
  //      "client_mac_address": "40:F5:20:23:01:2E";
  //    }
  //----------------------------------//2
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
    status = response["status"];                          //true -record exists
    expired = response["expired"];                        // false
    mac_address = response["mac_address"];                // "40:F5:20:23:01:2E"
    expiry_date_year = response["expiry_date"]["year"];   // 2023
    expiry_date_month = response["expiry_date"]["month"]; // 4
    expiry_date_day = response["expiry_date"]["day"];     // 23

    preferences.putUShort("expiry_date_year",expiry_date_year);
    preferences.putUShort("expiry_date_month",expiry_date_month);
    preferences.putUShort("expiry_date_day",expiry_date_day);
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



  // time_t now = time(nullptr);
  // struct tm *timeinfo = localtime(&now);


  expiry_date.tm_year = preferences.getUShort("expiry_date_year",0) - 1900; // Set the year
  expiry_date.tm_mon = preferences.getUShort("expiry_date_month",0) -1;   // Set the month (0-11)
  expiry_date.tm_mday = preferences.getUShort("expiry_date_day",0) +1; // Set the day
    
  // difftime(time_1,time_2) => time_1 - time_2 (in seconds)

  // bool isActiveLocal = (expiry_date_year > timeinfo->tm_year ||
  //                       (expiry_date_year == timeinfo->tm_year && expiry_date_month > timeinfo->tm_mon) ||
  //                       (expiry_date_year == timeinfo->tm_year && expiry_date_month == timeinfo->tm_mon && expiry_date_day > timeinfo->tm_mday));

  bool isActiveLocal = difftime(mktime(&expiry_date),mktime(currentDate)) >=0;

  if (status == true &&
      expired == false &&
      macAddressinDecimal().equals(mac_address) &&
      isActiveLocal == true)
  {
    Serial.println("Subscription Active");
    preferences.putBool("expiry_date_set",HIGH);

    Serial.printf("Status: %d\n", status);
    Serial.printf("Expired: %d \n", expired);
    Serial.printf("Mac Address: %s\n", mac_address);
    Serial.printf("Expiry date year: %d\n", expiry_date_year);
    Serial.printf("Expiry date month: %d\n", expiry_date_month);
    Serial.printf("Expiry date day: %d\n", expiry_date_day);

    isActive = true;
    return true;
  }
  else
  {
    preferences.putBool("expiry_date_set",LOW);
    preferences.putUShort("expiry_date_year",0);
    preferences.putUShort("expiry_date_month",0);
    preferences.putUShort("expiry_date_day",0);
    Serial.println("Subscription Expired");
    isActive = false;
    return false;
  }
}
bool isSavedSubscriptionActive(){

  time_t now = time(nullptr);
  struct tm* currentDate = localtime(&now);

  uint16_t expiry_date_year = preferences.getUShort("expiry_date_year",0);
  uint8_t expiry_date_month = preferences.getUShort("expiry_date_month",0);
  uint8_t expiry_date_day = preferences.getUShort("expiry_date_day",0);

  // Serial.printf("\nFUNCTION-IS_SAVED_SUBSCRIPTION_ACTIVE\nExpiration Year : %d\nExpiration Month : %d\nExpiration Day: %d\n",expiry_date_year,expiry_date_month,expiry_date_day);
  // Serial.printf("------------\n Year : %d\n Month : %d\n Day: %d",timeinfo->tm_year+1900,timeinfo->tm_mon+ 1,timeinfo->tm_mday);

  tm expiry_date = {};
  expiry_date.tm_year = preferences.getUShort("expiry_date_year",0) - 1900; // Set the year
  expiry_date.tm_mon = preferences.getUShort("expiry_date_month",0) -1;   // Set the month (0-11)
  expiry_date.tm_mday = preferences.getUShort("expiry_date_day",0) +1; // Set the day
    
    // difftime(time_1,time_2) => time_1 - time_2 (in seconds)
    return difftime(mktime(&expiry_date),mktime(currentDate)) >=0;
    
    // bool response = difftime(mktime(&expiry_date),mktime(currentDate)) >=0;
    // Serial.printf("Comparisionn responsne : %d\n", response);
}
// test function-----------------//
//------------------------------//
void TEST_schedules_variable_data()
{
  Serial.println("-------TESTING SCHEDULES DATA------------");
  Serial.printf("Index | hour | tMinutes | Duration(M) | Duration(S) | Deleted?\n");
  for (int i = 0; i < MAX_SCHEDULES; i++)  {
    Serial.printf("%d     |  %d  |   %d     |   %d   |   %d   | %d\n", i, schedules[i].start_time_hour, schedules[i].start_time_min,schedules[i].duration_minutes, schedules[i].duration_seconds, schedules[i].isDeleted);
  }
  Serial.println("-----------------------------------------");
}

String macAddressinDecimal() {
  String decimalMac = "";

  // Remove colons from MAC address
  String sanitizedMac = WiFi.macAddress();
  sanitizedMac.replace(":", "");

  // Convert the entire MAC address from hexadecimal to decimal
  unsigned long long decimalValue = strtoull(sanitizedMac.c_str(), NULL, 16);
  decimalMac = String(decimalValue);

  return decimalMac;
}
