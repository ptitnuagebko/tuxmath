/***************************************************************************
pause.c 
-  description: pause screen for the games
-------------------
begin                : Jan 22, 2003
copyright            : (C) 2003 by Jesse Andrews
email                : jdandr2@uky.edu

    Modified for use in tuxmath by David Bruce - 2006.
    email                : <dbruce@tampabay.rr.com>
                           <tuxmath-devel@lists.sourceforge.net>
***************************************************************************/

/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

// For tuxtype-related stuff:
#include "titlescreen.h"

// For Opts_UsingSound()
#include "options.h"
#include "SDL_extras.h"

Mix_Chunk *pause_sfx;
SDL_Surface *up, *down, *left, *right;
SDL_Rect rectUp, rectDown, rectLeft, rectRight;
TTF_Font *f1, *f2;
extern settings localsettings;

void pause_load_media(void) {
	if (Opts_UsingSound()) 
		pause_sfx = LoadSound( "tock.wav" );

	up = LoadImage("up.png", IMG_ALPHA);
	rectUp.w = up->w; rectUp.h = up->h;

	down = LoadImage("down.png", IMG_ALPHA);
	rectDown.w = down->w; rectDown.h = down->h;

	left = LoadImage("left.png", IMG_ALPHA);
	rectLeft.w = left->w; rectLeft.h = left->h;

	right = LoadImage("right.png", IMG_ALPHA);
	rectRight.w = right->w; rectRight.h = right->h;

	f1 = LoadFont( ttf_font, 24 );
	f2 = LoadFont( ttf_font, 36 );
}

void pause_unload_media(void) {
	if (Opts_UsingSound())
		Mix_FreeChunk(pause_sfx);
	SDL_FreeSurface(up);
	SDL_FreeSurface(down);
	SDL_FreeSurface(left);
	SDL_FreeSurface(right);
	TTF_CloseFont(f1);
	TTF_CloseFont(f2);
}

void pause_draw_info(void) {
	SDL_Rect s;
	SDL_Surface *t;

	rectLeft.y = rectRight.y = 200;
	rectDown.y = rectUp.y = 300;

	rectLeft.x = rectDown.x = 320 - (7*16) - rectLeft.w - 4;
	rectRight.x = rectUp.x  = 320 + (7*16) + 4;

	if (Opts_UsingSound()) {

		SDL_BlitSurface(left, NULL, screen, &rectLeft);
		SDL_BlitSurface(right, NULL, screen, &rectRight);

		SDL_BlitSurface(down, NULL, screen, &rectDown);
		SDL_BlitSurface(up, NULL, screen, &rectUp);
	}

	if (Opts_UsingSound()) {

		t = BlackOutline(_("Sound Effects Volume"), f1, &white);
		s.y = 160;
		s.x = 320 - t->w/2;
		SDL_BlitSurface(t, NULL, screen, &s);
		SDL_FreeSurface(t);

		t = BlackOutline(_("Music Volume"), f1, &white);
		s.y = 260;
		s.x = 320 - t->w/2;
		SDL_BlitSurface(t, NULL, screen, &s);
		SDL_FreeSurface(t);

	} else {

		t = BlackOutline(_("Sound & Music Disabled"), f1, &white);
		s.y = 160;
		s.x = 320 - t->w/2;
		SDL_BlitSurface(t, NULL, screen, &s);
		SDL_FreeSurface(t);
	}

	t = BlackOutline(_("Paused!"), f2, &white);
	s.y = 60;
	s.x = 320 - t->w/2;
	SDL_BlitSurface(t, NULL, screen, &s);
	SDL_FreeSurface(t);

	t = BlackOutline(_("Press escape again to return to menu"), f1, &white);
	s.y = 400;
	s.x = 320 - t->w/2;
	SDL_BlitSurface(t, NULL, screen, &s);
	SDL_FreeSurface(t);

	t = BlackOutline(_("Press space bar to return to game"), f1, &white);
	s.y = 440;
	s.x = 320 - t->w/2;
	SDL_BlitSurface(t, NULL, screen, &s);
	SDL_FreeSurface(t);
}

void draw_vols(int sfx, int mus) {
	SDL_Rect s,m;
	int i;

	s.y = rectLeft.y; 
	m.y = rectDown.y;
	m.w = s.w = 5;
	s.x = rectLeft.x + rectLeft.w + 5;
	m.x = rectDown.x + rectDown.w + 5;
	m.h = s.h = 40;

	for (i = 1; i<=32; i++){
		if (sfx >= i*4)
			SDL_FillRect(screen, &s, SDL_MapRGB(screen->format, 0, 0, 127+sfx));
		else
			SDL_FillRect(screen, &s, SDL_MapRGB(screen->format, 0, 0, 0));

		if (mus >= i*4)
			SDL_FillRect(screen, &m, SDL_MapRGB(screen->format, 0, 0, 127+mus));
		else
			SDL_FillRect(screen, &m, SDL_MapRGB(screen->format, 0, 0, 0));

		m.x = s.x += 7;
	}
}

/* ==== fillscreen ====
 * RESULT: it will darken the screen by a factor of 4
 * WARNING: only works on 16bit screens right now!
 */
void darkenscreen( void ){
	Uint16 rm = screen->format->Rmask;
	Uint16 gm = screen->format->Gmask;
	Uint16 bm = screen->format->Bmask;
	Uint16 *p; 
	int x, y;

	p = screen->pixels;

	for (y = 0; y<480; y++) 
		for (x = 0; x<640; x++) {
			*p = (((*p&rm)>>2)&rm) | (((*p&gm)>>2)&gm) | (((*p&bm)>>2)&bm); p++;
		}
}

int inRect( SDL_Rect r, int x, int y) {
	if ((x < r.x) || (y < r.y) || (x > r.x + r.w) || (y > r.y + r.h))
		return 0;
	return 1;
}

// QUESTION: For usability sake, should escape return to the game
//           and the user have to choose to quit the game, or ???
/**********************
Pause : Pause the game
***********************/
int Pause( void ) {
	int paused = 1;
	int sfx_volume=0;
	int old_sfx_volume;
	int mus_volume=0;
	int old_mus_volume;
	int mousePressed = 0;
	int quit=0;
	int tocks=0;  // used for keeping track of when a tock has happened
	SDL_Event event;

	LOG( "---GAME PAUSED---\n" );

	/* --- stop all sounds, play pause noise --- */

	if (Opts_UsingSound()) {
 		Mix_Pause(-1);
		Mix_PlayChannel(-1, pause_sfx, 0);
		sfx_volume = Mix_Volume(-1, -1);  // get sfx volume w/o changing it
		mus_volume = Mix_VolumeMusic(-1); // get mus volume w/o changing it
	}

	/* --- show the pause screen --- */

	SDL_ShowCursor(1);

	// Darken the screen...
	darkenscreen(); 

	pause_draw_info();
	if (Opts_UsingSound()) {
		draw_vols(sfx_volume, mus_volume);
	}

	SDL_Flip(screen);

	SDL_EnableKeyRepeat( 1, 20 );

	/* --- wait for space, click, or exit --- */

	while (paused) {
		old_sfx_volume = sfx_volume;
		old_mus_volume = mus_volume;
		while (SDL_PollEvent(&event)) 
			switch (event.type) {
				case SDL_QUIT: 
					exit(0);
					break;
				case SDL_KEYUP:
					if (Opts_UsingSound() && 
					   ((event.key.keysym.sym == SDLK_RIGHT) ||
					    (event.key.keysym.sym == SDLK_LEFT))) 
					    	tocks = 0;
					break;
				case SDL_KEYDOWN:
					if (event.key.keysym.sym == SDLK_SPACE) 
						paused = 0;
					if (event.key.keysym.sym == SDLK_ESCAPE) {
						paused = 0;
						quit = 1;
					}
					if (Opts_UsingSound()) { 
						if (event.key.keysym.sym == SDLK_RIGHT) 
							sfx_volume += 4;
						if (event.key.keysym.sym == SDLK_LEFT) 
							sfx_volume -= 4;
						if (event.key.keysym.sym == SDLK_UP) 
							mus_volume += 4;
						if (event.key.keysym.sym == SDLK_DOWN) 
							mus_volume -= 4;
					}
					break;
				case SDL_MOUSEBUTTONDOWN:
					mousePressed = 1;
					tocks = 0;
					break;
				case SDL_MOUSEBUTTONUP:
					mousePressed = 0;
					break;

					break;
			}
		if (Opts_UsingSound() && mousePressed) {
			int x, y;

			SDL_GetMouseState(&x, &y);
			/* check to see if they clicked on a button */

			if (inRect(rectUp, x, y)) {
				mus_volume += 4;
			} else if (inRect(rectDown, x, y)) {
				mus_volume -= 4;
			} else if (inRect(rectRight, x, y)) {
				sfx_volume += 4;
			} else if (inRect(rectLeft, x, y)) {
				sfx_volume -= 4;
			} else {

				/* check to see if they clicked a bar */

				if ((x > rectLeft.x + rectLeft.w) && (x < rectRight.x)) {
					if ((y >= rectLeft.y) && (y <= rectLeft.y + rectLeft.h)) {
						sfx_volume = 4+(int)(128.0 * ((x - rectLeft.x - rectLeft.w - 1.0) / (rectRight.x - rectLeft.x - rectLeft.w - 2.0)));
					}
					if ((y >= rectDown.y) && (y <= rectDown.y + rectDown.h)) {
						mus_volume = 4+(int)(128.0 * ((x - rectLeft.x - rectLeft.w - 1.0) / (rectRight.x - rectLeft.x - rectLeft.w - 2.0)));
					}

				}
			}
		}

		if (Opts_UsingSound()) {

			if (sfx_volume > MIX_MAX_VOLUME)
				sfx_volume = MIX_MAX_VOLUME;
			if (sfx_volume < 0)
				sfx_volume = 0;
			if (mus_volume > MIX_MAX_VOLUME)
				mus_volume = MIX_MAX_VOLUME;
			if (mus_volume < 0)
				mus_volume = 0;

			if ((mus_volume != old_mus_volume) || 
			    (sfx_volume != old_sfx_volume)) {

				if (mus_volume != old_mus_volume)
					Mix_VolumeMusic(mus_volume);

				if (sfx_volume != old_sfx_volume) {
					Mix_Volume(-1,sfx_volume);
					if (tocks%4==0)
						Mix_PlayChannel(-1, pause_sfx, 0);
					tocks++;
			    }

				draw_vols(sfx_volume, mus_volume);
				localsettings.mus_volume=mus_volume;
				localsettings.sfx_volume=sfx_volume;
				SDL_Flip(screen);
			}
		}

		SDL_Delay(33);
	}

	/* --- Return to previous state --- */

	SDL_EnableKeyRepeat( 0, SDL_DEFAULT_REPEAT_INTERVAL );

	SDL_ShowCursor(0);

	if (Opts_UsingSound()) {
		Mix_PlayChannel(-1, pause_sfx, 0);
		Mix_Resume(-1);
	}

	LOG( "---GAME RESUMED---\n" );

	return (quit);
}

