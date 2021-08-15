#include <stdlib.h>
#include <stdio.h>

#include "Sharp.h"
#include "Utilities.h"
#include "noteset.h"
#include "notetracker.h"

#include "LCD.h"
#include "MIDI.h"

/*
 * Keep track of all notes:
 *  - arpeggio defining notes
 *    - currently held down 
 *    - released but still playing (latched)
 *  - melody notes (i.e. above a split)
 *    - currently held down
 *    - released but still playing (latched)
 */
   
notetracker::notetracker()
{
  heldnotes = new noteset(N);
  sortedheld = new noteset(N);
  bypassnotes = new noteset(N);
  newnotestoplay = new noteset(N);
  notestostop = new noteset(N);
}

notetracker::~notetracker()
{
  if (heldnotes)
    delete heldnotes;
  if (sortedheld)
    delete sortedheld;
  if (bypassnotes)
    delete bypassnotes;
  if (newnotestoplay)
    delete newnotestoplay;
  if (notestostop)
    delete notestostop;
}
 
void notetracker::resettracker()
{
  heldnotes->clear();
  bypassnotes->clear();
  sortedheld->clear();
  needUpdateNoteDisplay(heldnotes);
}

byte notetracker::getheldnote(int i, byte channel, bool nolatched)
{
  if (i < heldnotes->count)
  {
    noteinfo note = heldnotes->notes[i];
    if (nolatched && note.latched)
      return 0;
    if (note.channel == channel)
      return heldnotes->notes[i].pitch;
  }
  return 0;
}

int notetracker::isheldnote(unsigned char pitch, unsigned char channel, bool nolatched)
{
  for (int i = 0; i < sortedheld->count; ++i)
  {
    noteinfo note = sortedheld->notes[i];
    if (note.pitch > pitch)
      return -1;
    else if (note.pitch == pitch)
    {
      if (nolatched && note.latched)
        return -1;
      else
        return note.channel == channel ? i : -1;
    }
  }
  return -1;
}

void notetracker::transposeheldnotes(int offset)
{
  for (int i = 0; i < heldnotes->count; ++i)
  {
    heldnotes->notes[i].pitch += offset;
    sortedheld->notes[i].pitch += offset;
  }
}

bool notetracker::isbypass(unsigned char pitch, unsigned char channel)
{
  int i = bypassnotes->findnote(pitch);
  if (i < 0)
    return false; // nothing to remove
  noteinfo note = bypassnotes->notes[i];
  return note.channel == channel;
}

void notetracker::sortnotes()
{
  delete sortedheld;
  sortedheld = heldnotes->copy();
  sortedheld->sort();
}

// a note was played - store it accordingly
bool notetracker::tracknoteon(unsigned char pitch, devicestate *state)
{
  notestostop->count = 0;
  newnotestoplay->count = 0;
  
  // make sure the pitch is valid
  if (pitch == 0 || pitch > 127)
    return false;

  noteinfo note = makenote(pitch);
  if (!state->common.active || note.bypass)
  {
    newnotestoplay->count = 1;
    newnotestoplay->notes[0] = note;
  }
  
  if (note.bypass)  // the note is either a bypass or a held note
  {
    bypassnotes->addnote(note);
  }
  else
  {
    // do we need to reset the latch?
    if (state->common.latched && heldnotes->heldcount == 0)
    {
      notestostop->count = heldnotes->count;
      for (int i = 0; i < heldnotes->count; ++i)
        notestostop->notes[i] = heldnotes->notes[i];
      // clear all latched notes and start again
      heldnotes->clear();
    }
    heldnotes->addnote(note);
    sortnotes();
    needUpdateNoteDisplay(sortedheld);

#if DEBUG_SERIAL
    Serial.println("Note on");
    Serial.println("--------------------------------------------");
    dump();
    Serial.println("--------------------------------------------");
#endif  
    return heldnotes->heldcount == 1;
  }

  return false;
}

// a note was lifted, update the sets
bool notetracker::tracknoteoff(byte pitch, devicestate *state)
{
  notestostop->count = 0;
  newnotestoplay->count = 0;
  
  // make sure the pitch is valid
  if (pitch == 0 || pitch > 127)
    return false;

  noteinfo note;
  int i = bypassnotes->findnote(pitch);
  if (i >= 0)
  {
    note = bypassnotes->notes[i];
    notestostop->count = 1;
    notestostop->notes[0] = note;
    bypassnotes->removenote(pitch);
  }
  else
  {
    i = heldnotes->findnote(pitch);
    if (i < 0)
    {
//      Serial.println("### ERROR ### Can't find the note");
      return false; // nothing to remove - bug?
    }
    if (state->common.latched)
    {
      heldnotes->notes[i].latched = true;
      heldnotes->heldcount--;
    }
    else
    {
      note = heldnotes->notes[i];
      heldnotes->removenote(pitch);
      notestostop->count = 1;
      notestostop->notes[0] = note;
    }
    sortnotes();
    needUpdateNoteDisplay(sortedheld);
  }
#if DEBUG_SERIAL
  Serial.println("Note off");
  Serial.println("--------------------------------------------");
  dump();
  Serial.println("--------------------------------------------");
#endif
  return true;
}

bool notetracker::haslatched()
{
  return heldnotes->count != heldnotes->heldcount;
}

void notetracker::clearlatchednotes()
{
  int i;
  notestostop->clear();
  for (i = 0; i < heldnotes->count; ++i)
  {
    if (heldnotes->notes[i].latched)
    {
      notestostop->notes[notestostop->count++] = heldnotes->notes[i];
    }
  }
  for (i = 0; i < notestostop->count; ++i)
  {
    heldnotes->removenote(notestostop->notes[i].pitch);
  }
  sortnotes();
}

void notetracker::adjustfornewsplit(unsigned char pitch)
{
  // remove bypass and latched notes below pitch
  int i;
  notestostop->clear();
  for (i = 0; i < heldnotes->count; ++i)
  {
    if (curmode == ARPL)
    {
      if (heldnotes->notes[i].pitch > pitch && heldnotes->notes[i].latched)
      {
        notestostop->notes[notestostop->count++] = heldnotes->notes[i];
      }
    }
    else if (curmode == ARPH)
    {
      if (heldnotes->notes[i].pitch < pitch && heldnotes->notes[i].latched)
      {
        notestostop->notes[notestostop->count++] = heldnotes->notes[i];
      }
    }
  }
  for (i = 0; i < notestostop->count; ++i)
  {
    heldnotes->removenote(notestostop->notes[i].pitch);
  }
}

int notetracker::getheldcount(bool includelatched)
{
  return includelatched ? heldnotes->count : heldnotes->heldcount;
}

void notetracker::dump()
{
  Serial.println(getnotesetstring("heldnotes", heldnotes));
  Serial.println(getnotesetstring("sortedheld", sortedheld));
  Serial.println(getnotesetstring("bypassnotes", bypassnotes));
  Serial.println(getnotesetstring("newnotestoplay", newnotestoplay));
  Serial.println(getnotesetstring("notestostop", notestostop));
}
