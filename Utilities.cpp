#include <Arduino.h>

#include "Utilities.h"
#include "Sharp.h"
#include "noteset.h"

static char *notestr = NULL;

const char *notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// A0 = 33 is the lowest I will support
// C8 = 120 is the highest I will support
char *getnotestring(byte midipitch, bool inclNumber)
{
  if (!notestr)
    notestr = (char *)calloc(12, sizeof(char));
  int octave = floor(midipitch / 12) - 1;
  const char *ns = (midipitch < 33 || midipitch > 120) ? "-" : notes[midipitch % 12];
  if (inclNumber)
    sprintf(notestr, "%s%d[%d]", ns, octave, midipitch);
  else
    sprintf(notestr, "%s%d", ns, octave);  
  return notestr;
}

// share the string
static char *diagstr = NULL;
char *getnotesetstring(const char *name, noteset *set)
{
  int i, n;

  if (diagstr)
    diagstr[0] = 0;
  else
    diagstr = (char *)calloc(300, sizeof(char));
  
  char *pstr = diagstr;
  n = sprintf(pstr, "%s %d [%d]: ", name, set->count, set->heldcount);
  pstr += n;
  for (i = 0; i < set->count; ++i)
  {
    char *notestr = getnotestring(set->notes[i].pitch, true);
    n = sprintf(pstr, "%s ", notestr);
    pstr += n;
    if (set->notes[i].latched)
    {
      if ((pstr - 1)[0] == ' ')
        pstr--;
      sprintf(pstr, "* ");
      pstr += 2;
    }
  }
  
  return diagstr;
}
