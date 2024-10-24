#include <Arduino.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <HX711.h>
#include <WiFi.h>

const byte DNS_PORT = 53;

AsyncWebServer server(80);
WebSocketsServer Websockets(81);

const char *FILENAME = "/URLGOOGLESHEET.txt";
bool CREATEFILE;
#define ledPin 2
#define pinTrig 33
#define pinEcho 32
float distance;

#define LOADCELL_DOUT_PIN 16
#define LOADCELL_SCK_PIN 17

HX711 scale;

String urlLink;

bool openFile(const char *dir, String* urlGoogleSheet){

    Serial.printf("Leyendo archivo en: %s\n", dir);
    if(LittleFS.exists(dir)){
        File file = LittleFS.open(dir, FILE_READ);
        if(!file){
            Serial.println("No se pudo leer archivo");
            return false;
        }
        *urlGoogleSheet = file.readString();
        Serial.print("Contenido de el archivo: ");
        Serial.println(*urlGoogleSheet);
        file.close();
        return true;
    }
    else{
        Serial.println("Archivo no existe");
        return false;
    }
}

bool writeFile(const char *dir, String text){
    Serial.printf("Escribiendo en: %s\n", dir);
    File file = LittleFS.open(dir, FILE_WRITE);
    if(!file){
        Serial.println("archivo no se puede abrir");
        return false;
    }
    if(file.print(text)){
        Serial.println("Escritura exitosa");
    }else{
        Serial.println("Escritura fallida");
    }
    file.close();
    return true;
}
// const char* link = "";
void returnPage(AsyncWebServerRequest *request){
    if(!LittleFS.begin()){
        Serial.println("** ERROR AL INICIAR LittleFS");
    }else{
        Serial.println("Se inicio LittleFS");
        if(!openFile(FILENAME, &urlLink)){
            Serial.println("** ARCHIVO NO EXISTE ");
            urlLink = "none";
            bool res = writeFile(FILENAME, urlLink);
            if(res)Serial.println("creacion de el archivo exitosa");
        }
    }
    String page="<!doctype html>"
"<html lang=en>"
"<head>"
"<meta charset=UTF-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Document</title>"
"</head>"
"<body>"
"<div style='text-align:center'>"
"<h1>ESP32 control panel</h1>"
"<button id='buttonGetDistance' style='background: #FF7200; border-radius: 5px; border:none; color:white; padding: 8px;'>obtener distancia</button>"
"<h2 id='distance'></h2>"
"<form id='form'>"
"<label >nombre</label>"
"<input type='text' id='name'>"
"<button style='cursor:pointer;' id='sendData'>guardar</button>"
"</form>"

"<form id='formUrlGoogleSheet' action='/saveUrl' method='post'>"
"<input type='text' name='urlgooglesheet' placeholder='agrega la url'/>"
"<button>guardar url</button>"
"</form>"
"</div>"
"<script>let socketConnection=new WebSocket(`ws://${location.hostname}:81/`);"
"let distancia, linkGoogleSheet=\""+urlLink+"\";"
"window.addEventListener('load', ()=>{if(linkGoogleSheet==='none') document.getElementById('sendData').disabled = true;});"
"socketConnection.onmessage = (e) => { let data = JSON.parse(e.data); updateTag('distance', `Altura: ${data.distance} Peso: ${data.peso}`);distancia = data.distance;};"

"const updateTag = (id, value) => { const tag = document.getElementById(id); tag.textContent = value};"
"const form = document.getElementById('form');"
"form.addEventListener('submit', (e) => {"
"    e.preventDefault();"
"    const name = document.getElementById('name').value;"
"        fetch('https://script.google.com/macros/s/AKfycbw8fU_lgP_QKXa_xOFLm3ur10FUUxcjql85VJTUxqerh2HawNTgo0KCIIu6bdZkjnGs2g/exec', {"
"       method: 'POST',"
"       header: {"
"           'Content-Type': 'application/json',"
"       },"
"       body: JSON.stringify({"
"           nombre:name,"
"           distance: distancia "
"       })"
"   }).then(response=>response.json())"
"   .then(data => {"
"       console.log('respuesta del servidor:', data);"
"   }).catch(error => {"
"       console.error('Error', error);"
"   });"

"    form.reset();"
"});"
"const buttonGetDistance = document.getElementById('buttonGetDistance');"
"buttonGetDistance.addEventListener('click', ()=>{socketConnection.send('getDistance');});"
"</script>"
"</body>"
"</html>";
LittleFS.end();
    request->send(200, "text/html", page);
}

float factor_calibration = 206140;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t lenght){
    switch(type){
        case WStype_DISCONNECTED:
            Serial.printf("[%u]] desconectado\n", num);
            break;
        
        case WStype_CONNECTED:
            {
                IPAddress ip = Websockets.remoteIP(num);
                Serial.printf("[%u] conectado en %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
                Websockets.sendTXT(num, "conectado en servidor: ");
            }
            break;
        
        case WStype_TEXT:
            Serial.printf("[%u] mensaje recibido: %s\n", num, payload);
            String msg = String((char *)(payload));

            if(msg.equalsIgnoreCase("getdistance")){
                digitalWrite(pinTrig, LOW);
                delayMicroseconds(2);
                digitalWrite(pinTrig, HIGH);
                delayMicroseconds(10);
                digitalWrite(pinTrig, LOW);

                long duration = pulseIn(pinEcho, HIGH);
                float peso;
                char buffer[10];
                scale.set_scale(factor_calibration);
                if(scale.is_ready()){
                    peso = scale.get_units(5);
                    Serial.println(peso);
                    dtostrf(peso, 3, 3, buffer); 
                }

                distance = (duration * 0.0343) / 2;
                String dataJson = "{\"distance\": " + String(distance) +","+"\"peso\":" + buffer +"}";
                Websockets.broadcastTXT(dataJson);
            }
            break;
    }   
}

// String redes[];

const char *pageWifiManage =
"<html lang=en>"
"<head>"
"<meta charset=UTF-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Document</title>"
"</head>"
"<body>"
"<h1> redes disponibles</h1>"
"<form action='/validar' method='post'>"
"<label> ssid: </label>"
"<input type='text' name='ssid' value='CLARO1_1E8957'>"
"<label>contraseña:</label>"
"<input type='text' name='password' value='434B8hiERc'>"
"<button>conectarse</button>"
"</form>"
"</body>"
"</html>";
;
void activeModeAp(void){
            WiFi.softAP("autoConnectIMC", "12345678");
        Serial.println("iniciando punto de acceso");

        Serial.println(WiFi.softAPIP());
}
void validarDatos(AsyncWebServerRequest *request){
    String men, route;
    if(request->hasParam("ssid", true) && request->hasParam("password", true)){
        String ssid = request->getParam("ssid", true)->value();
        String password = request->getParam("password", true)->value();


        Serial.printf("datos recibidos: ssid = %s, password = %s\n", ssid, password);
        Serial.println("connectando a una red wifi");
        
        WiFi.begin(ssid, password);
            int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(200);
        Serial.print(".");
        attempts++;
    }
        if(WiFi.status() != WL_CONNECTED){
            Serial.print("no se pudo conectar");
            activeModeAp();
        }else{
        Serial.println("red conedtada");
        Serial.println(WiFi.localIP());
            // request->send(200, "text/plain", "nose pudo conectar");
            route = "/";

        
        }


    }
    else{
        request->send(200, "text/plain", "datos no recibidos");
    }
    request->redirect(route);
}
void saveUrl(AsyncWebServerRequest *request){
    if(request->hasParam("urlgooglesheet", true)){
        if (LittleFS.begin()){
            if(writeFile(FILENAME, request->getParam("urlgooglesheet", true)->value())){ 
                Serial.println("se guardo con exito la nueva url");
            }
        }else{
            Serial.println("error en iniciar LittleFS");
        }
    }
    LittleFS.end();
    request->redirect("/");
}
void indexWifiManage(AsyncWebServerRequest *request){
    request->send(200, "text/html", pageWifiManage);
}

void notFound(AsyncWebServerRequest *request){
        request->redirect("/wifiManage.html");

        // WiFi.disconnect();
    // int redes = WiFi.scanNetworks();
    // if(redes == 0){
    //     Serial.println("redes no encontradas");
    // }
    // else{
    //             Serial.println("redes encontradas");
    //     for( int i=0; i < redes;i++){
    //         Serial.printf("%d: %s, señal: %d dBm, Encritado: %s\n", i+1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "abierta": "cifrada"));
    //     }
    // }
}



void setup(){
    Serial.begin(115200);
    scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
    scale.set_scale();
    scale.tare();

// formatear la memoria de el esp32
    #if 0
  if(!LittleFS.begin(true)){
    Serial.println("error al formatear LittleFS");
    return;
  }
  if(!LittleFS.format()){
    Serial.println("error al formatear");
  }
    #endif


    Serial.println("INICIANDO COMUNICACION SERIAL");
    // WiFi.begin("CLARO_2.4GHz_499508", "Du?!KgGxWHa=6ec");
    // recibir los datos mediante la request meidante un formulario
    // ver las redes wifi disponibles

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin();
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(200);
        Serial.print(".");
        attempts++;
    }
    if(WiFi.status()==WL_CONNECTED){
        Serial.println(WiFi.localIP());
        Serial.print("se ha conectado a la ultima red wifi");
    }else{                
        activeModeAp();
    }
    // WiFi.begin("CLARO1_1E8957", "434B8hiERc");Serial.print("conectando");
    // while(WiFi.status()!= WL_CONNECTED){delay(400);  Serial.print(".");}
    // Serial.println(WiFi.localIP());


// if(!MDNS.begin("elesp")){
//     Serial.println("Error al configurar el DNS");
//     while(1)delay(10);
// }
//     Serial.println("dns configurado");

    // MDNS.addService("http", "tcp", 50);


    Serial.println("\n\n\niniciando pines");
    pinMode(ledPin, OUTPUT);
    pinMode(pinTrig, OUTPUT);
    pinMode(pinEcho, INPUT);
// si hay cerca una de las redes que estan guardadas entonces que se conecte a una de ellas
    

    server.on("/", HTTP_GET, returnPage);
    server.on("/wifiManage.html", HTTP_GET, indexWifiManage);
    server.on("/validar", HTTP_POST, validarDatos);
    server.on("/saveUrl", HTTP_POST, saveUrl);
    server.onNotFound(notFound);
    server.begin();

    Websockets.begin();
    Websockets.onEvent(webSocketEvent);
    
}

void loop(){
  Websockets.loop();
 
//   static uint32_t prevMillis = 0;
//   if(millis() - prevMillis >= 50){

//     prevMillis = millis();
    
    
//   }
}