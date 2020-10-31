#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <Servo.h>
#include <Ticker.h>
#include <i2s.h>
#include <i2s_reg.h>

#ifndef STASSID
#define STASSID "YourNetwork"
#define STAPSK  "YourPassword"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;

unsigned int udpPort = 7777;

char packetBuffer[UDP_TX_PACKET_MAX_SIZE + 1];

WiFiUDP Udp;

//Animation controls:
Ticker animator;
Servo armServo, headServo, nodServo;

int wand_red, wand_green, wand_blue;
int new_red, new_green, new_blue;
int angle_head, angle_nod, angle_arms;
int new_head, new_nod, new_arms;
int vel_head, vel_nod, vel_arms;

//Audio fun:
Ticker voice;
int32_t integrator;
int16_t previousSample, audioBuffer[8][513];
int     activeSample, activeBuffer, firstFreeBuffer;



void setup()
{
  //LED Pins
  pinMode(15, OUTPUT);
  pinMode(2, OUTPUT);
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH); //Blue LED on

  Serial.begin(115200);
  Serial.println("Booting...");
  fillAudioBuffer();
  Serial.println("Starting WiFi...");

  digitalWrite(15, HIGH); //Red LED on  
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  wand_green = 0;
  while(WiFi.status() != WL_CONNECTED)
  {
    analogWrite(2, 63*min(wand_green, 32-wand_green));
    analogWrite(4, 63*max(16-wand_green, wand_green-16));
    wand_green++;
    wand_green &= 0x1F;
    delay(10);
  }
  
  SetUpOTA();
  
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  MDNS.addService("animation", "udp", 7777);

  Serial.printf("UDP server on port %d\n", udpPort);
  Udp.begin(udpPort);
  

  pinMode(5, OUTPUT);
  pinMode(3, OUTPUT);
  digitalWrite(3,0);

  headServo.attach(13);
  nodServo.attach(14);
  armServo.attach(12);

  angle_head = new_head = 90;
  angle_nod = new_nod = 90;
  angle_arms = new_arms = 180;

  wand_red = wand_green = wand_blue = 0;
  new_red = 127;
  new_green = 0;
  new_blue = 255;
  animator.attach_ms(15, updateAnimation);

  i2s_begin();
  i2s_set_rate(22050);
  voice.attach_ms(2, feedAudio);
}

void loop()
{
  ArduinoOTA.handle();
  CheckUDP();

  //feedAudio();
}

void updateAnimation()
{
  wand_red = approach(wand_red, new_red);
  wand_green = approach(wand_green, new_green);
  wand_blue = approach(wand_blue, new_blue);

  analogWrite(15, wand_red   << 2);
  analogWrite( 2, wand_green << 2);
  analogWrite( 4, wand_blue  << 2);

  angle_head = approach(angle_head, new_head);
  angle_nod = approach(angle_nod, new_nod);
  angle_arms = approach(angle_arms, new_arms);
  
  headServo.write(angle_head);
  nodServo.write(angle_nod);
  armServo.write(angle_arms);
  
}


void CheckUDP()
{
  int packetSize = Udp.parsePacket();
  if (packetSize)
  {
    // read the packet into packetBufffer
    int n = Udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
    packetBuffer[n] = 0;
    //Serial.print("Packet: ");
    //Serial.println(packetBuffer[0]);

    byte v1, v2, v3, h1, h2;
    boolean v1v, v2v, v3v;
    h1 = dehex(packetBuffer[1]);
    h2 = dehex(packetBuffer[2]);
    v1v = (h1 != 16 && h2 != 16);
    v1 = (h1 << 4) | h2;

    h1 = dehex(packetBuffer[3]);
    h2 = dehex(packetBuffer[4]);
    v2v = (h1 != 16 && h2 != 16);
    v2 = (h1 << 4) | h2;

    h1 = dehex(packetBuffer[5]);
    h2 = dehex(packetBuffer[6]);
    v3v = (h1 != 16 && h2 != 16);
    v3 = (h1 << 4) | h2;
    

    switch(packetBuffer[0])
    {
      case 'c': //fade-in color
        if(v1v && v2v && v3v)
        {
         new_red = v1;
         new_green = v2;
         new_blue = v3;
        }
        break;
      case 'C': //instant color
        if(v1v && v2v && v3v)
        {
         wand_red = new_red = v1;
         wand_green = new_green = v2;
         wand_blue = new_blue = v3;
        }
        break;
      case 'h': //head
        if(v1v)
        {
          new_head = v1;
        }
        break;
      case 'n': //nod
        if(v1v)
        {
          new_nod = v1;
        }
        break;
      case 'a': //arms
        if(v1v)
        {
          new_arms = v1;
        }
        break;
      case 'm': //motion
        if(v1v && v2v && v3v)
        {
         new_arms = v1;
         new_head = v2;
         new_nod = v3;
        }
        break;
      case 'Z': //Audio
        if(packetSize >= 1025 && nFreeBuffers() > 0)
        {
          int j, k;
          int16_t aSample;
          k = 0;
          for(j=1; j<1025; j+=2)
          {
            aSample = packetBuffer[j];
            aSample <<= 8;
            aSample |= packetBuffer[j+1];
            audioBuffer[firstFreeBuffer][k] = aSample;
            k++;
          }
          audioBuffer[firstFreeBuffer][512] = 1;
          if(activeBuffer == firstFreeBuffer)
            activeSample = 0;
          firstFreeBuffer++;
          firstFreeBuffer %= 8;
          digitalWrite(5, 1);
        }
        Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
        Udp.write('Z');
        Udp.write(nFreeBuffers());
        Udp.endPacket();
        break;
      default:
        //do nothing
        break;
    }
    /*
    // send a reply, to the IP address and port that sent us the packet we received
    Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
    Udp.write(ReplyBuffer);
    Udp.endPacket();
    */
  }
}


void fillAudioBuffer()
{
  int j;

  for(j=0; j<4096; j++)
  {
    audioBuffer[j >> 9][j & 0x1FF] = round(32700.0*sin(0.23*j));
  }
  
}

void feedAudio()
{
  int nSamples = 0;

  while(nSamples < 64 && i2s_write_sample_nb(deltaSigma(audioBuffer[activeBuffer][activeSample])))
  {
    audioBuffer[activeBuffer][activeSample] = 0;
    nSamples++;
    activeSample++;
    if(activeSample >= 512)
    {
      audioBuffer[activeBuffer][512] = 0;
      activeSample = 0;
      activeBuffer++;
      activeBuffer %= 8;
      digitalWrite(5, audioBuffer[activeBuffer][512]);
    }
  }
}

uint32_t deltaSigma(int16_t sample)
{
  int j, stepSize;
  int16_t virtualSample;
  uint32_t stream = 0;

  stepSize = (sample - previousSample) >> 5;
  virtualSample = previousSample;
  for(j=0; j<32; j++)
  {
    stream <<= 1;
    if(integrator > 0)
    {
      integrator += virtualSample + 32767;
    }
    else
    {
      integrator += virtualSample - 32767;
      stream |= 1;
    }
    virtualSample += stepSize;
  }

  previousSample = sample;
  return stream;
}

int nFreeBuffers()
{
  if(activeBuffer == firstFreeBuffer)
  {
    if(audioBuffer[activeBuffer][512] == 0)
      return 8;
    else
      return 0;
  }
  else
    return (8 + activeBuffer - firstFreeBuffer) % 8;
}


int approach(int old_n, int new_n)
{
  int delta = new_n - old_n;
  int s = sqrt(abs(delta))/4 + 1;
  if(old_n == new_n)
    return old_n;
  else if (old_n < new_n)
    return old_n + s;
  else
    return old_n - s;
}

char dehex(char x)
{  
  if ((x >= '0') && (x <= '9'))
    return x & 0x0F;

  if ((x >= 'A') && (x <= 'F'))
    return x - 55;

  if ((x >= 'a') && (x <= 'f'))
    return x - 87;

  return 16;
}

void SetUpOTA()
{
  ArduinoOTA.setHostname("FortuneTeller");

  // ArduinoOTA.setPassword("admin");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
    {
      type = "sketch";
    }
    else
    { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    animator.detach(); // stop the animation
    voice.detach(); //Stop the audio
    i2s_end();
    Serial.println("Start updating " + type);
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    int p2 = progress/(total/1023);
    analogWrite(15, 1023 - p2);
    analogWrite(2, p2);
    digitalWrite(4, 0);
    digitalWrite(5, 0); //turn off speaker
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
    {
      Serial.println("Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR)
    {
      Serial.println("Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR)
    {
      Serial.println("Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR)
    {
      Serial.println("Receive Failed");
    }
    else if (error == OTA_END_ERROR)
    {
      Serial.println("End Failed");
    }
  });
  
  ArduinoOTA.begin();
}
