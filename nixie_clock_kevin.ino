/*
  Nixie 4 Digit Clock

  Displays Time of Day in 4 digit format, with blinking decimal for colon. Updates time once per
  second, and blinks colon (decimal point) on-and-off on a one second basis (.5 sec on, followed
  by .5 sec off).

  The clock uses an Adafruit RP2040 Kee Boar Driver controller, a generic DS-3231 RTC module (connected to
  IIC/Qwiic port), a Taylor Electronics (www.shop-tes.com) 1373 high voltage power supply, 1N16 Nixie tubes, 
  and ZM-1005 Nixie tubes driver PCBAs (from OSHPark) that utilize NOS, Russian, 74141 BCD Nixe tube drivers. 
  The Nixie tube colon used to indicate seconds, as well as the Tens Hours Nixie digit (always one - either 
  on or off) are both driven with MPSA42, high voltage transistors. Also, using a USB-C breakout board for 
  connectivity (USB-C power supplies needed to power all 4 of the nixies at the same time).

  Display power switch is a SPDT switch with a center off position. The high position hard enables the display 
  ON. The low position hard diables the display OFF. The center switch position allws auto control of the display 
  enable based on the current time. At release time, this is hard-coded to turn on at 7:00AM, and turn off 
  at 6:30PM, every day.

  Time setting is accomplished with a combination of a SPDT swith with a center off position used to select 
  desired set/run mode, and a SPST switch used to set clock set adjustment speed (high position for fast number
  scrolling, and low position for slow number scrolling). The three position set/run switch is left in the center 
  position for normal clock operation. The high switch position will start incrementing the time diplayed by one 
  minute at a time - advanced at the fast or slow rate selected by the fast/slow switch. Similarly, the low 
  switch position will decrement the time displayed by one minute at a time at the rate set with the fast/slow 
  switch.
  The Forward Adjust/Run/Reverse Adjust switch must be left in the center position for the clock to run and keep 
  time.
  It should be noted that the clock ALWAYS counts one more minute in the selected clock set (adjust) direction 
  when returned to the center position. Therefore, it is necessary to return the switch to the center position 
  one minute before the desired time is reach when adjusting from either direction.

  The time adjust/run switches, and the fast/slow switch input to the RP2040 have additional 1k pullup resistors
  to the 3.3 volt power line. On previous controller boards this was unnecessary, and it was possible to rely 
  on the INPUT_PULLUP pin definition to handle the off case. Apparently the RP2040 inputs are more susceptible
  to noise on the input lines than the Teensy LC was. To be fair, due to the position of the RP2040 in the 
  chassis the switch input lines are twice as long as for the older Teensy LC based designs. And there is a LOT
  of noise for switching the Nixie high voltage lines on-and-off for the various tubes.

  Added support to set time through serial port (USB). To do this over Mac/Unix, use the following procedure:
  Start Terminal program (included with Mac OS X, usually located in the Applications/Utilities folder.
  Connect to the clock to the mac using the USB connector,
  In the terminal window, type:
  screen /dev/tty.usbmodem401201 57600
  Follow the directions displayed on the terminal to set the time in 24 hour mode.
  Exit the screen program by typing:
  <cntl>a <cntl>\
  Exit the Terminal program
  Time should be set correctly. This is the preferred method to set the time while maintaining the proper
  AM/PM reference, as the DS3231 gets lost occasionally when setting with the back panle switches.
  As the clock does not have any AM/PM indication, this really just shows up when trying to use the auto on 
  function of the display (center position on the back panel "Display" switch).
  

  13 October 2022
  by Brent Boren
  Modified for Kevin Fox' clock 22 March 2024

  This example code is in the public domain.
*/
#include <DS3231.h>
#include <Wire.h>

// DS-3231 RTC module setup
DS3231 clockRTC;

byte year;
byte month;
byte date;
byte dow;
byte hours;
byte minutes;
byte second;

bool century = false;
bool h12Flag;
bool pmFlag;
bool timeSetpmFlag;
bool relayState; 

// Alarm settings
//  onTime = (Hour ontime * 60) + minutes ontime + (60 * 12 for PM times)
//  offTime = (Hour offtime * 60) + minutes offtime + (60 * 12 for PM times)
//  Set times (in 24 hour format) for auto display on and off. (60 x hours)+minutes+(60x12 for PM times)
int  onTime = 420;
int  offTime = 1110;

// time set inputs
int alarm = 10;
int fastset = 9;
int forwardset = 1; 
//int reverseset = MOSI; //Input manually
int forward = 1;
int reversed = 1;
int fast = 1;

// Misc. variables setup
int timer = 1000;
int i=1;
int colon = 5;
//int minutes = 0;
int transminutes = 0;
int tens = 0;
int transtens = 0;
//int hours = 0;
int transhours = 0;

void setup() {
    // Start the serial port
  Serial.begin(57600);

//  Setup I/O pins for use  
  pinMode(A0, OUTPUT);  //BCD outputs for minutes digit
  pinMode(A1, OUTPUT);  //
  pinMode(A2, OUTPUT);  //
  pinMode(A3, OUTPUT);  //

  pinMode(SCK, OUTPUT);   //Single line output to drive tens hours digit (10,11,12)
  
  pinMode(2, OUTPUT);   //BCD output for hours digit
  pinMode(3, OUTPUT);   //
  pinMode(4, OUTPUT);   //
  pinMode(5, OUTPUT);   //

  pinMode(6, OUTPUT);   //BCD outputs for tens of minutes digit
  pinMode(7, OUTPUT);  //
  pinMode(8, OUTPUT);  //
//  pinMode(9, OUTPUT);  // not used in tens binary for clock count - reassigned to reverseset switch

  pinMode(MISO, OUTPUT);  //Single line output driving colon (decimal point on tens minutes digit)
  pinMode(alarm, OUTPUT); //Single line output driving HV enable pin. HIGH to enable

// time set inputs
  pinMode(fastset, INPUT_PULLUP);  // fast timeset. Also using external 1k pullups to 3.3 volts per above
  pinMode(forwardset, INPUT_PULLUP);  // forward timeset. Also using external 1k pullups to 3.3 volts per above 
  pinMode(MOSI, INPUT_PULLUP);  // reverse timeset. Also using external 1k pullups to 3.3 volts per above


// Start the I2C interface for the DS-3231 RTC module
  Wire.begin();

// Set the DS-3231 to 12 hour mode
  clockRTC.setClockMode(true);

// enable tube power supply  by default (if switch set to auto mode)
  digitalWrite(alarm, HIGH);

  digitalWrite(SCK, LOW);  //Initialize tens hours output low (meaning tens hours is initially off)
  digitalWrite(MISO, HIGH);  //Initialize colon output high (meaning colon digit is on)

  // Request the time correction on the Serial
  delay(4000);
  Serial.println("Format YYMMDDwhhmmssx");
  Serial.println("Where YY = Year (ex. 20 for 2020)");
  Serial.println("      MM = Month (ex. 04 for April)");
  Serial.println("      DD = Day of month (ex. 09 for 9th)");
  Serial.println("      w  = Day of week from 1 to 7, 1 = Sunday (ex. 5 for Thursday)");
  Serial.println("      hh = hours in 24h format (ex. 09 for 9AM or 21 for 9PM)");
  Serial.println("      mm = minutes (ex. 02)");
  Serial.println("      ss = seconds (ex. 42)");
  Serial.println("Example for input : 2004095090242x");
  Serial.println("-----------------------------------------------------------------------------");
  Serial.println("Please enter the current time to set on DS3231 ended by 'x':");
  
}

void checkAlarm(){
//    digitalWrite(alarm, HIGH); uncomment for debug - always on

//  get current hours and minutes from RTC  
  hours = clockRTC.getHour(h12Flag, pmFlag);
  minutes = clockRTC.getMinute();
  int currentTime = (hours * 60) + minutes; // convert hours and minutes to "currentTime" format
  if(pmFlag)
    currentTime  += 720;  // if currentTime pmFlag is set, add 720 (12 hours x 60 minutes) to currentTime
// check if currentTime is between on and off time settings (defined above) for auto-display enable function
  relayState = currentTime >= onTime && currentTime < offTime; 
// set alarm pin (active-high enable line for TE 1373 HV power supply) high or low depending on relayState 
  digitalWrite(alarm, relayState ? HIGH : LOW); 
}

// Main program loop. Update colon and time digits every 100 milliseconds (.1 second - roughly), 
//  and check to see if we've entered time adjust modes (Forward Set or Reverse Set)
void loop() {
// Get current time
      hours = clockRTC.getHour(h12Flag, pmFlag);
      minutes = clockRTC.getMinute();


// Serial port time setting routine
  if (Serial.available()>5) {
    inputDateFromSerial();
    clockRTC.setYear(year);
    clockRTC.setMonth(month);
    clockRTC.setDate(date);
    clockRTC.setDoW(dow);
    clockRTC.setHour(hours);
    clockRTC.setMinute(minutes);
    clockRTC.setSecond(second);
  }

  // manual timeset routines
  forward = digitalRead(forwardset);
  reversed = digitalRead(MOSI);

  if((forward == 0) || (reversed == 0)){
//    Make sure display is on for time setting if switch is set to auto mode
    digitalWrite(alarm, HIGH);

if(pmFlag)
  hours += 12;

//    Check to see if "Forward Set" switch is on    
    while(forward == 0){
      forward = digitalRead(forwardset);
      fast = digitalRead(fastset);
      // handle fast set / slow set
      if(fast == 0){
        timer = 50;  //loop delay for "fast" setting
      }
      else {
        timer = 700;  //loop delay for "slow" setting
      }
        
      // increment minutes variable unless minutes = 59
      if(minutes < 59){
        minutes += 1;
        }
      // if minutes = 59, then roll minutes over to zero
      //  also update the hours
      else{
        minutes = 0;
        // check to see if hours = 24, and increment if not
        if(hours < 24){
          hours += 1;
        }
        // if hours = 24, roll over to zero
        else{
          hours = 1;        }
      }
      if(12 <= hours < 24){
        pmFlag = HIGH;
      }
      else {
        pmFlag = LOW;
      }
      
      // routine to seperate single minutes digit setting from tens minutes digit setting
      tens = minutes/10;
      transminutes = minutes%10;
      
      //  send BCD code to minutes nixie
      digitalWrite(A3, HIGH && (transminutes & 0b00001000));
      digitalWrite(A2, HIGH && (transminutes & 0b00000100));
      digitalWrite(A1, HIGH && (transminutes & 0b00000010));
      digitalWrite(A0, HIGH && (transminutes & 0b00000001));

      //  send BCD code to tens minutes nixie
      digitalWrite(8, HIGH && (tens & 0b00000100));
      digitalWrite(7, HIGH && (tens & 0b00000010));
      digitalWrite(6, HIGH && (tens & 0b00000001));

      //  send BCD code to hours nixie
      if(hours < 10){
        digitalWrite(5, HIGH && (hours & 0b00001000));
        digitalWrite(4, HIGH && (hours & 0b00000100));
        digitalWrite(3, HIGH && (hours & 0b00000010));
        digitalWrite(2, HIGH && (hours & 0b00000001));
      }

      //  send BCD code to hours nixie
      //  check to see if the tens hours digit needs to be set and act accordingly
      if(hours > 9){
        transhours = hours%10;  //divide by 10 and take remainder to peel off the hours digit part
        digitalWrite(5, HIGH && (transhours & 0b00001000));
        digitalWrite(4, HIGH && (transhours & 0b00000100));
        digitalWrite(3, HIGH && (transhours & 0b00000010));
        digitalWrite(2, HIGH && (transhours & 0b00000001));
      }

      //    If Hours > 9, turn on tens hours digit - otherwise turn off 
      if(hours > 9){
        digitalWrite(SCK, HIGH); // tens hours digit on - displays "1"
        }
      if(hours <= 9) {
        digitalWrite(SCK, LOW); //  tens hours digit off - display is blank (off)
       }
      
      delay (timer);
    }
    
//    Check to see if "Reverse Set" switch is on    
    while(reversed == 0){
      reversed = digitalRead(MOSI);
      fast = digitalRead(fastset);
      // handle fast set / slow set
      if(fast == 0){
        timer = 50;
      }
      else {
        timer = 700;
      }
/*      hours = clockRTC.getHour(h12Flag, pmFlag);
      minutes = clockRTC.getMinute();*/
      if(minutes < 1){
        minutes = 59;
        if(hours < 2){
          hours = 24;
        }
        else{
          hours -= 1;
        }
      }
      else{
        minutes -= 1;
      }
      if(12 <= hours < 24){
        pmFlag = HIGH;
      }
      else {
        pmFlag = LOW;
      }
/*      
//      if(pmFlag)
//        hours += 12;
      clockRTC.setHour(hours);
      clockRTC.setMinute(minutes);

      hours = clockRTC.getHour(h12Flag, pmFlag);
      minutes = clockRTC.getMinute();*/

      tens = minutes/10;
      transminutes = minutes%10;
      
      //  send BCD code to minutes nixie
      digitalWrite(A3, HIGH && (transminutes & 0b00001000));
      digitalWrite(A2, HIGH && (transminutes & 0b00000100));
      digitalWrite(A1, HIGH && (transminutes & 0b00000010));
      digitalWrite(A0, HIGH && (transminutes & 0b00000001));

      //  send BCD code to tens minutes nixie
      digitalWrite(8, HIGH && (tens & 0b00000100));
      digitalWrite(7, HIGH && (tens & 0b00000010));
      digitalWrite(6, HIGH && (tens & 0b00000001));

      //  send BCD code to hours nixie
      if(hours < 10){
        digitalWrite(5, HIGH && (hours & 0b00001000));
        digitalWrite(4, HIGH && (hours & 0b00000100));
        digitalWrite(3, HIGH && (hours & 0b00000010));
        digitalWrite(2, HIGH && (hours & 0b00000001));
     }

      //  send BCD code to hours nixie
      if(hours > 9){
        transhours = hours%10;  //divide by 10 and take remainder to peel off the hours digit part
        digitalWrite(5, HIGH && (transhours & 0b00001000));
        digitalWrite(4, HIGH && (transhours & 0b00000100));
        digitalWrite(3, HIGH && (transhours & 0b00000010));
        digitalWrite(2, HIGH && (transhours & 0b00000001));
      }

      //    If Hours > 9, turn on tens hours digit - otherwise turn off (don't set to zero)
      if(hours > 9){
        digitalWrite(SCK, HIGH);
        }
      if(hours <= 9) {
        digitalWrite(SCK, LOW);
       }
      
      delay (timer);
    }

//      if(pmFlag)
//        hours += 12;
      clockRTC.setHour(hours);
      clockRTC.setMinute(minutes);

/*      hours = clockRTC.getHour(h12Flag, pmFlag);
      minutes = clockRTC.getMinute();
*/
//    Reset display to proper on-off state if switch is set to auto mode
  checkAlarm();

  }

// Normal Clock Run Mode (i.e. not trying to adjust the time)
// check to see if display should be on or off for auto display setting
checkAlarm();

// count routine for colon display
// turns colon on for (roughly) a half second, the off for a half second
if (colon > 4)
  digitalWrite(MISO, HIGH);

if (colon <= 4)
  digitalWrite(MISO, LOW);

colon += 1;
if (colon > 9){
  colon = 0;
}
  
//  get current time
hours = clockRTC.getHour(h12Flag, pmFlag);
minutes = clockRTC.getMinute();
  
  tens = minutes/10;
  transminutes = minutes%10;
       
//  send BCD code to minutes nixie
      digitalWrite(A3, HIGH && (transminutes & 0b00001000));
      digitalWrite(A2, HIGH && (transminutes & 0b00000100));
      digitalWrite(A1, HIGH && (transminutes & 0b00000010));
      digitalWrite(A0, HIGH && (transminutes & 0b00000001));

//  send BCD code to tens minutes nixie
      digitalWrite(8, HIGH && (tens & 0b00000100));
      digitalWrite(7, HIGH && (tens & 0b00000010));
      digitalWrite(6, HIGH && (tens & 0b00000001));

//  send BCD code to hours nixie
if(hours < 10){
        digitalWrite(5, HIGH && (hours & 0b00001000));
        digitalWrite(4, HIGH && (hours & 0b00000100));
        digitalWrite(3, HIGH && (hours & 0b00000010));
        digitalWrite(2, HIGH && (hours & 0b00000001));
}

//  send BCD code to hours nixie
if(hours > 9){
  transhours = hours%10;  //divide by 10 and take remainder to peel off the hours digit part
    digitalWrite(5, HIGH && (transhours & 0b00001000));
    digitalWrite(4, HIGH && (transhours & 0b00000100));
    digitalWrite(3, HIGH && (transhours & 0b00000010));
    digitalWrite(2, HIGH && (transhours & 0b00000001));
}

//    If Hours > 9, turn on tens hours digit - otherwise turn off (don't set to zero)
    if(hours > 9){
      digitalWrite(SCK, HIGH);
      }
      
    if(hours <= 9) {
      digitalWrite(SCK, LOW );
      }
      
    //  delay for apx. 100 milliseconds before running through loop again
    delay(100);

// Update time on serial port for debug    
/*
Serial.print("Time: ");
Serial.print(hours);
Serial.print(":");
Serial.print(minutes);
if(pmFlag)
  Serial.println(" PM");
else
  Serial.println(" ");*/
}

/*****************************************************************************************************
 * inputDateFromSerial
 *  - Read and interpret the data from the Serial input
 *  - Store the data in global variables
 *****************************************************************************************************/
void inputDateFromSerial() {
  // Call this if you notice something coming in on 
  // the serial port. The stuff coming in should be in 
  // the order YYMMDDwHHMMSS, with an 'x' at the end.
  boolean isStrComplete = false;
  char inputChar;
  byte temp1, temp2;
  char inputStr[20];

  uint8_t currentPos = 0;
  while (!isStrComplete) {
    if (Serial.available()) {
      inputChar = Serial.read();
      inputStr[currentPos] = inputChar;
      currentPos += 1;

      // Check if string complete (end with "x")
      if (inputChar == 'x') {
        isStrComplete = true;
      }
    }
  }
  Serial.println(inputStr);

  // Find the end of char "x"
  int posX = -1;
  for(uint8_t i = 0; i < 20; i++) {
    if(inputStr[i] == 'x') {
      posX = i;
      break;
    }
  }

  // Consider 0 character in ASCII
  uint8_t zeroAscii = '0';
 
  // Read Year first
  temp1 = (byte)inputStr[posX - 13] - zeroAscii;
  temp2 = (byte)inputStr[posX - 12] - zeroAscii;
  year = temp1 * 10 + temp2;
  
  // now month
  temp1 = (byte)inputStr[posX - 11] - zeroAscii;
  temp2 = (byte)inputStr[posX - 10] - zeroAscii;
  month = temp1 * 10 + temp2;
  
  // now date
  temp1 = (byte)inputStr[posX - 9] - zeroAscii;
  temp2 = (byte)inputStr[posX - 8] - zeroAscii;
  date = temp1 * 10 + temp2;
  
  // now Day of Week
  dow = (byte)inputStr[posX - 7] - zeroAscii;   
  
  // now Hour
  temp1 = (byte)inputStr[posX - 6] - zeroAscii;
  temp2 = (byte)inputStr[posX - 5] - zeroAscii;
  hours = temp1 * 10 + temp2;
  
  // now Minute
  temp1 = (byte)inputStr[posX - 4] - zeroAscii;
  temp2 = (byte)inputStr[posX - 3] - zeroAscii;
  minutes = temp1 * 10 + temp2;
  
  // now Second
  temp1 = (byte)inputStr[posX - 2] - zeroAscii;
  temp2 = (byte)inputStr[posX - 1] - zeroAscii;
  second = temp1 * 10 + temp2;

}
