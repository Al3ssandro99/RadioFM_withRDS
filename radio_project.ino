#include <Wire.h>
#include <LiquidCrystal.h>


int STATUS_LED = 13;
int resetPin = 7;
int SDIO = A4; //SDA/A4 on Arduino
int SCLK = A5; //SCL/A5 on Arduino
char printBuffer[50];
uint16_t si4703_registers[16]; //There are 16 registers, each 16 bits large

#define FAIL  0
#define SUCCESS  1

#define MAX_VOL 15
#define MIN_VOL 0

#define SI4703 0x10 //0b._001.0000 = I2C address of Si4703 - note that the Wire function assumes non-left-shifted I2C address, not 0b.0010.000W
#define I2C_FAIL_MAX  10 //This is the number of attempts we will try to contact the device before erroring out

#define IN_EUROPE //Use this define to setup European FM reception. I wuz there for a day during testing (TEI 2011).

#define SEEK_DOWN  0 //Direction used for seeking. Default is down
#define SEEK_UP  1

//Define the register names
#define DEVICEID 0x00
#define CHIPID  0x01
#define POWERCFG  0x02
#define CHANNEL  0x03
#define SYSCONFIG1  0x04
#define SYSCONFIG2  0x05
#define STATUSRSSI  0x0A
#define READCHAN  0x0B
#define RDSA  0x0C
#define RDSB  0x0D
#define RDSC  0x0E
#define RDSD  0x0F

//Register 0x02 - POWERCFG
#define SMUTE  15
#define DMUTE  14
#define SKMODE  10
#define SEEKUP  9
#define SEEK  8

//Register 0x03 - CHANNEL
#define TUNE  15

//Register 0x04 - SYSCONFIG1
#define RDS  12
#define DE  11

//Register 0x05 - SYSCONFIG2
#define SPACE1  5
#define SPACE0  4

//Register 0x0A - STATUSRSSI
#define RDSR  15
#define STC  14
#define SFBL  13
#define AFCRL  12
#define RDSS  11
#define STEREO  8

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);


void setup() {                
  pinMode(13, OUTPUT);
  pinMode(A0, INPUT); //Optional trimpot for analog station control

  Serial.begin(57600);
  Serial.println();

  si4703_init(); //Init the Si4703 - we need to toggle SDIO before Wire.begin takes over.
  
  
  lcd.begin(16, 2);
  clearLCD(2);
  
  printLCD("BENVENUTI", 0);
  printLCD("RADIO ARDUINO", 1);
  delay(1000);


      si4703_readRegisters(); //Read the current register set
      si4703_registers[SYSCONFIG2] &= 0xFFF0; //Clear volume bits
      si4703_registers[SYSCONFIG2] |= 8; //Set new volume
      si4703_updateRegisters(); //Update
}

void loop() {
  char option;
  int currentChannel = 1027; //Default the unit to a known good local radio station

  gotoChannel(currentChannel);

  Serial.println();
  Serial.println(" BENVENUTO SU RADIO FM");

  showMenu();

  currentChannel = readChannel();
  sprintf(printBuffer, "FM: %02d.%01dMHz", currentChannel / 10, currentChannel % 10);
  //Serial.println(printBuffer);

  clearLCD(2);
  lcd.setCursor(0,0);
  lcd.print(printBuffer);
  
  while(1) {
    while (!Serial.available());
    option = Serial.read();

    if(option == '1') {
      //Serial.println("flag muto");
      si4703_readRegisters();
      si4703_registers[POWERCFG] ^= (1<<DMUTE); //Toggle Mute bit
      si4703_updateRegisters();
      if (si4703_registers[POWERCFG] > 1){
        clearLCD(2);
        printLCD("Volume: ON", 0);
        delay(1000);
      }
      else{
        clearLCD(2);
        printLCD("Volume: OFF", 0); 
        delay(1000);     
      }     
    }
    else if(option == '2') {
      seek(SEEK_UP);
    }
    else if(option == '3') {
      seek(SEEK_DOWN);
    }
    else if(option == '4') {
      //Serial.println("Stampo dati radio - x per uscire");
      char stationName[8];
      char* pointerToStationNameData = stationName;
      char radioTextData[64];
      char* pointerToRadioTextData = radioTextData;

      memset(radioTextData, ' ', sizeof(radioTextData));
      memset(stationName, ' ', sizeof(stationName));
               
      clearLCD(2);
      lcd.setCursor(0,0);
      lcd.print("Nome: LOW SIGNAL");
      while (1) {

        if (Serial.available() > 0)
          if (Serial.read() == 'x') break;
        si4703_readRegisters();
        if(si4703_registers[STATUSRSSI] & (1<<RDSR)){
          if (isValidStationNameData())
          {
            if (isStationNameData())
            {
              // set each element of the radio station name as we get it
              setStationNameData(pointerToStationNameData);

              // now write the radio text to serial
              for (short i = 0; i < sizeof(stationName); i++) {
                //Serial.print(stationName[i]);
              }
                        
              //Serial.println();

              
              lcd.setCursor(14,0);
              lcd.print("  ");
              lcd.setCursor(6,0);
              for (short i = 0; i < sizeof(stationName); i++) {
                lcd.print(stationName[i]);
              }
              //Serial.println(" ");
          }
              //Serial.println(" ");
              
          }
if (isValidRdsData()){
          
          if (isRadioTextData())
            {
              // set each element of the radio text data as we get it
              setRadioTextData(pointerToRadioTextData);
              
               lcd.setCursor(0,1); 
              // now write the radio text to lcd
              //for (short i = 0; i < sizeof(radioTextData); i++) {
              for (short i = 0; i < 16; i++) {
                lcd.print(radioTextData[i]);
              }
              
               }
              // now write the radio text to serial
              for (short i = 0; i < sizeof(radioTextData); i++) {
                //Serial.print(radioTextData[i]);
              }
        }
          delay(1); //Wait for the RDS bit to clear
        }        
        else {
          delay(1); //From AN230, using the polling method 40ms should be sufficient amount of time between checks
        }
     }
    }
    else if(option == '+'){
      byte current_vol;
      int vol;
      char volume[16];
      si4703_readRegisters(); //Read the current register set
      current_vol = si4703_registers[SYSCONFIG2] & 0x000F; //Read the current volume level
      if(current_vol < MAX_VOL){
        current_vol++; //Limit max volume to 0x000F
        si4703_registers[SYSCONFIG2] &= 0xFFF0; //Clear volume bits
        si4703_registers[SYSCONFIG2] |= current_vol; //Set new volume
        si4703_updateRegisters(); //Update
      }
      else{
        current_vol = MAX_VOL;
        
        si4703_registers[SYSCONFIG2] &= 0xFFF0; //Clear volume bits
        si4703_registers[SYSCONFIG2] |= current_vol; //Set new volume
        si4703_updateRegisters(); //Update
      }
      
      vol = int(current_vol);
      char temp[16];
      itoa(vol, temp, 10);

      char buf[16];
      char *first = "Volume: ";
      char *second = "";

      if (vol == MAX_VOL){
        second = "MAX";
      }
      else{
        second = temp;
      }
      strcpy(buf,first);
      strcat(buf,second);
      
      //Serial.println(buf);
      
      clearLCD(2);
      printLCD(buf, 0);
      delay(1000);
      }

     else if(option == '-'){
      byte current_vol;
      int vol;
      char volume[16];
      si4703_readRegisters(); //Read the current register set
      current_vol = si4703_registers[SYSCONFIG2] & 0x000F; //Read the current volume level
      if(current_vol > MIN_VOL){
        current_vol--; //Limit max volume to 0x000F
        si4703_registers[SYSCONFIG2] &= 0xFFF0; //Clear volume bits
        si4703_registers[SYSCONFIG2] |= current_vol; //Set new volume
        si4703_updateRegisters(); //Update
      }
      else{
        current_vol = MIN_VOL;
        
        si4703_registers[SYSCONFIG2] &= 0xFFF0; //Clear volume bits
        si4703_registers[SYSCONFIG2] |= current_vol; //Set new volume
        si4703_updateRegisters(); //Update
      }
      
      vol = int(current_vol);
      char temp[16];
      itoa(vol, temp, 10);

      char buf[30];
      const char *first = "Volume: ";
      const char *second;

      if (vol == MIN_VOL){
        second = "MUTE";
      }
      else{
        second = temp;
      }
      strcpy(buf,first);
      strcat(buf,second);
      
      
      //Serial.println(buf);
      
      clearLCD(2);
      printLCD(buf, 0);
      delay(1000);
    }

    else if(option == '5') {
      currentChannel = readChannel();
#ifdef IN_EUROPE
      currentChannel += 1; //Increase channel by 100kHz
#else
      currentChannel += 2; //Increase channel by 200kHz
#endif
      if (currentChannel>1080){
        currentChannel = 875;
      }
      gotoChannel(currentChannel);
    }
    else if(option == '6') {
      currentChannel = readChannel();
#ifdef IN_EUROPE
      currentChannel -= 1; //Decreage channel by 100kHz
#else
      currentChannel -= 2; //Decrease channel by 200kHz
#endif
      if (currentChannel<875){
              currentChannel = 1080;
            }
      gotoChannel(currentChannel);
    }
    else {
        currentChannel = readChannel();
        sprintf(printBuffer, "FM: %02d.%01dMHz", currentChannel / 10, currentChannel % 10);
        //Serial.println(printBuffer);
        clearLCD(2);
        lcd.setCursor(0,0);
        lcd.print(printBuffer);
    }
  }
}

//Given a channel, tune to it
//Channel is in MHz, so 973 will tune to 97.3MHz
//Note: gotoChannel will go to illegal channels (ie, greater than 110MHz)
//It's left to the user to limit these if necessary
//Actually, during testing the Si4703 seems to be internally limiting it at 87.5. Neat.
void gotoChannel(int newChannel){
  //Freq(MHz) = 0.200(in USA) * Channel + 87.5MHz
  //97.3 = 0.2 * Chan + 87.5
  //9.8 / 0.2 = 49
  newChannel *= 10; //973 * 10 = 9730
  newChannel -= 8750; //9730 - 8750 = 980

#ifdef IN_EUROPE
    newChannel /= 10; //980 / 10 = 98
#else
  newChannel /= 20; //980 / 20 = 49
#endif

  //These steps come from AN230 page 20 rev 0.5
  si4703_readRegisters();
  si4703_registers[CHANNEL] &= 0xFE00; //Clear out the channel bits
  si4703_registers[CHANNEL] |= newChannel; //Mask in the new channel
  si4703_registers[CHANNEL] |= (1<<TUNE); //Set the TUNE bit to start
  si4703_updateRegisters();

  //delay(60); //Wait 60ms - you can use or skip this delay

  //Poll to see if STC is set
  while(1) {
    si4703_readRegisters();
    if( (si4703_registers[STATUSRSSI] & (1<<STC)) != 0) break; //Tuning complete!
  }

  si4703_readRegisters();
  si4703_registers[CHANNEL] &= ~(1<<TUNE); //Clear the tune after a tune has completed
  si4703_updateRegisters();

  //Wait for the si4703 to clear the STC as well
  while(1) {
    si4703_readRegisters();
    if( (si4703_registers[STATUSRSSI] & (1<<STC)) == 0) break; //Tuning complete!
    //Serial.println("Waiting...");
  }
}

//Reads the current channel from READCHAN
//Returns a number like 973 for 97.3MHz
int readChannel(void) {
  si4703_readRegisters();
  int channel = si4703_registers[READCHAN] & 0x03FF; //Mask out everything but the lower 10 bits

#ifdef IN_EUROPE
  //Freq(MHz) = 0.100(in Europe) * Channel + 87.5MHz
  //X = 0.1 * Chan + 87.5
  channel *= 1; //98 * 1 = 98 - I know this line is silly, but it makes the code look uniform
#else
  //Freq(MHz) = 0.200(in USA) * Channel + 87.5MHz
  //X = 0.2 * Chan + 87.5
  channel *= 2; //49 * 2 = 98
#endif

  channel += 875; //98 + 875 = 973
  return(channel);
}

//Seeks out the next available station
//Returns the freq if it made it
//Returns zero if failed
byte seek(byte seekDirection){
  si4703_readRegisters();

  //Set seek mode wrap bit
  //si4703_registers[POWERCFG] |= (1<<SKMODE); //Allow wrap
  si4703_registers[POWERCFG] &= ~(1<<SKMODE); //Disallow wrap - if you disallow wrap, you may want to tune to 87.5 first

  if(seekDirection == SEEK_DOWN) si4703_registers[POWERCFG] &= ~(1<<SEEKUP); //Seek down is the default upon reset
  else si4703_registers[POWERCFG] |= 1<<SEEKUP; //Set the bit to seek up

  si4703_registers[POWERCFG] |= (1<<SEEK); //Start seek

  si4703_updateRegisters(); //Seeking will now start

  //Poll to see if STC is set
  while(1) {
    si4703_readRegisters();
    if((si4703_registers[STATUSRSSI] & (1<<STC)) != 0) break; //Tuning complete!

    //Serial.print("Trying station:");
    //Serial.println(readChannel());
  }

  si4703_readRegisters();
  int valueSFBL = si4703_registers[STATUSRSSI] & (1<<SFBL); //Store the value of SFBL
  si4703_registers[POWERCFG] &= ~(1<<SEEK); //Clear the seek bit after seek has completed
  si4703_updateRegisters();

  //Wait for the si4703 to clear the STC as well
  while(1) {
    si4703_readRegisters();
    if( (si4703_registers[STATUSRSSI] & (1<<STC)) == 0) break; //Tuning complete!
    //Serial.println("Waiting...");
    clearLCD(2);
    printLCD("Waiting...", 0);
    delay(200);
  }

  if(valueSFBL) { //The bit was set indicating we hit a band limit or failed to find a station
    //Serial.println("Seek limit hit"); //Hit limit of band during seek
    clearLCD(2);
    printLCD("Seek limit hit", 0);
    delay(200);
    return(FAIL);
  }

  //Serial.println("Seek complete"); //Tuning complete!
  clearLCD(2);
  printLCD("Seek complete", 0);
  delay(200);
  return(SUCCESS);
}

//To get the Si4703 inito 2-wire mode, SEN needs to be high and SDIO needs to be low after a reset
//The breakout board has SEN pulled high, but also has SDIO pulled high. Therefore, after a normal power up
//The Si4703 will be in an unknown state. RST must be controlled
void si4703_init(void) {
  //Serial.println("Initializing I2C and Si4703");
  
  pinMode(resetPin, OUTPUT);
  pinMode(SDIO, OUTPUT); //SDIO is connected to A4 for I2C
  digitalWrite(SDIO, LOW); //A low SDIO indicates a 2-wire interface
  digitalWrite(resetPin, LOW); //Put Si4703 into reset
  delay(1); //Some delays while we allow pins to settle
  digitalWrite(resetPin, HIGH); //Bring Si4703 out of reset with SDIO set to low and SEN pulled high with on-board resistor
  delay(1); //Allow Si4703 to come out of reset

  Wire.begin(); //Now that the unit is reset and I2C inteface mode, we need to begin I2C

  si4703_readRegisters(); //Read the current register set
  //si4703_registers[0x07] = 0xBC04; //Enable the oscillator, from AN230 page 9, rev 0.5 (DOES NOT WORK, wtf Silicon Labs datasheet?)
  si4703_registers[0x07] = 0x8100; //Enable the oscillator, from AN230 page 9, rev 0.61 (works)
  si4703_updateRegisters(); //Update

  delay(500); //Wait for clock to settle - from AN230 page 9

  si4703_readRegisters(); //Read the current register set
  si4703_registers[POWERCFG] = 0x4001; //Enable the IC
  //  si4703_registers[POWERCFG] |= (1<<SMUTE) | (1<<DMUTE); //Disable Mute, disable softmute
  si4703_registers[SYSCONFIG1] |= (1<<RDS); //Enable RDS

#ifdef IN_EUROPE
    si4703_registers[SYSCONFIG1] |= (1<<DE); //50kHz Europe setup
  si4703_registers[SYSCONFIG2] |= (1<<SPACE0); //100kHz channel spacing for Europe
#else
  si4703_registers[SYSCONFIG2] &= ~(1<<SPACE1 | 1<<SPACE0) ; //Force 200kHz channel spacing for USA
#endif

  si4703_registers[SYSCONFIG2] &= 0xFFF0; //Clear volume bits
  si4703_registers[SYSCONFIG2] |= 0x0001; //Set volume to lowest
  si4703_updateRegisters(); //Update

  delay(110); //Max powerup time, from datasheet page 13
}

//Write the current 9 control registers (0x02 to 0x07) to the Si4703
//It's a little weird, you don't write an I2C addres
//The Si4703 assumes you are writing to 0x02 first, then increments
byte si4703_updateRegisters(void) {

  Wire.beginTransmission(SI4703);
  //A write command automatically begins with register 0x02 so no need to send a write-to address
  //First we send the 0x02 to 0x07 control registers
  //In general, we should not write to registers 0x08 and 0x09
  for(int regSpot = 0x02 ; regSpot < 0x08 ; regSpot++) {
    byte high_byte = si4703_registers[regSpot] >> 8;
    byte low_byte = si4703_registers[regSpot] & 0x00FF;

    Wire.write(high_byte); //Upper 8 bits
    Wire.write(low_byte); //Lower 8 bits
  }

  //End this transmission
  byte ack = Wire.endTransmission();
  if(ack != 0) { //We have a problem! 
    //Serial.print("Write Fail:"); //No ACK!
    //Serial.println(ack, DEC); //I2C error: 0 = success, 1 = data too long, 2 = rx NACK on address, 3 = rx NACK on data, 4 = other error
    clearLCD(2);
    printLCD("Write Fail", 0);
    return(FAIL);
  }

  return(SUCCESS);
}

//Read the entire register control set from 0x00 to 0x0F
void si4703_readRegisters(void){

  //Si4703 begins reading from register upper register of 0x0A and reads to 0x0F, then loops to 0x00.
  Wire.requestFrom(SI4703, 32); //We want to read the entire register set from 0x0A to 0x09 = 32 bytes.

  //Remember, register 0x0A comes in first so we have to shuffle the array around a bit
  for(int x = 0x0A ; ; x++) { //Read in these 32 bytes
    if(x == 0x10) x = 0; //Loop back to zero
    si4703_registers[x] = Wire.read() << 8;
    si4703_registers[x] |= Wire.read();
    if(x == 0x09) break; //We're done!
  }
}

void si4703_printRegisters(void) {
  //Read back the registers
  si4703_readRegisters();

  //Print the response array for debugging
  for(int x = 0 ; x < 16 ; x++) {
    sprintf(printBuffer, "Reg 0x%02X = 0x%04X", x, si4703_registers[x]);
    Serial.println(printBuffer);
  }
}

boolean isValidAsciiBasicCharacterSet(byte rdsData)
{
  return rdsData >= 32 && rdsData <= 127;
}

boolean isValidRdsData() {

  byte blockErrors = (byte)((si4703_registers[STATUSRSSI] & 0x0600) >> 9); //Mask in BLERA;

  byte Ch = (si4703_registers[RDSC] & 0xFF00) >> 8;
  byte Cl = (si4703_registers[RDSC] & 0x00FF);

  byte Dh = (si4703_registers[RDSD] & 0xFF00) >> 8;
  byte Dl = (si4703_registers[RDSD] & 0x00FF);

  return blockErrors == 0 && isValidAsciiBasicCharacterSet(Dh) && isValidAsciiBasicCharacterSet(Dl) && isValidAsciiBasicCharacterSet(Ch) && isValidAsciiBasicCharacterSet(Cl);
}

boolean isValidStationNameData() {

  byte blockErrors = (byte)((si4703_registers[STATUSRSSI] & 0x0600) >> 9); //Mask in BLERA;

  byte Dh = (si4703_registers[RDSD] & 0xFF00) >> 8;
  byte Dl = (si4703_registers[RDSD] & 0x00FF);

  return blockErrors == 0 && isValidAsciiBasicCharacterSet(Dh) && isValidAsciiBasicCharacterSet(Dl);
}

boolean isRadioTextData(void) {
  return (si4703_registers[RDSB] >> 11) == 4 || (si4703_registers[RDSB] >> 11) == 5;
}

boolean isStationNameData(void) {
  return (si4703_registers[RDSB] >> 11) == 0 || (si4703_registers[RDSB] >> 11) == 1;
}

void setRadioTextData(char* pointerToRadioTextData)
{
  // retrieve where this data sits in the RDS message
  byte positionOfData = (si4703_registers[RDSB] & 0x00FF & 0xf);
  byte characterPosition;

  byte Ch = (si4703_registers[RDSC] & 0xFF00) >> 8;
  byte Cl = (si4703_registers[RDSC] & 0x00FF);

  byte Dh = (si4703_registers[RDSD] & 0xFF00) >> 8;
  byte Dl = (si4703_registers[RDSD] & 0x00FF);

  characterPosition = positionOfData * 4;
  pointerToRadioTextData[characterPosition] = (char)Ch;

  characterPosition = positionOfData * 4 + 1;
  pointerToRadioTextData[characterPosition] = (char)Cl;

  characterPosition = positionOfData * 4 + 2;
  pointerToRadioTextData[characterPosition] = (char)Dh;

  characterPosition = positionOfData * 4 + 3;
  pointerToRadioTextData[characterPosition] = (char)Dl;
}

void setStationNameData(char* pointerToStationNameData)
{
  // retrieve where this data sits in the RDS message
  byte positionOfData = (si4703_registers[RDSB] & 0x00FF & 0x3);
  byte characterPosition;

  byte Dh = (si4703_registers[RDSD] & 0xFF00) >> 8;
  byte Dl = (si4703_registers[RDSD] & 0x00FF);

  characterPosition = positionOfData * 2;
  pointerToStationNameData[characterPosition] = (char)Dh;

  characterPosition = positionOfData * 2 + 1;
  pointerToStationNameData[characterPosition] = (char)Dl;
}

void showMenu(void){
    Serial.println("1) Muto On/Off");
    Serial.println("2) Ricerca automatica >>");
    Serial.println("3) Ricerca automatica <<");
    Serial.println("4) Stampa dati Stazione Radio (poi premi x per fermare)");
    Serial.println("+) Volume +");
    Serial.println("-) Volume -");
    Serial.println("5) Ricerca manuale >>");
    Serial.println("6) Ricerca manuale <<");
    Serial.print("");
}

void clearLCD(unsigned int row){
  if (row == 0){
    lcd.setCursor(0,0);
    lcd.print("                ");    
  }
  else if (row == 1){
    lcd.setCursor(0,1);
    lcd.print("                ");
  }
  else if (row == 2){
    lcd.setCursor(0,0);
    lcd.print("                ");  
    lcd.setCursor(0,1);
    lcd.print("                ");
  }
}

// scrive su schermo
void printLCD(char str1[], unsigned int row){
  String mystring = String(str1);
  unsigned int len = 0;
  
  len = mystring.length();

  if (len > 16){
    len = 0;
  }
  else {
    len = int(len/2);
    len = 8 - len;
  }

  if (row == 0){
    clearLCD(0);
    lcd.setCursor(len,0);
    lcd.print(str1);
  }
  else if (row ==1){
    clearLCD(1);
    lcd.setCursor(len,1);
    lcd.print(str1);
  }
}
