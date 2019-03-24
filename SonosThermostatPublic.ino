//Build Options for Sonos Basic:
//Generic ESP8266 Module
//ESP8266 core v2.5.0
//Flash Mode DOUT
//Flash Size:  1M (128k SPIFFS)
//lwIP variant:  v2 Lower Memory
//Crystal Freq:  26 MHz
//Flash Frequency:  40 MHz
//CPU Frequency:  80 MHz

//TODO
//Automatic time adjustment for daylight savings time

#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <TimeLib.h>
#include <math.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <ESP8266FtpServer.h>
/*********************************************************
 * CHANGE THESE PARAMETERS
 * ssid = your WiFi SSID name
 * password = your WiFi password
 * temp_ramp_rate_deg_per_min = rate at which your floor heats up in degrees per minute.  
 *   Used for predictive turn on.  Set to a really large number to effectively disable and use 
 *   a 'normal' thermostat with no predictive turn on
 * UDPhost = IP address of the computer you want to send debugging data to via UDP
 * UDPPort = UDP port to send out debugging info on
 */
const char* ssid = "SSID";
const char* password = "Password";
float temp_ramp_rate_deg_per_min = .0833; //20 degrees in 4 hours = 5 degrees per hour = .0833 degrees per minute
const char* UDPhost = "192.168.1.141";  //Address to send UDP data logging packets to
const int UDPPort = 21623;				//Port to send UDP data logging packets to
/**************************************************************/

String text = "";
WiFiUDP ntpUDP;
WiFiUDP UDPsender;
const int OutPin = 12;
uint32_t last_send_time = 0;
uint32_t last_filter_time = 0;
float filtered_temp_f = 68;  //starting value for filtering to reduce time it takes to stabilize
float temp_f = 0;
float set_temp_f = 0;  
float hyst = 1;  //hysteresis in degrees F for on/off bang-bang control 
float filter_alpha = .01;  //weight of the temperature filter
float ratio = 1;
float C = 25;
uint16_t ADC = 0;
boolean OutStatus = 0;
uint32_t WeekTimes[4];
uint8_t WeekSetTemps[4];
uint32_t SatTimes[4];
uint8_t SatSetTemps[4];
uint32_t SunTimes[4];
uint8_t SunSetTemps[4];
float temp_history[1440] = {};  //array of elements for one temperature measurement every 5 minutes over last 24 hours
uint32_t last_history_save_time = 0;
int8_t DST_Offset = 0;  //daylight savings time offset in hours

//Web server to host our thermostat GUI on
ESP8266WebServer server ( 80 );
/*
 * Set up FTP server to make it easier to update HTML page
 * default username is esp8266
 * default password is esp8266
 * defaults can be changed in setup() function below
 */
FtpServer ftpSrv;

// You can specify the time server pool and the offset, (in seconds)
// additionaly you can specify the update interval (in milliseconds).
NTPClient timeClient(ntpUDP, "us.pool.ntp.org", 0, 60000);

/*
time_t ntpSyncProvider() {
  return timeClient.getEpochTime();
}
*/

/*Function to send debugging info out via UDP port  */
void debugprint(String TextToSend){
  Serial.println(TextToSend);
  char SendCharArray[35];
  TextToSend.toCharArray(SendCharArray,35);
  UDPsender.beginPacket(UDPhost,UDPPort);
  UDPsender.write(SendCharArray);
  UDPsender.endPacket();
  delay(50);
}

/*Add a new temperature value to our history array*/
void add_to_history(float new_val){
  //shift to the right, new value goes in the leftmost position
  for(int i=1440-2;i>=0;i--)
  {
      temp_history[i+1] = temp_history[i]; //move all element to the right except last one
  }
  temp_history[0] = new_val;
  last_history_save_time = millis();
}

/*Read in the text file stored on SPIFFS containing our temperature set point schedule*/
void readschedule(){
  debugprint("reading schedule from SPIFFS");
  File f = SPIFFS.open("/schedule.txt","r");
  //First line is timestamps for Week Day schedule
  String temp = f.readStringUntil(',');
  WeekTimes[0] = temp.toInt();
  temp = f.readStringUntil(',');
  WeekTimes[1] = temp.toInt();
  temp = f.readStringUntil(',');
  WeekTimes[2] = temp.toInt();
  temp = f.readStringUntil('\n');
  WeekTimes[3] = temp.toInt();
  //Second line is temperature set points for Week Day Schedule
  temp = f.readStringUntil(',');
  WeekSetTemps[0] = temp.toInt();
  temp = f.readStringUntil(',');
  WeekSetTemps[1] = temp.toInt();
  temp = f.readStringUntil(',');
  WeekSetTemps[2] = temp.toInt();
  temp = f.readStringUntil('\n');
  WeekSetTemps[3] = temp.toInt();
  //Third line is timestamps for Saturday schedule
  temp = f.readStringUntil(',');
  SatTimes[0] = temp.toInt();
  temp = f.readStringUntil(',');
  SatTimes[1] = temp.toInt();
  temp = f.readStringUntil(',');
  SatTimes[2] = temp.toInt();
  temp = f.readStringUntil('\n');
  SatTimes[3] = temp.toInt();
  //Fourth line is temperature set points for Saturday Schedule
  temp = f.readStringUntil(',');
  SatSetTemps[0] = temp.toInt();
  temp = f.readStringUntil(',');
  SatSetTemps[1] = temp.toInt();
  temp = f.readStringUntil(',');
  SatSetTemps[2] = temp.toInt();
  temp = f.readStringUntil('\n');
  SatSetTemps[3] = temp.toInt();
  //Fifth line is timestamps for Sunday schedule
  temp = f.readStringUntil(',');
  SunTimes[0] = temp.toInt();
  temp = f.readStringUntil(',');
  SunTimes[1] = temp.toInt();
  temp = f.readStringUntil(',');
  SunTimes[2] = temp.toInt();
  temp = f.readStringUntil('\n');
  SunTimes[3] = temp.toInt();
  //Sixth line is temperature set points for Sunday Schedule
  temp = f.readStringUntil(',');
  SunSetTemps[0] = temp.toInt();
  debugprint(String(SunSetTemps[0]));
  temp = f.readStringUntil(',');
  SunSetTemps[1] = temp.toInt();
  debugprint(String(SunSetTemps[1]));
  temp = f.readStringUntil(',');
  SunSetTemps[2] = temp.toInt();
  debugprint(String(SunSetTemps[2]));
  temp = f.readStringUntil('\n');
  SunSetTemps[3] = temp.toInt();
  debugprint(String(SunSetTemps[3]));
  f.close();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  delay(5000);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  if (!SPIFFS.begin())
    {
      // Serious problem
      Serial.println("SPIFFS Mount failed");
      UDPsender.beginPacket(UDPhost,UDPPort);
      UDPsender.write("SPIFSS Mount failed");
      UDPsender.endPacket();
    } else {
      Serial.println("SPIFFS Mount succesfull");
      UDPsender.beginPacket(UDPhost,UDPPort);
      UDPsender.write("SPIFFS Mount succesfull");
      UDPsender.endPacket();
      ftpSrv.begin("esp8266", "esp8266"); // username, password for ftp. Set ports in ESP8266FtpServer.h (default 21, 50009 for PASV)
    }
  

  //specify static pages stored in SPIFFS
  server.serveStatic("/", SPIFFS, "/index.html");
  server.serveStatic("/schedule.txt",SPIFFS,"/schedule.txt");
  
  //'virtual' file that gets 'created' and sent when /data.txt is requested (for AJAX data request)
  server.on("/data.txt", [](){
   text = "";
   text.concat((String)filtered_temp_f);
   text.concat(",");
   text.concat((String)set_temp_f);
   text.concat(",");
   text.concat((String)OutStatus);
   text.concat(",");
   text.concat((String)timeClient.getDay());
   server.send(200, "text/html", text);
   });
   
   //'virtual' file that gets 'created' and sent when /temphistory.txt is requested (for AJAX data request)
   server.on("/temphistory.txt", [](){
    text="";
    for (int i=0;i<1440;i++){
     text.concat(String(temp_history[i]));
     if (i!=1439){
      text.concat(",");
     }
    }
    server.send(200, "text/plain", text);
   });
   
   //write updated schedule that was recieved via arguments to SPIFFS file
   server.on("/updateschedule.php",[](){
    debugprint(server.arg("line1"));
    debugprint(server.arg("line2"));
    debugprint(server.arg("line3"));
    debugprint(server.arg("line4"));
    debugprint(server.arg("line5"));
    debugprint(server.arg("line6"));
    server.send(200);
    File f = SPIFFS.open("/schedule.txt","w");
    f.println(server.arg("line1"));
    f.println(server.arg("line2"));
    f.println(server.arg("line3"));
    f.println(server.arg("line4"));
    f.println(server.arg("line5"));
    f.println(server.arg("line6"));
    f.close();
    readschedule();
   });
   
  server.begin();
  Serial.println ( "HTTP server started" );
  UDPsender.beginPacket(UDPhost,UDPPort);
  UDPsender.write("HTTP server started");
  UDPsender.endPacket();

  readschedule();
  
  pinMode(A0,INPUT);
  pinMode(OutPin,OUTPUT);
  digitalWrite(OutPin,LOW);

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
   ArduinoOTA.setHostname("mySonosTstat");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
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
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  timeClient.begin();
  timeClient.update();
  //setSyncProvider(&ntpSyncProvider);
  
}

void loop() {
  ArduinoOTA.handle();    //handle any over the air software update requests that come in
  server.handleClient();  //handle any HTTP rqeuests that come in
  ftpSrv.handleFTP();     //handle any FTP requests that come in

  //Turn output on or off depending on current state
  if (filtered_temp_f<set_temp_f){
    digitalWrite(OutPin,HIGH);
  }
  if (filtered_temp_f>(set_temp_f + hyst)){
    digitalWrite(OutPin,LOW);
  }

  //every 100ms, read in temperature and update filtered temperature value
  if (millis()-last_filter_time > 100){
    ADC = analogRead(A0);
    //ratio = (-ADC*100000/(ADC-1024*3.3))/10000; //100k pull up to 3.3v, 10k thermistor
    ratio = (2300*3.3/(ADC/1024.0)-2300.0)/10000.0; //2.2k pull down, 3.3v pull up voltage, 10k thermistor

    C = -23.28*log(ratio) + 25.994;  //themistor curve fit from datasheet
    temp_f = C*(9.0/5.0)+32;         
    filtered_temp_f = filtered_temp_f*(1-filter_alpha)+temp_f*filter_alpha;
    //saturate at 10F or 150F
    if (filtered_temp_f > 150){
       filtered_temp_f = 150;
    }
    if (filtered_temp_f < 10){
      filtered_temp_f = 10;
    }
    last_filter_time = millis();
  }
  
  OutStatus = digitalRead(OutPin);
  if(millis()-last_history_save_time > 60000){  //once per minute, add to our history stored in RAM
    add_to_history(filtered_temp_f);
  }
  /*Every ten seconds, do these things:
   *- Figure out if we're in daylight savings time or not and adjsut UTC offset accordingly
   *- Send data out via UDP port in case someone cares to listen
   *- Determine what our set point should be
   *   - When our next temperature set point is higher than current, turn on early
   *     so that set point is achieved by the desired time.  
   *   - When our next temperature set point is equal or less than current, turn off at the next time
   *     (no predictive cooling)
    */
  
  if (millis()-last_send_time > 10000){
    timeClient.forceUpdate();
    if (timeClient.getMinutes() >= 0){
      //here we can get the date and call timeClient.setTimeOffset() to change offset (in seconds) depending on whether we're in DST or not
      //DST in USA is from second sunday in March to first Sunday in November
      //Central time offset from UTC is -6 hours during CDT and -5 hours during CST
      int8_t day_of_month = day(timeClient.getEpochTime());
      int8_t day_of_week = timeClient.getDay();
      uint8_t current_month = month(timeClient.getEpochTime());
      
      //There's got to be a better way to figure out if we're in DST or not...
      //changes will occur at midnight rather than 2 a.m.  For this, who cares, close enough
      if ((current_month == 3 && (day_of_month-8-day_of_week >= 0)) ||
          (current_month >=4 && current_month <=10) ||
          (current_month == 11 && (day_of_month-8-day_of_week < 0))){
            //in daylight savings time
            DST_Offset = -5;
      }
      else {
        //not in daylight savings time
        DST_Offset = -6;
      }
      timeClient.setTimeOffset(DST_Offset*60*60); 
      timeClient.forceUpdate();
      //Serial.println(timeClient.getFormattedTime());
     }
    String SendString = String(timeClient.getHours());
    SendString.concat(":");
    SendString.concat(String(timeClient.getMinutes()));
    SendString.concat(":");
    SendString.concat(String(timeClient.getSeconds()));
    SendString.concat(",");
    SendString.concat(String(ADC));
    SendString.concat(",");
    SendString.concat(String(OutStatus));
    SendString.concat(",");
    SendString.concat(String(set_temp_f));
    SendString.concat(",");
    SendString.concat(String(timeClient.getEpochTime()));
    SendString.concat(",");
    SendString.concat(filtered_temp_f);
    char SendCharArray[35];
    SendString.toCharArray(SendCharArray,35);
    UDPsender.beginPacket(UDPhost,UDPPort);
    UDPsender.write(SendCharArray);
    UDPsender.endPacket();
    last_send_time = millis();
    
    if (timeClient.getDay()==0){  //Sunday
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 <= SunTimes[0]){
        if (SunSetTemps[0] > set_temp_f){  
          if (((SunSetTemps[0]-set_temp_f)/(SunTimes[0]/60.0 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = SunSetTemps[0];  //preheating to next set point
          }
        }
      }
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 > SunTimes[0]){
        set_temp_f = SunSetTemps[0];
        //If the next step is up, check to see if we should start pre-heating
        if (SunSetTemps[1] > set_temp_f){  
          if (((SunSetTemps[1]-set_temp_f)/(SunTimes[1]/60.0 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = SunSetTemps[1];  //preheating to next set point
          }
        }
      }
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 > SunTimes[1]){
        set_temp_f = SunSetTemps[1];
        //If the next step is up, check to see if we should start pre-heating
        if (SunSetTemps[2] > set_temp_f){  
          if (((SunSetTemps[2]-set_temp_f)/(SunTimes[2]/60.0 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = SunSetTemps[2];  //preheating to next set point
          }
        }
      }
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 > SunTimes[2]){
        set_temp_f = SunSetTemps[2];
        //If the next step is up, check to see if we should start pre-heating
        if (SunSetTemps[3] > set_temp_f){  
          if (((SunSetTemps[3]-set_temp_f)/(SunTimes[3]/60.0 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = SunSetTemps[3];  //preheating to next set point
          }
        }
      }
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 > SunTimes[3]){
        set_temp_f = SunSetTemps[3];
        //If the next step is up, check to see if we should start pre-heating
        if (WeekSetTemps[0] > set_temp_f){  
          if (((WeekSetTemps[0]-set_temp_f)/(WeekTimes[0]/60.0+24*60 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = WeekSetTemps[0];  //preheating to next set point
          }
        }
      }
    }
    else if (timeClient.getDay()==6){  //Saturday
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 <= SatTimes[0]){
        if (SatSetTemps[0] > set_temp_f){  
          if (((SatSetTemps[0]-set_temp_f)/(SatTimes[0]/60.0 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = SatSetTemps[0];  //preheating to next set point
          }
        }
      }
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 > SatTimes[0]){
        set_temp_f = SatSetTemps[0];
        //If the next step is up, check to see if we should start pre-heating
        if (SatSetTemps[1] > set_temp_f){  
          if (((SatSetTemps[1]-set_temp_f)/(SatTimes[1]/60.0 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = SatSetTemps[1];  //preheating to next set point
          }
        }
      }
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 > SatTimes[1]){
        set_temp_f = SatSetTemps[1];
        //If the next step is up, check to see if we should start pre-heating
        if (SatSetTemps[2] > set_temp_f){  
          if (((SatSetTemps[2]-set_temp_f)/(SatTimes[2]/60.0 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = SatSetTemps[2];  //preheating to next set point
          }
        }
      }
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 > SatTimes[2]){
        set_temp_f = SatSetTemps[2];
        //If the next step is up, check to see if we should start pre-heating
        if (SatSetTemps[3] > set_temp_f){  
          if (((SatSetTemps[3]-set_temp_f)/(SatTimes[3]/60.0 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = SatSetTemps[3];  //preheating to next set point
          }
        }
      }
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 > SatTimes[3]){
        set_temp_f = SatSetTemps[3];
        //If the next step is up, check to see if we should start pre-heating
        if (SunSetTemps[0] > set_temp_f){  
          if (((SunSetTemps[0]-set_temp_f)/(SunTimes[0]/60.0+24*60 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = SunSetTemps[0];  //preheating to next set point
          }
        }
      }
    }
    else{ //Weekday
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 <= WeekTimes[0]){
        if (WeekSetTemps[0] > set_temp_f){  
          if (((WeekSetTemps[0]-set_temp_f)/(WeekTimes[0]/60.0 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = WeekSetTemps[0];  //preheating to next set point
          }
        }
      }
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 > WeekTimes[0]){
        set_temp_f = WeekSetTemps[0];
        //If the next step is up, check to see if we should start pre-heating
        if (WeekSetTemps[1] > set_temp_f){  
          if (((WeekSetTemps[1]-set_temp_f)/(WeekTimes[1]/60.0 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = WeekSetTemps[1];  //preheating to next set point
          }
        }
      }
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 > WeekTimes[1]){
        set_temp_f = WeekSetTemps[1];
        //If the next step is up, check to see if we should start pre-heating
        if (WeekSetTemps[2] > set_temp_f){  
          if (((WeekSetTemps[2]-set_temp_f)/(WeekTimes[2]/60.0 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = WeekSetTemps[2];  //preheating to next set point
          }
        }
      } 
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 > WeekTimes[2]){
        set_temp_f = WeekSetTemps[2];
        //If the next step is up, check to see if we should start pre-heating
        if (WeekSetTemps[3] > set_temp_f){  
          if (((WeekSetTemps[3]-set_temp_f)/(WeekTimes[3]/60.0 - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = WeekSetTemps[3];  //preheating to next set point
          }
        }
      }
      if (timeClient.getHours()*60*60+timeClient.getMinutes()*60 > WeekTimes[3]){
        set_temp_f = WeekSetTemps[3];
        //If the next step is up, check to see if we should start pre-heating
        //If it's not Friday, keep using Weekday for the next set point
        if (WeekSetTemps[0] > set_temp_f && timeClient.getDay() != 5){  
          if (((WeekSetTemps[0]-set_temp_f)/((WeekTimes[0]/60.0+24*60) - (timeClient.getHours()*60+timeClient.getMinutes()))) > temp_ramp_rate_deg_per_min){
            set_temp_f = WeekSetTemps[0];  //preheating to next set point
          }
        }
        //If it's Friday, use Saturday for the next set point
        else if (SatSetTemps[0] > set_temp_f && timeClient.getDay() == 5){  
          if (((SatSetTemps[0]-set_temp_f)/(SatTimes[0]/60.0+24*60 - (timeClient.getHours()*60+timeClient.getMinutes()))) < temp_ramp_rate_deg_per_min){
            set_temp_f = SatSetTemps[0];  //preheating to next set point
          }
        }
      }
    }
  }
  
  
}
