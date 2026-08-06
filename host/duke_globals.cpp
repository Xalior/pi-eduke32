//
// duke_globals.cpp — global state upstream defines in a source file this
// build does not compile.
//
// mmulti.cpp is the Build engine's networking transport: raw BSD sockets,
// which this board has no stack for, and is left out of the source list
// entirely rather than compiled to nothing. But mmulti.h's five bookkeeping
// globals — myconnectindex, numplayers, networkmode, connecthead and
// connectpoint2 — are read and written throughout the single-player game
// code too, unconditionally, because upstream treats single-player as
// multiplayer with one participant rather than as a separate case. Every
// site that touches them already runs its own single-player initialization
// (network.cpp sets numplayers = 1 and myconnectindex = 0 during startup);
// this file only supplies the storage mmulti.cpp would otherwise own, with
// the same initial values mmulti.cpp itself gives them.
//
#include <mmulti.h>

int myconnectindex, numplayers, networkmode = -1;
int connecthead, connectpoint2[MAXMULTIPLAYERS];
