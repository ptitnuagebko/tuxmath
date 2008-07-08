/*
*  C Implementation: fileops.c
*
* (Note: read_config_file() was made possible by studying the file prefs.c in gtkpod:
*  URL: http://www.gtkpod.org/
*  URL: http://gtkpod.sourceforge.net/
*  Copyright (C) 2002-2005 Jorg Schuler <jcsjcs at users sourceforge net>.
*  Licensed under GNU GPL v2.
*  This code is a nearly complete rewrite but I would like to express my thanks.)
*
*
* Description: File operations - together, fileops.h and fileops.c contain 
* all code involving disk operations.  The intention is to make it easier to
* port tuxmath to other operating systems, as code to read and write as 
* well as paths and file locations may be more OS-dependent.
*
* This file contains functions to read and write config files.
* The config file contains name-value pairs, one pair per line, to control
* settings for the behavior of Tuxmath.
*
* Code for loading program data from disk is now also found here.
* 
* Author: David Bruce <dbruce@tampabay.rr.com>, (C) 2006
*
* Copyright: See COPYING file that comes with this distribution (briefly, GNU GPL)
*
*/

#include "config.h"

/* Tuxmath includes: */
#include "tuxmath.h"
#include "fileops.h"
#include "setup.h"
#include "mathcards.h"
#include "options.h"
#include "highscore.h"
#include "lessons.h"
#include "titlescreen.h"











/* SDL includes: */
#include "SDL.h"

#ifndef NOSOUND
#include "SDL_mixer.h"
#endif

#include "SDL_image.h"

/* OS includes - NOTE: these may not be very portable */
#include <dirent.h>  /* for opendir() */
#include <sys/stat.h>/* for mkdir() */
#include <unistd.h>  /* for getcwd() */
#include <sys/types.h> /* for umask() */

/* Standard C includes: */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>


/* Used by both write_pregame_summary() and */
/* write_postgame_summary() so defined with */
/* file scope:                              */
#ifdef BUILD_MINGW32
#define SUMMARY_EXTENSION ".txt"
#else
#define SUMMARY_EXTENSION ""
#endif 

static char* summary_filenames[NUM_SUMMARIES] = {
  "summary1" SUMMARY_EXTENSION,
  "summary2" SUMMARY_EXTENSION,
  "summary3" SUMMARY_EXTENSION,
  "summary4" SUMMARY_EXTENSION,
  "summary5" SUMMARY_EXTENSION,
  "summary6" SUMMARY_EXTENSION,
  "summary7" SUMMARY_EXTENSION,
  "summary8" SUMMARY_EXTENSION,
  "summary9" SUMMARY_EXTENSION,
  "summary10" SUMMARY_EXTENSION
};

TTF_Font  *default_font;
TTF_Font  *help_font;

/* local function prototypes: */
static int find_tuxmath_dir(void);
static int str_to_bool(const char* val);
static int read_config_file(FILE* fp, int file_type);
static int write_config_file(FILE* fp, int verbose);
static int is_lesson_file(const struct dirent *lfdirent);
static int read_goldstars(void);
static int read_lines_from_file(FILE *fp,char ***lines);
static void dirname_up(char *dirname);
static char* get_user_name(void);
static char* get_file_name(char *fullpath);


/* Mingw does not have localtime_r(): */
/* (this replacement is Windows-specific, so also check for Win32) */
#ifndef HAVE_LOCALTIME_R
#ifdef WIN32
#define localtime_r( _clock, _result ) \
        ( *(_result) = *localtime( (_clock) ), \
          (_result) )
#endif
#endif


/*************************************************************************
Using Autoconf's "config.h", we include our portability replacements
for scandir() and alphasort() if necessary:
*************************************************************************/

#ifndef HAVE_SCANDIR
#include "scandir.h"
#endif /* end of scandir() replacements */

/* fix HOME on windows */
#ifdef BUILD_MINGW32
#include <windows.h>





 
/* STOLEN from tuxpaint */

/*
  Removes a single '\' or '/' from end of path 
*/
static char *remove_slash(char *path)
{
  int len = strlen(path);

  if (!len)
    return path;

  if (path[len-1] == '/' || path[len-1] == '\\')
    path[len-1] = 0;

  return path;
}

/*
  Read access to Windows Registry
*/
static HRESULT ReadRegistry(const char *key, const char *option, char *value, int size)
{
  LONG        res;
  HKEY        hKey = NULL;

  res = RegOpenKeyEx(HKEY_CURRENT_USER, key, 0, KEY_READ, &hKey);
  if (res != ERROR_SUCCESS)
    goto err_exit;
  res = RegQueryValueEx(hKey, option, NULL, NULL, (LPBYTE)value, (LPDWORD)&size);
  if (res != ERROR_SUCCESS)
    goto err_exit;
  res = ERROR_SUCCESS;

err_exit:
  if (hKey) RegCloseKey(hKey);
  return HRESULT_FROM_WIN32(res);
}


/*
  Returns heap string containing default application data path.
  Creates suffix subdirectory (only one level).
  E.g. C:\Documents and Settings\jfp\Application Data\suffix
*/
char *GetDefaultSaveDir(const char *suffix)
{
  char          prefix[MAX_PATH];
  char          path[2*MAX_PATH];
  const char   *key    = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders";
  const char   *option = "AppData";
  HRESULT hr = S_OK;

  if (SUCCEEDED(hr = ReadRegistry(key, option, prefix, sizeof(prefix))))
  {
    remove_slash(prefix);
    snprintf(path, sizeof(path), "%s/%s", prefix, suffix);
    _mkdir(path);
    return strdup(path);
  }
  return strdup("userdata");
}


/* Windows XP: User/App Data/TuxMath/ */
/* Windows 98/ME: TuxMath install dir/userdata/Options */
#define OPTIONS_SUBDIR ""
#define OPTIONS_FILENAME "options.cfg"
#define HIGHSCORE_FILENAME "highscores.txt"
#define GOLDSTAR_FILENAME "goldstars.txt"
#define USER_MENU_ENTRIES_FILENAME "user_menu_entries.txt"
#define USER_LOGIN_QUESTIONS_FILENAME "user_login_questions.txt"
#else

# define get_home getenv("HOME")
#define OPTIONS_SUBDIR "/.tuxmath"
#define OPTIONS_FILENAME "options"
#define HIGHSCORE_FILENAME "highscores"
#define GOLDSTAR_FILENAME "goldstars"
#define USER_MENU_ENTRIES_FILENAME "user_menu_entries"
#define USER_LOGIN_QUESTIONS_FILENAME "user_login_questions"

#endif


/* This functions keep and returns the user data directory application path */
/* FIXME?: currently the best way to test whether we're using the user's    */
/* home directory, or using a different path, is to test add_subdir (which  */
/* is 1 if we're using the user's ~/.tuxmath directory, 0 otherwise). Is    */
/* this a bad example of using 1 thing for 2 purposes? So far there are     */
/* no conflicts. */
static char* user_data_dir = NULL;
static int add_subdir = 1;
static char* high_scores_file_path = NULL;

/* A variable for storing the "current" config filename */
static char* last_config_file_name = NULL;

char *get_user_data_dir ()
{ 
  if (! user_data_dir)
#ifdef BUILD_MINGW32
     user_data_dir = GetDefaultSaveDir(PROGRAM_NAME);
#else
     user_data_dir = strdup(getenv("HOME"));
#endif

  return user_data_dir;  
}

/* This function sets the user data directory, and also sets a flag
   indicating that this should function as a .tuxmath directory, and
   thus doesn't need the subdir appended. */
void set_user_data_dir(const char *dirname)
{
  int len;

  if (user_data_dir != NULL)
    free(user_data_dir);   // clear the previous setting

  // Allocate space for the directory name. We do it with +2 because
  // we have to leave room for a possible addition of a "/"
  // terminator.
  user_data_dir = (char*) malloc((strlen(dirname)+2)*sizeof(char));
  if (user_data_dir == NULL) {
    fprintf(stderr,"Error: insufficient memory for duplicating string %s.\n",dirname);
    exit(EXIT_FAILURE);
  }
  strcpy(user_data_dir,dirname);

  // Check to see that dirname is properly terminated
  len = strlen(user_data_dir);
  if (user_data_dir[len-1] != '/')
    strcat(user_data_dir,"/");

  // If the user supplies a homedir, interpret it literally and don't
  // add .tuxmath
  add_subdir = 0;
}

/* This gets the user data directory including the .tuxmath, if applicable */
void get_user_data_dir_with_subdir(char *opt_path)
{
  strcpy(opt_path, get_user_data_dir());
  if (add_subdir)
    strcat(opt_path, OPTIONS_SUBDIR "/");
}
  
/* FIXME should have better file path (/etc or /usr/local/etc) and name */
int read_global_config_file(void)
{
  FILE* fp;
  fp = fopen(DATA_PREFIX "/missions/options", "r");
  if (fp)
  {
    read_config_file(fp, GLOBAL_CONFIG_FILE);
    fclose(fp);
    fp = NULL;
    return 1;
  }
  else
    return 0;
}

/* Attempts to read in user's config file - on a *nix system, */
/* something like: /home/laura/.tuxmath/options               */
int read_user_config_file(void)
{
  FILE* fp;
  char opt_path[PATH_MAX];

  /* find $HOME and tack on file name: */
  get_user_data_dir_with_subdir(opt_path);
  strcat(opt_path, OPTIONS_FILENAME);

  #ifdef TUXMATH_DEBUG
  printf("\nIn read_user_config_file() full path to config file is: = %s\n", opt_path);
  #endif

  fp = fopen(opt_path, "r");
  if (fp) /* file exists */
  {
    read_config_file(fp, USER_CONFIG_FILE);
    fclose(fp);
    fp = NULL;
    return 1;
  }
  else  /* could not open config file: */
  {
    return 0;
  }
}

/* Looks for matching file in various locations:        */
/*   1. Current working directory                       */
/*   2. As an absolute path filename                    */
/*   3. In tuxmath's missions directory.                */
/*   4. In missions/lessons directory.                  */
/*   5. In missions/arcade directory.                   */
/*   6. In user's own .tuxmath directory                */
/* FIXME redundant code - figure out way to iterate through above */
int read_named_config_file(const unsigned char* fn)
{
  FILE* fp;
  char opt_path[PATH_MAX];
  /* Make compiler happy: */
  const char* filename = (const char*)fn;

  if (last_config_file_name != NULL)
    free(last_config_file_name);
  last_config_file_name = strdup(filename);

  #ifdef TUXMATH_DEBUG
  printf("\nIn read_named_config_file() filename is: = %s\n", filename);
  #endif


  /* First look in current working directory:  */
  getcwd(opt_path, PATH_MAX); /* get current working directory */
  /* add separating '/' unless cwd is '/' : */
  if (0 != strcmp("/", opt_path)) 
  {
    strcat(opt_path, "/");
  }
  strcat(opt_path, filename); /* tack on filename              */


  #ifdef TUXMATH_DEBUG
  printf("\nIn read_named_config_file() checking for %s (cwd)\n", opt_path);
  #endif


  fp = fopen(opt_path, "r");  /* try to open file */
  if (fp) /* file exists */
  {
    #ifdef TUXMATH_DEBUG
    printf("\nFound %s\n", opt_path);
    #endif

    if (read_config_file(fp, USER_CONFIG_FILE))
    {
      fclose(fp);
      fp = NULL;
      return 1;
    }
    else /* try matching filename elsewhere */
    {
      fclose(fp);
      fp = NULL;
    }
  }


  /* Next try matching filename as absolute:      */
  /* Supply leading '/' if not already there:   */
  if (0 == strncmp ("/", filename, 1))
  { 
    strcpy(opt_path, filename);
  }
  else
  {
    strcpy(opt_path, "/");
    strcat(opt_path, filename);
  }

  #ifdef TUXMATH_DEBUG
  printf("\nIn read_named_config_file() checking for %s (abs)\n", opt_path);
  #endif

  fp = fopen(opt_path, "r");
  if (fp) /* file exists */
  {
    #ifdef TUXMATH_DEBUG
    printf("\nFound %s\n", opt_path);
    #endif

    if (read_config_file(fp, USER_CONFIG_FILE))
    {
      fclose(fp);
      fp = NULL;
      return 1;
    }
    else /* keep trying to match filename elsewhere */
    {
      fclose(fp);
      fp = NULL;
    }
  }


  /* Next look in missions folder:      */
  strcpy(opt_path, DATA_PREFIX);
  strcat(opt_path, "/missions/");
  strcat(opt_path, filename);

  #ifdef TUXMATH_DEBUG
  printf("\nIn read_named_config_file() checking for %s (missions)\n", opt_path);
  #endif

  fp = fopen(opt_path, "r");
  if (fp) /* file exists */
  {
    #ifdef TUXMATH_DEBUG
    printf("\nFound %s\n", opt_path);
    #endif

    if (read_config_file(fp, USER_CONFIG_FILE))
    {
      fclose(fp);
      fp = NULL;
      return 1;
    }
    else /* keep trying to match filename elsewhere */
    {
      fclose(fp);
      fp = NULL;
    }
  }  

  /* Next look in missions/lessons folder (for prepared "lessons curriculum"):      */
  strcpy(opt_path, DATA_PREFIX);
  strcat(opt_path, "/missions/lessons/");
  strcat(opt_path, filename);

  #ifdef TUXMATH_DEBUG
  printf("\nIn read_named_config_file() checking for %s (missions/lessons)\n", opt_path);
  #endif

  fp = fopen(opt_path, "r");
  if (fp) /* file exists */
  {
    #ifdef TUXMATH_DEBUG
    printf("\nFound %s\n", opt_path);
    #endif

    if (read_config_file(fp, USER_CONFIG_FILE))
    {
      fclose(fp);
      fp = NULL;
      return 1;
    }
    else /* keep trying to match filename elsewhere */
    {
      fclose(fp);
      fp = NULL;
    }
  }  

  /* Next look in missions/arcade folder (for high score competition):      */
  strcpy(opt_path, DATA_PREFIX);
  strcat(opt_path, "/missions/arcade/");
  strcat(opt_path, filename);

  #ifdef TUXMATH_DEBUG
  printf("\nIn read_named_config_file() checking for %s (missions/arcade)\n", opt_path);
  #endif

  fp = fopen(opt_path, "r");
  if (fp) /* file exists */
  {
    #ifdef TUXMATH_DEBUG
    printf("\nFound %s\n", opt_path);
    #endif

    if (read_config_file(fp, USER_CONFIG_FILE))
    {
      fclose(fp);
      fp = NULL;
      return 1;
    }
    else /* keep trying to match filename elsewhere */
    {
      fclose(fp);
      fp = NULL;
    }
  }  

  /* Look in user's hidden .tuxmath directory  */
  /* find $HOME and tack on file name: */
  get_user_data_dir_with_subdir(opt_path);
  strcat(opt_path, filename);

  #ifdef TUXMATH_DEBUG
  printf("\nIn read_named_config_file() checking for %s (.tuxmath)\n", opt_path);
  #endif

  fp = fopen(opt_path, "r");
  if (fp) /* file exists */
  {
    #ifdef TUXMATH_DEBUG
    printf("\nFound %s\n", opt_path);
    #endif

    if (read_config_file(fp, USER_CONFIG_FILE))
    {
      fclose(fp);
      fp = NULL;
      return 1;
    }
    else /* keep trying to match filename elsewhere */
    {
      fclose(fp);
      fp = NULL;
    }
  }


  /* Look in user's home directory  */
  /* find $HOME and tack on file name: */
  strcpy(opt_path, get_user_data_dir());
  strcat(opt_path, "/");
  strcat(opt_path, filename);

  #ifdef TUXMATH_DEBUG
  printf("\nIn read_named_config_file() checking for %s (home)\n", opt_path);
  #endif

  fp = fopen(opt_path, "r");
  if (fp) /* file exists */
  {
    #ifdef TUXMATH_DEBUG
    printf("\nFound %s\n", opt_path);
    #endif

    if (read_config_file(fp, USER_CONFIG_FILE))
    {
      fclose(fp);
      fp = NULL;
      return 1;
    }
    else /* keep trying to match filename elsewhere */
    {
      fclose(fp);
      fp = NULL;
    }
  }

  /* Could not find file (or read it if found) in any location: */
  #ifdef TUXMATH_DEBUG
  printf("\nread_named_config_file() could not find/read: %s\n", opt_path);
  #endif
  return 0;
}

/* NOTE the cast to "const char*" just prevents compiler from complaining */
static int is_lesson_file(const struct dirent *lfdirent)
{
  return (0 == strncasecmp((const char*)&(lfdirent->d_name), "lesson", 6));
  /* FIXME Should somehow test each file to see if it is a tuxmath config file */
}



int parse_lesson_file_directory(void)
{
  unsigned char lesson_path[PATH_MAX];             //Path to lesson directory
  char* fgets_return_val;
  unsigned char name_buf[NAME_BUF_SIZE];
  int nchars;

  struct dirent **lesson_list_dirents = NULL;
  FILE* tempFile = NULL;

  int i = 0;
  int lessonIterator = 0;  //Iterator over matching files in lesson dir
  int length = 0;
  int lessons = 0;         //Iterator over accepted (& parsed) lesson files

  num_lessons = 0;

  /* find the directory containing the lesson files:  */
  nchars = snprintf(lesson_path, PATH_MAX, "%s/missions/lessons", DATA_PREFIX);
  if (nchars < 0 || nchars >= PATH_MAX) {
    perror("formatting lesson directory");
    return 0;
  }

#ifdef TUXMATH_DEBUG
  fprintf(stderr, "lesson_path is: %s\n", lesson_path);
#endif

  /* Believe we now have complete scandir() for all platforms :) */
  num_lessons = scandir(lesson_path, &lesson_list_dirents, is_lesson_file, alphasort);


#ifdef TUXMATH_DEBUG
  fprintf(stderr, "num_lessons is: %d\n", num_lessons);
#endif

  if (num_lessons < 0) {
    perror("scanning lesson directory");
    num_lessons = 0;
    return 0;
  }

  /* Allocate storage for lesson list */
  lesson_list_titles = (unsigned char**) malloc(num_lessons * sizeof(unsigned char*));
  lesson_list_filenames = (unsigned char**) malloc(num_lessons * sizeof(unsigned char*));
  if (lesson_list_titles == NULL || lesson_list_filenames == NULL) {
    perror("allocating memory for lesson list");
    return 0;
  }
  for (lessonIterator = 0; lessonIterator < num_lessons; lessonIterator++) {
    lesson_list_titles[lessonIterator] = (unsigned char*) malloc(NAME_BUF_SIZE * sizeof(unsigned char));
    lesson_list_filenames[lessonIterator] = (unsigned char*) malloc(NAME_BUF_SIZE * sizeof(unsigned char));
    if (lesson_list_titles[lessonIterator] == NULL || lesson_list_filenames[lessonIterator] == NULL) {
      perror("allocating memory for lesson filenames or titles");
      return 0;
    }
  }

  /* lessonIterator indexes the direntries, lessons indexes */
  /* the correctly-parsed files.  If successful in parsing, */
  /* lessons gets incremented. In case of problems, we can  */
  /* just continue onto the next entry without incrementing */
  /* lessons, and the bad entry will get overwritten by the */
  /* next one (or simply never used, if it was the last).   */
  for (lessonIterator = 0, lessons = 0; lessonIterator < num_lessons; lessonIterator++) {
    /* Copy over the filename (as a full pathname) */
    nchars = snprintf(lesson_list_filenames[lessons], NAME_BUF_SIZE, "%s/%s", lesson_path, lesson_list_dirents[lessonIterator]->d_name);
    if (nchars < 0 || nchars >= NAME_BUF_SIZE)
      continue;

#ifdef TUXMATH_DEBUG
    fprintf(stderr, "Found lesson file %d:\t%s\n", lessons, lesson_list_filenames[lessons]);
#endif

    /* load the name for the lesson from the file ... (1st line) */
    tempFile = fopen(lesson_list_filenames[lessons], "r");
    if (tempFile==NULL)
    {
      continue;
    }
    fgets_return_val = fgets(name_buf, NAME_BUF_SIZE, tempFile);
    if (fgets_return_val == NULL) {
      continue;
    }
    fclose(tempFile);


    /* check to see if it has a \r at the end of it (dos format!) */
    length = strlen(name_buf);
    while (length>0 && (name_buf[length - 1] == '\r' || name_buf[length - 1] == '\n')) {
      name_buf[length - 1] = '\0';
      length--;
    }

    /* Go past leading '#', ';', or whitespace: */
    /* NOTE getting i to the correct value on exit is the main goal of the loop */
    for (  i = 0;
           ((name_buf[i] == '#') ||
           (name_buf[i] == ';') ||
           isspace(name_buf[i])) &&
           (i < NAME_BUF_SIZE);
           i++  )
    {
      length--;
    }
    /* Now copy the rest of the first line into the list: */
    /* Note that "length + 1" is needed so that the final \0 is copied! */
    memmove(lesson_list_titles[lessons], &name_buf[i], length + 1); 


    /* Increment the iterator for correctly-parsed lesson files */
    lessons++;
  }
  /* Now free the individual dirents. We do this on a second pass */
  /* because of the "continue" approach used to error handling.   */
  for (lessonIterator = 0; lessonIterator < num_lessons; lessonIterator++)
    free(lesson_list_dirents[lessonIterator]);
  free(lesson_list_dirents);

  /* In case we didn't keep all of them, revise our count of how */
  /* many there are */
  num_lessons = lessons;

  /* Now we check to see which lessons have been previously completed */
  /* so we can display the Gold Stars: */
  /* Allocate storage for lesson list */

  /* prevent memory leak in case we called this already and */
  /* free the list:                                         */
  if(lesson_list_goldstars)
  {
    free(lesson_list_goldstars);
    lesson_list_goldstars = NULL;
  }

  lesson_list_goldstars = (int*)malloc(num_lessons*sizeof(int));
  if (!lesson_list_goldstars)
  {
    perror("unable to allocate memory for gold star list");
    return 0;
  }
  for (i = 0; i < num_lessons; i++)
  {
    lesson_list_goldstars[i] = 0;
  }

  /* Now read file to see what lessons have been previously completed: */
  read_goldstars();

  return (num_lessons > 0);  /* Success! */
}


/* Look for a completed lessons file in the user's homedir   */
/* and if found, pass the FILE* to read_goldstars_fp()       */
/* to actually read the data. The idea is to have TuxMath    */
/* keep track of what lessons the student has successfully   */
/* completed and display the "Gold Star" icon for those,     */
/* versus a grayed-out one for lessons remaining to be done. */
int read_goldstars(void)
{
  FILE* fp;
  char opt_path[PATH_MAX];

  /* find $HOME and tack on file name: */
  get_user_data_dir_with_subdir(opt_path);
  strcat(opt_path, GOLDSTAR_FILENAME);

  #ifdef TUXMATH_DEBUG
  printf("\nIn read_goldstars() full path to file is: = %s\n", opt_path);
  #endif

  fp = fopen(opt_path, "r");
  if (fp) /* file exists */
  {
    read_goldstars_fp(fp);
    fclose(fp);
    fp = NULL;
    return 1;
  }
  else  /* could not open goldstar file: */
  {
    return 0;
  }
}


/* Write gold star list in user's homedir in format     */
/* compatible with read_goldstars() above.              */
int write_goldstars(void)
{
  char opt_path[PATH_MAX];
  FILE* fp;

  if (!find_tuxmath_dir())
  {
    fprintf(stderr, "\nCould not find or create tuxmath dir\n");
    return 0;
  }

  /* find $HOME and add rest of path to config file: */
  get_user_data_dir_with_subdir(opt_path);
  strcat(opt_path, GOLDSTAR_FILENAME);

  #ifdef TUXMATH_DEBUG
  printf("\nIn write_goldstars() full path to file is: = %s\n", opt_path);
  #endif

  fp = fopen(opt_path, "w");
  if (fp)
  {
    write_goldstars_fp(fp);
    fclose(fp);
    fp = NULL;
    return 1;
  }
  else {
    fprintf(stderr, "\nUnable to write goldstars file.\n");
    return 0;
  }
}


/* Look for a highscore table file in the current user    */
/* data directory.  Return 1 if found, 0 if not.  This    */
/* is used for the multi-user login code, in deciding     */
/* where to put the highscore information.                */
int high_scores_found_in_user_dir(void)
{
  FILE* fp;
  char opt_path[PATH_MAX];

  /* find $HOME and tack on file name: */
  get_user_data_dir_with_subdir(opt_path);
  strcat(opt_path, HIGHSCORE_FILENAME);

  #ifdef TUXMATH_DEBUG
  printf("\nIn read_high_scores() full path to file is: = %s\n", opt_path);
  #endif

  fp = fopen(opt_path, "r");
  if (fp) /* file exists */
  {
    fclose(fp);
    return 1;
  }
  else
    return 0;
}

/* Set the path to the high score file to the current     */
/* user data dir                                          */
void set_high_score_path(void)
{
  char opt_path[PATH_MAX];

  /* find $HOME and tack on file name: */
  get_user_data_dir_with_subdir(opt_path);

  // Free any previous allocation
  if (high_scores_file_path != NULL)
    free(high_scores_file_path);

  high_scores_file_path = strdup(opt_path);
}

/* Look for a high score table file in the user's homedir */
/* and if found, pass the FILE* to read_high_scores_fp() in */
/* highscore.c to actually read in scores. (A "global"    */
/* location might in theory be better, but most schools   */
/* run Windows with all students sharing a common login   */
/* that may not be able to write to "global" locations).  */
int read_high_scores(void)
{
  FILE* fp;
  char opt_path[PATH_MAX];

  /* find $HOME and tack on file name: */
  if (high_scores_file_path == NULL)
    get_user_data_dir_with_subdir(opt_path);
  else
    strncpy(opt_path,high_scores_file_path,PATH_MAX);
  strcat(opt_path, HIGHSCORE_FILENAME);

  #ifdef TUXMATH_DEBUG
  printf("\nIn read_high_scores() full path to file is: = %s\n", opt_path);
  #endif

  fp = fopen(opt_path, "r");
  if (fp) /* file exists */
  {
    initialize_scores();  // clear any previous values
    read_high_scores_fp(fp);
    fclose(fp);
    fp = NULL;
    return 1;
  }
  else  /* could not open highscore file: */
  {
    return 0;
  }
}

/* On File Locking: With multiple users possibly updating the same
   high-scores table simultaneously, we have to be concerned with the
   possibility that the high score information might change between
   the time at which it was determined that the user gets a high
   score, and time at which the high score table actually gets
   written.  This is especially problematic if it takes kids a while
   to type in their name, and it's being assumed that the high scores
   table is valid over that entire time.

   As a first (easy) step, it's best to simply append new information
   to the high scores file, rather than re-writing the whole file; the
   read function can make sure that only the top scores are used.
   That way, the only time there would be trouble is if two appends
   start at exactly the same moment; and since the amount of
   information per line is small (and is thus written quickly) and
   updates are unlikely to be occurring on a
   millisecond-by-millisecond basis, it's pretty unlikely that
   problems will crop up.

   An even more robust alternative is to use real file locking.  One
   would need to design a cross-platform solution that also does
   sensible things (like, say, delete the lock if it's been held for
   more than 1s, so that locking doesn't block the application).  In
   researching this, the best approach seems to be:
   a) Open a second file - a lock file of a specific name - for read/write.
   b) If the lock file already contains your process ID, proceed
   c) If the lock file already contains a different process ID, deny
   d) If the lock file is new / empty write and flush your process ID
      to it, then go back to step (a)

   However, given that this information may not be "mission critical"
   (pun intended) and might be cleared on a somewhat regular basis
   anyway, it seems reasonable to just use the append strategy.
*/

/* Append a new high score to the high-scores file.       */
/* Using this approach is safer than writing the whole    */
/* high scores table if you're in an environment where    */
/* multiple users might be updating the table             */
/* simultaneously.                                        */
int append_high_score(int tableid,int score,char *player_name)
{
  char opt_path[PATH_MAX];
  FILE* fp;

  if (!find_tuxmath_dir())
  {
    fprintf(stderr, "\nCould not find or create tuxmath dir\n");
    return 0;
  }

  /* find $HOME and add rest of path to config file: */
  if (high_scores_file_path == NULL)
    get_user_data_dir_with_subdir(opt_path);
  else
    strncpy(opt_path,high_scores_file_path,PATH_MAX);
  strcat(opt_path, HIGHSCORE_FILENAME);

  #ifdef TUXMATH_DEBUG
  printf("\nIn write_high_scores() full path to file is: = %s\n", opt_path);
  #endif

  fp = fopen(opt_path, "a");
  if (fp)
  {
    fprintf(fp,"%d\t%d\t%s\t\n",tableid,score,player_name);
    fclose(fp);
    fp = NULL;
    return 1;
  }
  else
    return 0;
}
  


/* Checks to see if the current homedir has a menu_entries file, and if */
/* so returns the names of the menu entries. This is used in cases      */
/* where users must select their login information. Returns the number  */
/* of menu entries (0 if there are none), and sets the input            */
/* argument to a malloc-ed array of names (sets to NULL if there are no */
/* choices to be made).  */
int read_user_menu_entries(char ***user_names)
{
  FILE *fp;
  int n_entries;
  char opt_path[PATH_MAX],menu_entries_file[PATH_MAX];

  // Look for a menu_entries file
  get_user_data_dir_with_subdir(opt_path);
  strncpy(menu_entries_file,opt_path,PATH_MAX);
  strncat(menu_entries_file,USER_MENU_ENTRIES_FILENAME,PATH_MAX-strlen(menu_entries_file));
  n_entries = 0;
  fp = fopen(menu_entries_file,"r");
  if (fp)
  {
    // There is a menu_entries file, read it
    n_entries = read_lines_from_file(fp,user_names);
    fclose(fp);
  }

  return n_entries;
}

/* Reads the user_login_questions file. The syntax is identical to
   read_user_menu_entries. */
int read_user_login_questions(char ***user_login_questions)
{
  FILE *fp;
  int n_entries;
  char opt_path[PATH_MAX],user_login_questions_file[PATH_MAX];

  // Look for a user_login_questions file
  get_user_data_dir_with_subdir(opt_path);
  strncpy(user_login_questions_file,opt_path,PATH_MAX);
  strncat(user_login_questions_file,USER_LOGIN_QUESTIONS_FILENAME,PATH_MAX-strlen(user_login_questions_file));
  n_entries = 0;
  fp = fopen(user_login_questions_file,"r");
  if (fp)
  {
    // There is a user_login_questions file, read it
    n_entries = read_lines_from_file(fp,user_login_questions);
    fclose(fp);
   }
 
  return n_entries;
}

void user_data_dirname_up(void)
{
  dirname_up(user_data_dir);
}

void user_data_dirname_down(char *subdir)
{
  DIR *dir;

  // The space for user_data_dir has to have sufficient memory
  // available for concatenating subdir and a possible final "/",
  // hence the +2s.
  if (user_data_dir != NULL) {
    user_data_dir = (char*) realloc(user_data_dir,(strlen(user_data_dir) + strlen(subdir) + 2)*sizeof(char));
    if (user_data_dir == NULL) {
      fprintf(stderr,"Error allocating memory in user_data_dirname_down.\n");
      exit(EXIT_FAILURE);
    }
    strcat(user_data_dir,subdir);
  }
  else {
    user_data_dir = (char*) malloc((strlen(subdir)+2)*sizeof(char));
    if (user_data_dir == NULL) {
      fprintf(stderr,"Error allocating memory in user_data_dirname_down.\n");
      exit(EXIT_FAILURE);
    }
    strcpy(user_data_dir,subdir);
  }
  strcat(user_data_dir,"/");
  dir = opendir(user_data_dir);
  if (dir == NULL) {
    printf("User data directory cannot be opened, there is a configuration error\n");
    printf("Continuing anyway without saving or loading individual settings.\n");
  }
  else {
    closedir(dir);
    // If we have multi-user logins, don't create restrictive
    // permissions on new or rewritten files
    umask(0x0);
  }
}


/***********************************************************
*                                                          *
*       "Private methods" with file scope only             *
*                                                          *
***********************************************************/


/* This function does the heavy lifting, so to speak:     */
/* Note that file_type simply indicates whether or not    */
/* to change admin-only settings such as per_user_config. */
/* FIXME return value only tells whether file pointer valid */
int read_config_file(FILE *fp, int file_type)
{
  char buf[PATH_MAX];
  char *parameter, *param_begin, *param_end, *value, *value_end;
  DIR *dir;

  #ifdef TUXMATH_DEBUG
  printf("\nEntering read_config_file()\n");
  #endif

  /* get out if file pointer invalid: */
  if(!fp)
  {
    #ifdef TUXMATH_DEBUG
    printf("config file pointer invalid!\n");
    printf("Leaving read_config_file()\n");
    #endif

    fprintf(stderr, "config file pointer invalid!\n");
    return 0;
  }

  /* make sure we start at beginning: */
  rewind(fp);

  /* read in a line at a time: */
  while (fgets (buf, PATH_MAX, fp))
  { 
    #ifdef TUXMATH_DEBUG
    //printf("Beginning fgets() loop\n");
    #endif
    /* "parameter" and "value" will contain the non-whitespace chars */
    /* before and after the '=' sign, respectively.  e.g.:           */
    /*                                                               */
    /* fullscreen = 0;                                               */
    /* parameter is "fullscreen"                                     */
    /* value is '0'                                                  */
    /*                                                               */

    /* ignore comment lines */
    if ((buf[0] == ';') || (buf[0] == '#'))
    {
      #ifdef TUXMATH_DEBUG
      //printf("Skipping comment line\n");
      #endif
      continue;
    }
 
    /* First find parameter string and make a copy: */
    /* start at beginning: */
    param_begin = buf;
    /* skip leading whitespace */
    while (isspace(*param_begin))
    {
      ++param_begin;
    }

    /* If this was a blank line, then we don't have to process any more */
    if (param_begin-buf >= strlen(buf))
      continue;

    /* now go from here to end of string, stopping at either */
    /* whitespace or '=':   */
    param_end = param_begin;
    while (!isspace(*param_end)
         && ('=' != (*param_end)))
    {
      ++param_end;
    }

    /* copy chars from start of non-whitespace up to '=': */
    //parameter = strndup(param_begin, (param_end - param_begin));

    /* Next three lines do same as strndup(), which may not be available: */
    parameter = malloc((sizeof(char) * (param_end - param_begin)) + 1);
    strncpy(parameter, param_begin, (param_end - param_begin));
    parameter[param_end - param_begin] = '\0';
 
   /* Now get value string: */
    /* set value to first '=' in line: */
    value = strchr(buf, '=');

    if (!value || (value == buf))
    {
      #ifdef TUXMATH_DEBUG
      //fprintf(stderr, "Error while reading prefs - line with no '='!\n");
      #endif

      free(parameter);
      continue;
    }

    /* move past '=' sign: */
    ++value;

    /* skip leading whitespace */
    while (isspace(*value))
    { 
      ++value;
    }

    value_end = value;

    /* remove trailing whitespace or newline */
    while (!isspace(*value_end)
         && (0x0a != (*value_end))
         && (*value_end))
    {
      ++value_end;
    }
    /* terminate string here: */
    *value_end = 0;

    tmdprintf("parameter = '%s'\t, length = %zu\n", parameter, strlen(parameter));
    tmdprintf("value = '%s'\t, length = %zu\t, atoi() = %d\t, atof() = %.2f\n", value, strlen(value), atoi(value), atof(value));
    
    /* Now ready to handle each name/value pair! */
    
    /* Set general game_options struct (see tuxmath.h): */ 
    if(0 == strcasecmp(parameter, "per_user_config"))
    {
      /* Only let administrator change this setting */
      if (file_type == GLOBAL_CONFIG_FILE) 
      {
        int v = str_to_bool(value);
        if (v != -1)
          Opts_SetPerUserConfig(v);
      }
    }

    else if(0 == strcasecmp(parameter, "homedir"))
    {
      /* Only let administrator change this setting */
      if (file_type == GLOBAL_CONFIG_FILE && user_data_dir == NULL)
      {
        /* Check to see whether the specified homedir exists */
        dir = opendir(value);
        if (dir == NULL)
          fprintf(stderr,"homedir: %s is not a directory, or it could not be read\n", value);
        else {
          set_user_data_dir(value);  /* copy the homedir setting */
          closedir(dir);
        }
      }
    }

    else if(0 == strcasecmp(parameter, "use_sound"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetUseSound(v);
    }
    else if(0 == strcasecmp(parameter, "menu_sound"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetMenuSound(v);
    }

    else if(0 == strcasecmp(parameter, "menu_music"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetMenuMusic(v);
    }

    else if(0 == strcasecmp(parameter, "fullscreen"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetFullscreen(v);
    }

    else if(0 == strcasecmp(parameter, "use_bkgd"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetUseBkgd(v);
    }

    else if(0 == strcasecmp(parameter, "demo_mode"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetDemoMode(v);
    }

    else if(0 == strcasecmp(parameter, "oper_override"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetOperOverride(v);
    }

    else if(0 == strcasecmp(parameter, "use_keypad"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetUseKeypad(v);
    }

    else if(0 == strcasecmp(parameter, "allow_pause"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetAllowPause(v);
    }

    else if(0 == strcasecmp(parameter, "use_igloos"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetUseIgloos(v);
    }

    else if(0 == strcasecmp(parameter, "bonus_comet_interval"))
    {
      Opts_SetBonusCometInterval(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "bonus_speed_ratio"))
    {
      Opts_SetBonusSpeedRatio(atof(value));
    }

    else if(0 == strcasecmp(parameter, "save_summary"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetSaveSummary(v);
    }

    else if(0 == strcasecmp(parameter, "speed"))
    {
      Opts_SetSpeed(atof(value));
    }

    else if(0 == strcasecmp(parameter, "use_feedback"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetUseFeedback(v);
    }

    else if(0 == strcasecmp(parameter, "danger_level"))
    {
      Opts_SetDangerLevel(atof(value));
    }

    else if(0 == strcasecmp(parameter, "danger_level_speedup"))
    {
      Opts_SetDangerLevelSpeedup(atof(value));
    }

    else if(0 == strcasecmp(parameter, "danger_level_max"))
    {
      Opts_SetDangerLevelMax(atof(value));
    }

    else if(0 == strcasecmp(parameter, "city_explode_handicap"))
    {
      Opts_SetCityExplHandicap(atof(value));
    }

    else if(0 == strcasecmp(parameter, "allow_speedup"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetAllowSpeedup(v);
    }

    else if(0 == strcasecmp(parameter, "speedup_factor"))
    {
      Opts_SetSpeedupFactor(atof(value));
    }

    else if(0 == strcasecmp(parameter, "max_speed"))
    {
      Opts_SetMaxSpeed(atof(value));
    }

    else if(0 == strcasecmp(parameter, "slow_after_wrong"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        Opts_SetSlowAfterWrong(v);
    }

    else if(0 == strcasecmp(parameter, "starting_comets"))
    {
      Opts_SetStartingComets(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "extra_comets_per_wave"))
    {
      Opts_SetExtraCometsPerWave(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "max_comets"))
    {
      Opts_SetMaxComets(atoi(value));
    }

    #ifndef MC_USE_NEWARC
    /* Begin setting of math question options (see mathcards.h):   */ 

    /* General math options */

    else if(0 == strcasecmp(parameter, "allow_negatives"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetAllowNegatives(v);
    }

    else if(0 == strcasecmp(parameter, "max_answer"))
    {
      MC_SetMaxAnswer(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "max_questions"))
    {
      MC_SetMaxQuestions(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "play_through_list"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetPlayThroughList(v);
    }

    else if(0 == strcasecmp(parameter, "repeat_wrongs"))
    {
      int v = str_to_bool(value);
      if (v != -1) 
        MC_SetRepeatWrongs(v);
    }

    else if(0 == strcasecmp(parameter, "copies_repeated_wrongs"))
    {
      MC_SetCopiesRepeatedWrongs(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "question_copies"))
    {
        MC_SetQuestionCopies(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "randomize"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetRandomize(v);
    }

    else if(0 == strcasecmp(parameter, "fraction_to_keep"))
    {
        MC_SetFractionToKeep(atof(value));
    }

    else if(0 == strcasecmp(parameter, "format_answer_last"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatAnswerLast(v);
    }

    else if(0 == strcasecmp(parameter, "format_answer_first"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatAnswerFirst(v);
    }

    else if(0 == strcasecmp(parameter, "format_answer_middle"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatAnswerMiddle(v);
    }

    else if(0 == strcasecmp(parameter, "format_add_answer_last"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatAddAnswerLast(v);
    }

    else if(0 == strcasecmp(parameter, "format_add_answer_first"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatAddAnswerFirst(v);
    }

    else if(0 == strcasecmp(parameter, "format_add_answer_middle"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatAddAnswerMiddle(v);
    }

    else if(0 == strcasecmp(parameter, "format_sub_answer_last"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatSubAnswerLast(v);
    }

    else if(0 == strcasecmp(parameter, "format_sub_answer_first"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatSubAnswerFirst(v);
    }

    else if(0 == strcasecmp(parameter, "format_sub_answer_middle"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatSubAnswerMiddle(v);
    }

    else if(0 == strcasecmp(parameter, "format_mult_answer_last"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatMultAnswerLast(v);
    }

    else if(0 == strcasecmp(parameter, "format_mult_answer_first"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatMultAnswerFirst(v);
    }

    else if(0 == strcasecmp(parameter, "format_mult_answer_middle"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatMultAnswerMiddle(v);
    }

    else if(0 == strcasecmp(parameter, "format_div_answer_last"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatDivAnswerLast(v);
    }

    else if(0 == strcasecmp(parameter, "format_div_answer_first"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatDivAnswerFirst(v);
    }

    else if(0 == strcasecmp(parameter, "format_div_answer_middle"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetFormatDivAnswerMiddle(v);
    }


    /* Set the allowed math operations: */


    else if(0 == strcasecmp(parameter, "addition_allowed"))
    {
      MC_SetAddAllowed(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "subtraction_allowed"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetSubAllowed(v);
    }

    else if(0 == strcasecmp(parameter, "multiplication_allowed"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetMultAllowed(v);
    }

    else if(0 == strcasecmp(parameter, "division_allowed"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetDivAllowed(v);
    }

    else if(0 == strcasecmp(parameter, "typing_practice_allowed"))
    {
      int v = str_to_bool(value);
      if (v != -1)
        MC_SetTypingAllowed(v);
    }

    /* Set min and max for addition: */


    else if(0 == strcasecmp(parameter, "min_augend"))
    {
      MC_SetAddMinAugend(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "max_augend"))
    {
      MC_SetAddMaxAugend(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "min_addend"))
    {
      MC_SetAddMinAddend(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "max_addend"))
    {
      MC_SetAddMaxAddend(atoi(value));
    }


    /* Set min and max for subtraction: */


    else if(0 == strcasecmp(parameter, "min_minuend"))
    {
      MC_SetSubMinMinuend(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "max_minuend"))
    {
      MC_SetSubMaxMinuend(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "min_subtrahend"))
    {
      MC_SetSubMinSubtrahend(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "max_subtrahend"))
    {
      MC_SetSubMaxSubtrahend(atoi(value));
    }


    /* Set min and max for multiplication: */


    else if(0 == strcasecmp(parameter, "min_multiplier"))
    {
      MC_SetMultMinMultiplier(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "max_multiplier"))
    {
      MC_SetMultMaxMultiplier(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "min_multiplicand"))
    {
      MC_SetMultMinMultiplicand(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "max_multiplicand"))
    {
      MC_SetMultMaxMultiplicand(atoi(value));
    }


    /* Set min and max for division: */


    else if(0 == strcasecmp(parameter, "min_divisor"))
    {
      MC_SetDivMinDivisor(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "min_quotient"))
    {
      MC_SetDivMinQuotient(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "max_divisor"))
    {
      MC_SetDivMaxDivisor(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "max_quotient"))
    {
      MC_SetDivMaxQuotient(atoi(value));
    }


    /* Set min and max for typing practice: */


    else if(0 == strcasecmp(parameter, "min_typing_num"))
    {
      MC_SetTypeMin(atoi(value));
    }

    else if(0 == strcasecmp(parameter, "max_typing_num"))
    {
      MC_SetTypeMax(atoi(value));
    }

    #endif
    else
    {   
      MC_SetOp(parameter, atoi(value) ); //automatically handles bad parameters
    }
    free(parameter);
  }
  //handle min > max by disallowing operation
  if (MC_GetOpt(MIN_AUGEND) > MC_GetOpt(MAX_AUGEND) || 
      MC_GetOpt(MIN_ADDEND) > MC_GetOpt(MAX_ADDEND) )
      MC_SetOpt(ADDITION_ALLOWED, 0);
  if (MC_GetOpt(MIN_MINUEND) > MC_GetOpt(MAX_MINUEND) || 
      MC_GetOpt(MIN_SUBTRAHEND) > MC_GetOpt(MAX_SUBTRAHEND) )
      MC_SetOpt(SUBTRACTION_ALLOWED, 0);
  if (MC_GetOpt(MIN_MULTIPLICAND) > MC_GetOpt(MAX_MULTIPLICAND) || 
      MC_GetOpt(MIN_MULTIPLIER) > MC_GetOpt(MAX_MULTIPLIER) )
      MC_SetOpt(MULTIPLICATION_ALLOWED, 0);
  if (MC_GetOpt(MIN_DIVISOR) > MC_GetOpt(MAX_DIVISOR) || 
      MC_GetOpt(MIN_QUOTIENT) > MC_GetOpt(MAX_QUOTIENT) )
      MC_SetOpt(DIVISION_ALLOWED, 0);
  if (MC_GetOpt(MIN_TYPING_NUM) > MC_GetOpt(MAX_TYPING_NUM) )
      MC_SetOpt(TYPING_PRACTICE_ALLOWED, 0);
      
  #ifdef TUXMATH_DEBUG
  printf("\nAfter file read in:\n");
  write_config_file(stdout, 0);
  printf("Leaving read_config_file()\n");
  #endif

  return 1;
}



int write_user_config_file(void)
{
  char opt_path[PATH_MAX];
  FILE* fp;

  if (!find_tuxmath_dir())
  {
    fprintf(stderr, "\nCould not find or create tuxmath dir\n");
    return 0;
  }

  /* find $HOME and add rest of path to config file: */
  get_user_data_dir_with_subdir(opt_path);
  strcat(opt_path, OPTIONS_FILENAME);

  #ifdef TUXMATH_DEBUG
  printf("\nIn write_user_config_file() full path to config file is: = %s\n", opt_path);
  #endif

  /* save settings: */
  fp = fopen(opt_path, "w");
  if (fp)
  {
    write_config_file(fp, 1);
    fclose(fp);
    fp = NULL;
    return 1;
  }
  else
    return 0;
}



/* this function writes the settings for all game options to a */
/* human-readable file.                                        */
int write_config_file(FILE *fp, int verbose)
{
  int i, vcommentsprimed = 0;
  static char* vcomments[NOPTS]; //comments when writing out verbose
  if (!vcommentsprimed) //we only want to initialize these once
  {
    vcommentsprimed = 1;
    for (i = 0; i < NOPTS; ++i)
      vcomments[i] = NULL;
    vcomments[PLAY_THROUGH_LIST] =
    "############################################################\n"
    "#                                                          #\n"
    "#              Tuxmath Configuration File                  #\n"
    "#                                                          #\n"
    "# The behavior of Tuxmath can be controlled to a great     #\n"
    "# extent by editing this file with any and saving it in    #\n"
    "# the default options location ($HOME/.tuxmath/options).   #\n"
    "# The file consists of 'NAME = VALUE' pairs, one pair per  #\n"
    "# line. Each option is one of the following types:         #\n"
    "#                                                          #\n"
    "#     boolean: 1 (synonyms 'true', 'T', 'yes', 'Y', 'on')  #\n"
    "#              or                                          #\n"
    "#              0 (synonyms 'false, 'F', 'no', 'N', 'off')  #\n"
    "#     integer  (i.e. non-fractional numbers)               #\n"
    "#     float    (i.e decimal fractions)                     #\n"
    "#                                                          #\n"
    "# Lines beginning with '#' or ';' are ignored as comments. #\n"
    "# The synonyms for boolean '0' and '1' are accepted as     #\n"
    "# input, but always written as '0' or '1' when Tuxmath     #\n"
    "# writes a config file to disk.                            #\n"
    "# The file is organized with the more important options    #\n"
    "# first.                                                   #\n"
    "############################################################\n"
    "\n"
    "############################################################\n"
    "#                                                          #\n"
    "#                       Game Mode                          #\n"
    "#                                                          #\n"
    "# Parameter: play_through_list (Boolean)                   #\n"
    "# Default: 1                                               #\n"
    "#                                                          #\n"
    "# Tuxmath generates a list of math questions based on      #\n"
    "# parameters set below.  By default, (play_through_list =  #\n"
    "# 1) the questions are asked in a random order.            #\n"
    "# Correctly answered questions are removed from the list.  #\n"
    "# If the player fails to correctly answer a question       #\n"
    "# before it hits a city, the question will be reinserted   #\n"
    "# into the list in a random location.                      #\n"
    "# The player wins if all questions are answered correctly  #\n"
    "# before the cities are destroyed.                         #\n"
    "#                                                          #\n"
    "# Alternatively, Tuxmath can be played in 'Arcade Mode'    #\n"
    "# by setting play_through_list = 0 (i.e. 'false'). If this #\n"
    "# is done, all questions will be randomly reinserted into  #\n"
    "# the list whether or not they are answered correctly, and #\n"
    "# the game continues as long as there is a surviving city. #\n"
    "############################################################\n"
    "\n";                                                           
    
    vcomments[ADDITION_ALLOWED] = 
    "\n############################################################\n"
    "#                                                          #\n"
    "#               Selecting Math Operations                  #\n"
    "#                                                          #\n"
    "# Parameter: addition_allowed (boolean)                    #\n"
    "# Default: 1                                               #\n"
    "# Parameter: subtraction_allowed (boolean)                 #\n"
    "# Default: 1                                               #\n"
    "# Parameter: multiplication_allowed (boolean)              #\n"
    "# Default: 1                                               #\n"
    "# Parameter: division_allowed (boolean)                    #\n"
    "# Default: 1                                               #\n"
    "#                                                          #\n"
    "# These options enable questions for each of the four math #\n"
    "# operations.  All are 1 (yes) by default.                 #\n"
    "############################################################\n\n";
    vcomments[TYPING_PRACTICE_ALLOWED] =
    "\n############################################################\n"
    "#                                                          #\n"
    "#                    Typing Practice                       #\n"
    "#                                                          #\n"
    "# Parameter: typing_practice_allowed (boolean)             #\n"
    "# Default: 0                                               #\n"
    "#                                                          #\n"
    "# This option simply displays numbers for the youngest     #\n"
    "# players to type in to learn the keyboard.                #\n"
    "############################################################\n\n";
    vcomments[ALLOW_NEGATIVES] =
    "\n############################################################\n"
    "#                                                          #\n"
    "#                 Negative Number Support                  #\n"
    "#                                                          #\n"
    "# Parameter: allow_negatives (boolean)                     #\n"
    "# Default: 0                                               #\n"
    "#                                                          #\n"
    "# 'allow_negatives' allows or disallows use of negative    #\n"
    "# numbers as both operands and answers.  Default is 0      #\n"
    "# (no), which disallows questions like:                    #\n"
    "#          2 - 4 = ?                                       #\n"
    "# Note: this option must be enabled in order to set the    #\n"
    "# operand ranges to include negatives. If it is changed    #\n"
    "# from 1 (yes) to 0 (no), any negative operand limits will #\n"
    "# be reset to 0.                                           #\n"
    "############################################################\n\n";
    vcomments[MIN_AUGEND] = 
    "\n############################################################\n"
    "#                                                          #\n"
    "#      Minimum and Maximum Values for Operand Ranges       #\n"
    "#                                                          #\n"
    "# Parameters: (multiple - all integer type)                #\n"
    "#                                                          #\n"
    "# Operand limits can be set to any integer up to the       #\n"
    "# value of 'max_answer'. Tuxmath will generate questions   #\n"
    "# for every value in the specified range. The maximum must #\n"
    "# be greater than or equal to the corresponding minimum    #\n"
    "# for any questions to be generated for that operation.    #\n"
    "# Defaults are 0 for minima and 12 for maxima.             #\n"
    "#                                                          #\n"
    "# Note: 'allow_negatives' must be set to 1 for negative    #\n"
    "# values to be accepted (see 'Advanced Options').          #\n"
    "############################################################\n"
    "\n# Addition operands:\n"
    "# augend + addend = sum\n\n";
    vcomments[MIN_MINUEND] = 
    "\n# Subtraction operands:\n"
    "# minuend - subtrahend = difference\n\n";
    vcomments[MIN_MULTIPLIER] = 
    "\n# Multiplication operands:\n"
    "# multiplier * multiplicand = product\n\n";
    vcomments[MIN_DIVISOR] = 
    "\n# Division operands:\n"
    "# dividend / divisor = quotiend\n\n";
    vcomments[MIN_TYPING_NUM] =
    "\n# Typing practice:\n";
    vcomments[QUESTION_COPIES] = 
    "\n\n\n############################################################\n"
    "#                                                          #\n"
    "#                   Advanced Options                       #\n"
    "#                                                          #\n"
    "# The remaining settings further customize Tuxmath's       #\n"
    "# behavior.  Most users will probably not change them.     #\n"
    "############################################################\n\n";
    
    "\n############################################################\n"
    "#                                                          #\n"
    "#           Advanced Math Question List Options            #\n"
    "#                                                          #\n"
    "# Parameter: question_copies (integer)                     #\n"
    "# Default: 1                                               #\n"
    "# Parameter: repeat_wrongs (boolean)                       #\n"
    "# Default: 1                                               #\n"
    "# Parameter: copies_repeated_wrongs (integer)              #\n"
    "# Default: 1                                               #\n"
    "# Parameter: fraction_to_keep (float)                      #\n"
    "# Default: 1                                               #\n"
    "#                                                          #\n"
    "# These settings offer further control over the question   #\n"
    "# list and are generally only useful if 'play_through_list'#\n"
    "# is enabled (as it is by default).                        #\n"
    "#                                                          #\n"
    "# 'question_copies' is the number of times each question   #\n"
    "# is put into the initial list. It can be 1 to 10.         #\n"
    "#                                                          #\n"
    "# 'repeat_wrongs' determines whether questions the player  #\n"
    "# failed to answer correctly will be asked again.          #\n"
    "#                                                          #\n"
    "# 'copies_repeated_wrongs' gives the number of times a     #\n"
    "# missed question will reappear. This can be set anywhere  #\n"
    "# from 1 to 10.                                            #\n"
    "#                                                          #\n"
    "# 'fraction_to_keep' allows a list to be generated that    #\n"
    "# consists of a randomly-selected subset of the questions  #\n"
    "# fitting the criteria.  The parameter is a float that     #\n"
    "# must be greater than 0 and less than or equal to 1. For  #\n"
    "# example, a value of 0.1 means 10%% of the questions      #\n"
    "# meeting the criteria will go into the list.              #\n"
    "#                                                          #\n"
    "# The defaults for these values result in a 'mission'      #\n" 
    "# for Tux that is accomplished by answering all            #\n"
    "# questions correctly with at least one surviving city.    #\n"
    "############################################################\n\n";
    vcomments[FORMAT_ADD_ANSWER_LAST] =
    "\n############################################################\n"
    "#                                                          #\n"
    "#                 Math Question Formats                    #\n"
    "#                                                          #\n"
    "# The 'format_<op>_answer_<place>  options control         #\n"
    "# generation of questions with the answer in different     #\n"
    "# places in the equation.  i.e.:                           #\n"
    "#                                                          #\n"
    "#    format_add_answer_last:    2 + 2 = ?                  #\n"
    "#    format_add_answer_first:   ? + 2 = 4                  #\n"
    "#    format_add_answer_middle:  2 + ? = 4                  #\n"
    "#                                                          #\n"
    "# By default, 'format_answer_first' is enabled and the     #\n"
    "# other two formats are disabled.  Note that the options   #\n"
    "# are not mutually exclusive - the question list may       #\n"
    "# contain questions with different formats.                #\n"
    "#                                                          #\n"
    "# The formats are set independently for each of the four   #\n"
    "# math operations. All parameters are type 'boolean'.      #\n"
    "############################################################\n\n";
    vcomments[MAX_ANSWER] = 
    "\n############################################################\n"
    "#                                                          #\n"
    "# Parameter: max_answer (integer)                          #\n"
    "# Default: 999                                             #\n"
    "#                                                          #\n"
    "# 'max_answer' is the largest absolute value allowed in    #\n"
    "# any value in a question (not only the answer). Default   #\n"
    "# is 999, which is as high as it can be set. It can be set #\n"
    "# lower to fine-tune the list for certain 'lessons'.       #\n"
    "############################################################\n\n";
    vcomments[MAX_QUESTIONS] = 
    "\n############################################################\n"
    "#                                                          #\n"
    "# Parameter: max_questions (integer)                       #\n"
    "# Default: 5000                                            #\n"
    "#                                                          #\n"
    "# 'max_questions' is limit of the length of the question   #\n"
    "# list. Default is 5000 - only severe taskmasters will     #\n"
    "# need to raise it!                                        #\n"
    "############################################################\n\n";
    vcomments[RANDOMIZE] = 
    "\n############################################################\n"
    "#                                                          #\n"
    "# Parameter: randomize (boolean)                           #\n"
    "# Default: 1                                               #\n"
    "#                                                          #\n"
    "# If 'randomize' selected, the list will be shuffled       #\n"
    "# at the start of the game. Otherwise, the questions       #\n"
    "# appear in the order the program generates them.          #\n"
    "############################################################\n\n";
    
  }
  
  tmdprintf("\nEntering write_config_file()\n");

  /* get out if file pointer null */
  if(!fp)
  {
    fprintf (stderr, "write_config_file() - file pointer invalid/n");
    tmdprintf("Leaving write_config_file()\n");
    return 0;
  }
  
  for (i = 0; i < NOPTS; ++i) //for each option
  {
    if (verbose && vcomments[i]) //comment goes before
      fprintf(fp, vcomments[i]);
    fprintf(fp, "%s = %d\n", MC_OPTION_TEXT[i], MC_GetOpt(i) );
  }
  
  if (verbose)
  {
    //allow_speedup comment
  }
  fprintf(fp, "allow_speedup = %d\n", Opts_AllowSpeedup() );
  
  if (verbose)
  {
    //use_sound comment
  } 
  fprintf(fp, "use_sound = %d\n", Opts_UseSound() );
  
  if (verbose)
  {
    fprintf (fp, "\n############################################################\n" 
                 "#                                                          #\n"
                 "#                Advanced Comet Speed Options              #\n"
                 "#                                                          #\n"
                 "# Parameter: starting_comets (integer)                     #\n"
                 "# Default: 2                                               #\n"
                 "# Parameter: extra_comets_per_wave (integer)               #\n"
                 "# Default: 2                                               #\n"
                 "# Parameter: max_comets (integer)                          #\n"
                 "# Default: 10                                              #\n"
                 "# Parameter: speed (float)                                 #\n"
                 "# Default: 1.00                                            #\n"
                 "# Parameter: max_speed (float)                             #\n"
                 "# Default: 10.00                                           #\n"
                 "# Parameter: speedup_factor (float)                        #\n"
                 "# Default: 1.20                                            #\n"
                 "# Parameter: bonus_comet_interval (integer)                #\n"
                 "# Default: 10                                              #\n"
                 "# Parameter: bonus_speed_ratio (float)                     #\n"
                 "# Default: 1.50                                            #\n"
                 "# Parameter: slow_after_wrong (bool)                       #\n"
                 "# Default: 0                                               #\n"
                 "#                                                          #\n"
                 "# (for 'feedback' speed control system):                   #\n"
                 "# Parameter: danger_level (float)                          #\n"
                 "# Default: 0.35                                            #\n"
                 "# Parameter: danger_level_speedup (float)                  #\n"
                 "# Default: 1.1                                             #\n"
                 "# Parameter: danger_level_max (float)                      #\n"
                 "# Default: 0.9                                             #\n"
                 "# Parameter: city_explode_handicap (float)                 #\n"
                 "# Default: 0                                               #\n"
                 "#                                                          #\n"
                 "# The comet number parameters and initial/max speed apply  #\n"
                 "# whether or not the feedback system is activated.         #\n"
                 "#                                                          #\n"
                 "# 'speedup_factor' and 'slow_after_wrong' only apply if    #\n"
                 "# feedback is not activated.                               #\n"
                 "#                                                          #\n"
                 "# The 'danger_level_*' and 'city_explode_handicap'         #\n"
                 "# parameters are only used if feedback is activated.       #\n"
                 "############################################################\n\n");
  }

  if(verbose)
  {
    fprintf (fp, "\n# Number of comets for first wave. Default is 2.\n");
  }
  fprintf(fp, "starting_comets = %d\n", Opts_StartingComets());

  if(verbose)
  {
    fprintf (fp, "\n# Comets to add for each successive wave. Default is 2.\n");
  }
  fprintf(fp, "extra_comets_per_wave = %d\n", Opts_ExtraCometsPerWave());

  if(verbose)
  {
    fprintf (fp, "\n# Maximum number of comets. Default is 10.\n");
  }
  fprintf(fp, "max_comets = %d\n", Opts_MaxComets());

  if(verbose)
  {
    fprintf (fp, "\n# Starting comet speed. Default is 1.\n");
  }
  fprintf(fp, "speed = %.2f\n", Opts_Speed());

  if(verbose)
  {
    fprintf (fp, "\n# Maximum speed. Default is 10.\n");
  }
  fprintf(fp, "max_speed = %.2f\n", Opts_MaxSpeed());

  if(verbose)
  {
    fprintf (fp, "\n# 'speedup_factor': If feedback is not used but \n"
                 "# 'allow_speedup' is enabled, the comet speed will be\n"
                 "# multiplied by this factor with each new wave.\n"
                 "# Values from 0.5 to 2 are accepted (note that a \n"
                 "# value less than 1 causes the comets to be \n"
                 "# slower with each wave!).\n"
                 "# Default is 1.2 (i.e. 20 percent increase per wave)\n\n");
  }
  fprintf(fp, "speedup_factor = %.2f\n", Opts_SpeedupFactor());


  if(verbose)
  {
    fprintf (fp, "\n# 'bonus_comet_interval' controls how frequently\n"
                 "# special comets appear that cause a igloo to be  \n"
                 "# rebuilt if answered correctly. The bonus comet  \n"
                 "# appears after this number of regular comets (a  \n"
                 "# value of 0 disables bonus comets). Default is 10. \n");
  }
  fprintf(fp, "bonus_comet_interval = %d\n", Opts_BonusCometInterval());


  if(verbose)
  {
    fprintf (fp, "\n# 'bonus_speed_ratio' determines how fast the\n"
                 "# bonus comets fall relative to the regular comets.\n"
                 "# Range 1.0 - 3.0, default 1.5:\n");
  }
  fprintf(fp, "bonus_speed_ratio = %.2f\n", Opts_BonusSpeedRatio());


  if(verbose)
  {
    fprintf (fp, "\n# 'slow_after_wrong' tells Tuxmath to go back to  \n"
                 "# starting speed and number of comets if the player misses \n"
                 "# a question. Useful for smaller kids. Default is 0.\n\n");
  }

  fprintf(fp, "slow_after_wrong = %d\n", Opts_SlowAfterWrong());


  if(verbose)
  {
     fprintf (fp, "\n# (Feedback) Set the desired danger level.\n"
             "# 0 = too safe, comets typically exploded at the very top\n"
             "# 1 = too dangerous, comets typically exploded as they\n"
             "# hit cities. Set it somewhere between these extremes. As\n"
             "# a guideline, early elementary kids might prefer\n"
             "# 0.2-0.3, older kids at around 0.4-0.6. Default 0.35.\n\n");
  }
  fprintf(fp, "danger_level = %.2f\n", Opts_DangerLevel());

  if(verbose)
  {
     fprintf (fp, "\n# (Feedback) Set danger level speedup.\n"
                  "# The margin of safety will decrease by this factor each\n"
                  "# wave. Default 1.1. Note 1 = no increase in danger level.\n\n");
  }
  fprintf(fp, "danger_level_speedup = %.2f\n", Opts_DangerLevelSpeedup());

  if(verbose)
  {
     fprintf (fp, "\n# (Feedback) Set the maximum danger level.\n"
                  "# Default 0.9.\n");
  }
  fprintf(fp, "danger_level_max = %.2f\n", Opts_DangerLevelMax());

  if (verbose)
  { 
     fprintf (fp, "\n# (Feedback) Set the handicap for hitting cities.\n"
                  "# When bigger than 0, this causes the game to slow down\n"
                  "# by an extra amount after a wave in which one or more\n"
                  "# cities get hit. Note that this is similar to\n"
                  "# 'slow_after_wrong', but allows for more gradual\n"
                  "# changes. Default 0 (no extra handicap).\n\n");
  }
  fprintf(fp, "city_explode_handicap = %.2f\n", Opts_CityExplHandicap());

  if(verbose)
  {
    fprintf (fp, "\n\n############################################################\n" 
                 "#                                                          #\n"
                 "#                  Managing User Settings                  #\n"
                 "#                                                          #\n"
                 "# Parameter: per_user_config (boolean)                     #\n"
                 "# Default: 1                                               #\n"
                 "# Parameter: homedir (string)                              #\n"
                 "# Default: <none supplied>                                 #\n"
                 "#                                                          #\n"
                 "# 'per_user_config' determines whether Tuxmath will look   #\n"
                 "# in the user's home directory for settings. Default is 1  #\n"
                 "# (yes). If set to 0, the program will ignore the user's   #\n"
                 "# .tuxmath file and use the the global settings in the     #\n"
                 "# installation-wide config file.                           #\n"
                 "#                                                          #\n"
                 "# 'homedir' allows you to specify the location to look for #\n"
                 "# user home directories. You probably do not want to       #\n"
                 "# specify this unless all users share the same login       #\n"
                 "# account. See the README for details on configuration.    #\n"
                 "# To enable this feature, remove the '#' comment mark and  #\n"
                 "# set the path as desired.                                 #\n"
                 "#                                                          #\n"
                 "# These settings cannot be changed by an ordinary user, as #\n"
                 "# they are ignored unless the config file is Tuxmath's     #\n"
                 "# global config file. Thus, users cannot 'lock themselves  #\n"
                 "# out' by accidentally setting per_user_config to 0.       #\n"
                 "############################################################\n\n");
  }
  fprintf(fp, "per_user_config = %d\n", Opts_PerUserConfig());
  fprintf(fp, "# homedir = /servervolume/tuxmath_users\n");


  /* print general game options (passing '1' as second arg causes */
  /* "help" info for each option to be written to file as comments) */
//  print_game_options(fp, 1);
  /* print options pertaining to math questions from MathCards: */
//  MC_PrintMathOptions(fp, 1);

  #ifdef TUXMATH_DEBUG
  printf("Leaving write_config_file()\n");
  #endif

  return 1;
}


/* write_pregame_summary() and write_postgame_summary() are used to */
/* record data about the player's game to file for review (perhaps by */
/* teacher). write_pregame_summary() is called at the start of each  */
/* game and records the question list along with identifying data. It */
/* also rotates old game summaries to successive filenames, keeping   */
/* the last ten summaries for review. write_postgame_summary()       */
/* the list of questions that were not answered correctly and         */
/* calculates the percent correct.                                    */
int write_pregame_summary(void)
{
  int i;
  FILE* fp;
  char filepath1[PATH_MAX];
  char filepath2[PATH_MAX];

  /* Make sure tuxmath dir exists or can be created: */
  if (!find_tuxmath_dir())
  {
    fprintf(stderr, "\nCould not find or create tuxmath dir\n");
    return 0;
  }



  /* Rotate filenames of old summaries, oldest summary if present */
  /* and leaving summary1 available for current game:             */

  /* find $HOME and tack on file name: */
  get_user_data_dir_with_subdir(filepath1);
  strcat(filepath1, summary_filenames[NUM_SUMMARIES - 1]);

  fp = fopen(filepath1, "r");
  if (fp)
  {
    #ifdef TUXMATH_DEBUG
    printf("\nIn write_pregame_summary() - removing oldest summary file\n");
    #endif

    fclose(fp);
    remove(filepath1);
  }

  /* Now shift each old file back by one:       */
  /* 'filepath1' is the old name for each file, */
  /* 'filepath2' is the new name (i.e. we go from i - 1 to i). */
  for (i = NUM_SUMMARIES - 1; i > 0; i--)
  {
    /* old filename: */
    get_user_data_dir_with_subdir(filepath1);
    strcpy(filepath2,filepath1);
    strcat(filepath1, summary_filenames[i - 1]);
    /* new filename: */
    strcat(filepath2, summary_filenames[i]);
    /* now change the name: */
    rename(filepath1, filepath2);
  } 

  /* summary_filenames[0] (i.e. 'summary1') should now be vacant:     */
  get_user_data_dir_with_subdir(filepath1);
  strcat(filepath1, summary_filenames[0]);

  fp = fopen(filepath1, "w"); /* "w" means start writing with empty file */
  if (fp)
  {
    /* Write header and identifying data for summary file:       */
    fprintf(fp, "************************\n"
                "* Tuxmath Game Summary *\n"
                "************************\n");
    if (add_subdir)
    {
      /* Identify user by login if we're not in a multiuser configuration */
      fprintf(fp, "\nPlayer: %s\n", getenv("USER"));
    }
    else {
      /* Identify user by the directory name.*/
      fprintf(fp, "\nPlayer: %s\n", get_user_name());
    }

    fprintf(fp, "\nMission: %s\n", last_config_file_name);

    /* Write question list:  */
    fprintf(fp, "\nStarting Question List:");
    MC_PrintQuestionList(fp);
    fprintf(fp, "\n\nNumber of Questions: %d", MC_StartingListLength());

    fclose(fp);
    return 1;
  }
  else /* Couldn't write file for some reason: */
  {
    return 0;
  }
}

int write_postgame_summary(void)
{
  FILE *fp;
  char filepath1[PATH_MAX];
  int total_answered;
  float median_time;
  int success = 1;
  int write_column_names = 0;
  time_t filetime;
  struct stat filestat;
  struct tm datetime;
  char *mission_name;

  get_user_data_dir_with_subdir(filepath1);
  strcat(filepath1, summary_filenames[0]);

  total_answered = MC_NumAnsweredCorrectly() + MC_NumNotAnsweredCorrectly();
  median_time = MC_MedianTimePerQuestion();

  fp = fopen(filepath1, "a"); /* "a" means append to end of file */
  if (fp)
  {
    /* Write list of questions missed: */
    fprintf(fp, "\n\n\nList Of Questions Not Answered Correctly:");
                MC_PrintWrongList(fp);
    fprintf(fp, "\n\nNumber Of Distinct Questions Not Answered Correctly: %d",
                MC_WrongListLength());

    /* Write post-game statistics:     */

    fprintf(fp, "\n\nSummary:\n");
    fprintf(fp, "Questions Answered:\t%d\n", total_answered);
    fprintf(fp, "Questions Correct:\t%d\n",
                MC_NumAnsweredCorrectly());
    fprintf(fp, "Questions Missed:\t%d\n",
                MC_NumNotAnsweredCorrectly());
    /* Avoid divide-by-zero errror: */
    if (total_answered)
    {
      fprintf(fp, "Percent Correct:\t%d %%\n", 
              ((MC_NumAnsweredCorrectly() * 100)/ total_answered) );
    }
    else
      fprintf(fp, "Percent Correct: (not applicable)\n");

    fprintf(fp,"Median Time/Question:\t%g\n",median_time);

    fprintf(fp, "Mission Accomplished:\t");
    if (MC_MissionAccomplished())
    {
      fprintf(fp, "Yes!\n\n8^)\n");
    }
    else
    {
      fprintf(fp, "No.\n\n:^(\n");
    }

    fclose(fp);
  }
  else /* Couldn't write file for some reason: */
  {
    fprintf(stderr,"Summary not written.\n");
    success = 0;
  }

  /* Append brief summary to log */
  if (total_answered > 0) {
    /* We're going to want to write the date.  Use the filetime  */
    /* rather than calling "time" directly, because "time"       */
    /* returns the time according to whatever computer is        */
    /* running tuxmath, and in a server/client mode it's likely  */
    /* that some of the clients' clocks may be wrong. Use      */
    /* instead the time according to the server on which the     */
    /* accounts are stored, which can be extracted from the      */
    /* modification time of the summary we just wrote.           */
    if (stat(filepath1,&filestat) == 0) {
      filetime = filestat.st_mtime;
    } else {
      filetime = time(NULL);
    }
    localtime_r(&filetime,&datetime); /* generate broken-down time */

    get_user_data_dir_with_subdir(filepath1);
    strcat(filepath1, "log.csv");
    /* See whether the log file already exists; if not, write */
    /* the column names */
    fp = fopen(filepath1, "r");
    if (fp == NULL)
      write_column_names = 1;
    else
      fclose(fp);
    
    fp = fopen(filepath1, "a"); /* "a" means append to end of file */
    if (fp) {
      if (write_column_names) {
        fprintf(fp,"\"User\",\"Mission\",\"Date\",\"Completed?\",\"Number answered\",\"Percent correct\",\"Time per question\"\n");
      }
      mission_name = strdup(last_config_file_name);
      fprintf(fp,"\"%s\",\"%s\",%d/%d/%d,%d,%d,%d,%g\n", get_user_name(), get_file_name(mission_name), datetime.tm_year+1900, datetime.tm_mon+1, datetime.tm_mday, MC_MissionAccomplished(), total_answered, ((MC_NumAnsweredCorrectly() * 100)/ total_answered), median_time);
      fclose(fp);
      free(mission_name);
    } else
      success = 0;
  }
  return success;
}



/* Checks to see if user's .tuxmath directory exists and, if not, tries  */
/* to create it. Returns 1 if .tuxmath dir found or successfully created */
static int find_tuxmath_dir(void)
{
  char opt_path[PATH_MAX];
  DIR* dir_ptr;

  /* find $HOME */
  get_user_data_dir_with_subdir(opt_path);

  #ifdef TUXMATH_DEBUG
  printf("\nIn find_tuxmath_dir() tuxmath dir is: = %s\n", opt_path);
  #endif

  /* find out if directory exists - if not, create it: */
  dir_ptr = opendir(opt_path);
  if (dir_ptr)  /* don't leave DIR* open if it was already there */
  {
    #ifdef TUXMATH_DEBUG
    printf("\nIn find_tuxmath_dir() tuxmath dir opened OK\n");
    #endif

    closedir(dir_ptr);
    return 1;
  }
  else /* need to create tuxmath config directory: */
  {
    FILE* fp;
    int status;

    if (!add_subdir)
      return 0;      // fail if the user specified a directory, but it doesn't exist

    /* if user's home has a _file_ named .tuxmath (as from previous version */
    /* of program), need to get rid of it or directory creation will fail:  */
    fp = fopen(opt_path, "r");
    if (fp)
    {
      #ifdef TUXMATH_DEBUG
      printf("\nIn find_tuxmath_dir() - removing old .tuxmath file\n");
      #endif

      fclose(fp);
      remove(opt_path);
    }

    #ifdef TUXMATH_DEBUG
    printf("\nIn find_tuxmath_dir() - trying to create .tuxmath dir\n");
    #endif

    //status = mkdir(opt_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

#ifndef BUILD_MINGW32
    status = mkdir(opt_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#else
    status = mkdir(opt_path);
#endif

    #ifdef TUXMATH_DEBUG
    printf("\nIn find_tuxmath_dir() - mkdir returned: %d\n", status);
    #endif

    /* mkdir () returns 0 if successful */
    if (0 == status)
    {
      fprintf(stderr, "\nfind_tuxmath_dir() - $HOME" OPTIONS_SUBDIR " created\n");
      return 1;
    }
    else
    {
      fprintf(stderr, "\nfind_tuxmath_dir() - mkdir failed\n");
      return 0;
    }
  }
}



/* A utility function to read lines from a textfile.  Upon exit, it */
/* returns the # of lines successfully read, and sets the pointer   */
/* array so that (*lines)[i] is a pointer to the text on the ith    */
/* line.  Note this function also cleans up trailing whitespace,    */
/* and skips blank lines.                                           */
/* On entry, *lines must be NULL, as a sign that any previously     */
/* allocated memory has been freed.                                 */
static int read_lines_from_file(FILE *fp,char ***lines)
{
  char *fgets_return_val;
  char name_buf[NAME_BUF_SIZE];
  int n_entries;
  int length;

  n_entries = 0;
  if(*lines != NULL) {
    printf("Error: lines buffer was not NULL upon entry");
    exit(EXIT_FAILURE);
  }

  fgets_return_val = fgets(name_buf,NAME_BUF_SIZE,fp);
  while (fgets_return_val != NULL) {
    // Strip terminal whitespace and \r
    length = strlen(name_buf);
    while (length>0 && (name_buf[length - 1] == '\r' || name_buf[length - 1] == '\n'|| name_buf[length-1] == ' ' || name_buf[length-1] == '\t')) {
      name_buf[length - 1] = '\0';
      length--;
    }
    if (length == 0) {
      // If we get to a blank line, skip over it
      fgets_return_val = fgets(name_buf,NAME_BUF_SIZE,fp);
      continue;
    }
    n_entries++;
    *lines = (char**) realloc(*lines,n_entries*sizeof(char*));
    if (*lines == NULL) {
      // Memory allocation error
      printf("Error #1 allocating memory in read_lines_from_file\n");
      exit(EXIT_FAILURE);
    }
    // Copy the cleaned-up line to the list
    (*lines)[n_entries-1] = strdup(name_buf);
    if ((*lines)[n_entries-1] == NULL) {
      // Memory allocation error
      printf("Error #2 allocating memory in read_lines_from_file\n");
      exit(EXIT_FAILURE);
    }
    // Read the next line
    fgets_return_val = fgets(name_buf,NAME_BUF_SIZE,fp);
  }
  return n_entries;
}

/* A utility function to go up one level in a directory hierarchy */
static void dirname_up(char *dirname)
{
  int len;

  len = strlen(dirname);
  // Pass over all trailing "/"
  while (len > 0 && dirname[len-1] == '/')
    len--;

  // Now pass over all non-"/" characters at the end
  while (len > 0 && dirname[len-1] != '/')
    len--;
  
  // Terminate the string after that next-to-last "/"
  dirname[len] = '\0';
}

/* Identify user by the directory name. We don't want to use the */
/* whole path, just the name of the last subdirectory. */
static char* get_user_name(void)
{
  char filepath2[PATH_MAX];

  get_user_data_dir_with_subdir(filepath2);
  return get_file_name(filepath2);
}

/* Extract the last "field" in a full pathname */
static char* get_file_name(char *fullpath)
{
  char *file_name;

  file_name = &fullpath[strlen(fullpath)-1];
  /* Chop off trailing "/" */
  while (file_name > &fullpath[0] && *file_name == '/') {
    *file_name = '\0';
    file_name--;
  }
  /* Back up to the next "/" */
  while (file_name > &fullpath[0] && *file_name != '/')
    file_name--;

  return ++file_name;
}


/* Allows use of "true", "YES", T, etc. in text file for boolean values. */
/* Return value of -1 means value string is not recognized.              */
static int str_to_bool(const char* val)
{
  char* ptr;

  /* Check for recognized boolean strings: */
  if ((0 == strcasecmp(val, "true"))
    ||(0 == strcasecmp(val, "t"))
    ||(0 == strcasecmp(val, "yes"))
    ||(0 == strcasecmp(val, "y"))
    ||(0 == strcasecmp(val, "on")))
  {
    return 1;
  }

  if ((0 == strcasecmp(val, "false"))
    ||(0 == strcasecmp(val, "f"))
    ||(0 == strcasecmp(val, "no"))
    ||(0 == strcasecmp(val, "n"))
    ||(0 == strcasecmp(val, "off")))
  {
    return 0;
  }  

  /* Return -1 if any chars are non-digits: */
  ptr = (char*)val;
  while (*ptr)
  {
    if (!isdigit(*ptr))
      return -1;
    ptr++;
  }

  /* If we get to here, val should be an integer. */
  if (atoi(val))
    return 1;
  else
    return 0;
}





/*****************************************************************/
/*   Loading of data files for images and sounds.                */
/*   These functions also draw some user feedback to             */
/*   display the progress of the loading.                        */
/*****************************************************************/

/* returns 1 if all data files successfully loaded, 0 otherwise. */

/* TODO load only "igloo" or "city" files, not both.             */
/* TODO get rid of files no longer used.                         */

int load_image_data()
{
  int i;

  static char* image_filenames[NUM_IMAGES] = {
  DATA_PREFIX "/images/status/standby.png",
  DATA_PREFIX "/images/title/menu_bkg.jpg",
  DATA_PREFIX "/images/title/title1.png",
  DATA_PREFIX "/images/status/title.png",
  DATA_PREFIX "/images/status/left.png",
  DATA_PREFIX "/images/status/left_gray.png",
  DATA_PREFIX "/images/status/right.png",
  DATA_PREFIX "/images/status/right_gray.png",
  DATA_PREFIX "/images/status/tux4kids.png",
  DATA_PREFIX "/images/status/nbs.png",
  DATA_PREFIX "/images/cities/city-blue.png",
  DATA_PREFIX "/images/cities/csplode-blue-1.png",
  DATA_PREFIX "/images/cities/csplode-blue-2.png",
  DATA_PREFIX "/images/cities/csplode-blue-3.png",
  DATA_PREFIX "/images/cities/csplode-blue-4.png",
  DATA_PREFIX "/images/cities/csplode-blue-5.png",
  DATA_PREFIX "/images/cities/cdead-blue.png",
  DATA_PREFIX "/images/cities/city-green.png",
  DATA_PREFIX "/images/cities/csplode-green-1.png",
  DATA_PREFIX "/images/cities/csplode-green-2.png",
  DATA_PREFIX "/images/cities/csplode-green-3.png",
  DATA_PREFIX "/images/cities/csplode-green-4.png",
  DATA_PREFIX "/images/cities/csplode-green-5.png",
  DATA_PREFIX "/images/cities/cdead-green.png",
  DATA_PREFIX "/images/cities/city-orange.png",
  DATA_PREFIX "/images/cities/csplode-orange-1.png",
  DATA_PREFIX "/images/cities/csplode-orange-2.png",
  DATA_PREFIX "/images/cities/csplode-orange-3.png",
  DATA_PREFIX "/images/cities/csplode-orange-4.png",
  DATA_PREFIX "/images/cities/csplode-orange-5.png",
  DATA_PREFIX "/images/cities/cdead-orange.png",
  DATA_PREFIX "/images/cities/city-red.png",
  DATA_PREFIX "/images/cities/csplode-red-1.png",
  DATA_PREFIX "/images/cities/csplode-red-2.png",
  DATA_PREFIX "/images/cities/csplode-red-3.png",
  DATA_PREFIX "/images/cities/csplode-red-4.png",
  DATA_PREFIX "/images/cities/csplode-red-5.png",
  DATA_PREFIX "/images/cities/cdead-red.png",
  DATA_PREFIX "/images/cities/shields.png",
  DATA_PREFIX "/images/comets/comet1.png",
  DATA_PREFIX "/images/comets/comet2.png",
  DATA_PREFIX "/images/comets/comet3.png",
  DATA_PREFIX "/images/comets/cometex3.png",
  DATA_PREFIX "/images/comets/cometex3.png",
  DATA_PREFIX "/images/comets/cometex2.png",
  DATA_PREFIX "/images/comets/cometex2.png",
  DATA_PREFIX "/images/comets/cometex1a.png",
  DATA_PREFIX "/images/comets/cometex1a.png",
  DATA_PREFIX "/images/comets/cometex1.png",
  DATA_PREFIX "/images/comets/cometex1.png",
  DATA_PREFIX "/images/comets/mini_comet1.png",
  DATA_PREFIX "/images/comets/mini_comet2.png",
  DATA_PREFIX "/images/comets/mini_comet3.png",
  DATA_PREFIX "/images/comets/bonus_comet1.png",
  DATA_PREFIX "/images/comets/bonus_comet2.png",
  DATA_PREFIX "/images/comets/bonus_comet3.png",
  DATA_PREFIX "/images/comets/bonus_cometex3.png",
  DATA_PREFIX "/images/comets/bonus_cometex3.png",
  DATA_PREFIX "/images/comets/bonus_cometex2.png",
  DATA_PREFIX "/images/comets/bonus_cometex2.png",
  DATA_PREFIX "/images/comets/bonus_cometex1a.png",
  DATA_PREFIX "/images/comets/bonus_cometex1a.png",
  DATA_PREFIX "/images/comets/bonus_cometex1.png",
  DATA_PREFIX "/images/comets/bonus_cometex1.png",
  DATA_PREFIX "/images/status/nums.png",
  DATA_PREFIX "/images/status/lednums.png",
  DATA_PREFIX "/images/status/led_neg_sign.png",
  DATA_PREFIX "/images/status/paused.png",
  DATA_PREFIX "/images/status/demo.png",
  DATA_PREFIX "/images/status/demo-small.png",
  DATA_PREFIX "/images/status/keypad.png",
  DATA_PREFIX "/images/status/keypad_no_neg.png",
  DATA_PREFIX "/images/tux/console_led.png",
  DATA_PREFIX "/images/tux/console_bash.png",
  DATA_PREFIX "/images/tux/tux-console1.png",
  DATA_PREFIX "/images/tux/tux-console2.png",
  DATA_PREFIX "/images/tux/tux-console3.png",
  DATA_PREFIX "/images/tux/tux-console4.png",
  DATA_PREFIX "/images/tux/tux-relax1.png",
  DATA_PREFIX "/images/tux/tux-relax2.png",
  DATA_PREFIX "/images/tux/tux-egypt1.png",
  DATA_PREFIX "/images/tux/tux-egypt2.png",
  DATA_PREFIX "/images/tux/tux-egypt3.png",
  DATA_PREFIX "/images/tux/tux-egypt4.png",
  DATA_PREFIX "/images/tux/tux-drat.png",
  DATA_PREFIX "/images/tux/tux-yipe.png",
  DATA_PREFIX "/images/tux/tux-yay1.png",
  DATA_PREFIX "/images/tux/tux-yay2.png",
  DATA_PREFIX "/images/tux/tux-yes1.png",
  DATA_PREFIX "/images/tux/tux-yes2.png",
  DATA_PREFIX "/images/tux/tux-sit.png",
  DATA_PREFIX "/images/tux/tux-fist1.png",
  DATA_PREFIX "/images/tux/tux-fist2.png",
  DATA_PREFIX "/images/penguins/flapdown.png",
  DATA_PREFIX "/images/penguins/flapup.png",
  DATA_PREFIX "/images/penguins/incoming.png",
  DATA_PREFIX "/images/penguins/grumpy.png",
  DATA_PREFIX "/images/penguins/worried.png",
  DATA_PREFIX "/images/penguins/standing-up.png",
  DATA_PREFIX "/images/penguins/sitting-down.png",
  DATA_PREFIX "/images/penguins/walk-on1.png",
  DATA_PREFIX "/images/penguins/walk-on2.png",
  DATA_PREFIX "/images/penguins/walk-on3.png",
  DATA_PREFIX "/images/penguins/walk-off1.png",
  DATA_PREFIX "/images/penguins/walk-off2.png",
  DATA_PREFIX "/images/penguins/walk-off3.png",
  DATA_PREFIX "/images/igloos/melted3.png",
  DATA_PREFIX "/images/igloos/melted2.png",
  DATA_PREFIX "/images/igloos/melted1.png",
  DATA_PREFIX "/images/igloos/half.png",
  DATA_PREFIX "/images/igloos/intact.png",
  DATA_PREFIX "/images/igloos/rebuilding1.png",
  DATA_PREFIX "/images/igloos/rebuilding2.png",
  DATA_PREFIX "/images/igloos/steam1.png",
  DATA_PREFIX "/images/igloos/steam2.png",
  DATA_PREFIX "/images/igloos/steam3.png",
  DATA_PREFIX "/images/igloos/steam4.png",
  DATA_PREFIX "/images/igloos/steam5.png",
  DATA_PREFIX "/images/igloos/cloud.png",
  DATA_PREFIX "/images/igloos/snow1.png",
  DATA_PREFIX "/images/igloos/snow2.png",
  DATA_PREFIX "/images/igloos/snow3.png",
  DATA_PREFIX "/images/igloos/extra_life.png",
  DATA_PREFIX "/images/status/wave.png",
  DATA_PREFIX "/images/status/score.png",
  DATA_PREFIX "/images/status/stop.png",
  DATA_PREFIX "/images/status/numbers.png",
  DATA_PREFIX "/images/status/gameover.png",
  DATA_PREFIX "/images/status/gameover_won.png"
  };

  /* Load images: */
  for (i = 0; i < NUM_IMAGES; i++)
  {
    images[i] = IMG_Load(image_filenames[i]);

    if (images[i] == NULL)
    {
      fprintf(stderr,
              "\nError: I couldn't load a graphics file:\n"
              "%s\n"
              "The Simple DirectMedia error that occured was:\n"
              "%s\n\n", image_filenames[i], SDL_GetError());
      return 0;
    }
  }
  /* If we make it to here OK, return 1: */
  return 1;
}

/* returns 1 if default font successfully loaded, 0 otherwise. */
int load_default_font()
{
  default_font = LoadFont((const unsigned char*)DEFAULT_FONT_NAME,
                           DEFAULT_MENU_FONT_SIZE);
  help_font = LoadFont((const unsigned char*)DEFAULT_FONT_NAME,
                           DEFAULT_HELP_FONT_SIZE);

  if (default_font && help_font)
  {
#ifdef TUXMATH_DEBUG
    fprintf(stderr, "load_default_font(): %s loaded successfully\n\n",
            DEFAULT_FONT_NAME);
#endif
    return 1;
  }
  else
  {
    fprintf(stderr, "LoadFont(): %s NOT loaded successfully.\n",
            DEFAULT_FONT_NAME);
    return 0;
  }
}




#ifndef NOSOUND
int load_sound_data(void)
{
  int total_files, i;

  SDL_Rect dest;

  static char* sound_filenames[NUM_SOUNDS] = {
  DATA_PREFIX "/sounds/pop.wav",
  DATA_PREFIX "/sounds/tock.wav",
  DATA_PREFIX "/sounds/laser.wav",
  DATA_PREFIX "/sounds/buzz.wav",
  DATA_PREFIX "/sounds/alarm.wav",
  DATA_PREFIX "/sounds/shieldsdown.wav",
  DATA_PREFIX "/sounds/explosion.wav",
  DATA_PREFIX "/sounds/click.wav",
  DATA_PREFIX "/sounds/sizzling.wav",
  DATA_PREFIX "/sounds/towerclock.wav",
  DATA_PREFIX "/sounds/cheer.wav"
  };

  static char* music_filenames[NUM_MUSICS] = {
  DATA_PREFIX "/sounds/game.mod",
  DATA_PREFIX "/sounds/game2.mod",
  DATA_PREFIX "/sounds/game3.mod"
  };

  /* skip loading sound files if sound system not available: */
  if (Opts_UsingSound())
  {
    total_files = NUM_IMAGES + NUM_SOUNDS + NUM_MUSICS;

    for (i = 0; i < NUM_SOUNDS; i++)
    {
      sounds[i] = Mix_LoadWAV(sound_filenames[i]);

      if (sounds[i] == NULL)
      {
        fprintf(stderr,
                "\nError: I couldn't load a sound file:\n"
                "%s\n"
                "The Simple DirectMedia error that occured was:\n"
                "%s\n\n", sound_filenames[i], SDL_GetError());
        return 0;
      }
      
      
      dest.x = 0;
      dest.y = (screen->h) - 10;
      dest.w = ((screen->w) * (i + 1 + NUM_IMAGES)) / total_files;
      dest.h = 10;

      SDL_FillRect(screen, &dest, SDL_MapRGB(screen->format, 0, 255, 0));
      SDL_UpdateRect(screen, dest.x, dest.y, dest.w, dest.h);
      
    }


    for (i = 0; i < NUM_MUSICS; i++)
    {
      musics[i] = Mix_LoadMUS(music_filenames[i]);

      if (musics[i] == NULL)
      {
        fprintf(stderr,
                "\nError: I couldn't load a music file:\n"
                "%s\n"
                "The Simple DirectMedia error that occured was:\n"
                "%s\n\n", music_filenames[i], SDL_GetError());
        return 0;
      }
      
      if (i == NUM_MUSICS - 1)
      {
        dest.x = 0;
        dest.y = (screen->h) - 10;
        dest.w = ((screen->w) * (i + 1 + NUM_IMAGES + NUM_SOUNDS)) / total_files;
        dest.h = 10;
        
        SDL_FillRect(screen, &dest, SDL_MapRGB(screen->format, 0, 255, 0));
        SDL_UpdateRect(screen, dest.x, dest.y, dest.w, dest.h);
      }
    }
  }
  return 1;
}
#endif



