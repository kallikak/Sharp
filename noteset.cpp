#include <stdlib.h>
#include <stdio.h>

#include <Arduino.h>
#include "Sharp.h"
#include "noteset.h"

noteset::noteset(int n):size(n), count(0), heldcount(0)
{
  notes = new noteinfo[n];
}

noteset::~noteset()
{
  if (notes)
    delete notes;
}

void noteset::addnote(noteinfo note)
{
  if (count == size)
    return; // we're full
  int i = findnote(note.pitch);
  if (i >= 0) // note is already there - remove then update
    removenote(note.pitch);
  notes[count] = note;
  count++;
  if (!note.latched) // if not latched it was a genuine held
    heldcount++;
}

int noteset::findnote(unsigned char pitch)
{
  if (pitch == 0 || pitch > 127)
    return -1;
  int i = 0;
  while (i < count)
  {
    if (notes[i].pitch == pitch)
      return i;
    else
      i++;
  }
  return -1;
}

void noteset::removenote(unsigned char pitch)
{
  int i = findnote(pitch);
  if (i < 0)
    return; // nothing to remove
  noteinfo removednote = notes[i];
  for (int j = i; j < size - 1; ++j)
  {
    if (notes[j].pitch == 0)
      break;
    notes[j] = notes[j + 1];  // shuffle them back
  }
  count--;
  if (!removednote.latched) // if not latched it was a genuine held
    heldcount--;
}

void noteset::clearnote(noteinfo *note)
{
  note->pitch = 0;
  note->bypass = 0;
  note->channel = 0;
  note->latched = 0;
}

void noteset::clear()
{
  count = heldcount = 0;
  for (int i = 0; i < count; ++i)
    clearnote(&notes[i]);
}

noteinfo *noteset::getnote(int i)
{
  if (i < 0 || i >= count)
    return NULL;
  return &notes[i];
}

noteset *noteset::copy()
{
  noteset *result = new noteset(size);
  for (int i = 0; i < count; ++i)
  {
    result->addnote(*getnote(i));
  }
  return result;
}

noteinfo *noteset::getrandomnote()
{
  int i = (int)(1.0f * count * rand() / RAND_MAX);
  return &notes[i];
}

int compare(const void* pa, const void* pb)
{
  noteinfo a = *((noteinfo *)pa);
  noteinfo b = *((noteinfo *)pb);
  return (a.pitch > b.pitch) - (a.pitch < b.pitch);
}

void noteset::sort()
{
  qsort(notes, count, sizeof(noteinfo), compare);
}
