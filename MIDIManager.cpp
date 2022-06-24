#include <MIDI.h>
#include "USBHost_t36.h"

#include "Utilities.h"
#include "Sharp.h"
#include "LCD.h"
#include "MIDIManager.h"
#include "noteset.h"
#include "notetracker.h"
#include "arpeggiator.h"

#define ECHO_TO_SERIAL 0

extern notetracker tracker;
extern arpeggiator arpeggiators[];

extern bool transposing;
extern devicestate state;

void handleNoteOn(byte channel, byte pitch, byte velocity);
void handleNoteOff(byte channel, byte pitch, byte velocity);
void handlePitchBend(byte channel, int  bend);
void handleControlChange(byte channel, byte number, byte value);

#define MAXNOTEEVENT 200

bool waitingForCycle = false;

typedef struct
{
  int clock;
  byte note;
  byte velocity;
} note_event;

typedef struct
{
  devicestate state;
  note_event events[MAXNOTEEVENT];
} sequence;

#define MIN_NOTE 1
#define MAX_NOTE 127

bool replayNotes[MAX_NOTE + 1] = {false};

int eventIndex = 0;
int eventCount = 14;
int clockOffset = 0;
bool recording = false;
bool replayOn = false;
int replayoffset = 0;
bool replayEvent = false;

sequence seq;

void addEvent(long clock, byte pitch, byte velocity);
void checkReplay(long curClock);
void transposeReplay(int offset);
void setDemoSequence();

extern bool settingsplit;

#define CV_NOTE1 A22
#define CV_NOTE2 A21
#define CV_GATE1 27
#define CV_GATE2 26
//#define CV_CLK 32
#define CV_CLK 10
#define GATE_HIGH 568  // approximately 5V

MIDI_CREATE_DEFAULT_INSTANCE();
//MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

static USBHost usbHost;
static USBHIDParser hid1(usbHost);
static USBHIDParser hid2(usbHost);
static USBHub hub1(usbHost);
static MIDIDevice usbmidi(usbHost);

void handleControlChange(byte channel, byte number, byte value)
{
//  Serial.print("Control change: ");
//  Serial.print(number);
//  Serial.print(" = ");
//  Serial.println(value); 
  sendControlChange(channel, number, value);
}

void handlePitchBend(byte channel, int bend)
{
  sendPitchBend(channel, bend);
}

void setupMIDI()
{
  pinMode(CV_GATE1, OUTPUT);
  pinMode(CV_GATE2, OUTPUT);
  pinMode(CV_CLK, OUTPUT);
  digitalWrite(CV_GATE1, 0);
  digitalWrite(CV_GATE2, 0);
  digitalWrite(CV_CLK, 0);
  
  MIDI.begin(1);
  MIDI.turnThruOff();

  // all notes off
  stopallnotes(true);

  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandlePitchBend(handlePitchBend);
  MIDI.setHandleControlChange(handleControlChange);

  usbMIDI.setHandleNoteOn(handleNoteOn);
  usbMIDI.setHandleNoteOff(handleNoteOff);
  usbMIDI.setHandlePitchChange(handlePitchBend);
  usbMIDI.setHandleControlChange(handleControlChange);
  
  usbHost.begin();
  usbmidi.setHandleNoteOn(handleNoteOn);
  usbmidi.setHandleNoteOff(handleNoteOff);
  usbmidi.setHandlePitchChange(handlePitchBend);
  usbmidi.setHandleControlChange(handleControlChange);
//  usbmidi.setHandleClock(handleClock);
//  usbmidi.setHandleStart(handleStart);
//  usbmidi.setHandleContinue(handleContinue);
//  usbmidi.setHandleStop(handleStop);

  Serial.println("MIDI setup complete");
  
  setDemoSequence();
}

int noteToCV(byte note)
{
  // 9V range => 9 octaves, so volts per semitone is 0.083333
  // and 1023 = 9V, so scaling factor is 1023 / 12 / 9 = 9.47222
  // MIDI numbers A0 is 21, C8 is 108 (so 87 notes)
  // But max output is actually only 8.2, so correct this to 1023 / 12 / 8.2 = 10.396341
  // p->value / 1023.0 * 8.2

  return round((min(108, max(0, note)) - 21) * 10.396341);
}

void sendNoteOn(byte channel, byte note, byte velocity)
{
//  char *nstr = getnotestring(note, true);
//  DBG2n("Note on: ", nstr)DBG(channel)
  if (replayRunning())
  {
    note += replayoffset;
    replayNotes[note] = true;
  }
  
  if (curmode == ARPBOTH && !state.common.active)
  {
    MIDI.sendNoteOn(note, velocity, 1);
    usbMIDI.sendNoteOn(note, velocity, 1);
    usbmidi.sendNoteOn(note, velocity, 1);
    MIDI.sendNoteOn(note, velocity, 2);
    usbMIDI.sendNoteOn(note, velocity, 2);
    usbmidi.sendNoteOn(note, velocity, 2);
  }
  else
  {
    MIDI.sendNoteOn(note, velocity, channel);
    usbMIDI.sendNoteOn(note, velocity, channel);
    usbmidi.sendNoteOn(note, velocity, channel);
  }
  
  int cv = noteToCV(note);
  if (curmode == ARPBOTH)
  {
    if (state.common.active)
    {
      analogWrite(channel == 1 ? CV_NOTE1 : CV_NOTE2, cv);
      digitalWrite(channel == 1 ? CV_GATE1 : CV_GATE2, 1);
    }
    else
    {
      analogWrite(CV_NOTE1, cv);
      digitalWrite(CV_GATE1, 1);
      analogWrite(CV_NOTE2, cv);
      digitalWrite(CV_NOTE2, 1); 
    }
  }
  else
  {
    if (curcommon->split == 2 && channel == 2)
    {
      analogWrite(CV_NOTE2, cv);
      digitalWrite(CV_GATE2, 1);
    }
    else
    {
      analogWrite(CV_NOTE1, cv);
      digitalWrite(CV_GATE1, 1);
    }
  }
}

void sendNoteOff(byte channel, byte note, byte velocity)
{
//  char *nstr = getnotestring(note, true);
//  DBG2n("Note off: ", nstr)DBG(channel)
  if (replayRunning())
  {
    note += replayoffset;
    replayNotes[note] = false;
  }
  
  if (curmode == ARPBOTH && !state.common.active)
  {
    MIDI.sendNoteOff(note, velocity, 1);
    usbMIDI.sendNoteOff(note, velocity, 1);
    usbmidi.sendNoteOff(note, velocity, 1);
    MIDI.sendNoteOff(note, velocity, 2);
    usbMIDI.sendNoteOff(note, velocity, 2);
    usbmidi.sendNoteOff(note, velocity, 2);
  }
  else
  {
    MIDI.sendNoteOff(note, velocity, channel);
    usbMIDI.sendNoteOff(note, velocity, channel);
    usbmidi.sendNoteOff(note, velocity, channel);
  }
  
  if (curmode == ARPBOTH)
  {
    if (state.common.active)
    {
      digitalWrite(channel == 1 ? CV_GATE1 : CV_GATE2, 0);
    }
    else
    {
      digitalWrite(CV_GATE1, 0);
      digitalWrite(CV_GATE2, 0); 
    }
  }
  else
  {
    if (curcommon->split == 2 && channel == 2)
    {
      digitalWrite(CV_GATE2, 0);
    }
    else
    {
      digitalWrite(CV_GATE1, 0);
    }
  }
}

void sendPitchBend(byte channel, int bend)
{
  Serial.print("Pitch bend: ");
  Serial.println(bend);
  MIDI.sendPitchBend(bend, channel);
  usbMIDI.sendPitchBend(bend, channel);
  usbmidi.sendPitchBend(bend, channel);
}

void sendControlChange(byte channel, byte number, byte value)
{
#if ECHO_TO_SERIAL  
  Serial.print("Control change: ");
  Serial.print(number);
  Serial.print(" -> ");
  Serial.println(value);
#endif  
  MIDI.sendControlChange(number, value, channel);
  usbMIDI.sendControlChange(number, value, channel);
  usbmidi.sendControlChange(number, value, channel);
}

void sendClock(bool state)
{
  // FIXME add MIDI clock support (will need 24 clocks per beat)
  digitalWrite(CV_CLK, state ? 1 : 0); 
}

void handlestoppednotes()
{
  noteset *stops = tracker.notestostop;
  for (int i = 0; i < stops->count; ++i)
  {
//      DBG2("Stopping", stops->notes[i].pitch);
    if (stops->notes[i].pitch)
    {
//        char *nstr = getnotestring(stops->notes[i].pitch, true);
//        DBG2n("Note off(*): ", nstr)DBG(stops->notes[i].channel)
      sendNoteOff(stops->notes[i].channel, stops->notes[i].pitch, 0);
    }
  } 
}

bool isTransposeNote(byte pitch, byte velocity)
{
  return !replayEvent && replayRunning() && transposing && velocity > 0; 
}

bool isBypassNote(byte pitch, byte velocity, commonstate *common)
{
  if (!replayEvent && replayRunning() && !transposing && velocity > 0)
    return true;
  else if (common->split)
    return (curmode == ARPL && pitch >= common->splitnote) || 
      (curmode == ARPH && pitch <= common->splitnote);
  else
    return false;
}

void handleNoteOn(byte channel, byte pitch, byte velocity) 
{
  if (!pitch)
    return;

  if (isTransposeNote(pitch, velocity))
  {
    transposeReplay((int)pitch - 48);
    return;
  }
  
  if (settingsplit)
  {
    settingsplit = false;
    state.common.splitnote = pitch;
    tracker.adjustfornewsplit(pitch);
//    stopallnotes();
    handlestoppednotes();
    handleupdate();
    displayNote(state.common.splitnote, state.common.split == 2);
    return;
  }

  if (!isBypassNote(pitch, velocity, curcommon) && (tracker.getheldcount(false) == 0))
  {
    arpeggiators[0].resetnote();
    arpeggiators[1].resetnote();
  }
  tracker.tracknoteon(pitch, &state);
  lastvelocity = velocity;
  
  if (recording)
    addEvent(getClock(), pitch, velocity);

  handlestoppednotes();
  
  noteset *starts = tracker.newnotestoplay;
  if (state.common.active)
  {
    handleupdate();
//    Serial.println(getnotesetstring("Starts", starts));
    for (int i = 0; i < starts->count; ++i)
    {
      sendNoteOn(starts->notes[i].channel, starts->notes[i].pitch, velocity); 
    }
  }
  else if (starts->count)
  {
    sendNoteOn(starts->notes[0].channel, starts->notes[0].pitch, velocity); 
  }
}

void handleNoteOff(byte channel, byte pitch, byte velocity) 
{
  if (!pitch)
    return;

  if (!replayEvent && replayRunning() && setting == REPLAY && transposing)
  {
    return; // it was a transpose
  }

  if (recording)
    addEvent(getClock(), pitch, 0);
  bool bypass = tracker.isbypass(pitch, channel);
  if (tracker.tracknoteoff(pitch, &state))
  {
    handlestoppednotes();
    if (!bypass)
    {
      if (state.common.active)
        handleupdate();
      else if (tracker.getheldcount(false) == 0)
      {
        digitalWrite(CV_GATE1, 0);
        digitalWrite(CV_GATE2, 0);
      }
    }
  }
}

void stoponechannel(byte channel, bool allsound)
{
  MIDI.sendControlChange(123, 0, channel);
  usbMIDI.sendControlChange(123, 0, channel);
  usbmidi.sendControlChange(123, 0, channel);
  if (allsound)
  {
    MIDI.sendControlChange(120, 0, channel);
    usbMIDI.sendControlChange(120, 0, channel);
    usbmidi.sendControlChange(120, 0, channel);
  }
}

void stopallnotes(bool allsound)
{
  stopcv();
  
  noteset *stops = tracker.heldnotes;
  for (int i = 0; i < stops->count; ++i)
  {
    if (stops->notes[i].pitch)
    {
      MIDI.sendNoteOff(stops->notes[i].pitch, 0x00, stops->notes[i].channel);
      usbMIDI.sendNoteOff(stops->notes[i].pitch, 0x00, stops->notes[i].channel);
      usbmidi.sendNoteOff(stops->notes[i].pitch, 0x00, stops->notes[i].channel);
    }
  }        
  tracker.resettracker();
  stoponechannel(1, allsound);
  stoponechannel(2, allsound);
}

void stopcv()
{
  digitalWrite(CV_GATE1, 0);
  digitalWrite(CV_GATE2, 0); 
}

void checkMIDI(long curClock)
{
  MIDI.read();
  usbMIDI.read();
  usbHost.Task();
  usbmidi.read();
  checkReplay(curClock);
}

void setDemoSequence()
{
  seq = {
    {
      ARPBOTH,
      { false, false, 60, true },
      {
        { false, 4, 4, SHUFFLE, false, WALK, 1, CLOCKTRIGGER, 0 },
        { false, 4, 4, OUT, false, DOUBLE3, 2, CLOCKTRIGGER, 0 }
      }
    },
    {
      {0, 50, 127},
      {0, 53, 127},
      {0, 57, 127},         // D F A
      {4 * 96 * 12, 59, 127},    // D F A B
      {8 * 96 * 12, 53, 0},      // D A B
      {12 * 96 * 12, 48, 127},   // C D A B
      {16 * 96 * 12, 50, 0},     
      {16 * 96 * 12, 51, 127},   // C Eb A B
      {20 * 96 * 12, 59, 0},
      {20 * 96 * 12, 57, 0},
      {20 * 96 * 12, 55, 127},   // C Eb G
      {24 * 96 * 12, 48, 0},
      {24 * 96 * 12, 51, 0},
      {24 * 96 * 12, 49, 127},
      {24 * 96 * 12, 52, 127},   
      {24 * 96 * 12, 57, 127},   // C# E G A
      {28 * 96 * 12, 57, 127},   
      {28 * 96 * 12, 0}
    }
  };
}

bool isRecording()
{
  return recording;
}

void clearReplayNotes()
{
  for (int i = MIN_NOTE; i <= MAX_NOTE; ++i)
    replayNotes[i] = false;
}

int countReplayNotes()
{
  int c = 0;
  for (int i = MIN_NOTE; i <= MAX_NOTE; ++i)
    if (replayNotes[i])
      c++;
  return c;
}

bool hasReplayNote()
{
  int c = 0;
  for (int i = MIN_NOTE; i <= MAX_NOTE; ++i)
    if (replayNotes[i])
      c++;
  return c;
}

void startRecordingSequence()
{
  recording = true;
  seq.state = state;
  for (int i = 0; i < MAXNOTEEVENT; ++i)
    seq.events[i] = {0, 0, 0};
  clearReplayNotes();
  eventCount = 0;
}

void addEvent(long clock, byte pitch, byte velocity)
{
  seq.events[eventCount] = {12 * clock, pitch, velocity};
  replayNotes[pitch] = velocity > 0;
  eventCount++;
  if (eventCount == MAXNOTEEVENT)
    endRecordingSequence(12 * clock);
}

void endRecordingSequence(long curClock)
{
  recording = false;  
  if (!eventCount)
    setDemoSequence();
  else
  {
    long startclock = seq.events[0].clock;
    for (int i = 0; i < MAXNOTEEVENT; ++i)
    {
      if (!seq.events[i].clock)
        break;
      seq.events[i].clock -= startclock;
    }
    if (eventCount < MAXNOTEEVENT)
    {
      long finalclock;
//      if (hasReplayNote())
//      {
        // FIXME - timebase handling...
//        long cycles = ceil(curClock / 24 / state.timebase);
//        finalclock = cycles * 24 * state.timebase - startclock;
        long cycles = ceil(curClock / 24);
        finalclock = cycles * 24 - startclock;
//      }
//      else
//      {
//        finalclock = curClock - startclock;
//      }
      seq.events[eventCount] = {finalclock, 0, 0};
      eventCount++;
    }
    Serial.println("Recording");
    for (int i = 0; i < MAXNOTEEVENT; ++i)
    {
      if (!seq.events[i].clock && !seq.events[i].note)
        break;
      Serial.print(i);Serial.print("\t");
      Serial.print(seq.events[i].clock);Serial.print(": ");
      Serial.print(seq.events[i].note);Serial.println(seq.events[i].velocity ? " on" : " off");
    }
  }
}

int getEventCount()
{
  return eventCount;
}

bool replayRunning()
{
  return replayOn;  
}

void startReplay(long curClock)
{
  clockOffset = curClock;
  waitingForCycle = false;
  bool saveactive = state.common.active;
  state = seq.state;
  curmode = state.mode;
  curcommon = &state.common;
  state.common.active = saveactive;
  replayOn = true;
  eventIndex = 0;
  replayoffset = 0;
  clearReplayNotes();
  showState();
  showTempo(tempo, state.arp[curArp].tempo_factor, state.arp[curArp].swingoffset != 0);
  handleupdate();
}

void transposeReplay(int newoffset)
{
  if (replayoffset == newoffset)
    return;
    
  bool reverse = newoffset > replayoffset;
  int delta = newoffset - replayoffset;
  int start = reverse ? MAX_NOTE : MIN_NOTE;  
  int end = reverse ? MIN_NOTE : MAX_NOTE;
  int step = reverse ? -1 : 1;
  for (int i = start; i != end; i += step)
  {
    if (replayNotes[i])
    {
      replayNotes[i] = false;
      sendNoteOff(1, i, 0);
      replayNotes[i + delta] = true;
      sendNoteOn(1, i + delta, lastvelocity);
    }
  }
  
  replayoffset = newoffset;
}

void endReplay()
{
  replayOn = false;
  stopallnotes(false);
  clearReplayNotes();
}

void handleEvent(note_event e)
{
  byte n = e.note;
  if (n < MIN_NOTE || n > MAX_NOTE)
    return;
  replayEvent = true;
  if (e.velocity)
  {
    handleNoteOn(1, n, e.velocity);
  }
  else
  {
    handleNoteOff(1, n, 0);
  }
  replayEvent = false;
}

void notifyCycle(long curClock)
{
  if (waitingForCycle)
  { 
    // time the reset to match the start of a cycle
    eventIndex = 0;
    waitingForCycle = false;
    clockOffset = curClock;
    stopallnotes(false);
    checkReplay(curClock);
  }
}

void checkReplay(long curClock)
{
  if (!replayOn)// || waitingForCycle)
    return;
    
  note_event nextEvent = seq.events[eventIndex];
  while (nextEvent.clock + clockOffset <= curClock)
  {
#if DEBUG_SERIAL    
    Serial.print("Event: ");
    Serial.print(nextEvent.clock);
    Serial.print(": ");
    Serial.print(nextEvent.note);
    Serial.println(nextEvent.velocity ? " on " : " off");
#endif    
    int c = countReplayNotes();
#if DEBUG_SERIAL    
    Serial.print("Note count: ");
    Serial.println(c);
#endif    
    if (!nextEvent.note)
    {
      // are we at the end and no notes held?
      bool hasNote = hasReplayNote();
#if DEBUG_SERIAL    
      Serial.print("Waiting for cycle with hasNote = ");
      Serial.println(hasNote);
#endif    
      waitingForCycle = true;
//      if (state.mode == RANDOM || !hasNote)
      {
#if DEBUG_SERIAL    
        Serial.println("Immediate notify");
#endif    
        notifyCycle(curClock);
      }
      return;
    }
    handleEvent(nextEvent);
    eventIndex++;
    nextEvent = seq.events[eventIndex];
  }
}

int getTransposeOffset()
{
  return replayoffset;
}
