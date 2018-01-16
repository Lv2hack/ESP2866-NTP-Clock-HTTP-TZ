# ESP2866-NTP-Clock-HTTP-TZ

This is an NTP clock using a TM1637 display.  It (of course) checks the Internet for the time.

The timezone and brightness are configured through an HTTP server on the device after startup.  These settings are saved in a config file on the SPIFFS filesystem in JSON.
