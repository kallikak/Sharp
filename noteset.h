#ifndef noteset_h
#define noteset_h

class noteset
{
  private:
  
  public:
    noteset(int n);
    ~noteset();
    
    noteinfo *notes;
    int size;         // maximum number of notes
    int count;        // notes in the set
    int heldcount;    // notes in the set that are currently physically pressed (i.e. not latched)
    
    void addnote(noteinfo note);
    int findnote(unsigned char pitch);
    void removenote(unsigned char pitch);
    void clearnote(noteinfo *note);
    void clear();

    noteset *copy();
    void sort();
    void permute();
    
    noteinfo *getnote(int i);
    noteinfo *getrandomnote();
};

#endif
