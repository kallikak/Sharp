#ifndef sharp_h
#define sharp_h

#define DBG(v) {Serial.print(#v " = ");Serial.println(v);}
#define DBG2(s,v) {Serial.print(#s " = ");Serial.println(v);}
#define DBG3(s,v) {Serial.print(#s ", " #v " = ");Serial.println(v);}
#define DBGn(v) {Serial.print(#v " = ");Serial.print(v);Serial.print(" ");}
#define DBG2n(s,v) {Serial.print(#s " = ");Serial.print(v);Serial.print(" ");}
#define DBG3n(s,v) {Serial.print(#s ", " #v " = ");Serial.print(v);Serial.print(" ");}
#define DBGLINE {Serial.println("----------------");}

#define CLOCKS_PER_BEAT 24
#define CLOCKTRIGGER 12
#define SWING_OFFSET 8

#define N 50
#define N2 N * 14

#define NUMALT 8
#define NUMMODE 8
#define NUMSET 8
typedef enum { NORMAL, DOUBLE, DOUBLE2, DOUBLE3, OCTAVES, RATCHET1, RATCHET2, WALK } altmode;
typedef enum { UP, DOWN, UPDOWN, IN, OUT, INOUT, RANDOM, SHUFFLE } arpmode;
typedef enum { DEV_MODE, CLK_MUL, CLK_DIV, SPLIT, TRANSPOSE, REPLAY, RECORD, EDIT_MODE } settingsmode;

// ARPL mode: arpeggiate below split, upper note to ctrl cv and clock
// ARPH mode: arpeggiate above split, lower note to ctrl cv and clock
// ARPBOTH mode: arpeggiate any played notes in two ways, ctrl cv and clock for 2nd arp
typedef enum { ARPL, ARPH, ARPBOTH } devicemode;
typedef enum { ARP1, ARP2, ARP3 } arpeditmode;  // ARP3 = edit both together? (TODO: not sure how that will actually work...)

typedef struct
{
  bool active;
  int split;    // 0 = off, else 1 channel or 2 channel
  unsigned char splitnote;
  bool latched;
} commonstate;

typedef struct
{
  bool playinheldorder;
  int range;
  int timebase;
  arpmode mode;
  bool chords;
  altmode alt;
  float tempo_factor;
  int clocktrigger;
  int swingoffset;  // 0, +8 or -8 so 24-24 => 32-16
} arpstate;

typedef struct 
{
  devicemode mode;
  commonstate common;
  arpstate arp[2];
} devicestate;

typedef struct noteinfo_s
{
  unsigned char pitch;
  unsigned char bypass;
  unsigned char channel;
  bool latched;
} noteinfo;

noteinfo makenote(unsigned char pitch);

arpstate *getArpState(int index);

extern unsigned char lastvelocity;
extern int tempo;
extern int curArp;
extern settingsmode setting;
extern devicemode curmode;
extern commonstate *curcommon;

long getClock();
void handleupdate();
void showState();
void displayNote(unsigned char splitnote, bool ch2);

#endif
