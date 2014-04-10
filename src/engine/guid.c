/********************************************************************\
 * guid.c -- globally unique ID implementation                      *
 * Copyright (C) 2000 Dave Peticolas <peticola@cs.ucdavis.edu>      *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "guid.h"
#include "md5.h"

# ifndef P_tmpdir
#  define P_tmpdir "/tmp"
# endif


/** Constants *******************************************************/
#define BLOCKSIZE 4096
#define THRESHOLD (2 * BLOCKSIZE)


/** Static global variables *****************************************/
static gboolean guid_initialized = FALSE;
static struct md5_ctx guid_context;


/** Function implementations ****************************************/

/* This code is based on code in md5.c in GNU textutils. */
static size_t
init_from_stream(FILE *stream, size_t max_size)
{
  char buffer[BLOCKSIZE + 72];
  size_t sum, block_size, total;

  if (max_size <= 0)
    return 0;

  total = 0;

  /* Iterate over file contents. */
  while (1)
  {
    /* We read the file in blocks of BLOCKSIZE bytes.  One call of the
     * computation function processes the whole buffer so that with the
     * next round of the loop another block can be read.  */
    size_t n;
    sum = 0;

    if (max_size < BLOCKSIZE)
      block_size = max_size;
    else
      block_size = BLOCKSIZE;

    /* Read block.  Take care for partial reads.  */
    do
    {
      n = fread (buffer + sum, 1, block_size - sum, stream);

      sum += n;
    }
    while (sum < block_size && n != 0);

    max_size -= sum;

    if (n == 0 && ferror (stream))
      return total;

    /* If end of file or max_size is reached, end the loop. */
    if ((n == 0) || (max_size == 0))
      break;

    /* Process buffer with BLOCKSIZE bytes.  Note that
     * BLOCKSIZE % 64 == 0  */
    md5_process_block (buffer, BLOCKSIZE, &guid_context);

    total += sum;
  }

  /* Add the last bytes if necessary.  */
  if (sum > 0)
  {
    md5_process_bytes (buffer, sum, &guid_context);
    total += sum;
  }

  return total;
}

static size_t
init_from_file(const char *filename, size_t max_size)
{
  struct stat stats;
  size_t total = 0;
  FILE *fp;

  if (stat(filename, &stats) == 0)
  {
    md5_process_bytes(&stats, sizeof(stats), &guid_context);
    total += sizeof(stats);
  }

  if (max_size <= 0)
    return total;

  fp = fopen (filename, "r");
  if (fp == NULL)
    return total;

  total += init_from_stream(fp, max_size);

  fclose(fp);

  return total;
}

static size_t
init_from_dir(const char *dirname, unsigned int max_files)
{
  char filename[1024];
  struct dirent *de;
  struct stat stats;
  size_t total;
  int result;
  DIR *dir;

  if (max_files <= 0)
    return 0;

  dir = opendir (dirname);
  if (dir == NULL)
    return 0;

  total = 0;

  do
  {
    de = readdir(dir);
    if (de == NULL)
      break;

    md5_process_bytes(de, sizeof(struct dirent), &guid_context);
    total += sizeof(struct dirent);

    result = snprintf(filename, sizeof(filename),
                      "%s/%s", dirname, de->d_name);
    if ((result < 0) || (result >= sizeof(filename)))
      continue;

    if (stat(filename, &stats) != 0)
      continue;
    md5_process_bytes(&stats, sizeof(stats), &guid_context);
    total += sizeof(stats);

    max_files--;
  } while (max_files > 0);

  closedir(dir);

  return total;
}

static size_t
init_from_time(void)
{
  size_t total;
  time_t t_time;
  clock_t clocks;
  struct tms tms_buf;

  total = 0;

  t_time = time(NULL);
  md5_process_bytes(&t_time, sizeof(t_time), &guid_context);
  total += sizeof(t_time);

  clocks = times(&tms_buf);
  md5_process_bytes(&clocks, sizeof(clocks), &guid_context);
  md5_process_bytes(&tms_buf, sizeof(tms_buf), &guid_context);
  total += sizeof(clocks) + sizeof(tms_buf);

  return total;
}

void
guid_init(void)
{
  size_t bytes = 0;

  md5_init_ctx(&guid_context);

  /* files */
  {
    const char * files[] =
    { "/dev/urandom",
      "/etc/passwd",
      "/proc/loadavg",
      "/proc/meminfo",
      "/proc/net/dev",
      "/proc/rtc",
      "/proc/self/environ",
      "/proc/self/stat",
      "/proc/stat",
      "/proc/uptime",
      "/dev/urandom", /* once more for good measure :) */
      NULL
    };
    int i;

    for (i = 0; files[i] != NULL; i++)
      bytes += init_from_file(files[i], BLOCKSIZE);
  }

  /* directories */
  {
    const char * dirname;
    const char * dirs[] =
    {
      "/proc",
      P_tmpdir,
      "/var/lock",
      "/var/log",
      "/var/mail",
      "/var/spool/mail",
      "/var/run",
      NULL
    };
    int i;

    for (i = 0; dirs[i] != NULL; i++)
      bytes += init_from_dir(dirs[i], 32);

    dirname = getenv("HOME");
    if (dirname != NULL)
      bytes += init_from_dir(dirname, 32);
  }

  /* process and parent ids */
  {
    pid_t pid;

    pid = getpid();
    md5_process_bytes(&pid, sizeof(pid), &guid_context);
    bytes += sizeof(pid);

    pid = getppid();
    md5_process_bytes(&pid, sizeof(pid), &guid_context);
    bytes += sizeof(pid);
  }

  /* user info */
  {
    uid_t uid;
    gid_t gid;
    char *s;

    s = getlogin();
    if (s != NULL)
    {
      md5_process_bytes(s, strlen(s), &guid_context);
      bytes += strlen(s);
    }

    uid = getuid();
    md5_process_bytes(&uid, sizeof(uid), &guid_context);
    bytes += sizeof(uid);

    gid = getgid();
    md5_process_bytes(&gid, sizeof(gid), &guid_context);
    bytes += sizeof(gid);
  }

  /* host info */
  {
    char string[1024];

    gethostname(string, sizeof(string));
    md5_process_bytes(string, sizeof(string), &guid_context);
    bytes += sizeof(string);
  }

  /* plain old random */
  {
    int n, i;

    srand((unsigned int) time(NULL));

    for (i = 0; i < 32; i++)
    {
      n = rand();

      md5_process_bytes(&n, sizeof(n), &guid_context);
      bytes += sizeof(n);
    }
  }

  /* time in secs and clock ticks */
  bytes += init_from_time();

  if (bytes < THRESHOLD)
    fprintf(stderr,
            "WARNING: guid_init only got %u bytes.\n"
            "The identifiers might not be very random.\n", bytes);

  guid_initialized = TRUE;
}

void
guid_init_with_salt(const void *salt, size_t salt_len)
{
  guid_init();

  md5_process_bytes(salt, salt_len, &guid_context);
}

void
guid_init_only_salt(const void *salt, size_t salt_len)
{
  md5_init_ctx(&guid_context);

  md5_process_bytes(salt, salt_len, &guid_context);

  guid_initialized = TRUE;
}

void
guid_new(GUID *guid)
{
  struct md5_ctx ctx;

  if (guid == NULL)
    return;

  if (!guid_initialized)
    guid_init();

  /* make the id */
  ctx = guid_context;
  md5_finish_ctx(&ctx, guid->data);

  /* update the global context */
  init_from_time();
}

/* needs 32 bytes exactly, doesn't print a null char */
static void
encode_md5_data(const unsigned char *data, char *buffer)
{
  size_t count;

  for (count = 0; count < 16; count++, buffer += 2)
    sprintf(buffer, "%02x", data[count]);
}

/* returns true if the first 32 bytes of buffer encode
 * a hex number. returns false otherwise. Decoded number
 * is packed into data in little endian order. */
static gboolean
decode_md5_string(const char *string, unsigned char *data)
{
  unsigned char n1, n2;
  size_t count;
  char c1, c2;

  if (string == NULL)
    return FALSE;

  for (count = 0; count < 16; count++)
  {
    c1 = tolower(string[2 * count]);
    if (!isxdigit(c1))
      return FALSE;

    c2 = tolower(string[2 * count + 1]);
    if (!isxdigit(c2))
      return FALSE;

    if (isdigit(c1))
      n1 = c1 - '0';
    else
      n1 = c1 - 'a' + 10;

    if (isdigit(c2))
      n2 = c2 - '0';
    else
      n2 = c2 - 'a' + 10;

    if (data != NULL)
      data[count] = (n1 << 4) | n2;
  }

  return TRUE;
}

char *
guid_to_string(const GUID * guid)
{
  char *string = malloc(GUID_ENCODING_LENGTH+1);
  if (!string) return NULL;

  encode_md5_data(guid->data, string);

  string[GUID_ENCODING_LENGTH] = '\0';

  return string;
}

gboolean
string_to_guid(const char * string, GUID * guid)
{
  return decode_md5_string(string, (guid != NULL) ? guid->data : NULL);
}

gboolean
guid_equal(const GUID *guid_1, const GUID *guid_2)
{
  if (guid_1 && guid_2)
    return (memcmp(guid_1, guid_2, sizeof(GUID)) == 0);
  else
    return FALSE;
}