#pragma once

class noteset;

#define DBG(v) {Serial.print(#v " = ");Serial.println(v);}
#define DBG2(s,v) {Serial.print(#s " = ");Serial.println(v);}
#define DBG3(s,v) {Serial.print(#s ", " #v " = ");Serial.println(v);}
#define DBGLINE {Serial.println("----------------");}

char *getnotestring(unsigned char midipitch, bool inclNumber);

char *getnotesetstring(const char *name, noteset *set);
