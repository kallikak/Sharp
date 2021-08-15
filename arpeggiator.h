#ifndef arpeggiator_h
#define arpeggiator_h

#define ARP_NONE 0
#define ARP_PREFIRE 1
#define ARP_NEXT 2
#define ARP_RATCHET 3

class notetracker;

/**
 * An arpeggiator calculates and stores a set of notes to stream,
 * based on the settings and the sets of held, latched and bypass notes from the tracker.
 */
class arpeggiator
{
  private:
    int index;
    notetracker *tracker;
    
    int playcount = 0;
    int trueplaycount = 0;
    unsigned char toplay[N2] = {0};
    unsigned char temp[N2] = {0};
    int chordcount = 0;
    
    int timebase = 1;
    
    unsigned char note = 0;
    unsigned char lastnote = 0;
    unsigned char chord[4] = {0, 0, 0, 0};
    int chordindex = 0;
    int chordsize = 0;
    int abschordindex = 0;
    int noteindex = 0;
    
    volatile bool ratchet = false;
    volatile bool prefire = false;
    volatile bool next = false;
    
    void calcplaycount();
    
    bool doublenote(int index, altmode alt);
    int makechords(int n, int j, int k, int split, unsigned char toplay[], noteinfo sortednotes[]);
    void sortnotes();
    void permute(unsigned char notes[], int n, altmode alt, int range);
      
  public:
    arpeggiator(notetracker *tracker, int i);
    ~arpeggiator() {}

    bool ontimer(int clock, int fullclock, devicestate *state);
    int onloop(int fullclock, devicestate *state);

    int getnoteindex() { return noteindex; }
    
    bool havenotes();
    int getchordcount() { return chordcount; }
  
    void settimebase(int timebase);
    
    void lastnoteoff(devicestate *state);
    void lastchordoff(devicestate *state);
    void playchord(int fullclock, devicestate *state);
    void playnext(int fullclock, devicestate *state);
    void resetchord();
    void resetnote();
      
    noteset getnewnotestoplay();
    noteset getnotestostop();
    
    unsigned char getnote(int i);
    unsigned char getrandomnote(int i, arpstate *arp);
    void updatenotestoplay(devicestate *state);
};

#endif
