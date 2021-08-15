#include <stdlib.h>

#include "Sharp.h"

#include "noteset.h"
#include "notetracker.h"
#include "arpeggiator.h"

#include "MIDIManager.h"

arpeggiator::arpeggiator(notetracker *tr, int i)
{
  index = i;
  tracker = tr;
  chordsize = 0;
  chordindex = 0;
  for (int i = 0; i < 4; ++i)
    chord[i] = 0;
}

void arpeggiator::resetnote()
{
  noteindex = 0;
}

void arpeggiator::resetchord()
{
  chordsize = getchordcount();
  chordindex = 0;
  noteindex = chordsize ? chordsize * ceil(noteindex / chordsize) : 0;
}

bool arpeggiator::ontimer(int clock, int fullclock, devicestate *state)
{  
  next = false;
  bool ratcheting;
  int noteindex = getnoteindex();
  int hc = tracker->getheldcount(true);
  if (state->arp[index].alt == RATCHET1 && (noteindex - 1) % hc == 0)
    ratcheting = true;
  else if (state->arp[index].alt == RATCHET2 && (noteindex - 1) % hc == hc - 1)
    ratcheting = true;
  else
    ratcheting = false;  
  if (ratcheting && clock == (CLOCKS_PER_BEAT - state->arp[index].swingoffset) / 2 - 3)
  {
    prefire = true;
  }
  else if (ratcheting && clock == (CLOCKS_PER_BEAT - state->arp[index].swingoffset) / 2)
  {
    ratchet = true;
  }
  else if (clock == CLOCKS_PER_BEAT - state->arp[index].swingoffset - 3)
  {
    prefire = true;
  }
  else if (clock >= CLOCKS_PER_BEAT - state->arp[index].swingoffset)
  {
    next = true;
    state->arp[index].swingoffset = -state->arp[index].swingoffset;
  }
//  DBG3("ontimer", clock)
//  DBG3("ontimer", prefire)
//  DBG3("ontimer", next)

  return next;
}

int arpeggiator::onloop(int fullclock, devicestate *state)
{
//  DBG3("onloop", prefire)
  // need a little gap else repeated notes don't sound cleanly
  if (prefire)
  {
    prefire = false;
    lastchordoff(state);
    lastnoteoff(state);
    return ARP_PREFIRE;
  }
  else if (next)
  {
    next = false;
    if (state->common.active)
    {
      if (state->arp[index].chords)
        playchord(fullclock, state);
      else
        playnext(fullclock, state);
    }
    return ARP_NEXT;
  }
  else if (ratchet)
  {
    if (lastnote > 0 && !tracker->isbypass(lastnote, 1))
    {
      sendNoteOn(state->common.split == 2 ? index + 1 : 1, lastnote, lastvelocity);
      note = lastnote;  // so it gets turned off properly
    }
    ratchet = false;
    return ARP_RATCHET;
  }

  return ARP_NONE;
}

// turn off arpeggio note - but not if it is a current bypass
void arpeggiator::lastnoteoff(devicestate *state)
{
  if (note > 0 && !tracker->isbypass(note, 1))
  {
    if (curmode == ARPBOTH)
      sendNoteOff(index + 1, note, 0x00);
    else
      sendNoteOff(state->common.split == 2 ? index + 1 : 1, note, 0x00);
    note = 0;
  }
}

void arpeggiator::lastchordoff(devicestate *state)
{
  for (int i = 0; i < 4; ++i)
  {
    byte chordnote = chord[i];
    if (chordnote > 0 && !tracker->isbypass(chordnote, 1))
    {
      sendNoteOff(index + 1, chordnote, 0x00);
      chord[i] = 0;
    }
  }
}

void arpeggiator::playchord(int fullclock, devicestate *state)
{
  int i;
  byte chordnote;
  lastnoteoff(state);
  arpstate arp = state->arp[index];
  bool israndom = arp.mode == RANDOM;
  bool chordtick = ++abschordindex % 2;
  if (/*chordindex == 0 && */chordtick)  // new chord
  {
    chordsize = getchordcount();
    lastchordoff(state);
    for (i = 0; i < chordsize; ++i)
    {
      chord[i] = israndom ? getrandomnote(noteindex, &arp) : getnote(noteindex + i);
      if (chord[i] == 0)
      {
        noteindex = 0;
        notifyCycle(fullclock);
        chord[i] = getnote(i);
      }
      chordnote = chord[i];
      if (chordnote > 0 && !tracker->isbypass(chordnote, 1))
      {
        sendNoteOn(index + 1, chordnote, lastvelocity);
      }
    }
    noteindex += chordsize;
  }
//  if (++chordindex == chordsize)
//    chordindex = 0;
}

void arpeggiator::playnext(int fullclock, devicestate *state)
{
  arpstate arp = state->arp[index];
  lastchordoff(state);
  lastnoteoff(state);
  if (arp.mode == RANDOM)
  {
    note = getrandomnote(noteindex, &arp);
  }
  else if (arp.alt == WALK)
  {
    float r = 1.0 * rand() / RAND_MAX;
    if (noteindex > 1 && r > 0.85)
    {
      // previous note
      noteindex -= 2;
    }
    else if (noteindex && r > 0.8)
    {
      // same note
      noteindex--;
    }
    note = getnote(noteindex);
  }
  else
    note = getnote(noteindex);
  if (note == 0)
  {
    noteindex = 0;
    notifyCycle(fullclock);
    note = getnote(0);
  }
  lastnote = note;
  if (note > 0 && !tracker->isbypass(note, 1))
  {
    sendNoteOn(index + 1, note, lastvelocity);
  }
  noteindex++;
}

void arpeggiator::permute(unsigned char notes[], int n, altmode alt, int range)
{
  // need to muck around a bit to maintain alt structure
  int i, j;
  if (alt == DOUBLE)
  {
    // if alt is repeat need to exchange pairs
    for (i = n / 2 - 1; i >= 0; --i)
    {
      j = rand() % (i + 1);
      unsigned char temp = notes[2 * i];
      unsigned char temp1 = notes[2 * i + 1];
      notes[2 * i] = notes[2 * j];
      notes[2 * i + 1] = notes[2 * j + 1];
      notes[2 * j] = temp;
      notes[2 * j + 1] = temp1;
    }
  }
  else if (alt == DOUBLE2)
  {
    // Pattern: ABBCDD...
    for (i = n - 1; i >= 0; --i)
    {
      if ((i + 1) % 3 == 0)
        continue;
      do 
      {
        j = rand() % (i + 1);
      } while ((j + 1) % 3 == 0); // don't choose spot 3n
  
      // swap the last element with element at random index
      unsigned char temp = notes[i];
      notes[i] = notes[j];
      notes[j] = temp;
    }
    for (i = 1; i < n - 1; i += 3)  // now do the repeats
      notes[i + 1] = notes[i];
  }
  else if (alt == DOUBLE3)
  {
    // Pattern: ABCBDEFE...
    for (i = n - 1; i >= 0; --i)
    {
      if ((i + 1) % 4 == 0)
        continue;
      do 
      {
        j = rand() % (i + 1);
      } while ((j + 1) % 4 == 0); // don't choose spot 4n
  
      // swap the last element with element at random index
      unsigned char temp = notes[i];
      notes[i] = notes[j];
      notes[j] = temp;
    }
    for (i = 1; i < n - 1; i += 4)  // now do the repeats
      notes[i + 2] = notes[i];
  }
  else if (alt == OCTAVES)
  {
    // Pattern: AAAABBBB...
    int r = range;
    for (i = n / r - 1; i >= 0; --i)
    {
      j = rand() % (i + 1);
      unsigned char temp = notes[r * i];
      notes[r * i] = notes[r * j];
      notes[r * j] = temp;
    }
    for (i = 0; i < n; i += r)  // now do the repeats
    {
      for (int k = 1; k < r; ++k)
      {
        notes[i + k] = notes[i] + 12 * k;
      }
    }
  }
  else
  {
    for (i = n - 1; i >= 0; --i)
    {
      // swap the last element with element at random index
      j = rand() % (i + 1);
      unsigned char temp = notes[i];
      notes[i] = notes[j];
      notes[j] = temp;
    }
  }
}

void arpeggiator::calcplaycount()
{
  if (timebase > 1 && trueplaycount > timebase)
  {
    playcount = int((trueplaycount + 1) / timebase) * timebase;
  }
  else
    playcount = trueplaycount;
}

void arpeggiator::settimebase(int tb)
{
  timebase = tb;
  calcplaycount();
  tracker->sortnotes();
}

unsigned char arpeggiator::getnote(int i)
{
  if (i >= playcount)
    return 0;
  return toplay[i];
}

unsigned char arpeggiator::getrandomnote(int i, arpstate *arp)
{
  static int index = 0;
  altmode alt = arp->alt;
  switch (alt)
  {
    case NORMAL: // standard arp
    default:
      return toplay[(int)(1.0f * playcount * rand() / RAND_MAX)];
    case OCTAVES: // octaves
      if (i % arp->range == 0)
        index = (int)(1.0f * playcount * rand() / RAND_MAX);
      return toplay[index] + (i % arp->range) * 12;
    case DOUBLE: // repeat all
      if (i % 2 == 0)
        index = (int)(1.0f * playcount * rand() / RAND_MAX);
      return toplay[index];
    case DOUBLE2: // repeat every 2nd
      if (i % 3 != 2)
        index = (int)(1.0f * playcount * rand() / RAND_MAX);
      return toplay[index];
    case DOUBLE3: // repeat every 3rd
      if (i % 4 != 3)
        index = (int)(1.0f * playcount * rand() / RAND_MAX);
      return toplay[index];
  }
}

void arpeggiator::updatenotestoplay(devicestate *state)
{
  int n = tracker->heldnotes->count;
  int i, j = 0, m = 0;
  arpstate *arp = getArpState(index);
  int range = arp->range;
  altmode alt = arp->alt;
  arpmode mode = arp->mode;
  int k = n * range;
  chordcount = 0;
  noteinfo *notes = arp->playinheldorder ? tracker->heldnotes->notes : tracker->sortedheld->notes;
  switch (mode)
  {
    // these modes do not support the OCTAVE variant
    case IN:
    case OUT:
    case INOUT:
    {
      // start with a full upward arpeggio
      for (k = 0; k < range; ++k)
      {
        for (i = 0; i < n; ++i)
        {
          temp[j++] = notes[i].pitch + k * 12;
        }
      }
      // now sort the notes as required 
      int i_d = 0;
      int i_u = n * k - 1;
      j = 0;
      while (i_d <= i_u)
      {
        toplay[j] = temp[i_d];
        if (doublenote(m, alt))
        {
          toplay[j + 1] = alt == DOUBLE3 ? toplay[j - 1] : toplay[j];
          j++;
        }
        j++;
        m++;

        if (i_d != i_u)
        {
          toplay[j] = temp[i_u];
          if (doublenote(m, alt))
          {
            toplay[j + 1] = alt == DOUBLE3 ? toplay[j - 1] : toplay[j];
            j++;
          }
          j++;
          m++;
        }
        
        i_d++;
        i_u--;
      }
      if (mode == OUT)  // reverse the array
      {
        int size = j - 1;
        for (i = 0; i < size / 2; i++)
        {
            int temp = toplay[i];
            toplay[i] = toplay[size - 1 - i];
            toplay[size - 1 - i] = temp;
        }
      }
      else if (mode == INOUT)
      {
        int size = j - 1;
        for (i = 1; i < size; i++)
        {
            toplay[j++] = toplay[size - i];
        }
      }
      if (arp->chords)
        chordcount = n > 3 ? 3 : n;
      break;
    }
    case UP:
    case RANDOM:
    case SHUFFLE:
      if (!arp->chords && alt == OCTAVES)
      {
        for (i = 0; i < n; ++i)
          for (k = 0; k < range; ++k)  
            // for RANDOM we make the octaves when the random choice is made
            // and for SHUFFLE when permuting
            toplay[j++] = notes[i].pitch + (mode == UP ? k : 1) * 12;
      }
      else
      {
        for (k = 0; k < range; ++k)
        {
          if (arp->chords)
          {
            j = makechords(n, j, k, state->common.split, toplay, tracker->sortedheld->notes);
          }
          else
          {
            for (i = 0; i < n; ++i)
            {
              toplay[j] = notes[i].pitch + k * 12;
              if (doublenote(m, alt))
              {
                toplay[j + 1] = alt == DOUBLE3 ? toplay[j - 1] : toplay[j];
                j++;
              }
              j++;
              m++;
            }
          }
        }
      }
      break;
    case DOWN:
      if (!arp->chords && alt == OCTAVES)
      {
        for (i = 0; i < n; ++i)
          for (k = range - 1; k >= 0; --k)
            toplay[j++] = notes[n - i - 1].pitch + k * 12;
      }
      else
      {
        for (k = range - 1; k >= 0; --k)
        {
          if (arp->chords)
          {
            j = makechords(n, j, k, state->common.split, toplay, tracker->sortedheld->notes);
          }
          else
          {
            for (i = 0; i < n; ++i)
            {
              toplay[j] = notes[n - i - 1].pitch + k * 12;
              if (doublenote(m, alt))
              {
                toplay[j + 1] = alt == DOUBLE3 ? toplay[j - 1] : toplay[j];
                j++;
              }
              j++;
              m++;
            }
          }
        }
      }
      break;
    case UPDOWN:
      if (!arp->chords && alt == OCTAVES)
      {
        for (i = 0; i < n; ++i)
          for (k = range - 1; k >= 0; --k)
            toplay[j++] = notes[i].pitch + k * 12;
      }
      else
      {
        for (k = 0; k < range; ++k)
        {
          if (arp->chords)
          {
            j = makechords(n, j, k, state->common.split, toplay, tracker->sortedheld->notes);
          }
          else
          {
            for (i = 0; i < n; ++i)
            {
              toplay[j] = notes[i].pitch + k * 12;
              if (doublenote(m, alt))
              {
                toplay[j + 1] = alt == DOUBLE3 ? toplay[j - 1] : toplay[j];
                j++;
              }
              j++;
              m++;
            }
          }
        }
      }
      k = j - 2;
      if (doublenote(m - 1, alt))
        k--;
      while (k > 0)
        toplay[j++] = toplay[k--];
      if (doublenote(0, alt))
        toplay[j - 1] = 0;
      break;   
  }
  trueplaycount = j;// - 1;
  calcplaycount();
  
  while (j < N2)
    toplay[j++] = 0;

  if (mode == SHUFFLE && playcount)
  {
    permute(toplay, playcount, alt, range);
  }
}

bool arpeggiator::doublenote(int index, altmode alt)
{
  switch (alt)
  {
    case NORMAL: // standard arp
    case OCTAVES: // octaves
    default:
      return false;
    case DOUBLE: // repeat all
      return true;
    case DOUBLE2: // repeat every 2nd
      return index % 2 == 1;
    case DOUBLE3: // repeat every 3rd
      return index % 3 == 2;
  }
}

int arpeggiator::makechords(int n, int j, int k, int split, byte toplay[], noteinfo sortednotes[])
{
  int random1;

  if (n == 1)
  {
    toplay[j++] = sortednotes[0].pitch + k * 12;
  }
  else if (n == 2)
  {
    toplay[j++] = sortednotes[0].pitch + k * 12;
    toplay[j++] = sortednotes[1].pitch + k * 12;
  }
  else if (n == 3)
  {
    toplay[j++] = sortednotes[0].pitch + k * 12;
    toplay[j++] = sortednotes[1].pitch + k * 12;
    toplay[j++] = sortednotes[2].pitch + k * 12;
  }
  else if (n > 3)
  {
    toplay[j++] = sortednotes[0].pitch + k * 12;
    if (sortednotes[n - 1].pitch - sortednotes[0].pitch == 12)
    {
      random1 = (int)(1.0f * (n - 3) * rand() / RAND_MAX);
      toplay[j++] = sortednotes[1 + random1].pitch + k * 12;
      toplay[j++] = sortednotes[n - 2].pitch + k * 12;      
    }
    else
    {
      random1 = (int)(1.0f * (n - 2) * rand() / RAND_MAX);
      toplay[j++] = sortednotes[1 + random1].pitch + k * 12;
      toplay[j++] = sortednotes[n - 1].pitch + k * 12;
    }
  }
  
  chordcount = n > 3 ? 3 : n;
  
  return j;
}
