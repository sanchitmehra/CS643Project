#include <Bridge.h>
#include <Console.h>
#include <FileIO.h>
#include <HttpClient.h>
#include <Mailbox.h>
#include <Process.h>
#include <YunClient.h>
#include <YunServer.h>

#include <Bridge.h>
#include <BridgeClient.h>
#include <BridgeServer.h>
#include <BridgeSSLClient.h>
#include <BridgeUdp.h>
#include <Console.h>
#include <FileIO.h>
#include <HttpClient.h>
#include <Mailbox.h>
#include <Process.h>
#include <YunClient.h>
#include <YunServer.h>

#include <BVSP.h>
#include <BVSMic.h>
#include <BVSSpeaker.h>
#include <DAC.h>

// Defines the Arduino pin that will be used to capture audio 
#define BVSM_AUDIO_INPUT 7

// Defines the LED pins
#define RED_LED_PIN 6
#define YELLOW_LED_PIN 9
#define GREEN_LED_PIN 10

// Defines the constants that will be passed as parameters to 
// the BVSP.begin function
const unsigned long STATUS_REQUEST_TIMEOUT =  3000;
const unsigned long STATUS_REQUEST_INTERVAL = 4000;

// Defines the size of the mic audio buffer 
const int MIC_BUFFER_SIZE = 64;

// Defines the size of the speaker audio buffer
const int SPEAKER_BUFFER_SIZE = 128;

// Defines the size of the receive buffer
const int RECEIVE_BUFFER_SIZE = 2;

// Initializes a new global instance of the BVSP class 
BVSP bvsp = BVSP();

// Initializes a new global instance of the BVSMic class 
BVSMic bvsm = BVSMic();

// Initializes a new global instance of the BVSSpeaker class 
BVSSpeaker bvss = BVSSpeaker();

// Creates a buffer that will be used to read recorded samples 
// from the BVSMic class 
byte micBuffer[MIC_BUFFER_SIZE];

// Creates a buffer that will be used to write audio samples 
// into the BVSSpeaker class 
byte speakerBuffer[SPEAKER_BUFFER_SIZE];

// Creates a buffer that will be used to read the commands sent
// from BitVoicer Server.
// Byte 0 = pin number
// Byte 1 = pin value
byte receiveBuffer[RECEIVE_BUFFER_SIZE];

// These variables are used to control when to play
// "LED Notes". These notes will be played along with 
// the song streamed from BitVoicer Server.
bool playLEDNotes = false;
unsigned int playStartTime = 0;

void setup() 
{
  // Sets up the pin modes
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);

  // Sets the initial state of all LEDs
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);
  
  // Starts serial communication at 115200 bps 
  Serial.begin(115200); 
  
  // Sets the Arduino serial port that will be used for 
  // communication, how long it will take before a status request 
  // times out and how often status requests should be sent to 
  // BitVoicer Server. 
  bvsp.begin(Serial, STATUS_REQUEST_TIMEOUT, STATUS_REQUEST_INTERVAL);
    
  // Defines the function that will handle the frameReceived 
  // event 
  bvsp.frameReceived = BVSP_frameReceived;

  // Sets the function that will handle the modeChanged 
  // event 
  bvsp.modeChanged = BVSP_modeChanged; 
  
  // Sets the function that will handle the streamReceived 
  // event 
  bvsp.streamReceived = BVSP_streamReceived;
  
  // Prepares the BVSMic class timer 
  bvsm.begin();

  // Sets the DAC that will be used by the BVSSpeaker class 
  bvss.begin(DAC);
}

void loop() 
{
  // Checks if the status request interval has elapsed and if it 
  // has, sends a status request to BitVoicer Server 
  bvsp.keepAlive();
  
  // Checks if there is data available at the serial port buffer 
  // and processes its content according to the specifications 
  // of the BitVoicer Server Protocol 
  bvsp.receive();

  // Checks if there is one SRE available. If there is one, 
  // starts recording.
  if (bvsp.isSREAvailable()) 
  {
    // If the BVSMic class is not recording, sets up the audio 
    // input and starts recording 
    if (!bvsm.isRecording)
    {
      bvsm.setAudioInput(BVSM_AUDIO_INPUT, DEFAULT); 
      bvsm.startRecording();
    }

    // Checks if the BVSMic class has available samples 
    if (bvsm.available)
    {
      // Makes sure the inbound mode is STREAM_MODE before 
      // transmitting the stream
      if (bvsp.inboundMode == FRAMED_MODE)
        bvsp.setInboundMode(STREAM_MODE); 
        
      // Reads the audio samples from the BVSMic class
      int bytesRead = bvsm.read(micBuffer, MIC_BUFFER_SIZE);
      
      // Sends the audio stream to BitVoicer Server
      bvsp.sendStream(micBuffer, bytesRead);
    }
  }
  else
  {
    // No SRE is available. If the BVSMic class is recording, 
    // stops it.
    if (bvsm.isRecording)
      bvsm.stopRecording();
  }

  // Plays all audio samples available in the BVSSpeaker class
  // internal buffer. These samples are written in the 
  // BVSP_streamReceived event handler. If no samples are 
  // available in the internal buffer, nothing is played.
  bvss.play();

  // If playLEDNotes has been set to true, 
  // plays the "LED notes" along with the music.
  if (playLEDNotes)
    playNextLEDNote();
}

// Handles the frameReceived event 
void BVSP_frameReceived(byte dataType, int payloadSize) 
{
  // Checks if the received frame contains binary data
  // 0x07 = Binary data (byte array)
  if (dataType == DATA_TYPE_BINARY)
  {
    // If 2 bytes were received, process the command.
    if (bvsp.getReceivedBytes(receiveBuffer, RECEIVE_BUFFER_SIZE) == 
      RECEIVE_BUFFER_SIZE)
    {
      analogWrite(receiveBuffer[0], receiveBuffer[1]);
    }
  }
  // Checks if the received frame contains byte data type
  // 0x01 = Byte data type
  else if (dataType == DATA_TYPE_BYTE)
  {   
    // If the received byte value is 255, sets playLEDNotes
    // and marks the current time.
    if (bvsp.getReceivedByte() == 255)
    {
      playLEDNotes = true;
      playStartTime = millis();
    }
  }
}

// Handles the modeChanged event 
void BVSP_modeChanged() 
{ 
  // If the outboundMode (Server --> Device) has turned to 
  // FRAMED_MODE, no audio stream is supposed to be received. 
  // Tells the BVSSpeaker class to finish playing when its 
  // internal buffer become empty. 
  if (bvsp.outboundMode == FRAMED_MODE)
    bvss.finishPlaying();
} 

// Handles the streamReceived event 
void BVSP_streamReceived(int size) 
{ 
  // Gets the received stream from the BVSP class 
  int bytesRead = bvsp.getReceivedStream(speakerBuffer, 
    SPEAKER_BUFFER_SIZE); 
    
  // Enqueues the received stream to play
  bvss.enqueue(speakerBuffer, bytesRead);
}

// Lights up the appropriate LED based on the time 
// the command to start playing LED notes was received.
// The timings used here are syncronized with the music.
void playNextLEDNote()
{
  // Gets the elapsed time between playStartTime and the 
  // current time.
  unsigned long elapsed = millis() - playStartTime;

  // Turns off all LEDs
  allLEDsOff();

  // The last note has been played.
  // Turns off the last LED and stops playing LED notes.
  if (elapsed >= 11500)
  {
    analogWrite(RED_LED_PIN, 0);
    playLEDNotes = false;
  }
  else if (elapsed >= 9900)
    analogWrite(RED_LED_PIN, 255); // C note
  else if (elapsed >= 9370)
    analogWrite(RED_LED_PIN, 255); // C note
  else if (elapsed >= 8900)
    analogWrite(YELLOW_LED_PIN, 255); // D note
  else if (elapsed >= 8610)
    analogWrite(RED_LED_PIN, 255); // C note
  else if (elapsed >= 8230)
    analogWrite(YELLOW_LED_PIN, 255); // D note
  else if (elapsed >= 7970)
    analogWrite(YELLOW_LED_PIN, 255); // D note
  else if (elapsed >= 7470)
    analogWrite(RED_LED_PIN, 255); // C note
  else if (elapsed >= 6760)
    analogWrite(GREEN_LED_PIN, 255); // E note
  else if (elapsed >= 6350)
    analogWrite(RED_LED_PIN, 255); // C note
  else if (elapsed >= 5880)
    analogWrite(YELLOW_LED_PIN, 255); // D note
  else if (elapsed >= 5560)
    analogWrite(RED_LED_PIN, 255); // C note
  else if (elapsed >= 5180)
    analogWrite(YELLOW_LED_PIN, 255); // D note
  else if (elapsed >= 4890)
    analogWrite(YELLOW_LED_PIN, 255); // D note
  else if (elapsed >= 4420)
    analogWrite(RED_LED_PIN, 255); // C note
  else if (elapsed >= 3810)
    analogWrite(GREEN_LED_PIN, 255); // E note
  else if (elapsed >= 3420)
    analogWrite(RED_LED_PIN, 255); // C note
  else if (elapsed >= 2930)
    analogWrite(YELLOW_LED_PIN, 255); // D note
  else if (elapsed >= 2560)
    analogWrite(RED_LED_PIN, 255); // C note
  else if (elapsed >= 2200)
    analogWrite(YELLOW_LED_PIN, 255); // D note
  else if (elapsed >= 1930)
    analogWrite(YELLOW_LED_PIN, 255); // D note
  else if (elapsed >= 1470)
    analogWrite(RED_LED_PIN, 255); // C note
  else if (elapsed >= 1000)
    analogWrite(GREEN_LED_PIN, 255); // E note
}

// Turns off all LEDs.
void allLEDsOff()
{
  analogWrite(RED_LED_PIN, 0);
  analogWrite(YELLOW_LED_PIN, 0);
  analogWrite(GREEN_LED_PIN, 0);
}