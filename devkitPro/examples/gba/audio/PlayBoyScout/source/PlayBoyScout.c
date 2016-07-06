//---------------------------------------------------------------------------------
// GBA BoyScout sample code for devkitARM - http://www.devkit.tk
//---------------------------------------------------------------------------------
#include <stdlib.h>
#include <gba_video.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <pcx.h>
#include <fade.h>
#include <BoyScout.h>
//---------------------------------------------------------------------------------
// header for binary data generated by bin2o macro in makefile
//---------------------------------------------------------------------------------
#include "ScoutSplash_pcx.h"
#include "tune_bgf.h"
#define TUNE tune_bgf
//---------------------------------------------------------------------------------
// storage space for palette data
//---------------------------------------------------------------------------------
u16 PaletteBuffer[256];

unsigned int frame;

//---------------------------------------------------------------------------------
void VblankInterrupt()
//---------------------------------------------------------------------------------
{
	BoyScoutUpdateSong();
	frame += 1;
}


//---------------------------------------------------------------------------------
// Program entry point
//---------------------------------------------------------------------------------
int main(void)
//---------------------------------------------------------------------------------
{

	// Set up the interrupt handlers
	irqInit();

	unsigned int nBSSongSize;
	// Initialize BoyScout
	BoyScoutInitialize();

	// Get needed song memory
	nBSSongSize = BoyScoutGetNeededSongMemory(TUNE);

	// Allocate and set BoyScout memory area
	BoyScoutSetMemoryArea((unsigned int)malloc(nBSSongSize));

	// Open song
	BoyScoutOpenSong(TUNE);

	// Play song and loop
	BoyScoutPlaySong(1);

	irqSet( IRQ_VBLANK, VblankInterrupt);

	// Enable Vblank Interrupt to allow VblankIntrWait
	irqEnable(IRQ_VBLANK);
	// Allow Interrupts
	REG_IME = 1;

	SetMode( MODE_4 | BG2_ON );		// screen mode & background to display

	DecodePCX(ScoutSplash_pcx, (u16*)VRAM , PaletteBuffer);

	FadeToPalette( PaletteBuffer, 60);


	while (1)
	{
		VBlankIntrWait();
	}

	// This part will never be reached but just for completion
	// Free memory
	free((void *)BoyScoutGetMemoryArea());


}


