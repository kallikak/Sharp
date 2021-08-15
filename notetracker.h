#ifndef notetracker_h
#define notetracker_h

class noteset;

/*
 * The tracker maintains the sets of notes that are the input to the arpeggiator,
 * and sends updates to the arpeggiator when necessary.
 */
class notetracker
{
  private:
    
  public:
    notetracker();
    ~notetracker();
    
    noteset *heldnotes;      // notes currently held down (either physically or via the latch)
    noteset *sortedheld;
    noteset *bypassnotes;    // notes that do not contribute to the arpeggio generation
    
    noteset *newnotestoplay;   // any new notes to send via MIDI in response to latest change
    noteset *notestostop;      // any notes to stop via MIDI in response to latest change
    
    int getheldcount(bool includelatched);
    unsigned char getheldnote(int i, unsigned char channel, bool nolatched);
    void transposeheldnotes(int offset);
    int isheldnote(unsigned char pitch, unsigned char channel, bool nolatched);
    bool haslatched();
    
    bool tracknoteon(unsigned char pitch, devicestate *state);
    bool tracknoteoff(unsigned char pitch, devicestate *state);
    void resettracker();
    void clearlatchednotes();
    void adjustfornewsplit(unsigned char pitch);
    bool isbypass(unsigned char pitch, unsigned char channel);
    
    void updatenotestoplay(const devicestate *state);
    void sortnotes();
    
    void dump();
};

#endif
