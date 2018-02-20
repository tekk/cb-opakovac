/* ***********************************
Project name: SMS brana na opakovac
Author: Peter Javorsky
Date: 08.09.2015
************************************ */

//#include <SoftwareSerial.h>
//SoftwareSerial gsmSerial(PD4, PD5);

#define debug // zakomentuj pre skutocnu prevadzku

#include <Arduino.h>
#include <string.h>
#include <avr/eeprom.h>
#include <EEPROM.h>
#include <SPI.h>
#include <DS3232RTC.h>        //http://github.com/JChristensen/DS3232RTC
#include <Streaming.h>        //http://arduiniana.org/libraries/streaming/
#include <Time.h>             //http://playground.arduino.cc/Code/Time
#include <Wire.h>             //http://arduino.cc/en/Reference/Wire
#include <Math.h>

DS3232RTC RTC;

#define TERMISTOR_PIN PIN_F7
#define LED_PIN PIN_D6
#define CHANNEL_CHANGE_RELAY_PIN PIN_C3

#define TX_TIMEOUT 40000
#define PIN_TX PIN_C4
#define PIN_RX PIN_C5

#define MAX_BUFF_LENGTH 512
#define MAX_SMSNO_LENGTH 20
char buff[MAX_BUFF_LENGTH];
char cisloSMS[MAX_SMSNO_LENGTH];

#define MAX_PRIJEMCOV 10
#define POCET_RELATOK 4
#define DLZKA_CISLA 13

#define RELAY_ON LOW
#define RELAY_OFF HIGH

#define LED_ON HIGH
#define LED_OFF LOW

const int relatka[POCET_RELATOK] = { 8, 7, 5, 4, };
const int ledky[POCET_RELATOK] = { PIN_F6, PIN_F6, PIN_F6, PIN_F6 };

const int CS = 20;

long vysielalKedyNaposledy = 0;
long prijimalKedyNaposledy = 0;

#define gsmSerial Serial1

char tekkovoCislo[14] = "+421948431163";

// EEPROM NASTAVENIA, nacitaju sa pri spusteni
char PocetCisel = 0;
char OdosielanieZapnute = 0;
char IbaHlavnyPrijemca = 0;
char SpravaPriZapnuti = 0;
unsigned char Squelch = 128;
char CislaPrijemcov[MAX_PRIJEMCOV][DLZKA_CISLA + 1];
unsigned long redLed = 0;

int count = 0;

float TeplotaRTC()
{
  int t = RTC.temperature();
  float celsius = t / 4.0;
  return celsius;
}

double Teplota()
{
  int RawADC = analogRead(TERMISTOR_PIN);
  double Temp;
  Temp = log(10000.0 * ((1024.0 / RawADC - 1)));
  Temp = 1 / (0.001129148 + (0.000234125 + (0.0000000876741 * Temp * Temp )) * Temp );
  Temp = Temp - 273.15;            // Convert Kelvin to Celcius
  return Temp;
}

String TeplotaString()
{
  return String((int)Teplota());
}

String TeplotaRTCString()
{
  return String(TeplotaRTC());
}

String Cas()
{
  //setTime(23, 31, 30, 13, 2, 2009);   //set the system time to 23h31m30s on 13Feb2009
  //RTC.set(now());                     //set the RTC from the system time

  String result = "";
  tmElements_t tm;
  RTC.read(tm);
  result += String(tm.Day, DEC);
  result += ".";
  result += String(tm.Month, DEC);
  result += ".";
  result += String(tm.Year, DEC);
  result += " ";
  result += String(tm.Hour, DEC);
  result += ":";
  result += String(tm.Minute, DEC);
  result += ":";
  result += String(tm.Second, DEC);

  return result;
}

//Print an integer in "00" format (with leading zero),
//followed by a delimiter character to Serial.
//Input value assumed to be between 0 and 99.
void printI00(int val, char delim)
{
    if (val < 10) Serial.print('0');
    Serial.print(val, DEC);
    if (delim > 0) Serial.print(delim);
    return;
}


//print time to Serial
void printTime(time_t t)
{
    printI00(hour(t), ':');
    printI00(minute(t), ':');
    printI00(second(t), ' ');
}

//print date to Serial
void printDate(time_t t)
{
    printI00(day(t), 0);
    Serial.print(monthShortStr(month(t)));
    Serial.println(year(t) - 30, DEC);
}

//print date and time to Serial
void printDateTime(time_t t)
{
    printDate(t);
    Serial.print(' ');
    printTime(t);
}


void VypisPrijemcov()
{
   for (int p = 0; p < PocetCisel; p++)
   {
     Serial.print("Nacitane z EEPROM: Prijemca: ");
     Serial.println(CislaPrijemcov[p]);
   }
}

char SquelchPercent()
{
  return map(Squelch, 0, 255, 0, 100);
}

String SquelchPercentString()
{
  return String(SquelchPercent(), DEC);
}

void PosliSMS(char* text)
{
  Serial.println("Subrutina na odoslanie SMS.");
  delay(5000);

  if (!OdosielanieZapnute) return;

  Serial.println("Odosielanie SMS je zapnute.");

  delay(100);

   for (int p = 0; p < (IbaHlavnyPrijemca ? 1 : PocetCisel); p++)
   {
      unsigned long t1 = millis() + 5000;

      while (millis() < t1)
      {
        digitalWrite(LED_PIN, (millis() % 100) > 50);
        if (gsmSerial.available() > 0) Serial.write(gsmSerial.read());
      }

      digitalWrite(LED_PIN, HIGH);
      String msg = String("AT+CMGS=\"" + String(CislaPrijemcov[p]) + "\"\r");
      gsmSerial.print(msg.c_str()); //+421948431163
      delay(1500);
      gsmSerial.print(text);
      delay(100);
      gsmSerial.println((char)26);

      Serial.print("Odoslana SMS na cislo ");
      Serial.print(CislaPrijemcov[p]);
      Serial.print(". Text: ");
      Serial.println(text);

      unsigned long t = millis() + 15000;

      Serial.println("---");

      while (millis() < t)
      {
        digitalWrite(LED_PIN, (millis() % 500) > 250);
        if (gsmSerial.available() > 0) Serial.write(gsmSerial.read());
      }
      Serial.println("---");

      digitalWrite(LED_PIN, LOW);
   }
}

String PrecitajSMS(char* cisloSMS)
{
  digitalWrite(LED_PIN, HIGH);
  gsmSerial.print("AT+CMGR=");
  gsmSerial.print(cisloSMS);
  gsmSerial.print("\r");

  count = 0;
  int cr13 = 0;
  int opak = 100;                       // Number of 100ms intervals before
  // assuming there is no more data
  while (opak-- != 0)
  { // Loop until count = 0
    delay(100);                        // Delay 100ms

    while (gsmSerial.available() > 0) { // If there is data, read it and reset
      char c = (char)gsmSerial.read(); // the counter, otherwise go try again
      Serial.print(c);
      if (c == 13)
      {
        cr13++;
        Serial.println();
        Serial.print("Riadok ");
        Serial.println(cr13);
      }
      else if (cr13 == 3)
      {
        if (c == 10) continue;
        Serial.println();
        Serial.print("Zapisujem ");
        Serial.println(c);
        buff[count++] = c;
        buff[count] = '\0';
        // Avoid overflow
        if (count == MAX_BUFF_LENGTH)
          break;
      }

      opak = 100;
    }
  }

  Serial.println("Mazem SMS.");

  gsmSerial.print("AT+CMGD=");
  gsmSerial.print(cisloSMS);
  gsmSerial.print("\r");
  delay(1000);

  Serial.println("SMS vymazana.");

  digitalWrite(LED_PIN, LOW);

  return String(buff);
}


void NacitajEEPROM()
{
   unsigned int adresa = 0;

   EEPROM.get(adresa, OdosielanieZapnute);
   Serial.print("Nacitane z EEPROM: Odosielanie potvrdzovacich SMS ");
   Serial.println(OdosielanieZapnute ? "ZAPNUTE" : "VYPNUTE");
   adresa++;
   EEPROM.get(adresa, IbaHlavnyPrijemca);
   Serial.print("Nacitane z EEPROM: Odosielanie SMS iba hlavnemu prijemcovi ");
   Serial.println(IbaHlavnyPrijemca ? "ZAPNUTE" : "VYPNUTE");
   adresa++;
   EEPROM.get(adresa, SpravaPriZapnuti);
   Serial.print("Nacitane z EEPROM: SMS pri zapnuti ");
   Serial.println(SpravaPriZapnuti ? "ZAPNUTA" : "VYPNUTA");
   adresa++;
   EEPROM.get(adresa, Squelch);
   Serial.print("Nacitane z EEPROM: Squelch je na hladine ");
   Serial.print(Squelch, DEC);
   Serial.print(" to je ");
   Serial.print(SquelchPercent(), DEC);
   Serial.println(" percent.");

   adresa++;
   EEPROM.get(adresa, PocetCisel);
   Serial.print("Nacitane z EEPROM: Pocet prijemcov: ");
   Serial.println(PocetCisel, DEC);
   adresa++;

   char cislo[14];

   for (int p = 0; p < PocetCisel; p++)
   {
     eeprom_read_block((void*)cislo, (void*)adresa, DLZKA_CISLA + 1);
     memcpy(CislaPrijemcov[p], cislo, DLZKA_CISLA + 1);
     adresa += DLZKA_CISLA + 1;
   }

   VypisPrijemcov();
}

void EEPROM_FirstSetup()
{
  unsigned int adresa = 0;

  EEPROM.update(adresa, (char)1); // Zapnute posielanie potvrdzovacich sprav, 1 alebo 0
  adresa++;
  EEPROM.update(adresa, (char)1); // Odosielanie iba hlavnemu prijemcovi, 1 alebo 0
  adresa++;
  EEPROM.update(adresa, (char)0); // SpravaPriZapnuti, 1 alebo 0
  adresa++;
  EEPROM.update(adresa, (unsigned char)128); // Squelch - 128 = presna polovica
  adresa++;
  EEPROM.update(adresa, (char)1); // Pocet aktualne ulozenych cisel, max. 255
  adresa++;
  eeprom_write_block((void *)tekkovoCislo, (void *)adresa, strlen(tekkovoCislo)+1); // Prve cislo
  adresa += DLZKA_CISLA + 1;
}

void PrepniKanalOJeden()
{
  digitalWrite(CHANNEL_CHANGE_RELAY_PIN, LOW);
  delay(100);
  digitalWrite(CHANNEL_CHANGE_RELAY_PIN, HIGH);
  delay(100);
}

void PrepniKanalOX(int x)
{
  for (int i = 0; i < x; i++)
    PrepniKanalOJeden();
}

void setup()
{
  pinMode (CS, OUTPUT);
  pinMode (CHANNEL_CHANGE_RELAY_PIN, OUTPUT);
  digitalWrite(CHANNEL_CHANGE_RELAY_PIN, HIGH);

  Serial.begin(115200);
  //setSyncProvider(RTC.get);
  Wire.begin();
  //setTime(23, 15, 00, 19, 2, 18);   //set the system time to 23h31m30s on 13Feb2009
  //RTC.set(now());

  Serial.print(F("RTC Sync"));

  if (timeStatus() != timeSet)
  {
    Serial.print(F(" FAIL!"));
  }
  else
  {
    Serial.print(F(" OK."));
  }

  Serial.println();

  gsmSerial.begin(9600);
  SPI.begin();

/*
  EEPROM_FirstSetup();
  while(1) { }
*/

  NacitajEEPROM();

  pinMode(LED_PIN, OUTPUT);

  // zapni vsetky relatka
  for (int i = 0; i < POCET_RELATOK; i++)
  {
    pinMode(relatka[i], OUTPUT);
    pinMode(ledky[i], OUTPUT);
    digitalWrite(relatka[i], RELAY_ON);
    digitalWrite(ledky[i], LED_ON);
    delay(1000);
  }

  //PrepniKanalOX(16);

  delay(10000);

  digitalWrite(LED_PIN, HIGH);
  Serial.println("Prihlasujem k sieti...");
  gsmSerial.println("AT+CMGF=1\r");
  delay(100);
  gsmSerial.println("AT+CPIN=\"3012\"\r");
  delay(100);
  Serial.println("PIN poslany do GSM modulu.");

  unsigned long t = millis() + 6000;
  Serial.println("---");

  while (millis() < t)
  {
    digitalWrite(LED_PIN, (millis() % 500) > 250);
    if (gsmSerial.available() > 0) Serial.write(gsmSerial.read());
  }
  Serial.println("---");

  digitalWrite(LED_PIN, LOW);

  String sprava = String("CB Opakovac pripraveny. Teplota NTC " + TeplotaString() +  " st.C. teplota RTC " + TeplotaRTCString() + " Cas " + Cas() + " OK.");
  Serial.println(sprava);

  if (SpravaPriZapnuti) PosliSMS((char*)sprava.c_str());
}

void MCP41010Write(byte value)
{
  // Note that the integer vale passed to this subroutine
  // is cast to a byte

  digitalWrite(CS,LOW);
  SPI.transfer(B00010001); // This tells the chip to set the pot
  SPI.transfer(value);     // This tells it the pot position
  digitalWrite(CS,HIGH);
}

void UlozNastaveniaDoEEPROM()
{
  unsigned int adresa = 0;

  EEPROM.update(adresa, OdosielanieZapnute); // Zapnute posielanie potvrdzovacich sprav, 1 alebo 0
  adresa++;
  EEPROM.update(adresa, IbaHlavnyPrijemca); // Odosielanie iba hlavnemu prijemcovi, 1 alebo 0
  adresa++;
  EEPROM.update(adresa, SpravaPriZapnuti); // SpravaPriZapnuti, 1 alebo 0
  adresa++;
  EEPROM.update(adresa, Squelch); // Squelch - hodnota od 0 do 255
  adresa++;
  EEPROM.update(adresa, PocetCisel); // Pocet aktualne ulozenych cisel, max. 255
  adresa++;

  for (int p = 0; p < PocetCisel; p++)
  {
    eeprom_write_block((void *)CislaPrijemcov[p], (void *)adresa, DLZKA_CISLA + 1);
    adresa += DLZKA_CISLA + 1;
  }
}

void NastavPosielanieSMSPriZapnuti(bool val)
{
  SpravaPriZapnuti = (char)val;
  UlozNastaveniaDoEEPROM();
  Serial.print("Posielanie SMS pri zapnuti bolo zmenene. Nove nastavenie je ");
  Serial.println(val ? " ZAPNUTE." : " VYPNUTE.");
}

void NastavSquelch(unsigned char novySquelch)
{
  Squelch = novySquelch;
  MCP41010Write(Squelch);
  UlozNastaveniaDoEEPROM();

  Serial.print("Squelch zmeneny. Nova hodnota je ");
  Serial.print(Squelch, DEC);
  Serial.print(" to je ");
  Serial.print(SquelchPercent(), DEC);
  Serial.println(" percent.");

  String msg = String("Squelch zmeneny. Nova hodnota je ");
  msg += SquelchPercentString();
  msg += " percent. ";
  msg += "Teplota NTC " + TeplotaString() +  " st.C., teplota RTC " + TeplotaRTCString() + " OK.";
  PosliSMS((char*)msg.c_str());
}

void NastavRele(int cislo, bool stav)
{
  if (stav)
  {
    Serial.print("Zapinam rele cislo ");
  }
  else
  {
    Serial.print("Vypinam rele cislo ");
  }

  Serial.print(cislo + 1);
  Serial.println(".");


  digitalWrite(ledky[cislo], stav ? LED_ON : LED_OFF);
  delay(250);
  digitalWrite(relatka[cislo], stav ? RELAY_ON : RELAY_OFF);
  delay(500);
}

void NastavVsetkyRelatka(bool stav)
{
  if (stav)
  {
    Serial.println("Zapinam vsetky relatka.");
  }
  else
  {
    Serial.println("Vypinam vsetky relatka.");
  }

  for (int i = 0; i < POCET_RELATOK; i++)
  {
    NastavRele(i, stav);
  }
}

void ResetniRelatko(int cisloRelatka)
{
  Serial.print("Resetujem relatko cislo ");
  Serial.print(cisloRelatka + 1);
  Serial.println(".");

  NastavRele(cisloRelatka, false);
  //delay(500);
  NastavRele(cisloRelatka, true);
  //delay(500);
}

void ResetniVsetkyRelatka()
{
  Serial.println("Resetujem vsetky relatka.");

  for (int i = 0; i < POCET_RELATOK; i++)
  {
    ResetniRelatko(i);
  }
}


void SpracujSMSPrikaz(String text)
{
  text.toUpperCase();

  Serial.print("Dlzka textu je ");
  Serial.println(text.length());

  if (text.length() == 2)
  {
    char c1 = text.charAt(0);
    char c2 = text.charAt(1);

    if (c1 == 'N')
    {
      NastavPosielanieSMSPriZapnuti(c2 == '1');
      String sprava2 = String("Posielanie SMS pri zapnuti ");
      sprava2 += (c2 == '1') ? "zapnute" : "vypnute";
      sprava2 += ". OK.";
      PosliSMS((char *)sprava2.c_str());
    }

    if (c1 == 'S')
  	{
  	  if (c2 >= '0' && c2 <= '9')
  	  {
  	    unsigned char novySquelch = map(c2 - '0', 0, 9, 0, 255); // todo skontrolovat
  		  NastavSquelch(novySquelch);
  	  }
  	}

    int novyStav = RELAY_ON;
    if (c2 == '1') novyStav = RELAY_ON;
    if (c2 == '0') novyStav = RELAY_OFF;

    int pin = -1;

    if (c1 >= '1' && c1 <= '1' + POCET_RELATOK - 1)
    {
      pin = c1 - '1';
    }

    if (c1 >= 'A' && c1 <= 'A' + POCET_RELATOK - 1)
    {
      pin = c1 - 'A';
    }

    if (pin == -1) return;

    if (c2 == 'R')
    {
      ResetniRelatko(pin);
      String sprava1 = String("Relatko ");
      sprava1 += pin + 1;
      sprava1 += " resetnute. OK.";

      PosliSMS((char*)sprava1.c_str());
    }
    else
    {
      NastavRele(pin, novyStav);
      String sprava2 = String("Relatko ");
      sprava2 += pin + 1;
      sprava2 += novyStav ? "zapnute" : "vypnute";
      sprava2 += ". OK.";
      PosliSMS((char *)sprava2.c_str());
    }
  }

  if (text.length() == 3)
  {
    if (text.startsWith("TX"))
	{
	  OdosielanieZapnute = (text.charAt(2) == '1');
	  Serial.print("Nastavene: Odosielanie SMS celkovo ");
	  Serial.println(OdosielanieZapnute ? "ZAPNUTE" : "VYPNUTE");
	  UlozNastaveniaDoEEPROM();
	}

  if (text.startsWith("HP"))
	{
	  IbaHlavnyPrijemca = (text.charAt(2) == '1');
	  Serial.print("Nastavene: Odosielanie SMS iba hlavnemu prijemcovi ");
	  Serial.println(IbaHlavnyPrijemca ? "ZAPNUTE" : "VYPNUTE");
	  UlozNastaveniaDoEEPROM();
	}
}

if (text.length() == 4)
{
  if (text.charAt(0) == 'S' && text.charAt(1) == 'Q')
  {
    char string[3];
    string[2] == 0;
    string[0] == text.charAt(2);
    string[1] == text.charAt(3);

    int sql = atoi(string);

    Serial.print("Jemna zmena sqelchu: ");
    Serial.print(sql, DEC);
    Serial.println(" percent.");

    unsigned char novySquelch = map(sql, 0, 99, 0, 255); // todo skontrolovat
    NastavSquelch(novySquelch);
  }

  if (text.charAt(0) == 'A' && text.charAt(1) == 'L' && text.charAt(2) == 'L')
  {
    int c2 = text.charAt(3);
    int level = RELAY_ON;

    if (c2 == '0')
    {
      level = RELAY_OFF;
    }

    if (c2 == '1')
    {
      level = RELAY_ON;
    }

    if (c2 == 'R')
    {
      ResetniVsetkyRelatka();
      PosliSMS((char*)"Vsetky relatka resetnute. OK.");
    }
    else
    {
      NastavVsetkyRelatka(level == RELAY_ON);
      String sprava1 = String("Vsetky relatka ");
      sprava1 += (level == RELAY_ON ? "zapnute" : "vypnute");
      sprava1 += ". OK.";
      PosliSMS((char*)sprava1.c_str());
    }
  }

  if (text.charAt(0) == 'T' && text.charAt(1) == 'E' && text.charAt(2) == 'M' && text.charAt(3) == 'P')
  {
     Serial.println("Odosielam dotaz na teplotu.");
     String sprava = String("Dotaz na teplotu. NTC " + TeplotaString() +  " st.C. RTC " + TeplotaRTCString() + " OK.");
     PosliSMS((char*)sprava.c_str());
  }

  if (text.charAt(0) == 'S' && text.charAt(1) == 'H' && text.charAt(2) == 'O' && text.charAt(3) == 'W')
  {
	  Serial.println("Odosielam dotaz na nastavenia.");
    String sprava = String("Nastavenia: ");
	  sprava += " TX: ";
	  sprava += OdosielanieZapnute ? "ON" : "OFF";
	  sprava += " HP: ";
	  sprava += IbaHlavnyPrijemca ? "ON" : "OFF";
	  sprava += ". Squelch ";
    sprava += SquelchPercentString();
    sprava += " percent. ";
	  sprava += "Prijemcovia (";
	  sprava += (char)('0' + PocetCisel);
	  sprava += ") su ";

    for (int p = 0; p < PocetCisel; p++)
	  {
	    sprava += (p + 1);
		  sprava += ". ";
		  sprava += String(CislaPrijemcov[p]);
	    sprava += ", ";
    }

	  sprava += ". Teplota NTC ";
	  sprava += TeplotaString();
	  sprava += " st.C.";
    sprava += " teplota RTC ";
	  sprava += TeplotaRTCString();
	  sprava += " st.C. OK.";
      PosliSMS((char*)sprava.c_str());
    }

  }

  if (text.length() == 16)
  {
  	if (text.charAt(0) == 'A' && text.charAt(1) == 'R' && text.charAt(2) == ' ')
  	{
  		PocetCisel++;
  		String textPrijemca = text.substring(3);
  		char* prijemca = (char*)textPrijemca.c_str();
          memcpy(CislaPrijemcov[PocetCisel - 1], prijemca, DLZKA_CISLA + 1);
  		UlozNastaveniaDoEEPROM();

  		String msg = String();
  		msg += "Pridany prijemca cislo ";
  		msg += (char)('0' + PocetCisel);
  		msg += ", cislo: ";
  		msg += "\"" + String(CislaPrijemcov[PocetCisel - 1]) + "\"";
  		msg += ". OK.";

  		Serial.println(msg);
  		PosliSMS((char*) msg.c_str());
  	}
  }

  if (text.length() == 3)
  {
  	if (text.charAt(0) == 'R' && text.charAt(1) == 'L' && text.charAt(2) == 'R')
  	{
  		PocetCisel--;
  		UlozNastaveniaDoEEPROM();

  		String msg = String();
  		msg += "Odstraneny prijemca cislo ";
  		msg += (char)('0' + PocetCisel + 1);
  		msg += ", cislo: ";
  		msg += "\"" + String(CislaPrijemcov[PocetCisel]) + "\"";
  		msg += ". OK.";

  		Serial.println(msg);
  		PosliSMS((char*) msg.c_str());
  	}
  }
}


void loop()
{
  count = 0;
  digitalWrite(PIN_E1, LOW);
  pinMode (PIN_C4, INPUT);
  pinMode (PIN_C5, INPUT);
  Serial.printf("C4: %d\n", (int)digitalRead(PIN_C4));
  Serial.printf("C5: %d\n", (int)digitalRead(PIN_C5));
  // Serial.println(redLed);

  if (!digitalRead(PIN_TX)) // transmitting
  {
   redLed++;
  }
  else
  {
   redLed = 0;
  }

  if (redLed > TX_TIMEOUT)
  {
    ResetniRelatko(3);
    redLed = 0;
  }

  delay(1);

  while (gsmSerial.available())
  {
    char c;
    digitalWrite(LED_PIN, HIGH);
    c = gsmSerial.read ();
    if (c == 13 || c == 10) continue;
    buff[count++] = c;
    buff[count] = '\0';
    // Avoid overflow
    if (count == MAX_BUFF_LENGTH)
      break;

    delay(10);
  }

  digitalWrite(LED_PIN, LOW);

  if (count == 0) return;

  Serial.print("Serial nacitany, dlzka ");
  Serial.print(count);
  Serial.println(".");

  if (count < 5) return;

  for (int x = 0; x < count; x++)
  {
    Serial.print(x);
    Serial.print(" - ");
    Serial.print(buff[x], DEC);
    Serial.print(" - ");
    Serial.println(buff[x]);
  }

  if (buff[0] == 43 && buff[1] == 67 && buff[2] == 77 && buff[3] == 84 && buff[4] == 73 && buff[5] == 58)
  {
    Serial.println("Bola prijata SMS.");
  }

  if (strstr(buff, "+CMTI:") != NULL)
  {
    Serial.println("SMS prijata.");
    char *result = strstr(buff, ",") + 1;
    int i = -1;

    do
    {
      i++;
      cisloSMS[i] = result[i];
      cisloSMS[i + 1] = 0;
    }
    while ((result[i] != 0) && (result[i] != '\r') && (result[i] != '\n'));

    Serial.print("SMS ma poradove cislo ");
    Serial.print(cisloSMS);
    Serial.println(".");

    String text = PrecitajSMS(cisloSMS);

    Serial.print("Text spravy: \"");
    Serial.print(text);
    Serial.println("\"");

    SpracujSMSPrikaz(text);
  }
}
