#pragma once

#include <MIDI.h>

void setupMIDI();

void handlestoppednotes();

void notifyCycle(long curClock);

void sendNoteOn(byte channel, byte note, byte velocity);
void sendNoteOff(byte channel, byte note, byte velocity);
void sendPitchBend(byte channel, int bend);
void sendControlChange(byte channel, byte number, byte value);

void stoponechannel(byte channel, bool allsound);
void stopallnotes(bool allsound);
void stopcv();
void checkMIDI(long curClock);
              
bool isRecording();
void startRecordingSequence();
void endRecordingSequence(long curClock);

bool replayRunning();
void startReplay(long curClock);
void transposeReplay(byte offset);
int getTransposeOffset();
int getEventCount();
void endReplay();
bool isBypassNote(byte pitch, byte velocity, commonstate *common);
