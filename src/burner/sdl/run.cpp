// Run module
#include "burner.h"

#include <sys/time.h>

static unsigned int nDoFPS = 0;
bool bAltPause = 0;

int bAlwaysDrawFrames = 0;

int counter;                                // General purpose variable used when debugging

static unsigned int nNormalLast = 0;        // Last value of timeGetTime()
static int          nNormalFrac = 0;        // Extra fraction we did

static bool bAppDoStep = 0;
bool        bAppDoFast = 0;
bool        bAppShowFPS = 0;
static int  nFastSpeed = 6;

UINT32 messageFrames = 0;
char lastMessage[MESSAGE_MAX_LENGTH];

/// Save States
#ifdef BUILD_SDL2
static char* szSDLSavePath = NULL;
#endif

int bDrvSaveAll = 0;

// The automatic save
int StatedAuto(int bSave)
{
	static TCHAR szName[MAX_PATH] = _T("");
	int nRet;

#ifdef BUILD_SDL2
	if (szSDLSavePath == NULL)
	{
		szSDLSavePath = SDL_GetPrefPath("fbneo", "states");
	}

	snprintf(szName, MAX_PATH, "%s%s.fs", szSDLSavePath, BurnDrvGetText(DRV_NAME));

#else

	_stprintf(szName, _T("config/games/%s.fs"), BurnDrvGetText(DRV_NAME));

#endif

	if (bSave == 0)
	{
		printf("loading state %i %s\n", bDrvSaveAll, szName);
		nRet = BurnStateLoad(szName, bDrvSaveAll, NULL);		// Load ram
		if (nRet && bDrvSaveAll)
		{
			nRet = BurnStateLoad(szName, 0, NULL);				// Couldn't get all - okay just try the nvram
		}
	}
	else
	{
		printf("saving state %i %s\n", bDrvSaveAll, szName);
		nRet = BurnStateSave(szName, bDrvSaveAll);				// Save ram
	}

	return nRet;
}


/// End Save States


static int GetInput(bool bCopy)
{
	InputMake(bCopy);                                // get input
	return 0;
}

char fpsstring[20];

static time_t fpstimer;
static unsigned int nPreviousFrames;

static void DisplayFPSInit()
{
	nDoFPS = 0;
	fpstimer = 0;
	nPreviousFrames = nFramesRendered;
}

static void DisplayFPS()
{
	time_t temptime = clock();
	double fps = (double)(nFramesRendered - nPreviousFrames) * CLOCKS_PER_SEC / (temptime - fpstimer);
	if (bAppDoFast) {
		fps *= nFastSpeed + 1;
	}
	if (fpstimer && temptime - fpstimer > 0) { // avoid strange fps values
		sprintf(fpsstring, "%2.2lf", fps);
	}

	fpstimer = temptime;
	nPreviousFrames = nFramesRendered;
}


//crappy message system
void UpdateMessage(char* message)
{
	snprintf(lastMessage, MESSAGE_MAX_LENGTH, "%s", message);
	messageFrames = MESSAGE_MAX_FRAMES;
}

// define this function somewhere above RunMessageLoop()
void ToggleLayer(unsigned char thisLayer)
{
	nBurnLayer ^= thisLayer;                         // xor with thisLayer
	VidRedraw();
	VidPaint(0);
}


struct timeval start;

unsigned int timeGetTime(void)
{
	unsigned int ticks;
	struct timeval now;
	gettimeofday(&now, NULL);
	ticks = (now.tv_sec - start.tv_sec) * 1000000 + now.tv_usec - start.tv_usec;
	return ticks;
}


// With or without sound, run one frame.
// If bDraw is true, it's the last frame before we are up to date, and so we should draw the screen
static int RunFrame(int bDraw, int bPause)
{
	static int bPrevPause = 0;
	static int bPrevDraw = 0;

	if (bPrevDraw && !bPause)
	{
		VidPaint(0);                                              // paint the screen (no need to validate)
	}

	if (!bDrvOkay)
	{
		return 1;
	}

	if (bPause)
	{
		GetInput(false);                                          // Update burner inputs, but not game inputs
		if (bPause != bPrevPause)
		{
			VidPaint(2);                                                   // Redraw the screen (to ensure mode indicators are updated)
		}
	}
	else
	{
		nFramesEmulated++;
		nCurrentFrame++;
		GetInput(true);                                   // Update inputs
	}
	if (bDraw)
	{
		nFramesRendered++;
		if (VidFrame())
		{                                     // Do one frame
			AudBlankSound();
		}
	}
	else
	{                                       // frame skipping
		pBurnDraw = NULL;                    // Make sure no image is drawn
		BurnDrvFrame();
	}

	if (bAppShowFPS) {
		if (nDoFPS < nFramesRendered) {
			DisplayFPS();
			nDoFPS = nFramesRendered + 30;
		}
	}

	bPrevPause = bPause;
	bPrevDraw = bDraw;

	return 0;
}

// Callback used when DSound needs more sound
static int RunGetNextSound(int bDraw)
{
	if (nAudNextSound == NULL)
	{
		return 1;
	}

	if (bRunPause)
	{
		if (bAppDoStep)
		{
			RunFrame(bDraw, 0);
			memset(nAudNextSound, 0, nAudSegLen << 2);                                        // Write silence into the buffer
		}
		else
		{
			RunFrame(bDraw, 1);
		}

		bAppDoStep = 0;                                                   // done one step
		return 0;
	}

	if (bAppDoFast)
	{                                            // do more frames
		for (int i = 0; i < nFastSpeed; i++)
		{
			RunFrame(0, 0);
		}
	}

	// Render frame with sound
	pBurnSoundOut = nAudNextSound;
	RunFrame(bDraw, 0);
	if (bAppDoStep)
	{
		memset(nAudNextSound, 0, nAudSegLen << 2);                // Write silence into the buffer
	}
	bAppDoStep = 0;                                              // done one step

	return 0;
}

int RunIdle()
{
	int nTime, nCount;

	if (bAudPlaying)
	{
		// Run with sound
		AudSoundCheck();
		return 0;
	}

	// Run without sound
	nTime = timeGetTime() - nNormalLast;
	nCount = (nTime * nAppVirtualFps - nNormalFrac) / 100000;
	if (nCount <= 0) {						// No need to do anything for a bit
		//Sleep(2);
		return 0;
	}

	nNormalFrac += nCount * 100000;
	nNormalLast += nNormalFrac / nAppVirtualFps;
	nNormalFrac %= nAppVirtualFps;

	if (nCount > 100) {						// Limit frame skipping
		nCount = 100;
	}
	if (bRunPause) {
		if (bAppDoStep) {					// Step one frame
			nCount = 10;
		}
		else {
			RunFrame(1, 1);					// Paused
			return 0;
		}
	}
	bAppDoStep = 0;


	if (bAppDoFast)
	{									// do more frames
		for (int i = 0; i < nFastSpeed; i++)
		{
			RunFrame(0, 0);
		}
	}

	if (!bAlwaysDrawFrames)
	{
		for (int i = nCount / 10; i > 0; i--)
		{              // Mid-frames
			RunFrame(0, 0);
		}
	}
	RunFrame(1, 0);                                  // End-frame
	// temp added for SDLFBA
	//VidPaint(0);
	return 0;
}

int RunReset()
{
	// Reset the speed throttling code
	nNormalLast = 0; nNormalFrac = 0;
	if (!bAudPlaying)
	{
		// run without sound
		nNormalLast = timeGetTime();
	}
	return 0;
}

int RunInit()
{
	gettimeofday(&start, NULL);
	DisplayFPSInit();
	// Try to run with sound
	AudSetCallback(RunGetNextSound);
	AudSoundPlay();

	RunReset();
	StatedAuto(0);
	return 0;
}

int RunExit()
{
	nNormalLast = 0;
	StatedAuto(1);
	return 0;
}

#ifndef BUILD_MACOS
// The main message loop
int RunMessageLoop()
{
	int quit = 0;

	RunInit();
	GameInpCheckMouse();                                                                     // Hide the cursor

	while (!quit)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT:                                        /* Windows was closed */
				quit = 1;
				break;

			case SDL_KEYDOWN:                                                // need to find a nicer way of doing this...
				switch (event.key.keysym.sym)
				{
				case SDLK_F1:
					bAppDoFast = 1;
					break;
				case SDLK_F9:
					QuickState(0);
					break;
				case SDLK_F10:
					QuickState(1);
					break;
				case SDLK_F11:
					bAppShowFPS = !bAppShowFPS;
					break;

				default:
					break;
				}
				break;

			case SDL_KEYUP:                                                // need to find a nicer way of doing this...
				switch (event.key.keysym.sym)
				{
				case SDLK_F1:
					bAppDoFast = 0;
					break;

				case SDLK_F12:
					quit = 1;
					break;

				default:
					break;
				}
				break;
			}
		}
		RunIdle();
	}

	RunExit();

	return 0;
}

#endif
