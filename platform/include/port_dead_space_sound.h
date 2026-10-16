#pragma once

// Dead Space Prime's own sound cues, played from WAV files the player supplies.
// Dead Space's sounds belong to EA, so none ship with the port: each cue is a
// file of that name in Folder(), taken from the player's own copy of the game.
// A cue without a file stays silent and Play says so, so the caller can fall
// back to one of Metroid Prime's sounds.
//
// The files are read with SDL_LoadWAV (PCM or ADPCM WAV, any rate, mono or
// stereo) and played through their own SDL audio stream, which SDL mixes with
// the game's. Main thread only.

#include <string>

namespace PortDeadSpaceSound {

enum class ECue : int {
  StasisFire, // the charge leaving the hand
  StasisHit,  // the charge catching its enemy
  Count,
};

// The cue's file name, e.g. "stasis_fire.wav".
const char* FileName(ECue cue);
// Where the files go: dead_space/ in the port's user folder (user\ beside the
// executable on Windows), ending in a separator.
std::string Folder();
// Looks in `folder` instead, and forgets what was loaded (for tests).
void SetFolder(const std::string& folder);

// Whether `cue` has a file that loaded. A missing or unreadable one is looked
// for again every few seconds, so a file added during play is picked up.
bool Available(ECue cue);
// Plays `cue` from its start at `gain` (1 is as recorded), cutting off its
// last play if that is still going. False when it has no file or there is no
// audio device.
bool Play(ECue cue, float gain);

// Stops and frees everything.
void Shutdown();

} // namespace PortDeadSpaceSound
