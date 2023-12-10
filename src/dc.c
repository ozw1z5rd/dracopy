/* -*- c-basic-offset: 2; tab-width: 2; indent-tabs-mode: nil -*-
 * vi: set shiftwidth=2 tabstop=2 expandtab:
 * :indentSize=2:tabSize=2:noTabs=true:
 */
/** @file
 * \date 10.01.2009
 * \author bader
 *
 * DraCopy (dc*) is a simple copy program.
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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <screen.h>
#include <conio.h>
#include <cbm.h>
#include <errno.h>
#include "cat.h"
#include "dir.h"
#include "base.h"
#include "defines.h"
#include "version.h"
#include "ops.h"
#if defined(REU)
#include <em.h>
#endif

#if defined(KERBEROS)
#include <peekpoke.h>
#define REU 1
#define em_pagecount() 512u
static void*
em_use(unsigned page)
{
  POKEW(0xDE3E,page);
  return (void*)0xDF00;
}
static void*
em_map(unsigned page)
{
  POKEW(0xDE3E,page);
  return (void*)0xDF00;
}
#define em_commit()
#endif

#if defined(REU)
void readFile(const BYTE context);
BYTE writeFile(const BYTE context);
static unsigned long cachedFileSize = 0;
static char cachedFileName[16+2+1];
#ifdef CHAR80
#define REUMENUX MENUXT-1
#else
#define REUMENUX MENUXT
#endif
#endif

/* declarations */
BYTE really(void);
void doRenameOrCopy(const BYTE context, const BYTE mode);
void doToggleAll(const BYTE context);
void doCopy(const BYTE context);
void doDelete(const BYTE context);
int doDiskCopy(const BYTE deviceFrom, const BYTE deviceTo, const BYTE optimized);
int copy(const char *srcfile, const BYTE srcdevice, const char *destfile, const BYTE destdevice, BYTE type);
void doMakeImage(const BYTE device);
void doRelabel(const BYTE device);
void nextWindowState(const BYTE context);
void updateMenu();

/* definitions */
#if !defined(CHAR80)
static BYTE windowState = 0;
#endif
static BYTE sorted = 0;

void
nextWindowState(const BYTE context)
{
#if !defined(CHAR80)
  if (context == 1 &&
      windowState == 0)
    {
      windowState = 1;
    }

  initDirWindowHeight();
  switch(++windowState)
    {
    default:
    case 0:
      windowState = 0;
      break;
    case 1:
      DIR1H += DIR2H - 2;
      DIR2H = 2;
      break;
    case 2:
      DIR2H += DIR1H - 2;
      DIR1H = 2;
      break;
    }

  showDir(0, context);
  showDir(1, context);
#else
  (void)context;
#endif
}

void
updateMenu()
{
  BYTE menuy=MENUY;

  revers(0);
  textcolor(DC_COLOR_TEXT);
  drawFrame(" " DRA_VERNUM " ",MENUX,MENUY,MENUW,MENUH,NULL);

  cputsxy(MENUXT,++menuy,"F1 READ DIR");
  cputsxy(MENUXT,++menuy,"F2 DEVICE");
  cputsxy(MENUXT,++menuy,"F3 VIEW HEX");
  cputsxy(MENUXT,++menuy,"F4 VIEW ASC");
  cputsxy(MENUXT,++menuy,"F5 COPY");
  cputsxy(MENUXT,++menuy,"F6 DELETE");
  cputsxy(MENUXT,++menuy,"F7 RUN");
  cputsxy(MENUXT,++menuy,"F8 DISKCOPY");
  cputsxy(MENUXT,++menuy,"CR CHG DIR");
  cputsxy(MENUXT,++menuy,"BS DIR UP");
#ifdef __PLUS4__
  cputsxy(MENUXT,++menuy,"EC SWITCH W");
#else
  cputsxy(MENUXT,++menuy," \xff SWITCH W");
#endif
  cputsxy(MENUXT,++menuy,"SP SELECT");
  cputsxy(MENUXT,++menuy," * INV SEL");
  cputsxy(MENUXT,++menuy," S SORT DIR");
  cputsxy(MENUXT,++menuy," R RENAME");
  cputsxy(MENUXT,++menuy," M MAKE DIR");
  cputsxy(MENUXT,++menuy," F FORMAT");
  cputsxy(MENUXT,++menuy," L RELABEL");
  cputsxy(MENUXT,++menuy," \xfc DEV ID");
  cputsxy(MENUXT,++menuy," @ DOS CMD");
  cputsxy(MENUXT,++menuy," I MAKE IMG");
  cputsxy(MENUXT,++menuy," D DSKCPY-O");
#if defined(CHAR80)
  cputsxy(MENUXT,++menuy," . HELP");
#else
  cputsxy(MENUXT,++menuy," W WIN SIZE");
#endif
}

void
mainLoop(void)
{
  Directory * cwd = NULL;
  DirElement * current = NULL;
  unsigned int pos = 0;
  BYTE lastpage = 0;
  BYTE nextpage = 0;
  BYTE context = 0;
  BYTE i;

  initDirWindowHeight();

  dirs[0] = dirs[1] = NULL;
  updateScreen(context, 2);

  for(i = 0; i < 16; ++i)
    {
      cbm_close(i);
      cbm_closedir(i);
    }

  // find the first drive for the upper/left window
  textcolor(DC_COLOR_HIGHLIGHT);
  i = 7;
  while(++i < 12)
    {
      devices[0] = i;
      dirs[0] = readDir(NULL, devices[0], 0, sorted);
      if (dirs[0])
        {
          getDeviceType(devices[0]);
          showDir(0, context);
          break;
        }
    }

  // find the next drive for the lower/right window
  textcolor(DC_COLOR_TEXT);
  while(++i < 12)
    {
      devices[1] = i;
      dirs[1] = readDir(NULL, devices[1], 1, sorted);
      if (dirs[1])
        {
          getDeviceType(devices[1]);
          showDir(1, context);
          goto found_lower_drive;
        }
    }

  // if no drive was found for the lower window,
  // enlarge the upper window.
  nextWindowState(context);

 found_lower_drive:
  while(1)
    {
      {
        size_t s = _heapmemavail();
        if (s < 0x1000)
          {
            gotoxy(MENUXT,BOTTOM);
            textcolor(DC_COLOR_HIGHLIGHT);
            cprintf("lowmem:%04x",s);
            textcolor(DC_COLOR_TEXT);
          }
#if defined(REU)
        else
          {
            gotoxy(REUMENUX,BOTTOM);
            if (cachedFileSize)
              textcolor(DC_COLOR_HIGHLIGHT);
            cprintf("GEO:%lu/%lu", cachedFileSize/1024ul, em_pagecount()*256ul/1024ul);
            textcolor(DC_COLOR_TEXT);
          }
#endif
      }

      // use define to make an alias variable
#define key_pressed i
      key_pressed = cgetc();
      switch (key_pressed)
        {
        case 's':
          sorted = ! sorted;
          // fallthrough
        case '1':
        case CH_F1:
          textcolor(DC_COLOR_HIGHLIGHT);
          dirs[context] = readDir(dirs[context], devices[context], context, sorted);
          showDir(context, context);
          break;

        case '2':
        case CH_F2:
          // cycle through devices until we find the next one, not
          // used by the other context.
          do
            {
              if (++devices[context] > 11)
                devices[context] = 8;
            }
          while(devices[context] == devices[context^1]);
          freeDir(&dirs[context]);
          if (! devicetype[devices[context]])
            {
              getDeviceType(devices[context]);
            }
          showDir(context, context);
          break;

        case '3':
        case CH_F3:
          cathex(devices[context],dirs[context]->selected->dirent.name);
          updateScreen(context, 2);
          break;

        case '4':
        case CH_F4:
          catasc(devices[context],dirs[context]->selected->dirent.name);
          updateScreen(context, 2);
          break;

        case '5':
        case CH_F5:
          {
            const BYTE other_context = context^1;
            freeDir(&dirs[other_context]);
            doCopy(context);
            updateScreen(context, 2);
            // refresh destination dir
            dirs[other_context] = readDir(dirs[other_context], devices[other_context], other_context, sorted);
            showDir(other_context, context);
          }
          break;

        case '6':
        case CH_F6:
          doDelete(context);
          break;

        case '7':
        case CH_F7:
        execute_prg:
          cwd = GETCWD;
          if (cwd->selected && cwd->selected->dirent.type == CBM_T_PRG)
            {
              execute(cwd->selected->dirent.name, devices[context]);
            }
          break;

        case '8':
        case CH_F8:
        case 'd':
          {
            const BYTE other_context = context^1;
            freeDir(&dirs[other_context]);
            doDiskCopy(devices[context], devices[other_context], key_pressed == 'd');
            updateScreen(context, 2);
            refreshDir(other_context, sorted, context);
          }
          break;

          // ----- switch context -----
        case '0':
        case CH_ESC:
        case CH_LARROW:  // arrow left
          {
            const BYTE prev_context = context;
            context = context ^ 1;
            drawDirFrame(context, context);
            drawDirFrame(prev_context, context);
          }
          break;

        case 't':
        case CH_HOME:
          cwd=GETCWD;
          cwd->selected=cwd->firstelement;
          cwd->pos=0;
          printDir(context, DIRX+1, DIRY);
          break;

        case 'b':
          cwd = GETCWD;
          current = cwd->firstelement;
          while (1)
            {
              if (current->next!=NULL)
                {
                  current=current->next;
                  ++pos;
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

        case 'n':
          cwd = GETCWD;
          current = cwd->selected;
          for(pos = 0; pos < (context ? DIR2H : DIR1H) && current->next; ++pos)
            {
              current = current->next;
            }
          cwd->pos += pos;
          cwd->selected = current;
          printDir(context, DIRX+1, DIRY);
          break;

        case 'p':
          cwd = GETCWD;
          current = cwd->selected;
          for(pos = 0; pos < (context ? DIR2H : DIR1H) && current->prev; ++pos)
            {
              current = current->prev;
            }
          assert(cwd->pos >= pos);
          cwd->pos -= pos;
          cwd->selected = current;
          printDir(context, DIRX+1, DIRY);
          break;

        case 'q':
          goto done;
          break;

        case ' ':
          cwd=GETCWD;
          cwd->selected->flags=!cwd->selected->flags;

          // go to next entry
          if (cwd->selected->next!=NULL)
            {
              cwd->selected=cwd->selected->next;
              cwd->pos++;
            }
          showDir(context, context);
          break;

        case 'f':
          strcpy(linebuffer, "n:");
          doDOScommand(context, sorted, 1, "format disk");
          updateScreen(context, 2);
          break;

        case 'r':
          doRenameOrCopy(context, 0);
          break;

        case 'm':
          strcpy(linebuffer, "md:");
          doDOScommand(context, sorted, 1, "make directory");
          updateScreen(context, 2);
          break;

        case '*':
          doToggleAll(context);
          break;

        case '.':
          about("DraCopy");
          updateScreen(context, 2);
          break;

        case '@':
          doDOScommand(context, sorted, 0, "DOS command");
          updateScreen(context, 2);
          break;

        case 'c':
          doRenameOrCopy(context, 1);
          break;

        case 'i':
          doMakeImage(devices[context]);
          updateScreen(context, 2);
          refreshDir(context, sorted, context);
          break;

        case 'l':
          doRelabel(devices[context]);
          updateScreen(context, 2);
          refreshDir(context, sorted, context);
          break;

        case CH_POUND:
          changeDeviceID(devices[context]);
          updateScreen(context, 2);
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

          // --- enter directory
        case CH_ENTER:
        case CH_CURS_RIGHT:
          cwd=GETCWD;
          if (cwd->selected)
            {
              if (changeDir(context, devices[context], cwd->selected->dirent.name, sorted) != 0)
                {
                  if (key_pressed == CH_ENTER)
                    {
                      // enter directory failed, try to execute a PRG
                      goto execute_prg;
                    }
                }
            }
          break;

          // --- leave directory
        case CH_DEL:
        case CH_CURS_LEFT:
          {
            char buf[2];
            buf[0] = CH_LARROW; // arrow left
            buf[1] = 0;
            changeDir(context, devices[context], buf, sorted);
          }
          break;

        case CH_UARROW:
          changeDir(context, devices[context], NULL, sorted);
          break;

        case 'w':
          nextWindowState(context);
          break;

#if defined(REU)
        case 'z':
          readFile(context);
          updateScreen(context, 2);
          break;
        case 'x':
          if (writeFile(context))
            {
              updateScreen(context, 2);
              refreshDir(context, sorted, context);
            }
          break;
#endif
        }
    }

 done:;
  // nobody cares about freeing memory upon program exit.
#if 0
  freeDir(&dirs[0]);
  freeDir(&dirs[1]);
#endif

#undef key_pressed
}

void
doCopy(const BYTE context)
{
  BYTE flag = 0;
  BYTE cnt = 0xf0;
  DirElement * current;
  const BYTE srcdev = devices[context];
  const BYTE destdev = devices[context^1];
  Directory * cwd = GETCWD;
  int ret;

  sprintf(linebuffer,"Filecopy from %i to %i",srcdev,destdev);
  for(current = cwd->firstelement; current; current=current->next)
    {
      if (++cnt >= BOTTOM)
        {
          cnt = 0;
          newscreen(linebuffer);
        }
      if (current->flags==1)
        {
          flag = 1;
          ret = copy(current->dirent.name, srcdev, current->dirent.name, destdev, current->dirent.type);
          if (ret == OK)
            {
              // deselect
              current->flags=0;
            }
          else if (ret == ABORT)
            {
              return;
            }
        }
    }

  if (! flag)
    {
      current = cwd->selected;
      if (current)
        {
          ret = copy(current->dirent.name, srcdev, current->dirent.name, destdev, current->dirent.type);
          if (ret == ERROR)
            {
              cputc(CH_ENTER);
              cputc(10);
              waitKey(0);
            }
        }
    }
}

void
doToggleAll(const BYTE context)
{
  DirElement * current;
  if (dirs[context]==NULL)
    {
      //cputs("no directory");
      return;
    }
  else
    {
      current = dirs[context]->firstelement;
      while (current!=NULL)
        {
          current->flags=1-(current->flags);
          current=current->next;
        }
      showDir(context, context);
    }
}

BYTE
really(void)
{
  char c;
  cputs("Really (Y/N)? ");
  c = cgetc();
  cputc(c);
  cputs("\n\r");
  return (c=='y' || c=='Y');
}

void
doDelete(const BYTE context)
{
  DirElement * current;
  int idx = 0;
  Directory * cwd = GETCWD;
  int ret;

  if (dirs[context]==NULL)
    return;

  for(current = dirs[context]->firstelement; current; current=current->next)
    {
      if (current->flags)
        ++idx;
    }

  if (idx == 0)
    {
      cwd->selected->flags = 1;
      idx = 1;
    }

  sprintf(linebuffer, "Delete %i files from %i", idx, devices[context]);
  newscreen(linebuffer);
  if (! really())
    {
      updateScreen(context, 2);
      return;
    }

  idx = 3;
  for(current = dirs[context]->firstelement; current; current=current->next)
    {
      if (! current->flags)
        continue;

      gotoxy(0,idx);
      cprintf("%s.%s ", current->dirent.name, fileTypeToStr(current->dirent.type));

      sprintf(linebuffer2,
              "%s:%s",
              (current->dirent.type==CBM_T_DIR) ? "rd" : "s", // TODO: when is rd required?
              current->dirent.name);
      ret = cmd(devices[context], linebuffer2);
      cputc(' ');
      revers(1);
      if (ret == 1)
        {
          cputs("DELETED");
        }
      else
        {
          textcolor(DC_COLOR_ERROR);
          cputs("ERROR");
          break;
        }
      textcolor(DC_COLOR_TEXT);
      revers(0);

      if (++idx > BOTTOM)
        {
          newscreen(linebuffer);
          idx = 1;
        }
    }

  updateScreen(context, 2);
  // refresh directories
  cwd = readDir(cwd, devices[context], context, sorted);
  dirs[context]=cwd;
  cwd->selected=cwd->firstelement;
  showDir(context, context);
#if 0
  if (devices[0] == devices[1])
    {
      const BYTE other_context = context^1;
      dirs[other_context] = cwd;
      showDir(other_context, context);
    }
#endif
}

void
doRenameOrCopy(const BYTE context, const BYTE mode)
{
  int n;
  Directory * cwd = GETCWD;

  if (cwd->selected == NULL)
    return;

  sprintf(linebuffer,
          "%s file %s on device %i",
          mode ? "Copy" : "Rename",
          cwd->selected->dirent.name,devices[context]);
  newscreen(linebuffer);
  cputs("\n\rNew name: ");
  strcpy(linebuffer2, cwd->selected->dirent.name);
  n = textInput(10,2,linebuffer2,16+3);
  if (n > 0 && strcmp(linebuffer2,cwd->selected->dirent.name))
    {
      cputs("\n\rWorking...");
      sprintf(linebuffer,
              "%c:%s=%s",
              mode ? 'c' : 'r',
              linebuffer2,
              cwd->selected->dirent.name);
      if(cmd(devices[context],linebuffer)==OK)
        {
          dirs[context] = readDir(dirs[context], devices[context], context, sorted);
        }
      else
        {
          cputs("ERROR\n\r\n\r");
          waitKey(0);
        }
    }
  updateScreen(context, 2);
}

static void
copyERRMSG(const BYTE color, const char *msg)
{
  cputc(' ');
  revers(1);
  textcolor(color);
  cputs(msg);
  textcolor(DC_COLOR_TEXT);
  revers(0);
  cputs("\r\n");
}

int
copy(const char *srcfile, const BYTE srcdevice, const char *destfile, const BYTE destdevice, BYTE type)
{
  int length=0;
  int ret = OK;
  unsigned long total_length = 0;
  BYTE xpos, ypos;

  BYTE *buf = (BYTE*) malloc(BUFFERSIZE);
  if (! buf)
    {
#if !defined(__PET__)
      cputs("Can't alloc\n\r");
#endif
      return ERROR;
    }

  switch(type)
    {
    case _CBM_T_SEQ:
      type = 's';
      break;
    case _CBM_T_PRG:
      type = 'p';
      break;
    case _CBM_T_USR:
      type = 'u';
      break;
    default:
#if !defined(__PET__)
      cputs("unsupported file type\n\r");
#endif
      ret = ERROR;
      goto done;
    }

  sprintf(linebuffer, "%s,%c", srcfile, type);
  if (cbm_open(6, srcdevice, CBM_READ, linebuffer) != 0)
    {
#if !defined(__PET__)
      cputs("Can't open input file!\n\r");
#endif
      ret = ERROR;
      goto done;
    }

  // create destination string with filetype like "FILE,P"
  sprintf(linebuffer, "%s,%c", destfile, type);
  if (cbm_open(7, destdevice, CBM_WRITE, linebuffer) != 0)
    {
#if !defined(__PET__)
      cputs("Can't open output file\n\r");
#endif
      ret = ERROR;
      goto done;
    }

  cprintf("%-16s:",srcfile);
  xpos = wherex();
  ypos = wherey();
  while(1)
    {
      if (kbhit())
        {
          char c = cgetc();
          if (c == CH_ESC || c == CH_LARROW)
            {
              ret = ABORT;
              break;
            }
        }

      cputsxy(xpos, ypos, "R");
      length = cbm_read (6, buf, BUFFERSIZE);
      if (length < 0)
        {
          copyERRMSG(DC_COLOR_ERROR,"READ ERROR");
          ret = ERROR;
          break;
        }

      if (kbhit())
        {
          char c = cgetc();
          if (c == CH_ESC || c == CH_LARROW)
            {
              ret = ABORT;
              copyERRMSG(DC_COLOR_WARNING,"ABORT");
              break;
            }
        }

      if (length > 0)
        {
          cputsxy(xpos, ypos, "W");
          if (cbm_write(7, buf, length) != length)
            {
              copyERRMSG(DC_COLOR_ERROR,"WRITE ERROR");
              ret = ERROR;
              break;
            }
          total_length += length;
#if !defined(__PET__)
          cprintf(" %lu bytes", total_length);
#endif
        }

      if (length < BUFFERSIZE)
        {
          copyERRMSG(DC_COLOR_TEXT,"OK");
          break;
        }
    } // while(1)

 done:
  free(buf);
  cbm_close(6);
  cbm_close(7);
  return ret;
}

/*
  int printErrorChannel(BYTE device)
  {
  unsigned char buf[128];
  unsigned char msg[100]; // should be large enough for all messages
  unsigned char e, t, s;

  if (cbm_open(15, device, 15, "") == 0)
  {
  if (cbm_read(1, buf, sizeof(buf)) < 0) {
  puts("can't read error channel");
  return(1);
  }
  cbm_close(15);
  }
  if (  sscanf(buf, "%hhu, %[^,], %hhu, %hhu", &e, msg, &t, &s) != 4) {
  puts("parse error");
  puts(buf);
  return(2);
  }
  printf("%hhu,%s,%hhu,%hhu\n", (int) e, msg, (int) t, (int) s);
  return(0);
  }
*/

const BYTE sectors1541[42] = {
  21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
  19, 19, 19, 19, 19, 19, 19,
  18, 18, 18, 18, 18, 18,
  17, 17, 17, 17, 17,
  17, 17, 17, 17, 17, // track 36-40
  17, 17 // track 41-42
};

BYTE
sectors1571(const BYTE t)
{
  if (t < 35)
    return sectors1541[t];
  if (t < 70)
    return sectors1541[t-35];
  return 0;
}

#if defined(SFD1001)
BYTE
sectors1001(const BYTE t)
{
  if (t < 39)
    return 29;
  if (t < 53)
    return 27;
  if (t < 64)
    return 25;
  if (t < 77)
    return 23;

  if (t < 116)
    return 29;
  if (t < 130)
    return 27;
  if (t < 141)
    return 25;
  if (t < 154)
    return 23;

  return 0;
}
#endif

BYTE sectorBuf[256];

BYTE
IS_1541(const BYTE dt)
{
  return
    dt == D1541 ||
    dt == D1540 ||
    dt == D1551 ||
    dt == D1570 ||
    dt == D2031 ||
    dt == D8040;
}

BYTE
maxTrack(BYTE dt)
{
  if (IS_1541(dt))
    return 35;//42;
#if !defined(__PET__)
  if (dt == D1571)
    return 70;
  if (dt == D1581)
    return 80;
#endif
#if defined(SFD1001)
  if (dt == D1001)
    return 154;
#endif
  return 0;
}

BYTE
maxSector(BYTE dt, BYTE t)
{
  if (IS_1541(dt))
    return sectors1541[t];
#if !defined(__PET__)
  if (dt == D1571)
    return sectors1571(t);
  if (dt == D1581)
    return 40;
#endif
#if defined(SFD1001)
  if (dt == D1001)
    return sectors1001(t);
#endif
  return 0;
}

void
printSecStatus(BYTE dt, BYTE t, BYTE s, BYTE st)
{
  switch(st)
    {
    case '!':
    case 'e':
    case 'E': textcolor(DC_COLOR_ERROR); break;
    case 'O': textcolor(DC_COLOR_GRAY); break;
    default: textcolor(DC_COLOR_GRAYBRIGHT);
    }

#if defined(SFD1001)
  if (dt == D1001)
    {
#if defined(CHAR80)
      if (t >= 77)
        {
          textcolor(DC_COLOR_HIGHLIGHT);
          t -= 77;
        }
      if ((s&1) == 0)
        {
          if (st == 'R') st = 'r';
          else if (st == 'W') st = 'w';
        }
      s >>= 1;
#else
      const BYTE tt = (t % 23) + 1;
      if (s == 0)
        {
          gotoxy(0,tt);
          cprintf("%3u", t);
          cclearxy(5,tt,30);
        }
      cputcxy(s+4,tt,st);
      return;
#endif
    }
#endif

  if (IS_1541(dt))
    {
      if (t >= 35)
        textcolor(DC_COLOR_GRAY);
      if (t >= 40)
        t = 39;
    }
#if !defined(__PET__)
#if !defined(CHAR80)
  else if (dt == D1571 && t >= 35)
    {
      t -= 35;
      textcolor(DC_COLOR_HIGHLIGHT);
    }
#endif
  else if (dt == D1581)
    {
#if !defined(CHAR80)
      if (t >= 40)
        {
          textcolor(DC_COLOR_HIGHLIGHT);
          t -= 40;
        }
#endif
      if ((s&1) == 0)
        {
          if (st == 'R') st = 'r';
          else if (st == 'W') st = 'w';
        }
      s >>= 1;
    }
#endif // PET
  cputcxy(t, 3+s, st);
}

#if defined(REU)
/// @return disk image size in bytes of a device type @p dt.
/// @return 0 if @p dt is not a disk image type.
unsigned long
diskImageSize(const BYTE dt)
{
  if (IS_1541(dt))
    return 174848UL;
  if (dt == D1571)
    return 349696UL;
  if (dt == D1581)
    return 819200UL;
  if (dt == D1001)
    return 1066496UL;
  return 0;
}
#endif

/// check if a drive type @p dt is a device that can have multiple
/// sizes (emulates multiple Commodore drives).
/// @param dt drive type, which should be from enum drive_e
/// @return 1 if @p dt is a drive which can emulate different Commodore drives with different sizes.
/// @return 0 otherwise.
static BYTE
isMultiType(const BYTE dt)
{
  return dt == SD2IEC ||
    dt == CMD ||
    dt == U64;
}

static char * optimized_str = "optimized";

static void
diskCopyTitle(const BYTE optimized, const BYTE yesno, const BYTE use_reu, const BYTE deviceFrom, const BYTE deviceTo)
{
#if defined(REU)
  if (use_reu)
    {
      sprintf(linebuffer, "%s diskcopy from GEO to %i? (Y/N)",
              optimized ? optimized_str : "",
              deviceTo);
    }
  else
#else
    (void)use_reu;
#endif
    {
      sprintf(linebuffer, "%s diskcopy from %i to %i? %s",
              optimized ? optimized_str : "",
              deviceFrom,
              deviceTo,
              yesno ? "(Y/N)" : ""
              );
    }
  newscreen(linebuffer);
}

// the 128KB RAM of the Kerberos interface is too small for a disk image.
// If the Kerberos emd driver is used, the REU feature for diskcopy would
// still be enabled.
#if defined(REU) && !defined(KERBEROS)
#define USE_REU_DISKCOPY 1
#endif

/**
 * disk sector copy from device @p deviceFrom to @p deviceTo.
 * based on version 1.0e, then heavily modified.
 * @param optimized if true don't write sectors with only 0.
 * @return OK if copy was successful.
 * @return ERROR if copy failed or devices are incompatible.
 * @return ABORT if copy was aborted.
 */
int
doDiskCopy(const BYTE deviceFrom, const BYTE deviceTo, const BYTE optimized)
{
  // lookup some info of the "to" device
  int ret = devicetype[deviceTo];
  const char *drivetype_to = drivetype[ret];
  // store the max track number of the "to" device in variable track. Later on track is used as a for loop variable.
  BYTE track = maxTrack(ret);
  // lookup some info of the "from" device.
  // However if the "from" device is a SD2IEC like device, it doesn't have a useful maximum track value.
  // We'll use the values of the "to" device instead.
  // We assume the user is copying the correct image and set the maximum track value of the "from" device
  // to the maximum track value of the "to" device.
  const BYTE devicetype_from_real = devicetype[deviceFrom];
  BYTE devicetype_from = isMultiType(devicetype_from_real) ? ret : devicetype_from_real;
  const char *drivetype_from = drivetype[devicetype_from_real];
  BYTE max_track = maxTrack(devicetype_from);
  BYTE sectorContent;
  BYTE use_reu = 0;
#if defined(USE_REU_DISKCOPY)
  struct em_copy emc;
  unsigned page = 0;
  use_reu = cachedFileSize == diskImageSize(devicetype_from);
#endif

#if !defined(__PET__)
  if (max_track == 0)
    {
      diskCopyTitle(optimized, /*yesno=*/0, use_reu, deviceFrom, deviceTo);
      cprintf("\n\rcan't determine number of tracks\r\nof drive type %s\r\nPlease select number of tracks:\r\n1: 35 tracks\r\n2: 70 tracks\r\n3: 80 tracks\r\n", drivetype_from);
      while(1)
        {
          ret = cgetc();
          if (ret == '1' || ret == '5')
            {
              max_track = track = 35;
              devicetype_from = D1541;
              break;
            }
          if (ret == '2' || ret == '7')
            {
              max_track = track = 70;
              devicetype_from = D1571;
              break;
            }
          if (ret == '3' || ret == '8')
            {
              max_track = track = 80;
              devicetype_from = D1581;
              break;
            }
          if (ret == 'n' ||
              ret == CH_ESC ||
              ret == CH_LARROW)
            {
              return ABORT;
            }
        }
      drivetype_to = drivetype_from = drivetype[devicetype_from];
    }
#endif

  diskCopyTitle(optimized, /*yesno=*/1, use_reu, deviceFrom, deviceTo);

  if (max_track != track)
    {
#if !defined(__PET__)
      cprintf("\n\rcan't copy from %s (%i tracks)\n\rto %s (%i tracks)", drivetype_from, max_track, drivetype_to, track);
      cgetc();
#endif
      return ERROR;
    }

#if !defined(__PET__)
  if (isMultiType(devicetype_from_real))
    {
      cprintf("\n\rdiskcopy from %s to %s.\r\nmake sure that target device is compatible\r\nwith source image size.", drivetype_from, drivetype_to);
    }
#endif

  while(1)
    {
      ret = cgetc();
      if (ret == 'y')
        break;
      if (ret == 'n' ||
          ret == CH_ESC ||
          ret == CH_LARROW)
        return ABORT;
    }

  sprintf(linebuffer2, "%s -> %s", drivetype_from, drivetype_to);

#if defined(USE_REU_DISKCOPY)
  if (! use_reu)
    {
      cachedFileSize = 0;
    }
#endif

#if !defined(CHAR80)
  if (devicetype_from != D1001)
#endif
    {
      for(track = 0; track < 80; ++track)
        {
          const BYTE max_s = maxSector(devicetype_from, track);
          BYTE sector;
          BYTE sector_inc = 1;
          if (IS_1541(devicetype_from))
            {
              if (track == 40)
                {
                  break;
                }
            }
#if !defined(__PET__)
          else if (devicetype_from == D1571)
            {
#if defined(CHAR80)
              if (track == 70)
                break;
#else
              if (track == 35)
                break;
#endif
            }
          else if (devicetype_from == D1581)
            {
#if !defined(CHAR80)
              if (track == 40)
                break;
#endif
              sector_inc = 2;
            }
#endif // PET
#if defined(SFD1001)
          else if (devicetype_from == D1001)
            {
              if (track == 77)
                break;
              sector_inc = 2;
            }
#endif

          textcolor(DC_COLOR_TEXT);
          cputcxy(track,1,((track+1)/10)+'0');
          cputcxy(track,2,((track+1)%10)+'0');
          for(sector = 0; sector < max_s; sector += sector_inc)
            {
              printSecStatus(devicetype_from, track, sector, '.');
            }
        }
    }

  if ((ret = cbm_open(9, deviceFrom, 5, "#")) != 0)
    {
#if !defined(__PET__)
      sprintf((char*)sectorBuf, "device %i: open data failed: %i", deviceFrom, ret);
#endif
      goto error;
    }
  if ((ret = cbm_open(6, deviceFrom, 15, "")) != 0)
    {
#if !defined(__PET__)
      sprintf((char*)sectorBuf, "device %i: open cmd failed: %i", deviceFrom, ret);
#endif
      goto error;
    }
  if ((ret = cbm_open(7, deviceTo,   5, "#")) != 0)
    {
#if !defined(__PET__)
      sprintf((char*)sectorBuf, "device %i: open data failed: %i", deviceTo, ret);
#endif
      goto error;
    }
  if ((ret = cbm_open(8, deviceTo,   15, "")) != 0)
    {
#if !defined(__PET__)
      sprintf((char*)sectorBuf, "device %i: open cmd failed: %i", deviceTo, ret);
#endif
      goto error;
    }

  ret = OK;
  for(track = 0; track < max_track; ++track)
    {
      const BYTE max_sector = maxSector(devicetype_from, track);
      BYTE sector;

#if !defined(__PET__)
      cclearxy(0,BOTTOM,SCREENW);
      textcolor(DC_COLOR_EE);
      gotoxy(0,BOTTOM);
      if (track == 0)
        {
          cputs("press \xff to abort");
        }
      else if (track == 1 && !optimized)
        {
          cputs("use D for optimized diskcopy");
        }
      else if (track == max_track-1)
        {
          cputs("writing last track!!!");
        }
      else if ((devicetype_from == D1571 && track >= 35) ||
               (devicetype_from == D1581 && track >= 40) ||
               (devicetype_from == D1001 && track >= 77))
        {
          cputs("copy the back side");
        }
      else if (IS_1541(devicetype_from))
        {
          switch(track)
            {
            case 5: cputs("this will take a while"); break;
            case 18: cputs("halftime"); break;
            case 32: cputs("almost there"); break;
            case 33: cputs("any minute now"); break;
            case 35: cputs("this disk is oversized"); break;
            }
        }
#endif

      textcolor(DC_COLOR_TEXT);
      cputsxy(SCREENW-strlen(linebuffer2), BOTTOM, linebuffer2);

#if !defined(CHAR80)
      // check if 1541 writes to extra tracks
      if (IS_1541(devicetype_from)
          && track >= 40)
        {
          textcolor(DC_COLOR_TEXT);
          cputcxy(39,2, '1'+track-40);
        }
      // check if 1571 writes on back side
      if (devicetype_from == D1571 && track == 35)
        {
          textcolor(DC_COLOR_TEXT);
          cputsxy(0,1,"33334444444444555555555566666666667     ");
          cputsxy(0,2,"67890123456789012345678901234567890     ");
        }
      // check if 1581 writes on back side
      if (devicetype_from == D1581 && track == 40)
        {
          textcolor(DC_COLOR_TEXT);
          cputsxy(0,1,"4444444445555555555666666666677777777778");
        }
#endif

      for(sector = 0; sector < max_sector; ++sector)
        {
          // check for abort
          if (kbhit())
            {
              ret = cgetc();
              if (ret == CH_ESC || ret == CH_LARROW)
                {
                  goto done;
                }
            }

          // read sector
          printSecStatus(devicetype_from, track, sector, 'R');

#define SECTOR_ERROR(ch)                                  \
                  textcolor(DC_COLOR_ERROR);              \
                  cputsxy(0,BOTTOM,(char*)sectorBuf);     \
                  printSecStatus(devicetype_from, track, sector, ch);

#if defined(USE_REU_DISKCOPY)
          if (use_reu &&
              page < em_pagecount())
            {
              emc.buf = sectorBuf;
              emc.offs = 0;
              emc.page = page++;
              emc.count = 256;
              em_copyfrom(&emc);
            }
          else
#endif
            {
              ret = cbm_write(6, linebuffer, sprintf(linebuffer, "u1:5 0 %i %i", track + 1, sector));
              if (ret < 0)
                {
#if !defined(__PET__)
                  sprintf((char*)sectorBuf, "read sector %i/%i failed: %i", track+1, sector, _oserror);
                  SECTOR_ERROR('e');
#endif
                  continue;
                }

              ret = cbm_read(9, sectorBuf, 256);
              if (ret != 256)
                {
                  // check for expected failures at the end of a disk
                  if (IS_1541(devicetype_from)
                      && track >= 35
                      && sector == 0)
                    {
                      ret = OK;
                      goto success;
                    }
                  if (devicetype_from == D1571 && track == 35 && sector == 0)
                    {
                      ret = OK;
                      goto success;
                    }
#if !defined(__PET__)
                  sprintf((char*)sectorBuf, "read %i/%i failed: %i", track+1, sector, _oserror);
                  SECTOR_ERROR('e');
#endif
                  continue;
                }
            } // if USE_REU_DISKCOPY

#if defined(USE_REU_DISKCOPY)
          if (! use_reu &&
              page < em_pagecount())
            {
              emc.buf = sectorBuf;
              emc.offs = 0;
              emc.page = page++;
              emc.count = 256;
              em_copyto(&emc);
              cachedFileSize += 256;
            }
#endif

#if !defined(__PET__)
          // check what the sector contains
          sectorContent = 'O';
          for(ret = 255; ret > 1; --ret)
            {
              if (sectorBuf[ret] != 0)
                {
                  sectorContent = 'W';
                  goto allZeroDone;
                }
            }
          for(; ret >= 0; --ret)
            {
              if (sectorBuf[ret] != 0)
                {
                  sectorContent = 'o';
                  goto allZeroDone;
                }
            }
          assert(sectorContent == 'O');
          goto sectorCheckDone;
        allZeroDone:
          if (sectorBuf[0] == 0)
            {
              sectorContent = sectorBuf[1] / 25;
              if (sectorContent > 9)
                sectorContent = 9;
              sectorContent += '0';
            }
          else if (sectorBuf[0] > max_track)
            {
              sectorContent = '!';
            }
        sectorCheckDone:
          printSecStatus(devicetype_from, track, sector, sectorContent);
          if (optimized && sectorContent == 'O')
            {
              // all bytes are 0, skip writing this sector
              continue;
            }
#else
          printSecStatus(devicetype_from, track, sector, 'W');
#endif

          // write sector
          ret = cbm_write(8, "b-p:5 0", 7);
          if (ret < 0)
            {
#if !defined(__PET__)
              sprintf((char*)sectorBuf, "setup buffer failed: %i", track+1, sector, _oserror);
              SECTOR_ERROR('E');
#endif
              continue;
            }

          ret = cbm_write(7, sectorBuf, 256);
          if (ret != 256)
            {
#if !defined(__PET__)
              sprintf((char*)sectorBuf, "write %i/%i failed: %i", track+1, sector, _oserror);
              SECTOR_ERROR('E');
#endif
              continue;
            }

          ret = cbm_write(8, linebuffer, sprintf(linebuffer, "u2:5 0 %i %i", track + 1, sector));
          if (ret < 0)
            {
#if !defined(__PET__)
              sprintf((char*)sectorBuf, "write cmd %i/%i failed: %i", track+1, sector, _oserror);
              SECTOR_ERROR('E');
#endif
              continue;
            }
        }
    }

 success:
  // send Initialize command to destination drive, so it will read the new BAM
  ret = cbm_write(8, "i", 1);
  goto done;

 error:
  ret = ERROR;
  textcolor(DC_COLOR_ERROR);
  cputsxy(0,BOTTOM,(char*)sectorBuf);
  cgetc();

 done:
  cbm_close(6);
  cbm_close(7);
  cbm_close(8);
  cbm_close(9);

  textcolor(DC_COLOR_TEXT);
  return ret;
}

void
doMakeImage(const BYTE device)
{
  BYTE dt = 0;
  int bam;
  int n;
  int i;
  int answer_len;

  sprintf(linebuffer, "Make Image on device %i", device);
  newscreen(linebuffer);

#if !defined(__PET__)
  cputs("\n\rValid Image extensions: .d64 .d71 .d81");
#endif
  cputs("\n\rImage name: ");
  answer_len = n = textInput(12,3,linebuffer2,16);
  if (n < 0)
    return;
  if (n > 0 && n < 5)
    {
#if !defined(__PET__)
      cputsxy(0,6,"invalid image name\n\r");
      waitKey(0);
#endif
      return;
    }

  if (linebuffer2[n-3] == 'd' || linebuffer2[n-3] == 'D')
    {
      if (linebuffer2[n-2] == '6' &&
          linebuffer2[n-1] == '4')
        {
          dt = D1541;
          n = 683;
          bam = 357;
        }
      else if (linebuffer2[n-2] == '7' &&
               linebuffer2[n-1] == '1')
        {
          dt = D1571;
          n = 1366;
          bam = 357;
        }
      else if (linebuffer2[n-2] == '8' &&
               linebuffer2[n-1] == '1')
        {
          dt = D1581;
          n = 80*40;
          bam = 39*40;
        }
    }
  if (dt == 0)
    {
#if !defined(__PET__)
      cputsxy(0,6,"invalid image type\n\r");
      waitKey(0);
#endif
      return;
    }

  sprintf(linebuffer, "%s,%c",
          linebuffer2,
          (devicetype[device] == U64) ? 's' : 'p');
  if (cbm_open(7, device, CBM_WRITE, linebuffer) != 0)
    {
#if !defined(__PET__)
      cputsxy(0,6,"Can't open output file\n\r");
      waitKey(0);
#endif
      goto done;
    }

  for(i = 0; i < n; ++i)
    {
#if !defined(__PET__)
      gotoxy(0,4);
      cprintf("write block %i/%i", i, n);
#endif
      memset(sectorBuf, 0, 256);
      if (dt == D1541 || dt == D1571)
        {
          if (i == bam ||
              (dt == D1571 && i == 1040))
            {
              int j;
              // https://ist.uwaterloo.ca/~schepers/formats/D64.TXT

              // track/sector of first directory block
              sectorBuf[0] = 18;
              sectorBuf[1] = 1;

              sectorBuf[2] = 0x41; // DOS version

#if !defined(__PET__)
              // https://ist.uwaterloo.ca/~schepers/formats/D71.TXT
              if (dt == D1571)
                {
                  sectorBuf[3] = 0x80;
                  for(j = 0; j < 35; ++j)
                    {
                      sectorBuf[0xDD + j] = maxSector(dt, j+35);
                    }
                }
#endif

              // write BAM,
              // 35 objects of 4 bytes.
              for(j = 0; j < 35; ++j)
                {
                  // first byte, sectors per track
                  BYTE max_sector = maxSector(dt, j);
                  sectorBuf[4 + j*4] = max_sector;

                  // 21, 19, 18, 17 bits set for each free block per track
                  if (j == bam)
                    {
                      sectorBuf[5 + j*4] = 0x3f;
                    }
                  else
                    {
                      sectorBuf[5 + j*4] = 0xff;
                    }

                  sectorBuf[6 + j*4] = 0xff;

                  switch(max_sector)
                    {
                    case 21: max_sector = 0x1f; break;
                    case 19: max_sector = 0x07; break;
                    case 18: max_sector = 0x03; break;
                    case 17: max_sector = 0x01; break;
                    }
                  sectorBuf[7 + j*4] = max_sector;
                }

              // disk name
              memset(&sectorBuf[0x90], 0xA0, 0xAB - 0x90);
              memcpy(&sectorBuf[0x90], linebuffer2, answer_len-4);
              // disk ID
              sectorBuf[0xA2] = 'd';
              sectorBuf[0xA3] = 'c';
              // DOS type
              sectorBuf[0xA5] = '2';
              sectorBuf[0xA6] = 'a';
            }
          else if (i == bam+1)
            {
              // first directory sector
              sectorBuf[1] = 0xff;
            }
        }

#if !defined(__PET__)
      if (dt == D1581)
        {
          // https://ist.uwaterloo.ca/~schepers/formats/D81.TXT

          // header sector at 40/0
          if (i == bam)
            {
              // track/sector of first directory sector
              sectorBuf[0] = 40;
              sectorBuf[1] = 3;
              // DOS type
              sectorBuf[2] = 0x44;
              // disk name
              memset(&sectorBuf[4], 0xA0, 0x1D-4);
              memcpy(&sectorBuf[4], linebuffer2, answer_len - 4);
              // disk ID
              sectorBuf[0x16] = 'd';
              sectorBuf[0x17] = 'c';
              // DOS version
              sectorBuf[0x19] = '3';
              // disk version
              sectorBuf[0x1a] = 'd';
            }
          // first BAM at 40/1
          // second BAM at 40/2
          else if (i == bam+1 || i == bam+2)
            {
              BYTE *b;
              int t;
              // next track/sector
              if (i == bam+1)
                {
                  sectorBuf[0] = 40;
                  sectorBuf[1] = 2;
                }
              else
                {
                  sectorBuf[1] = 0xff;
                }
              // version
              sectorBuf[2] = 'd';
              sectorBuf[3] = 0xBB;
              // disk ID
              sectorBuf[4] = 'd';
              sectorBuf[5] = 'c';
              // I/O byte
              sectorBuf[6] = 0xC0;

              b = &sectorBuf[0x10];
              for(t = 1; t <= 40; ++t)
                {
                  if (i == bam+2 && t == 40)
                    {
                      *b++ = 36;
                      *b++ = 0xf0;
                    }
                  else
                    {
                      *b++ = 40;
                      *b++ = 0xff;
                    }
                  *b++ = 0xff;
                  *b++ = 0xff;
                  *b++ = 0xff;
                  *b++ = 0xff;
                }
            }
          // first directory sector at 40/3
          else if (i == bam+3)
            {
              sectorBuf[1] = 0xff;
            }
        }
#endif // PET

      if (i == n-1)
        {
          strcpy((char*)(sectorBuf+220), "image created by dracopy " DRA_VERNUM);
        }

      if (cbm_write(7, sectorBuf, 256) != 256)
        {
#if !defined(__PET__)
          cputsxy(0,6,"write error\n\r");
          waitKey(0);
#endif
          goto done;
        }

      if (kbhit())
        {
          char c = cgetc();
          if (c == CH_ESC || c == CH_LARROW)
            {
#if !defined(__PET__)
              cputsxy(0,6,"abort");
#endif
              cbm_close(7);
              sprintf(linebuffer, "s:%s", linebuffer2);
              cmd(device, linebuffer);
              goto done;
            }
        }
    }

 done:
  cbm_close(7);
}

// copied from version 1.0e
void
doRelabel(const BYTE device)
{
  BYTE track, sector, name_offset, id_offset;
#define id_len 5
  int i, j;
  sprintf(linebuffer, "Change disk name of device %i", device);
  newscreen(linebuffer);

  switch(devicetype[device])
    {
      // https://ist.uwaterloo.ca/~schepers/formats/D64.TXT
    case D1540:
    case D1541:
    case D1551:
    case D1570:
      // https://ist.uwaterloo.ca/~schepers/formats/D71.TXT
    case D1571:
      track = 18;
      sector = 0;
      name_offset = 0x90;
      id_offset = 0xA2;
      break;

#if !defined(__PET__)
      // https://ist.uwaterloo.ca/~schepers/formats/D81.TXT
    case D1581:
      track = 40;
      sector = 0;
      name_offset = 0x04;
      id_offset = 0x16;
      break;
#endif

      // https://ist.uwaterloo.ca/~schepers/formats/D80-D82.TXT
    case D1001:
      track = 38;
      sector = 0;
      name_offset = 6;
      id_offset = 0x18;
      break;

    default:
#if !defined(__PET__)
      cputs("unsupported device: ");
      cputs(drivetype[devicetype[device]]);
      cputs("\n\r");
      waitKey(0);
#endif
      goto done;
    }

  // read BAM sector
  i = cbm_open(2, device, 5, "#");
  if (i != 0)
    {
#if !defined(__PET__)
      gotoxy(0,6);
      cprintf("could not open 2: %i\n\r", i);
      waitKey(0);
#endif
      goto done;
    }

  i = cbm_open(4, device, 15, "");
  if (i != 0)
    {
#if !defined(__PET__)
      gotoxy(0,6);
      cprintf("could not open 4: %i\n\r", i);
      waitKey(0);
#endif
      goto done;
    }

  j = sprintf(linebuffer, "u1:5 0 %i %i", track, sector);
  i = cbm_write(4, linebuffer, j);
  if (i != j)
    {
#if !defined(__PET__)
      gotoxy(0,6);
      cprintf("could not write u1: %i\n\r", i);
      waitKey(0);
#endif
      goto done;
    }

  i = cbm_read(2, sectorBuf, 256);
  if (i != 256)
    {
#if !defined(__PET__)
      gotoxy(0,6);
      cprintf("could not read BAM: %i\n\r", i);
      waitKey(0);
#endif
      goto done;
    }

  // copy out disk name
  for(i = 0; i < 16; ++i)
    {
      linebuffer2[i] = sectorBuf[name_offset + i];
    }
  linebuffer2[i] = 0;
  // strip disk name
  for(i = 15; i > 0; --i)
    {
      if (linebuffer2[i] == 0xA0)
        linebuffer2[i] = 0;
      else
        break;
    }
  if (i > 0)
    ++i;
  linebuffer2[i++] = ',';
  for(j = 0; j < id_len; ++j)
    {
      linebuffer2[i++] = sectorBuf[id_offset + j];
    }
  linebuffer2[i] = 0;

  cputsxy(0,2,"disk name: ");
  i = textInput(10,2, linebuffer2, 16+1+id_len);
  if (i >= 0)
    {
      // check if disk ID was given
      for(j = i-1; j >= 0; --j)
        {
          if (linebuffer2[j] == ',')
            {
              // copy ID
              BYTE k;
              for(k = 0; k < id_len && linebuffer2[j+k+1]; ++k)
                {
                  sectorBuf[id_offset + k] = linebuffer2[j + k + 1];
                }
              i = j;
              break;
            }
        }

      // fill up disk name with $A0
      memset(linebuffer2 + i, 0xA0, 16-i);
      memcpy(sectorBuf + name_offset, linebuffer2, 16);

      // write new BAM sector
      cbm_write(4, "b-p:5 0", 7);
      i = cbm_write(2, sectorBuf, 256);
      cbm_write(4, linebuffer, sprintf(linebuffer, "u2:5 0 %i %i", track, sector));

      if (i != 256)
        {
#if !defined(__PET__)
          cputsxy(0,6,"relabel error\n\r");
          waitKey(0);
#endif
        }
    }

 done:
  cbm_close(4);
  cbm_close(2);

  // reset the device, so it will read the updated BAM
  cmd(device, "ui");
}

#if defined(REU)
void
readFile(const BYTE context)
{
  int i;
  unsigned page = 0;
  const BYTE device = devices[context];
  Directory *cwd = GETCWD;
  if (cwd->selected == NULL)
    return;
  strcpy(cachedFileName, cwd->selected->dirent.name);
  sprintf(linebuffer, "read %s into GEO", cachedFileName);
  newscreen(linebuffer);
  i = strlen(cachedFileName);
  cachedFileName[i] = ',';
  ++i;
  switch(cwd->selected->dirent.type)
    {
    case _CBM_T_SEQ:
      cachedFileName[i] = 's';
      break;
    case _CBM_T_PRG:
      cachedFileName[i] = 'p';
      break;
    case _CBM_T_USR:
      cachedFileName[i] = 'u';
      break;
    default:
      cputs("unsupported file type\n\r");
      goto error;
    }
  cachedFileName[++i] = 0;
  cachedFileSize = 0;

  i = cbm_open(device, device, CBM_READ, cachedFileName);
  if (i != 0)
    {
      cprintf("could not open %s: %i\n\r", cachedFileName, i);
      goto error;
    }
  while(1)
    {
      void *p = em_use(page++);
      if (page > em_pagecount())
        {
          cputs("GEO too small\n\r");
          goto error;
        }
      if (! p)
        {
          cputs("em map error\n\r");
          goto error;
        }
      i = cbm_read(device, p, 256);
      if (i < 0)
        {
          cprintf("read error: %i\n\r", i);
          goto error;
        }
      assert(i <= 256);
      em_commit();
      cachedFileSize += i;
      if (i < 256)
        break;
      cprintf("\rread %lu ", cachedFileSize);

      if (kbhit())
        {
          char c = cgetc();
          if (c == CH_ESC || c == CH_LARROW)
            {
              cputs("abort");
              goto abort;
            }
        }
    }
  goto done;

 error:
  waitKey(0);
 abort:
  cachedFileSize = 0;
 done:
  cbm_close(device);
}

BYTE
writeFile(const BYTE context)
{
  int i, w;
  unsigned page = 0;
  unsigned long size = cachedFileSize;
  const BYTE device = devices[context];
  if (size == 0)
    return 0;
  i = strlen(cachedFileName);
  if (i < 3)
    return 0;

  sprintf(linebuffer, "write %s from GEO to %i", cachedFileName, device);
  newscreen(linebuffer);

  i = cbm_open(device, device, CBM_WRITE, cachedFileName);
  if (i != 0)
    {
      cprintf("can't open file: %i\n\r", i);
      goto error;
    }

  while(size)
    {
      void *p = em_map(page++);
      if (! p)
        {
          cputs("em map error\n\r");
          goto error;
        }
      if (size > 256)
        i = 256;
      else
        i = size;
      w = cbm_write(device, p, i);
      if (w != i)
        {
          cprintf("write error: %i\n\r", w);
          goto error;
        }
      size -= i;
      cprintf("\rwrite %lu          ", size);

      if (kbhit())
        {
          char c = cgetc();
          if (c == CH_ESC || c == CH_LARROW)
            {
              cputs("abort");
              goto done;
            }
        }
    }

  goto done;
 error:
  waitKey(0);
 done:
  cbm_close(device);
  return 1;
}
#endif
