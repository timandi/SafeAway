/**===========================================================================
 *       
 *                      SafeAway - Home security system
 *     
 *                   
 * Done: - Telegram bot - "t.me/SafeAway_bot"  
 *       - OTA firmware update - through Telegram file-sharing feature
 *       - Two relay output (Lights and Gas valve)
 *       
 * 
 * ToDo: - Mimic indoor presence by toggling lights at random intervals
 *       - Persistent storage of sensible data
 *       - WiFi connection fallback mechanism
 *       - Customizable settings interface  
 *       - Implement unique ID validation ^
 *       - Implement a timing/scheduler mechanism
 * 
 *===========================================================================**/

// Libraries used in this project
#include <FS.h>
#include <HTTPUpdate.h>
#include <SD.h>
#include <SPIFFS.h>
#include <UniversalTelegramBot.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// Define wifi credentials and a friendly host name
#include "credentials.h"
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASS;
const char *host = MDNS_HOST;
const char *bot_token = BOT_TOKEN;
const char *chat_id = CHAT_ID;

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -2;
const int daylightOffset_sec = 3600;

// Define output pins
#define LIGHT_PIN 25  //output pin - relay
#define GAS_PIN 26    //output pin - relay

// Variable for storing the states
bool lightState = false;
bool gasState = true;
bool holidayState = false;

// Define wifi and Telegram API objects
WiFiClientSecure client;
UniversalTelegramBot bot(bot_token, client);

// Checks for new messages every 1 second.
int botRequestDelay = 1000;
unsigned long lastTimeBotRan;

//================================================================
// Main callback function reached when a new message arrives
//================================================================
void handleNewMessages(int numNewMessages) {
    // Cycle through the messages queue
    for (int i = 0; i < numNewMessages; i++) {
        // Store message metadata for the scope of this parsing,
        // print the text output for debug and then normalize to lowercase
        int message_id = bot.messages[i].message_id;
        String chat_id = String(bot.messages[i].chat_id);
        String text = bot.messages[i].text;
        String type = bot.messages[i].type;
        String from_name = bot.messages[i].from_name;
        bool has_document = bot.messages[i].hasDocument;
        Serial.println(text);
        text.toLowerCase();

        // Assert a valid field in case it's empty.
        // ToDo: this will later use to prevent outside requests :)
        if (from_name == "")
            from_name = "Guest";

        // Handle simple message callback
        if (type == "message") {
            // Handle the file transfer (forced firmware update)
            if (has_document == true) {
                httpUpdate.rebootOnUpdate(false);
                t_httpUpdate_return ret = (t_httpUpdate_return)3;

                Serial.println("Updating the firmware");
                bot.sendMessage(chat_id, "Updating the firmware...", "");

                // Update the board firmware
                ret = httpUpdate.update(client, bot.messages[i].file_path);

                switch (ret) {
                    case HTTP_UPDATE_FAILED:
                        bot.sendMessage(chat_id, "HTTP_UPDATE_FAILED Error (" + String(httpUpdate.getLastError()) + "): " + httpUpdate.getLastErrorString(), "");
                        break;

                    case HTTP_UPDATE_NO_UPDATES:
                        bot.sendMessage(chat_id, "HTTP_UPDATE_NO_UPDATES", "");
                        break;

                    case HTTP_UPDATE_OK:
                        bot.sendMessage(chat_id, "Firmware successfully updated.\nRestarting...", "");
                        Serial.println("Firmware successfully updated. Restarting...");
                        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
                        ESP.restart();

                        break;
                    default:
                        break;
                }
            }

            //Handle welcoming message callback
            else if (strstr("/start help hello", text.c_str())) {
                String welcomeMsg =
                    "Salut, " + from_name +
                    ".\n"
                    "Acesta este un bot menit sa iti faca concediul mai lipsit de griji.\n"
                    "Prin acest bot, poti controla de oriunde din lume siguranta casei tale.\n"
                    "Poti selecta una din urmatoarele optiuni:\n\n"
                    "/vacanta : Acest meniu permite atat inchiderea de siguranta a alimentarii cu gaz, cat si setarea modului 'PLECAT', care simuleaza o prezenta umana in casa, prin pornirea si oprirea unei surse de lumina la un anumit interval de timp presetat de utilizator.  \n"
                    "/status  : Vezi starea curenta a intrerupatoarelor. \n"
                    "/lumina  : Acest :bulb: meniu ofera o interfata pentru controlul luminii. \n"
                    "/gaz     : Controlul manual al electrovalvei de siguranta. \n"
                    "/setari  : Configurarea aplicatiei. \n\n"
                    "Poti accesa din nou acest meniu prin comanda /start, sau simplu prin a scrie 'help' sau 'salut'. \n\n";

                String keyboardJson =
                    "["
                    "[\":bulb: LUMINA :sparkles:\", \":fuelpump: GAZ :hotsprings:\"],"
                    "[\":ok_hand: STATUS :question:\", \":airplane: VACANTA :boat:\", \":wrench: SETARI :nut_and_bolt: \"],"
                    "[\":house_with_garden: PAGINA PRINCIPALA :house_with_garden:\"]"
                    "]";

                bot.sendMessageWithReplyKeyboard(chat_id, welcomeMsg, "Markdown", keyboardJson, true);
            }

            // Handle manual light command
            else if (strstr("/lumina lumina bec", text.c_str())) {
                String lightMsg = "In acest moment, iluminatul este ";
                if (lightState == true) {
                    lightMsg += "pornit.";
                } else {
                    lightMsg += "oprit.";
                }
                String keyboardJson =
                    "["
                    "[{ \"text\" : \"OFF\", \"callback_data\" : \"/lightOff\"}],"
                    "[{ \"text\" : \"ON\" , \"callback_data\" : \"/lightOn\"}]"
                    "]";
                bot.sendMessageWithInlineKeyboard(chat_id, lightMsg, "Markdown", keyboardJson);
            }

            // Handle manual valve command
            else if (strstr("/gaz valva robinet", text.c_str())) {
                String gasMsg = "In acest moment, gazul este ";

                if (gasState == true) {
                    gasMsg += "pornit.";
                }
                else {
                    gasMsg += "oprit.";
                }
                String keyboardJson =
                    "["
                    "[{ \"text\" : \"OFF\", \"callback_data\" : \"/gasOff\"}],"
                    "[{ \"text\" : \"ON\" , \"callback_data\" : \"/gasOn\"}]"
                    "]";
                bot.sendMessageWithInlineKeyboard(chat_id, gasMsg, "Markdown", keyboardJson);
            }

        }
        //----------------------------------------------------------------
        //- Handle the callback_queries coming from the buttons
        else if (type == "callback_query") {
            if (text == "/lightON") {
                lightState = true;
                digitalWrite(LIGHT_PIN, HIGH);
            }
            if (text == "/lightOff") {
                lightState = false;
                digitalWrite(LIGHT_PIN, LOW);
            }
            if (text == "/gasON") {
                gasState = true;
                digitalWrite(GAS_PIN, HIGH);
            }
            if (text == "/gasOff") {
                gasState = false;
                digitalWrite(GAS_PIN, LOW);
            }
        }
    }
    Serial.println("done");
}

// Computing time in a readable manner
void printLocalTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("failed to get local time");
        return;
    }
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    Serial.print("Day of week: ");
    Serial.println(&timeinfo, "%A");
    Serial.print("Month: ");
    Serial.println(&timeinfo, "%B");
    Serial.print("Day of Month: ");
    Serial.println(&timeinfo, "%d");
    Serial.print("Year: ");
    Serial.println(&timeinfo, "%Y");
    Serial.print("Hour: ");
    Serial.println(&timeinfo, "%H");
    Serial.print("Hour (12 hour format): ");
    Serial.println(&timeinfo, "%I");
    Serial.print("Minute: ");
    Serial.println(&timeinfo, "%M");
    Serial.print("Second: ");
    Serial.println(&timeinfo, "%S");

    Serial.println("Time variables");
    char timeHour[3];
    strftime(timeHour, 3, "%H", &timeinfo);
    Serial.println(timeHour);
    char timeWeekDay[10];
    strftime(timeWeekDay, 10, "%A", &timeinfo);
    Serial.println(timeWeekDay);
    Serial.println();
}

//====================================================================
// Main setup function, it run only once at wake-up
//====================================================================
void setup() {
    // Initialize the pins as outputs and set them to null for now.
    pinMode(LIGHT_PIN, OUTPUT);
    pinMode(GAS_PIN, OUTPUT);
    digitalWrite(LIGHT_PIN, LOW);
    digitalWrite(GAS_PIN, LOW);

    // Start serial connection for debugging
    Serial.begin(115200);

    // Mount the SPIFFS storage
    if (!SPIFFS.begin(true)) {
        Serial.println("An Error has occurred while mounting SPIFFS");
    }

    // Additional SSL certificate
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

    // Attempt to connect to Wifi network:
    Serial.print("Connecting to: ");
    Serial.println(ssid);

    // Start the connection and wait for network confirmation
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }

    // Connection ready
    Serial.println("");
    Serial.println("Connection successfull");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Init and get the time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    printLocalTime();
}

//====================================================================
// Main loop function
//====================================================================
void loop() {
    //
    if (millis() > lastTimeBotRan + botRequestDelay) {

        // Check for new messages
        int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

        while (numNewMessages) {
            Serial.println("got response");
            handleNewMessages(numNewMessages);
            numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        }
        lastTimeBotRan = millis();
    }
}
