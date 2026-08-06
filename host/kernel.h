//
// kernel.h — the Circle kernel that hosts EDuke32.
//
// Device ownership: this kernel brings up interrupts, the timer, a serial
// console carrying stdio, the SD card (FatFs, holding the GRP and the
// configuration files) and the cooperative scheduler. Video, USB input and
// audio belong to circle-libsdl2 and are created inside SDL_Init when the
// game calls it. So do the CPU clock and the case fan, which the shim
// manages for its host; this kernel only says when they come up.
//
// Core roles, and the reason there are three of them:
//
//   core 0   the HARDWARE core. Circle's world lives here — scheduler,
//            interrupts, USB, the SD card, sound — and by construction no
//            other core may touch a device at all. It also runs the shim's
//            servo, which answers the other cores' marshalled calls.
//   core 1   the APPLICATION core. EDuke32, alone: the game loop and the
//            Build engine's classic renderer, and nothing else. Every
//            platform call it makes is marshalled back to core 0 by the
//            shim.
//   core 2   the elected PRESENTATION core. It consumes the shim's frame
//            mailbox: scale, compose, present. Electing it is this
//            kernel's decision, not the library's — the library never
//            starts a core.
//   core 3   parked.
//
// `rapi-split=0` in cmdline.txt collapses all of this back onto core 0, so
// the split and the single-core path can be measured against each other on
// one binary.
//
#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/input/console.h>
#include <circle/multicore.h>
#include <circle/memory.h>
#include <circle/types.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>
#include <SDL2/SDL_circle.h>

enum TShutdownMode
{
    ShutdownNone,
    ShutdownHalt,
    ShutdownReboot
};

// Secondary-core dispatch. Core 0 starts these and they take their roles
// from Run(), in kernel.cpp. A core that is handed no role parks: returning
// from Run() would leave it executing whatever follows.
class CSplitCores : public CMultiCoreSupport
{
public:
    CSplitCores(void) : CMultiCoreSupport(CMemorySystem::Get()) {}
    void Run(unsigned nCore) override;
};

class CKernel
{
public:
    CKernel(void);

    boolean Initialize(void);
    TShutdownMode Run(void);

private:
    // No CScreenDevice: the SDL window owns the display.
    CActLED             m_ActLED;
    CKernelOptions      m_Options;
    CDeviceNameService  m_DeviceNameService;
    CSerialDevice       m_Serial;
    CExceptionHandler   m_ExceptionHandler;
    CInterruptSystem    m_Interrupt;
    CTimer              m_Timer;
    CLogger             m_Logger;
    CScheduler          m_Scheduler;
    CEMMCDevice         m_EMMC;
    FATFS               m_FileSystem;
    CConsole            m_Console;
    // The shim's board hardware — the CPU clock and, where cmdline.txt names
    // a fan pin, the case fan. Declared here rather than left to SDL_Init so
    // the clock is already at maximum while this kernel builds its world: the
    // SD card, the filesystem and the console all come up before the game
    // does, and they come up at the speed the game will run at.
    CSDL2CircleHardware m_SDL2Hardware;
    CSplitCores         m_Cores;

    // Declared last so the cores are the last thing started and the first
    // thing that has a fully built world to run in.
    boolean             m_bSplit;        // cmdline rapi-split (default on)
};

#endif
