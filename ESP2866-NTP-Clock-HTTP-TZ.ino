//
//   NTP-Based Clock based on code from  https://steve.fi/Hardware/
//
//   This is a simple program which uses WiFi & an 4x7-segment display
//   to show the current time, complete with blinking ":".
//
//   Added a web configuration page which is available while the clock
//   is running.  This allows changing the timezone and brightness.
//   Parameters are saved to a json configuration file for persistence.
//   Yes, this is unnecessarily complex (could have used integer for both
//   settings) but I was learning the json stuff
//   along with pointers which I still haven't figured out completely!
//  
//   1/25/18 - Added single button to set the clock.  Long press (3 seconds)
//             places the clock into set mode then single press increments the TZ.
//             Single button click also sets the brightness of the display.
//
//   Alex T.


//
// WiFi & over the air updates
//
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "PMButton.h"

//
// For dealing with NTP & the clock.
//
#include "NTPClient.h"

//
// The display-interface
//
#include "TM1637.h"

//
// WiFi setup.
//
#include "WiFiManager.h"

//
// For fetching URLS & handling URL-parameters
//
#include "url_fetcher.h"
#include "url_parameters.h"

//
// Debug messages over the serial console.
//
#include "debug.h"

//
// The name of this project.
//
// Used for:
//   Access-Point name, in config-mode
//   OTA name.
//
#define PROJECT_NAME "NTP-CLOCK"

//
// The timezone - comment out to stay at GMT.
//
// #define TIME_ZONE (-7)

// Configuration that we'll store on disk
struct Config {
  char brightness[5];
  int timeZone;
};

const char *filename = "/config.txt";  // <- SD library uses 8.3 filenames
Config config;                         // <- global configuration object

//
// The HTTP-server we present runs on port 80.
//
WiFiServer server(80);

//
// NTP client, and UDP socket it uses.
//
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

//
// Pin definitions for TM1637 and can be changed to other ports
//
#define CLK D3
#define DIO D2
#define BRT D1

// Brightness variables
// We're setting the brightness with a button connected to the BRT digital pin
//
int int_brightness = 2;
int brtLevel = 1;  // This will be 0, 1, 2 for low med high brightness

TM1637 tm1637(CLK, DIO);

// Setup our button 
PMButton button1(BRT);

//
// Called just before the date/time is updated via NTP
//
void on_before_ntp()
{
    DEBUG_LOG("Updating date & time");
}
//
// Called just after the date/time is updated via NTP
//
void on_after_ntp()
{
    DEBUG_LOG("Updated NTP client");
}

//
// This function is called when the device is powered-on.
//
void setup()
{

    // Enable our serial port.
    Serial.begin(115200);

    // One time format of SPIFFS
    //DEBUG_LOG("Formatting SPIFFS...");
    //SPIFFS.format();
    //DEBUG_LOG("SPIFSS formatted please comment out this section!");
    //delay(100000);

    // Setup button
    button1.begin();
  
    //You can set button timing values for each button to fine tune interaction.
    button1.debounce(20);//Default is 10 milliseconds
    button1.dcGap(150);//Time between clicks for Double click. Default is 200 milliseconds
    button1.holdTime(1500);//Default is 2 seconds
    button1.longHoldTime(4500);//Default is 5 seconds

    //
    // Enable access to the SPIFFS filesystem.
    //
    if (!SPIFFS.begin()) {
       DEBUG_LOG("Failed to mount file system");
       return;
     }
    
    //
    // Set the intensity - valid choices include:
    //
    //   BRIGHT_DARKEST   = 0
    //   BRIGHT_TYPICAL   = 2
    //   BRIGHT_BRIGHTEST = 7
    //
    // tm1637.set(BRIGHT_DARKEST);

    //
    // Handle WiFi setup
    //
    WiFiManager wifiManager;
    wifiManager.autoConnect(PROJECT_NAME);

    // Load configuration from file if present, otherwise proceed with defaults
    //
    Serial.println("Loading configuration...");
    loadConfiguration(filename, config);

    Serial.print("Initial tz: ");
    Serial.println(config.timeZone);
    Serial.print("Initial brightness: ");
    Serial.println(config.brightness);
    
    // initialize the display
    tm1637.init();

    // We want to see ":" between the digits.
    tm1637.point(true);
    
  
    // Set the brightness after retrieving above
    //
    if (config.brightness != NULL)
    {
       if (strcmp(config.brightness, "low") == 0) {
           DEBUG_LOG("Setting brightness to low");
           int_brightness = BRIGHT_DARKEST;
       }
       if (strcmp(config.brightness, "med") == 0)  {
           DEBUG_LOG("Setting brightness to medium");        
           int_brightness = BRIGHT_TYPICAL;
       }
       if (strcmp(config.brightness, "high") == 0) {
           DEBUG_LOG("Setting brightness to brightest");        
           int_brightness = BRIGHT_BRIGHTEST;
       }
       tm1637.set(int_brightness);
    }
  
    //
    // Ensure our NTP-client is ready.
    //
    timeClient.begin();

    //
    // Configure the callbacks.
    //
    timeClient.on_before_update(on_before_ntp);
    timeClient.on_after_update(on_after_ntp);

    //
    // Setup the timezone & update-interval.
    //
    Serial.print("Setting time zone offset to timeZone: ");
    Serial.println(config.timeZone);
    timeClient.setTimeOffset(config.timeZone * (60 * 60));
    timeClient.setUpdateInterval(600 * 1000);  // This is the number of milliseconds so 300 * 1000 = 300 sec

    // Now we can start our HTTP server
    //
    server.begin();
    DEBUG_LOG("HTTP-Server started on http://%s/",
              WiFi.localIP().toString().c_str());

    //
    // The final step is to allow over the air updates
    //
    // This is documented here:
    //     https://randomnerdtutorials.com/esp8266-ota-updates-with-arduino-ide-over-the-air/
    //
    // Hostname defaults to esp8266-[ChipID]
    //
    ArduinoOTA.setHostname(PROJECT_NAME);

    ArduinoOTA.onStart([]()
    {
        DEBUG_LOG("OTA Start");
    });
    ArduinoOTA.onEnd([]()
    {
        DEBUG_LOG("OTA End");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
    {
        char buf[32];
        memset(buf, '\0', sizeof(buf));
        snprintf(buf, sizeof(buf) - 1, "Upgrade - %02u%%", (progress / (total / 100)));
        DEBUG_LOG(buf);
    });
    ArduinoOTA.onError([](ota_error_t error)
    {
        DEBUG_LOG("Error - ");

        if (error == OTA_AUTH_ERROR)
            DEBUG_LOG("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
            DEBUG_LOG("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
            DEBUG_LOG("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
            DEBUG_LOG("Receive Failed");
        else if (error == OTA_END_ERROR)
            DEBUG_LOG("End Failed");
    });

    //
    // Ensure the OTA process is running & listening.
    //
    ArduinoOTA.begin();

}

//
// This function is called continously, and is responsible
// for flashing the ":", and otherwise updating the display.
//
// We rely on the background NTP-updates to actually make sure
// that that works.
//
void loop()
{
    static char buf[10] = { '\0' };
    static char prev[10] = { '\0' };
    static long last_read = 0;
    static bool flash = true;

    //
    // Resync the clock?
    //
    timeClient.update();

    //
    // Handle any pending over the air updates.
    //
    ArduinoOTA.handle();

    //
    // Get the current hour/min
    //
    int cur_hour = timeClient.getHours();
    int cur_min  = timeClient.getMinutes();

    //
    // Format them in a useful way.
    //
    sprintf(buf, "%02d%02d", cur_hour, cur_min);

    //
    // If the current "hourmin" is different to
    // that we displayed last loop ..
    //
    if (strcmp(buf, prev) != 0)
    {
        // Update the display
        tm1637.display(0, buf[0] - '0');
        tm1637.display(1, buf[1] - '0');
        tm1637.display(2, buf[2] - '0');
        tm1637.display(3, buf[3] - '0');

        // And cache it
        strcpy(prev , buf);
    }

    //Get the current button state
    button1.checkSwitch();
    if (button1.clicked()) {         
       // We've now detected a button state change (High or Low)
       // If HIGH then change the brightness 
       Serial.println("Button pushed");
       brtLevel++;  // increment the brightness 
       if (brtLevel > 2) { brtLevel = 0; }  // if brightness goes past 2 then reset it to zero 
          
       switch (brtLevel) {
       case 0:
          int_brightness = BRIGHT_DARKEST;
          break;
       case 1:
          int_brightness = BRIGHT_TYPICAL;
          break;
       case 2:
          int_brightness = BRIGHT_BRIGHTEST;
          break;
       }
       tm1637.set(int_brightness);   
          
       // Update the display
       tm1637.display(0, buf[0] - '0');
       tm1637.display(1, buf[1] - '0');
       tm1637.display(2, buf[2] - '0');
       tm1637.display(3, buf[3] - '0');
    }

    if (button1.held())  {
       Serial.println("Button held - setting time");
       // Enter loop here to set display TZ
       int setHour = 0;
       int setTZ = config.timeZone;
       while (true) {
          button1.checkSwitch();      
          if (button1.clicked()) {  
             //increment the timezone
             if (setTZ < 12) {   // if the TZ is smaller than 12
                setTZ++;         // we can add one to it
             } else { 
                 setTZ = -11;    // the next TZ after 12 is -11
             }
             Serial.print("setTZ=");
             Serial.println(setTZ);
       
             timeClient.setTimeOffset(setTZ * (60 * 60));   // update the TZ in the clock
             int cur_hour = timeClient.getHours();
             sprintf(buf, "%02d",  cur_hour); 
             tm1637.display(0, buf[0] - '0'); 
             tm1637.display(1, buf[1] - '0'); 
             
             yield();
          }
          if (button1.held()) break;
          yield();
       }
       Serial.println("Updating with new timezone");
       // Next you need to save the updated setting to SPIFFS
       config.timeZone = setTZ;
       saveConfiguration(filename, config);
    }

    long now = millis();

    if ((last_read == 0) ||
            (abs(now - last_read) > 500))
    {
        // Invert the "show :" flag
        flash = !flash;

        // Apply it.
        tm1637.point(flash);

        //
        // Note that the ":" won't redraw unless/until you update.
        // So we'll force that to happen by removing the cached
        // value here.
        //
        memset(prev, '\0', sizeof(prev));
        last_read = now;
    }

    WiFiClient client = server.available();
    if (client) processHTTPRequest(client);

}

//
// Process an incoming HTTP-request.
//
void processHTTPRequest(WiFiClient client)
{
    // Wait until the client sends some data
    while (client.connected() && !client.available())
        delay(1);

    // Read the first line of the request
    String request = client.readStringUntil('\r');
    client.flush();

    //
    // Find the URL we were requested
    //
    // We'll have something like "GET XXXXX HTTP/XX"
    // so we split at the space and send the "XXX HTTP/XX" value
    //
    request = request.substring(request.indexOf(" ")  + 1);

    //
    // Now we'll want to peel off any HTTP-parameters that might
    // be present, via our utility-helper.
    //
    URL url(request.c_str());

    //
    // Does the user want to change the brightness?
    //

    if (url.param("url_brightness") != NULL)
    {
       DEBUG_LOG("Brightness update detected");    
       Serial.print("url.param(url_brightness = ");
       Serial.println(url.param("url_brightness"));
       
       if (strcmp(url.param("url_brightness"), "low") == 0)  {
           DEBUG_LOG("Setting brightness to low");
           int_brightness = BRIGHT_DARKEST;
       } else if (strcmp(url.param("url_brightness"), "med") == 0)  {
           DEBUG_LOG("Setting brightness to medium");        
           int_brightness = BRIGHT_TYPICAL;
       } else if (strcmp(url.param("url_brightness"), "high") == 0)  {
           DEBUG_LOG("Setting brightness to brightest");        
           int_brightness = BRIGHT_BRIGHTEST;
       }
       tm1637.set(int_brightness);

       strcpy(config.brightness, url.param("url_brightness"));  
     
       Serial.print("Saving timeZone: ");
       Serial.println(config.timeZone);
       Serial.print("Saving brightness: ");
       Serial.println(config.brightness);
  
       Serial.println(F("Saving configuration..."));
       saveConfiguration(filename, config);

        // Redirect to the server-root
        redirectIndex(client);
        return;   
     }

     //
     // Does the user want to change the time-zone?
     //
     if (url.param("tz") != NULL)
     {
        DEBUG_LOG("TZ update detected");
        // Change the timezone now
        config.timeZone = atoi(url.param("tz"));

        // Update the offset.
        timeClient.setTimeOffset(config.timeZone * (60 * 60));
        timeClient.forceUpdate();
       
        Serial.print("Saving timeZone: ");
        Serial.println(config.timeZone);
        Serial.print("Saving brightness: ");
        Serial.println(config.brightness);
  
        Serial.println(F("Saving configuration..."));
        saveConfiguration(filename, config);

        // Redirect to the server-root
        redirectIndex(client);
        return;
    }

    //
    // At this point we've either received zero URL-paramters
    // or we've only received ones we didn't recognize.
    //
    // Either way return a simple response.
    //
    serveHTML(client);

}

//
// This is a bit horrid.
//
// Serve a HTML-page to any clients who connect, via a browser.
//
void serveHTML(WiFiClient client)
{
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("");

    client.println("<!DOCTYPE html>");
    client.println("<html lang=\"en\">");
    client.println("<head>");
    client.println("<title>NTP Clock</title>");
    client.println("<meta charset=\"utf-8\">");
    client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
    client.println("<link href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css\" rel=\"stylesheet\" integrity=\"sha384-BVYiiSIFeK1dGmJRAkycuHAHRg32OmUcww7on3RYdg4Va+PmSTsz/K68vbdEjh4u\" crossorigin=\"anonymous\">");
    client.println("<script src=\"//code.jquery.com/jquery-1.12.4.min.js\"></script>");
    client.println("<script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/bootstrap.min.js\" integrity=\"sha384-Tc5IQib027qvyjSMfHjOMaLkfuWVxZxUPnCJA7l2mCWNIpG9mGCD8wGNIcPD7Txa\" crossorigin=\"anonymous\"></script>");
    client.println("<style> body { margin: 25px; } </style>");
    client.println("</head>");
    client.println("<body>");
    client.println("<strong>NTP Clock Settings</strong><br><br>");
    
    client.println("Time zone offset: ");
    client.print("<form action=\"/\" method=\"GET\">");
    client.print("<input type=\"text\" name=\"tz\" size=\"3\" value=\"");
    client.print(config.timeZone);
    client.print("\"><br>");
    client.println("<input type=\"submit\" value=\"Update TZ\"></form>");
    
    client.println("<br><br>Brightness Level:<br>");
    client.print("<form action=\"/\" method=\"GET\">");
    
    if (strcmp(config.brightness, "high") == 0) {
      client.println("<input type=\"radio\" name=\"url_brightness\" value=\"high\" checked> High<br>"); 
      client.println("<input type=\"radio\" name=\"url_brightness\" value=\"med\" > Medium<br>");
      client.println("<input type=\"radio\" name=\"url_brightness\" value=\"low\" > Low<br>");
    } else if (strcmp(config.brightness, "med") == 0) {
      client.println("<input type=\"radio\" name=\"url_brightness\" value=\"high\" > High<br>"); 
      client.println("<input type=\"radio\" name=\"url_brightness\" value=\"med\" checked> Medium <br>"); 
      client.println("<input type=\"radio\" name=\"url_brightness\" value=\"low\" > Low<br>");
    } else if (strcmp(config.brightness, "low") == 0) {
      client.println("<input type=\"radio\" name=\"url_brightness\" value=\"high\" > High<br>"); 
      client.println("<input type=\"radio\" name=\"url_brightness\" value=\"med\" > Medium<br>"); 
      client.println("<input type=\"radio\" name=\"url_brightness\" value=\"low\" checked > Low<br>");
    } else {
      client.println("<input type=\"radio\" name=\"url_brightness\" value=\"high\" > High<br>"); 
      client.println("<input type=\"radio\" name=\"url_brightness\" value=\"med\"> Medium<br>"); 
      client.println("<input type=\"radio\" name=\"url_brightness\" value=\"low\"> Low<br>");      
    }
    client.println("<input type=\"submit\" value=\"Update Brightness\"></form>");
    client.println("</body>");
    client.println("</html>");

}

//
// Serve a redirect to the server-root
//
void redirectIndex(WiFiClient client)
{
    client.println("HTTP/1.1 302 Found");
    client.print("Location: http://");
    client.print(WiFi.localIP().toString().c_str());
    client.println("/");
}




