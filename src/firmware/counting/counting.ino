#include <EEPROM.h>
#include "TimerOne.h"
#include <avr/wdt.h>

#define IN_1       7
#define IN_1_PORT  PIND
#define IN_1_PIN   7

#define IN_2       6
#define IN_2_PORT  PIND
#define IN_2_PIN   6

#define LED_PIN   13

#define OUT_1     14
#define OUT_2     15

#define LF 10
#define CR 13

#define BAUD_RATE 19200

// ---------------------- [ Receive Data ]------------------
#define RX_SIZE  19
byte RxIndex = 0;
char RxBuffer[RX_SIZE+1];

// ---------------------- [ Setup Data ]------------------
struct Setup
{
  unsigned long CRC;          // CRC of the data
  byte          StructLen;    // Length of the structure
  unsigned int  PollCycle;    // Poll cycle in ms
  byte          PollCount;    // Poll-Count till level is stable
  bool          CountOnLH;    // Count on .. edge
  unsigned int  SendCycle;    // Send cycle in ms
} TheSetup;

// ---------------------- [ Input & Debounce ] ----------------
typedef struct {
  byte current_val = 0;
  byte poll_counter = 0;
  unsigned long Counter = 0;
} CounterControl;

CounterControl Counter1;
CounterControl Counter2;

int ledState = LOW;

// ---------------------- [ Releais ] ----------------

typedef struct {
  byte puls_timer  = 0;
} RelaisControl;

RelaisControl Relais1;
RelaisControl Relais2;

void setup() 
{
  Serial.begin(BAUD_RATE);

  Serial.println( "STRT" );

  SetupRead();
  
  pinMode(IN_1,INPUT); 
  digitalWrite(IN_1, HIGH); // activate pull up resistor 
  
  pinMode(IN_2,INPUT); 
  digitalWrite(IN_2, HIGH); // activate pull up resistor 
  
  pinMode(OUT_1,OUTPUT);
  pinMode(OUT_2,OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  setup_interrupt();
}

void setup_interrupt()
{
  unsigned long timeInMicro = TheSetup.PollCycle;
  timeInMicro *= 1000;
  Timer1.initialize(timeInMicro); 
  Timer1.detachInterrupt();
  Timer1.attachInterrupt(timerInterrupt);
}

unsigned long lastSendMillis = 0;
unsigned long lastSecondMillis = 0;

unsigned long loopCounter = 0;

void loop() 
{
  DoCheckRxData();
  unsigned long currentMillis = millis();
  if (currentMillis - lastSendMillis  >= TheSetup.SendCycle) 
  {
    lastSendMillis = currentMillis;
    loopCounter++;
    Serial.print( "CNTR "); 
    Serial.print(loopCounter, DEC ); 
    Serial.print( " ");
    Serial.print(Counter1.Counter, DEC ); 
    Serial.print( " "); 
    Serial.println(Counter2.Counter, DEC );
    Serial.print( "STAT "); 
    Serial.print(loopCounter, DEC ); 
    Serial.print( " ");
    Serial.print(Counter1.current_val, DEC ); 
    Serial.print( " "); 
    Serial.println(Counter2.current_val, DEC );
  }

  if (currentMillis - lastSecondMillis  >= 1000) 
  {
    lastSecondMillis = currentMillis;
    if( Relais1.puls_timer != 0 )
    {
      if( --Relais1.puls_timer == 0 )
      {
        digitalWrite(OUT_1, 0);
        Serial.println( "REL1 0" );
      }
    }
    if( Relais2.puls_timer != 0 )
    {
      if( --Relais2.puls_timer == 0 )
      {
        digitalWrite(OUT_2, 0);
        Serial.println( "REL2 0" );
      }
    }
  }
}

void timerInterrupt()
{
  if (ledState == LOW)
    ledState = HIGH;
  else
    ledState = LOW;
  digitalWrite(LED_PIN, ledState);
  //  Counter 1 
  byte input = bitRead( IN_1_PORT, IN_1_PIN );
  input = (input==1 )?0:1;
  if( Counter1.current_val  == input && input == TheSetup.CountOnLH)
  {
    if( Counter1.poll_counter  != 0xff ) // schon gemeldet
    {
      Counter1.poll_counter ++;
      if( Counter1.poll_counter == TheSetup.PollCount )
      {
         Counter1.Counter++;
         Counter1.poll_counter  = 0xff;
      }
    }
  }
  else
  {
    Counter1.poll_counter = 0;
    Counter1.current_val  = input;
  }

  //  Counter 2
  input = bitRead( IN_2_PORT, IN_2_PIN );
  input = (input==1 )?0:1;
  if( Counter2.current_val  == input && input == TheSetup.CountOnLH)
  {
    if( Counter2.poll_counter  != 0xff ) // allready sent
    {
      Counter2.poll_counter ++;
      if( Counter2.poll_counter == TheSetup.PollCount )
      {
         Counter2.Counter++;
         Counter2.poll_counter  = 0xff;
      }
    }
  }
  else
  {
    Counter2.poll_counter = 0;
    Counter2.current_val  = input;
  }
}


// ----------------------------------------------------------------------------------
// Setup
// ----------------------------------------------------------------------------------
unsigned long SetupCalcCrc()
{
  const unsigned long crc_table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
  };

  unsigned long crc = ~0L;

  byte * p = (byte*)&TheSetup.StructLen;
  int len = sizeof( TheSetup ) - sizeof( TheSetup.CRC );
  for (int i = 0 ; i<len; i++)
  {
    crc = crc_table[(crc ^ *p) & 0x0f] ^ (crc >> 4);
    crc = crc_table[(crc ^ (*p >> 4)) & 0x0f] ^ (crc >> 4);
    p++;
    crc = ~crc;
  }
  return crc;
}

void SetupDefault()
{
  TheSetup.PollCycle = 10;    // Poll cykle 
  TheSetup.PollCount = 3;     // Poll count till accepted
  TheSetup.CountOnLH = 1;     // LH Edge
  TheSetup.SendCycle = 5000;  // Cycle in ms
}

#define EEPROM_ADDRESS  0
void SetupRead()
{
  EEPROM.get(EEPROM_ADDRESS, TheSetup);
  if( TheSetup.CRC != SetupCalcCrc() )
  {
    Serial.println( "ERROR CONFIG SET TO DEFAULT" );
    DoCmdInfo();
    SetupDefault();
    SetupWrite();
  }
}

void SetupWrite()
{
  TheSetup.StructLen = sizeof( TheSetup );
  TheSetup.CRC = SetupCalcCrc();
  EEPROM.put(EEPROM_ADDRESS, TheSetup);
}

// ----------------------------------------------------------------------------------
// R X D
// ----------------------------------------------------------------------------------
void DoCheckRxData()
{
  if( Serial.available() > 0 )
  {
    if( RxIndex >= RX_SIZE )
    {
      RxIndex = 0;
    }
    byte rx = Serial.read();

    if( RxIndex == 0 && (rx == LF || rx == CR) )
      return;
    if( rx == LF || rx == CR )
    {
      RxBuffer[RxIndex++] = 0;
      OnDataReceived(); 
      RxIndex = 0;
    }
    else
    {
      RxBuffer[RxIndex++] = rx;
    }
  }
}


// ----------------------------------------------------------------------------------
// C O M M A N D S
// ----------------------------------------------------------------------------------
// RESET          ( Restart controller)
// INFO           ( print settings)
// POLL 1000      ( Poll cycle in ms )
// EDGE 1|0       ( count on Edge HL or LH )
// SEND 5000      ( send all xxx ms )
// REL1 0|1       ( set releais 1 to on or off )
// REL2 0|1       ( set releais 2 to on or off )
// RPU1 1000      ( pulse relais 1 for nnn ms )
// RPU2 1000      ( pulse relais 2 for nnn ms )
void OnDataReceived()
{
  bool result = false;
  bool writeSetup = false;
  bool callSetupInterrupt = false;
  String cmd = RxBuffer;
  cmd.toUpperCase();
  
  if( cmd.startsWith("RESET"))
  {
    Serial.println( "OK" );
    DoCmdReset();
  }
  
  if( cmd.startsWith("INFO"))
  {
    DoCmdInfo();
    result = true;
  }

  int value = toInt( cmd, 5 );
  if( value != -1 )
  {
    if( cmd.startsWith("POLL "))
    {
      if( !checkRange( value, 10, 1000 ) )
        return;
      TheSetup.PollCycle = value;
      Serial.print( "POLL ");
      Serial.println( value, DEC );
      writeSetup = true;
      result = true;
      callSetupInterrupt = true;
    }

    if( cmd.startsWith("DEBO "))
    {
      if( !checkRange( value, 1, 20 ) )
        return;
      TheSetup.PollCount = value;
      Serial.print( "DEBO ");
      Serial.println( value, DEC );
      result = true;
      writeSetup = true;
    }
  
    if( cmd.startsWith("EDGE "))
    {
      if( !checkRange( value, 0, 1 ) )
        return;
      TheSetup.CountOnLH = value;
      Serial.print( "EDGE ");
      Serial.println( value, DEC );
      result = true;
      writeSetup = true;
    }

    if( cmd.startsWith("REL1 "))
    {
      if( !checkRange( value, 0, 1 ) )
        return;
      digitalWrite(OUT_1, value);
      Serial.print( "REL1 ");
      Serial.println( value, DEC );
      result = true;
    }

    if( cmd.startsWith("REL2 "))
    {
      if( !checkRange( value, 0, 1 ) )
        return;
      digitalWrite(OUT_2, value);
      Serial.print( "REL2 ");
      Serial.println( value, DEC );
      result = true;
    }

    if( cmd.startsWith("RPU1 "))
    {
      if( !checkRange( value, 1, 255 ) )
        return;
      digitalWrite(OUT_1, 1);
      Relais1.puls_timer = value;
      Serial.print( "RPU1 ");
      Serial.println( value, DEC );
      Serial.println( "REL1 1" );
      result = true;
    }

    if( cmd.startsWith("RPU2 "))
    {
      if( !checkRange( value, 0, 255 ) )
        return;
      digitalWrite(OUT_2, 1);
      Relais2.puls_timer = value;
      Serial.print( "RPU2 ");
      Serial.println( value, DEC );
      Serial.println( "REL2 1" );
      result = true;
    }

  
    if( cmd.startsWith("SEND "))
    {
      if( !checkRange( value, 1000, 50000 ) )
        return;
       TheSetup.SendCycle = value;
      Serial.print( "SEND ");
      Serial.println( value, DEC );
      writeSetup = true;
      result = true;
    }
  }
  if( result )
  {
    if( writeSetup )
      SetupWrite();
    if( callSetupInterrupt )
      setup_interrupt();
  }
  else
    Serial.println( "SYNTAX" );
}

void DoCmdInfo()
{
    Serial.print( "POLL "); Serial.println( TheSetup.PollCycle, DEC );
    Serial.print( "DEBO "); Serial.println( TheSetup.PollCount, DEC );
    Serial.print( "EDGE "); Serial.println(TheSetup.CountOnLH, DEC );
    Serial.print( "SEND "); Serial.println( TheSetup.SendCycle, DEC);
    Serial.print( "REL1 "); Serial.println( digitalRead(OUT_1), DEC);
    Serial.print( "REL2 "); Serial.println( digitalRead(OUT_2), DEC);
}

void DoCmdReset()
{
  wdt_enable( WDTO_60MS );
   while(1) {}
}


bool checkRange( unsigned int val, unsigned int mini, unsigned int maxi )
{
  if( val < mini || val > maxi )
  {
    Serial.print( "INVALID. MIN=" );
    Serial.print( mini, DEC );
    Serial.print( " MAX=" );
    Serial.println( maxi, DEC );
    return false;
  }
  return true;
}

int toInt( String s, int pos  )
{
  String valString = s.substring(pos);
  valString.trim();
  if( valString.length() == 0 )
    return -1;
  if( !isNumeric(valString) )
    return -1;
  return valString.toInt();
}

boolean isNumeric(String str) 
{
    for(char i = 0; i < str.length(); i++) 
    {
        if ( !(isDigit(str.charAt(i)) || str.charAt(i) == '.' )) 
        {
            return false;
        }
    }
    return true;
}


