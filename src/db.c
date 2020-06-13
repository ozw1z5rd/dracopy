/* -*- c-basic-offset: 2; tab-width: 2; indent-tabs-mode: nil -*-
 * vi: set shiftwidth=2 tabstop=2 expandtab:
 * :indentSize=2:tabSize=2:noTabs=true:
 */
/** @file
 * \date 10.01.2009
 * \author bader
 *
 * DraBrowse (db*) is a simple file browser.
 *
 * Since both programs make use of kernal routines they shall
 * be able to work with most file oriented IEC devices.
 *
 * Created 2009 by Sascha Bader
 *
 * The code can be used freely as long as you retain
 * a notice describing original source and author.
 *
 * THE PROGRAMS ARE DISTRIBUTED IN THE HOPE THAT THEY WILL BE USEFUL,
 * BUT WITHOUT ANY WARRANTY. USE THEM AT YOUR OWN RISK!
 *
 * https://github.com/doj/dracopy
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <screen.h>
#include <conio.h>
#include "cat.h"
#include "dir.h"
#include "base.h"
#include "defines.h"
#include "ops.h"
#include "version.h"

extern BYTE devices[];
extern char linebuffer[];
extern Directory* dirs[];

static BYTE sorted = 0;

void
updateMenu(void)
{
	BYTE menuy=MENUY;

	revers(0);
	textcolor(DC_COLOR_TEXT);
	drawFrame(" " DRA_VERNUM " ",MENUX,MENUY,MENUW,MENUH,NULL);

	++menuy;
	cputsxy(MENUXT+1,++menuy,"F1 DIR");
	cputsxy(MENUXT+1,++menuy,"F2 DEVICE");
	cputsxy(MENUXT+1,++menuy,"F3 HEX");
	cputsxy(MENUXT+1,++menuy,"F4 ASC");
	cputsxy(MENUXT+1,++menuy,"CR RUN/CD");
	cputsxy(MENUXT+1,++menuy,"BS DIR UP");
	cputsxy(MENUXT+1,++menuy," T TOP");
	cputsxy(MENUXT+1,++menuy," B BOTTOM");
	cputsxy(MENUXT+1,++menuy," . ABOUT");
	cputsxy(MENUXT+1,++menuy," Q QUIT");
	cputsxy(MENUXT+1,++menuy," @ DOScmd");
	cputsxy(MENUXT+1,++menuy," S SORT");
	++menuy;
	gotoxy(MENUXT+1,++menuy);
	cprintf("Device:%02d",devices[0]);
}

void
mainLoop(void)
{
	Directory * cwd = NULL;
	DirElement * current = NULL;
	unsigned int pos = 0;
	BYTE c;
	BYTE lastpage = 0;
	BYTE nextpage = 0;
  BYTE context = 0;

  DIR1H = 23;
  DIR2H = 23;
	devices[0]=8;
	devices[1]=9;
	dirs[0]=readDir(NULL, devices[0], 0, sorted);
	dirs[1]=NULL;

	updateScreen(context, 1);
	while(1)
    {
#if 0
      {
        // TODO: find out where a file is left open, then remove this workaround block
        BYTE i;
        for(i = 0; i < 16; ++i)
          {
            cbm_close(i);
            cbm_closedir(i);
          }
      }
#endif
      c = cgetc();
    	switch (c)
      	{
        case 's':
          sorted = ! sorted;
          // fallthrough
        case '1':
        case CH_F1:
          textcolor(DC_COLOR_HIGHLIGHT);
					dirs[context]=readDir(dirs[context], devices[context], context, sorted);
					showDir(context, context);
					break;

        case '2':
        case CH_F2:
					if (++devices[context] > 11)
            devices[context]=8;
					freeDir(&dirs[context]);
					showDir(context, context);
					break;

        case '3':
        case CH_F3:
					cathex(devices[context],dirs[context]->selected->dirent.name);
					updateScreen(context, 1);
					break;

        case '4':
        case CH_F4:
					catasc(devices[context],dirs[context]->selected->dirent.name);
					updateScreen(context, 1);
					break;

		    case 't':
        case CH_HOME:
					cwd=GETCWD;
					cwd->selected=cwd->firstelement;
					cwd->pos=0;
					printDir(context, DIRX+1, DIRY);
          break;

		    case 'b':
					cwd=GETCWD;
					current = cwd->firstelement;
					pos=0;
					while (1)
            {
              if (current->next!=NULL)
                {
                  current=current->next;
                  pos++;
                }
              else
                {
                  break;
                }
            }
					cwd->selected=current;
					cwd->pos=pos;
					printDir(context, DIRX+1, DIRY);
					break;

		    case 'q':
          goto done;

        case '.':
					about("DraBrowse");
          updateScreen(context, 1);
          break;

        case '@':
          doDOScommand(context, sorted, 0);
          updateScreen(context, 1);
          break;

    		case CH_CURS_DOWN:
					cwd=GETCWD;
					if (cwd->selected!=NULL && cwd->selected->next!=NULL)
            {
              cwd->selected=cwd->selected->next;
              pos=cwd->pos;
              lastpage=pos/DIRH;
              nextpage=(pos+1)/DIRH;
              if (lastpage!=nextpage)
                {
                  cwd->pos++;
                  printDir(context, DIRX+1, DIRY);
                }
              else
                {
                  printElement(context, cwd, DIRX+1, DIRY);
                  cwd->pos++;
                  printElement(context, cwd, DIRX+1, DIRY);
                }

            }
          break;

    		case CH_CURS_UP:
					cwd=GETCWD;
					if (cwd->selected!=NULL && cwd->selected->prev!=NULL)
            {
              cwd->selected=cwd->selected->prev;
              pos=cwd->pos;
              lastpage=pos/DIRH;
              nextpage=(pos-1)/DIRH;
              if (lastpage!=nextpage)
                {
                  cwd->pos--;
                  printDir(context, DIRX+1, DIRY);
                }
              else
                {
                  printElement(context, cwd, DIRX+1, DIRY);
                  cwd->pos--;
                  printElement(context, cwd, DIRX+1, DIRY);
                }
            }
          break;

          // --- start / enter directory
		    case CH_ENTER:
					cwd=GETCWD;
					if (cwd->selected && cwd->selected->dirent.type==CBM_T_PRG)
            {
              execute(dirs[context]->selected->dirent.name,devices[context]);
            }
					// else fallthrough to CURS_RIGHT

    		case CH_CURS_RIGHT:
					cwd=GETCWD;
					if (cwd->selected)
            {
              changeDir(context, devices[context], cwd->selected->dirent.name, sorted);
            }
					break;

          // --- leave directory
        case CH_DEL:
    		case CH_CURS_LEFT:
          {
            char buf[2];
            buf[0] = CH_LARROW;
            buf[1] = 0;
            changeDir(context, devices[context], buf, sorted);
          }
					break;

        case CH_UARROW:
          changeDir(context, devices[context], NULL, sorted);
          break;
        }
    }

 done:;
  // nobody cares about freeing memory upon program exit.
#if 0
  freeDir(&dirs[0]);
#endif
}
