/*
 * TODO
 *    - add MIDI clock in/out support
 *    - A cv note is getting stuck when switching into split == 2 (hacked around for the moment)
 *    - Implement Sysex for switching states (make it sysex so standard MIDI can just pass through to the synth it is controlling - make sure messages are passed through)
 *      * Arp1 mode
 *      * Arp2 mode
 *      * Arp1 alt (incl chord)
 *      * Arp2 alt (incl chord)
 *      * Arp1 range
 *      * Arp2 range
 *      * Arp1 factor
 *      * Arp2 factor
 *      * Arp1 timebase
 *      * Arp2 timebase
 *      * Arp1 sort
 *      * Arp2 sort
 *      * On/Off
 *      * Split mode
 *      * Split note
 *      * Latch
 *      * Tempo
 *      * Device mode
 *      * Panic
 */

#include "Bounce2.h"
#include "Bounce2mcp.h"
#include <IntervalTimer.h>
#include "Adafruit_MCP23017.h"
#include <Encoder.h>

#include "Sharp.h"
#include "noteset.h"
#include "notetracker.h"
#include "arpeggiator.h"
#include "MIDIManager.h"
#include "LCD.h"
#include "Utilities.h"

#define max(a,b) (((a) > (b)) ? (a) : (b))

#define TEXTFLASHMILLIS 500

#define PANIC_BUTTON 20
Bounce panicDebouncer = Bounce();

#define SWAP_BUTTON 16
Bounce swapDebouncer = Bounce();

#define RETRIGGER_BUTTON 12
Bounce retriggerDebouncer = Bounce();

notetracker tracker;
arpeggiator arpeggiators[] = { arpeggiator(&tracker, 0), arpeggiator(&tracker, 1) };

settingsmode setting = DEV_MODE;

bool transposing = false;

typedef struct
{
  int pin;
  int led1pin;
  int led2pin;
  int state;
  long heldsince;
  BounceMcp *debouncer;
} button;

#define NUM_BUTTONS 7

button buttons[] = 
{
  {0,  9,  8, 0, -1, NULL},    // split
  {1, 10, -1, 0, -1, NULL},    // latch
  {2, 11, -1, 0, -1, NULL},    // sort
  {3, 12, -1, 0, -1, NULL},    // range1
  {4, 13, -1, 0, -1, NULL},    // range2
  {5, 14, -1, 0, -1, NULL},    // range3
  {6, 15, -1, 0, -1, NULL},    // range4
};

#define SPLIT_BTN  0
#define LATCH_BTN  1
#define SORT_BTN   2
#define RANGE1_BTN 3
#define RANGE2_BTN 4
#define RANGE3_BTN 5
#define RANGE4_BTN 6

#define LONG_PRESS_MILLIS 300

typedef struct 
{
  int pin;
  int value;
} pot;

pot tempoPot = {A1, 0};  // Tempo

Adafruit_MCP23017 *expander = new Adafruit_MCP23017();

typedef struct
{
  int pinA;
  int pinB;
  int pinSW;
  int minval;
  int maxval;
  Bounce *debouncer;
  Encoder *enc;
  int pos;
} encoder;

#define NUM_ENCODERS 3

encoder encoders[] = {
  { 5,  4, 2, 0, 4, NULL, NULL, 0 }, // Mode
  {25, 24, 3, 0, 4, NULL, NULL, 0 }, // Alt
  { 8,  7, 6, 0, 4, NULL, NULL, 0 }, // Settings
};

#define MODE_ENC 0
#define ALT_ENC 1
#define SETTINGS_ENC 2

commonstate common = { false, false, 60, false };
arpstate state1 = { false, 4, 4, UP, false, NORMAL, 1, CLOCKTRIGGER, 0 };
arpstate state2 = { false, 4, 4, UP, false, NORMAL, 1, CLOCKTRIGGER, 0 };
devicemode curmode = ARPL;
int curArp = 0;
devicestate state = { ARPL, common, { state1, state2 }};
commonstate *curcommon = &state.common;
bool statechange = false;

int tempo = 20;
unsigned char lastvelocity = 0;

bool settingsplit = false;
bool settingmul = false;
bool settingdiv = false;
bool settingdevmode = false;

bool updateFactor[] = {false, false};
float updateValue[] = {1, 1};

#define TEMPO_LED 17
bool tempoLEDstate = false;

int loopcount = 0;
long flashmarker = -1;

static volatile int clock = 0;
static volatile int clocks[] = {0, 0};
static volatile long fullclock = 0;
IntervalTimer timer;

void updateForBypass(noteinfo *note)
{
  if (state.mode == ARPL)  // bypass notes above the split point
    note->bypass = state.common.split && note->pitch >= state.common.splitnote;
  else if (state.mode == ARPH)  // bypass notes below the split point
    note->bypass = state.common.split && note->pitch <= state.common.splitnote;
  else
    note->bypass = false;
  
  note->channel = (unsigned char)(note->bypass ? max(1, state.common.split) : 1); // latch is set if necessary on note off
}

noteinfo makenote(unsigned char pitch)
{
  noteinfo note = { pitch, false, false, false };
  updateForBypass(&note);
  return note;
}

arpstate *getArpState(int index) 
{ 
  return &(state.arp[index]); 
}

long getClock()
{
  return (int)(fullclock / CLOCKTRIGGER);
}

void swapArps()
{
  arpstate temp = state.arp[0]; 
  state.arp[0] = state.arp[1];
  state.arp[1] = temp;
  arpeggiators[0].updatenotestoplay(&state);
  arpeggiators[1].updatenotestoplay(&state);
  showState();
}

void setupEncoders()
{
  for (int i = 0; i < NUM_ENCODERS; ++i)
  {
    encoder *e = &encoders[i];
    pinMode(e->pinA, INPUT_PULLUP);
    pinMode(e->pinB, INPUT_PULLUP);
    pinMode(e->pinSW, INPUT_PULLUP);
    e->debouncer = new Bounce();
    e->debouncer->attach(e->pinSW);
    e->debouncer->interval(50);
    e->enc = new Encoder(e->pinA, e->pinB);
  }
}

static bool swingupdate = false;
void checkEncoders()
{
  bool cleanup = false;
  bool update = false;
  for (int i = 0; i < NUM_ENCODERS; ++i)
  {
    encoder *e = &encoders[i];
    e->debouncer->update();

    if (!swingupdate && i == ALT_ENC && !e->debouncer->read() && e->debouncer->duration() > LONG_PRESS_MILLIS)
    {
      for (int j = 0; j < 2; ++j)
      {
        if (state.arp[j].swingoffset)
          state.arp[j].swingoffset = 0;
        else
          state.arp[j].swingoffset = SWING_OFFSET;
      }
      swingupdate = true;
      showTempo(tempo, state.arp[curArp].tempo_factor, state.arp[curArp].swingoffset != 0);
      showState();
    }  
    else if (e->debouncer->rose())
    {
      switch (i)
      {
        case MODE_ENC:
          state.common.active = !state.common.active;
          cleanup = true;
          if (state.mode == ARPBOTH)
            stoponechannel(2, true);
          state.arp[0].swingoffset = abs(state.arp[0].swingoffset);
          state.arp[1].swingoffset = abs(state.arp[1].swingoffset);
          break;
        case ALT_ENC:
          if (!swingupdate)
            state.arp[curArp].chords = !state.arp[curArp].chords;
          break;
        case SETTINGS_ENC:
          switch (setting)
          {
            case TRANSPOSE:
              transposing = !transposing;
              break;
            case REPLAY:
              if (replayRunning())
              {
                endReplay();
                flashmarker = -1;
              }
              else
              {
                startReplay(fullclock);
                flashmarker = millis();
              }
              break;
            case RECORD:
              if (isRecording())
              {
                endRecordingSequence(fullclock);
                flashmarker = -1;
              }
              else
              {
                startRecordingSequence();
                flashmarker = millis();
              }
              break;
            case SPLIT:
              if (/*state.split && */!settingsplit)
              {
                // next note played will be the splitting note...
                settingsplit = true;
                showControl("Split: <play note>", false);
              }
              break;
            case CLK_MUL:
              settingmul = !settingmul;
              break;
            case CLK_DIV:
              settingdiv = !settingdiv;
              break;
            case DEV_MODE:
              settingdevmode = !settingdevmode;
              break;
          }
          showModeState();
          break;
      }
      swingupdate = false;
      statechange = true;
      update = true;
    }
    else
    {
      int pos = e->enc->read();
      if (pos != e->pos && pos % 4 == 0) 
      {  
        int d = pos > e->pos ? -1 : 1;
        e->pos = pos;
        if (settingdevmode || settingmul || settingdiv)
        {
          settingdevmode = settingmul = settingdiv = false;
        }
        switch (i)
        {
          case MODE_ENC:
            state.arp[curArp].mode = arpmode(((int)state.arp[curArp].mode + NUMMODE + d) % NUMMODE);
            update = true;
            break;
          case ALT_ENC:
            state.arp[curArp].alt = altmode(((int)state.arp[curArp].alt + NUMALT + d) % NUMALT);
            state.arp[curArp].chords = false;
            update = true;
            break;
          case SETTINGS_ENC:
            setting = settingsmode(((int)setting + NUMSET + d) % NUMSET);
            if ((isRecording() && setting == REPLAY) || (replayRunning() && setting == RECORD))
            {
              setting = settingsmode(((int)setting + NUMSET + d) % NUMSET);
            }
            showModeState();
            break;
        }
        statechange = d != 0;
      }
    }
  }

  if (cleanup)
  {
    noteset *held = tracker.heldnotes;
    for (int i = 0; i < held->count; ++i)
    {
      if (held->notes[i].pitch)
      {
        if (state.common.active) // make sure the held notes are not sounding
          sendNoteOff(held->notes[i].channel, held->notes[i].pitch, 0x00);
        else if (held->notes[i].latched)  // drop the latch too
          sendNoteOff(held->notes[i].channel, held->notes[i].pitch, 0x00);
        else // make sure the held notes are sounding 
          sendNoteOn(held->notes[i].channel, held->notes[i].pitch, 0x7f); // FIXME get velocity
      }
    }
    if (!state.common.active)
      tracker.clearlatchednotes();
  }

  if (update)
  {
    handleupdate();
  }
}

void setupExpander()
{
  expander->begin((uint8_t)0);
  for (int j = 0; j < 16; ++j)
  {
    if (j < 8)
    {
      expander->pinMode(j, INPUT);
      expander->pullUp(j, HIGH);  // turn on a 100K pullup internally
    }
    else
    {
      expander->pinMode(j, OUTPUT);
    }
  }
  Serial.println("Expander setup complete");
}

void setupButtons()
{
  uint16_t blockMillis = 50;
  for (int i = 0; i < NUM_BUTTONS; ++i)
  {
    int pin = buttons[i].pin;
    BounceMcp *debouncer = new BounceMcp();
    buttons[i].debouncer = debouncer;
    debouncer->attach(*expander, pin, blockMillis);
    buttons[i].state = 0;
    expander->digitalWrite(buttons[i].led1pin, LOW);
    if (buttons[i].led2pin >= 0)
      expander->digitalWrite(buttons[i].led2pin, LOW);
  }
  
  pinMode(PANIC_BUTTON, INPUT_PULLUP);
  panicDebouncer.attach(PANIC_BUTTON);
  panicDebouncer.interval(50);
  
  pinMode(SWAP_BUTTON, INPUT_PULLUP);
  swapDebouncer.attach(SWAP_BUTTON);
  swapDebouncer.interval(50);
  
  pinMode(RETRIGGER_BUTTON, INPUT_PULLUP);
  retriggerDebouncer.attach(RETRIGGER_BUTTON);
  retriggerDebouncer.interval(50);
}

void handleSettingButton(int i)
{
//  char tempStr[30];
  if (settingmul || settingdiv)
  {
    i++;
    // update is delayed in order to keep the clocks synchronised
    if (settingdiv && i != 1)
    {
      updateFactor[curArp] = true;
      updateValue[curArp] = 1.0 / i;
//      sprintf(tempStr, "Clock factor: 1/%d", i);
    }
    else
    {
      updateFactor[curArp] = true;
      updateValue[curArp] = i;
//      sprintf(tempStr, "Clock factor: %d", i);
    }
  }
  else if (settingdevmode)
  {
    if (state.mode == ARPBOTH)
      stoponechannel(2, false);
    curmode = (devicemode)i;
    state.mode = curmode;
    showControl("Device mode", false);
  }
//  textatrow(4, tempStr, LCD_WHITE, LCD_BLACK);
  settingmul = settingdiv = settingdevmode = false;
  showState();
}

void handleButton(int i, button *b)
{
  if (settingsplit)
  {
    settingsplit = false;
    buttons[SPLIT_BTN].heldsince = -1;
    statechange = true;
  }
  if (i >= RANGE1_BTN && i <= RANGE4_BTN)
  {
    if (i >= 3 && i <= 6 && (settingdevmode || settingmul || settingdiv))
    {
      handleSettingButton(i - 3);
    }
    else
    {
      state.arp[curArp].range = (i - RANGE1_BTN) + 1;
      for (int j = RANGE1_BTN; j <= RANGE4_BTN; ++j)
        buttons[j].state = j == i ? 1 : 0;
    }
  }
  else
  {
    if (i == SPLIT_BTN)
    {
      b->state++;
      if (b->state > 2)
        b->state = 0;
      state.common.split = b->state;
      statechange = true;
      if (setting != SPLIT && !replayRunning() && !isRecording())
        setting = SPLIT;
      if (state.common.split == 2)
        stopcv(); // FIXME simple work around for a bug due to difficulty with a high arpeggio note when going into split 2
      showModeState();
    }
    else if (i == LATCH_BTN)
    {
      state.common.latched = !state.common.latched;
      b->state = state.common.latched;
      if (!state.common.latched)
      {
        tracker.clearlatchednotes();
        if (tracker.getheldcount(false) == 0)
        {
          stopallnotes(false);
        }
        else
        {
          handlestoppednotes();
        }
      }
    }
    else if (i == SORT_BTN)
    {
      state.arp[curArp].playinheldorder = !state.arp[curArp].playinheldorder;
      b->state = state.arp[curArp].playinheldorder;
    }
  }
  
  handleupdate();
}

#define PANIC_LONG_MILLIS 500
bool panicLong = false;

void checkButtons(unsigned long now)
{
  swapDebouncer.update();
  if (swapDebouncer.rose())
  {
    swapArps();
  }
  
  retriggerDebouncer.update();
  if (retriggerDebouncer.rose())
  {
    arpeggiators[0].resetchord();
    arpeggiators[0].resetnote();
    arpeggiators[1].resetchord();
    arpeggiators[1].resetnote();
  }
  
  panicDebouncer.update();
  if (!panicLong && !panicDebouncer.read() && panicDebouncer.duration() > PANIC_LONG_MILLIS)
  {
    Serial.println("$$$ Panic Long Press $$$");
    panicLong = true;
    stopallnotes(true);
  }
  else if (panicDebouncer.rose())
  {
    if (panicLong)
      panicLong = false;
    else
    {
      Serial.println("$$$ Panic Short Press $$$");    
      // cycle edit arpeggio
      curArp = !curArp;
      char tempStr[30] = {0};
      sprintf(tempStr, "Current arp: %d", curArp + 1);
      showTempo(tempo, state.arp[curArp].tempo_factor, state.arp[curArp].swingoffset != 0);
      textatrow(4, tempStr, LCD_WHITE, LCD_BLACK);
      settingmul = settingdiv = settingdevmode = false;
      showState();
    }
  }
  
  for (int j = 0; j < NUM_BUTTONS; ++j)
  {
    button *b = &buttons[j];
    b->debouncer->update();
    long d = b->debouncer->duration();
    if (b->debouncer->rose())
    {
      if (b->heldsince < 0) // check if it's a long press
      {
        handleButton(j, b);
      }
      if (!settingsplit || j != SPLIT_BTN)
        b->heldsince = -1;
    }
    else if (j == SPLIT_BTN && settingsplit && b->debouncer->fell())
    {
      settingsplit = false;
      statechange = true;
    }
    else if (j >= RANGE1_BTN && j <= RANGE4_BTN)
    {
      if (b->heldsince < 0 && !b->debouncer->read() && d > LONG_PRESS_MILLIS)
      {
        b->heldsince = now;
        state.arp[curArp].timebase = (j - RANGE1_BTN) + 1;
        arpeggiators[curArp].settimebase(state.arp[curArp].timebase);
        statechange = true;
      }
    }
  }
}

#define BUFFERSIZE 21
#define MIDBUFFER (BUFFERSIZE >> 1)
int tempobuffer[BUFFERSIZE] = { 0 };
int sortbuffer[BUFFERSIZE];
int tempobufferindex = 0;

int sort_desc(const void *cmp1, const void *cmp2)
{
  // Need to cast the void * to int *
  float a = *((int *)cmp1);
  float b = *((int *)cmp2);
  return b - a;
}

float median() 
{
  memcpy(sortbuffer, tempobuffer, BUFFERSIZE * sizeof(float));
  qsort(sortbuffer, BUFFERSIZE, sizeof(sortbuffer[0]), sort_desc);
  return sortbuffer[MIDBUFFER];
}

int16_t movingaverage(int16_t newvalue, int16_t avg, int n)
{
  if (newvalue < 3)
    return 0;
  if (newvalue > 1020)
    return 1023;
  else
    return (n * avg + newvalue) / (n + 1);
}

void updateTempo()
{
//  long ms = round(tempo / state.arp[curArp].tempo_factor * 1000.0 / CLOCKS_PER_BEAT);
  // use max speed up factor of 4 for the main timer and correct in the callback
  long ms = round(tempo / CLOCKTRIGGER * 1000.0 / CLOCKS_PER_BEAT);
  timer.update(ms); 
  showTempo(tempo, state.arp[curArp].tempo_factor, state.arp[curArp].swingoffset != 0);
}

void handleTempoPot()
{
  // linear tempo from 30 to 600 bpm  - pad a bit so extremes don't drop out
  int d = max(30, min(600, map(tempoPot.value, 0, 1023, 28, 602)));
  
// ?+ [0:1023:(1023/15)] :: 1023 {x~_;d~Int (2000 * E^(-x * 2.99 / 1024));]Int x,"\t",d,"\t", Int (60000.0 / d)}
//  int d = (int)(600 * exp(-(1023 - tempoPot.value) * 2.99 / 1024));
  int newtempo = (int)(60000.0 / d);
  if (tempo == newtempo)
    return;
  tempo = newtempo;
  updateTempo();
}

void checkTempo()
{
//  int v = movingaverage(analogRead(tempoPot.pin), tempoPot.value, 3);
  int newv = analogRead(tempoPot.pin);
// FIXME only use the median with external clock
  if (abs(newv - tempoPot.value) > 5)
  {
    tempoPot.value = newv;
    handleTempoPot();
  }
  return;
  tempobuffer[tempobufferindex] = newv;
  tempobufferindex = (tempobufferindex + 1) % BUFFERSIZE;
  int v = median();
  int dv = abs(v - tempoPot.value);
  int dnewv = abs(newv - tempoPot.value);
  if (dv > 2 || dnewv > 10 || v == 0 || v == 1023)
  {
    tempoPot.value = dnewv > 10 ? newv : v;
    handleTempoPot();
  }
}

void initstate()
{
  arpeggiators[0].settimebase(state.arp[0].timebase);
  arpeggiators[1].settimebase(state.arp[1].timebase);
  handleupdate();
}

void manageTempoFactor(int i)
{
  if (updateFactor[i])
  {
    state.arp[i].tempo_factor = updateValue[i];
    updateFactor[i] = false;
    /*
     * f  nclock  nbeat
     * 4      3 = 1 clock   72 = 1 beat
     * 3      4 = 1 clock   96 = 1 beat
     * 2      6 = 1 clock  144 = 1 beat
     * 1     12 = 1 clock  288 = 1 beat
     * 1/2   24 = 1 clock  576 = 1 beat
     * 1/3   36 = 1 clock  864 = 1 beat
     * 1/4   48 = 1 clock 1152 = 1 beat
     * 
     * so clocktrigger = 12 / f
     */
    state.arp[i].clocktrigger = round(12 / state.arp[i].tempo_factor);
    updateTempo();
  }
}

void timerFired()
{
  clock++;
  fullclock++;

  if (clock % LEDCLOCKCOUNT == 0)
  {
    digitalWrite(TEMPO_LED, tempoLEDstate);
    sendClock(tempoLEDstate);
    tempoLEDstate = !tempoLEDstate;
  }
  
  for (int i = 0; i < 2; ++i)
  {
    if (clock % state.arp[i].clocktrigger == 0)
    {
      clocks[i]++;
      if (arpeggiators[i].ontimer(clocks[i], fullclock, &state))
      {
        clocks[i] = 0;     
        manageTempoFactor(i);
      }
    }
  }
//  DBGn(clock)DBGn(clocks[0])DBG(clocks[1])
    
  if (clock == 1152)
    clock = 0;
}

void togglearpeggiate()
{
  state.common.active = !state.common.active;
  handleupdate();
}

void handleupdate()
{
  for (int i = 0; i < 2; ++i)
  {
    arpeggiators[i].updatenotestoplay(&state);
    if (state.arp[i].chords)
    {
      arpeggiators[i].resetchord();
    }
  }
  ensurevalid();
}

void ensurevalid()
{
  if (!state.common.latched && tracker.haslatched())
  {
    tracker.clearlatchednotes();
    handlestoppednotes();
  }
}

void displayNote(unsigned char splitnote, bool ch2)
{
  if (splitnote >= 33 && splitnote <= 120)
  {
    char *notestr = getnotestring(splitnote, false);
    char str[15] = {0};
    if (state.common.split == 2)
      sprintf(str, "Split: %s [ch 2]", notestr);
    else if (state.common.split == 1)
      sprintf(str, "Split: %3s", notestr);
    else 
      sprintf(str, "Split: off (%s)", notestr);
    showControl(str, false);
  }
}

void showLEDs()
{
  long d;
  for (int i = 0; i < NUM_BUTTONS; ++i)
  {
    switch (i)
    {
      case RANGE1_BTN:
      case RANGE2_BTN:
      case RANGE3_BTN:
      case RANGE4_BTN:
        if (settingmul || settingdiv || (settingdevmode && i < RANGE4_BTN))
        {
          d = (millis() - buttons[i].heldsince) / 150;
          expander->digitalWrite(buttons[i].led1pin, d % 2);
        }
        else if (buttons[i].heldsince > 0)
        {
          d = (millis() - buttons[i].heldsince) / 150;
          expander->digitalWrite(buttons[i].led1pin, d % 2);
        }
        else
          expander->digitalWrite(buttons[i].led1pin, state.arp[curArp].range == i - RANGE1_BTN + 1);
        break;
      case SPLIT_BTN:
        if (settingsplit)
        {
          d = (millis() - buttons[i].heldsince) / 150;
          if (state.common.split == 2)
            expander->digitalWrite(buttons[i].led2pin, d % 2);
          else
            expander->digitalWrite(buttons[i].led1pin, d % 2);
        }
        else
        {
          expander->digitalWrite(buttons[i].led1pin, state.common.split == 1);
          expander->digitalWrite(buttons[i].led2pin, state.common.split == 2);
        }
        break;
      case LATCH_BTN:
        expander->digitalWrite(buttons[i].led1pin, state.common.latched);
        break;
      case SORT_BTN:
        expander->digitalWrite(buttons[i].led1pin, !state.arp[curArp].playinheldorder);
        break;
    }
  }
}

void showModeState()
{
  static bool invert;
  invert = flashmarker > 0 && !invert;
  if (flashmarker > 0)
    flashmarker += TEXTFLASHMILLIS;
    
  char str[27] = {0};
  switch (setting)
  {    
    case SPLIT:
      displayNote(state.common.splitnote, state.common.split == 2);
      break;
    case TRANSPOSE:
      sprintf(str, "Transpose: %s", transposing ? "on" : "off");
      showControl(str, invert);
      break;
    case REPLAY:
      if (replayRunning())
      {
        int transoffset = getTransposeOffset();
        if (transoffset)
        {
          sprintf(str, "Replaying [%+d]", transoffset);
          showControl(str, invert);
        }
        else
          showControl("Replaying", invert);
      }
      else
      {
        showControl("Replay", invert);
      }
      break;
    case RECORD:
    {
      if (isRecording())
      {
        sprintf(str, "Recording [%d]", getEventCount());
        showControl(str, invert);
      }
      else
      {
        showControl("Record", invert);
      }
      break;
    }
    case CLK_MUL:
      showControl("Set multiplier", invert);
      break;
    case CLK_DIV:
      showControl("Set divider", invert);
      break;
    case DEV_MODE:
      showControl("Device mode", invert);
      break;
  }
}

void showState()
{
  showLEDs();
  showMode(0, curArp, &state);
  showMode(1, curArp, &state);
}

// simple watchdog to catch stuck note errors
void watchdog()
{
  static bool watchdog = false;
  int held = tracker.getheldcount(true);
  if (!watchdog && held > 0)
    watchdog = true;
  if (loopcount % 1000 == 0)
  {
    if (watchdog)
    {
      if (tracker.getheldcount(true) == 0)
      {
        stopallnotes(false);
        watchdog = false;
      }
    }
  }
}

void setup() 
{
  Serial.begin(9600);
  pinMode(TEMPO_LED, OUTPUT);
  setupMIDI();
  setupExpander();
  setupButtons();
  pinMode(tempoPot.pin, INPUT);
  handleTempoPot();
  setupEncoders();
  setupLCD();
  initstate();
  showState();
  showModeState();

  clock = 0;
  fullclock = 0;
  timer.begin(timerFired, tempo * 1000);  

  analogWriteResolution(10);
  updateTempo();
}

void loop() 
{
  loopcount++;
  unsigned long now = millis();
  checkMIDI(fullclock);
  checkEncoders();

  int arp1 = arpeggiators[0].onloop(fullclock, &state);
  if (curmode == ARPBOTH && state.common.active)
    arpeggiators[1].onloop(fullclock, &state);
    
  if (arp1 == ARP_NEXT)
    ensurevalid();
  
  int checkclock = clocks[curArp];
  if (checkclock < CLOCKS_PER_BEAT - 3 && loopcount % 10 == 0)
  {
    checkButtons(now);
    showLEDs();
    if (checkclock < CLOCKS_PER_BEAT / 2)
    {
      if (statechange)
      {
        showState();
        statechange = false;
      }
      if (flashmarker > 0 && (long)millis() - flashmarker > 0)
      {
        showModeState();
      }
      doUpdateNoteDisplay();
    }

    watchdog();
  }
  if (loopcount % 4 == 0)
     checkTempo();
     
  if (Serial.available())
  {
    char c = Serial.read();
    if (iscntrl(c))
    {
      // ignore control data 
    }
    else if (c == 'd')
    {
      tracker.dump();
    }
    while (Serial.available())
      Serial.read();
  }
}
