/*
    HydraBus/HydraNFC - Copyright (C) 2012-2014 Benjamin VERNOUX

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <string.h>
#include <stdio.h> /* sprintf */
#include "ch.h"
#include "hal.h"

#include "shell.h"
#include "chprintf.h"

#include "common.h"
#include "ff.h"

#include "usb1cfg.h"
#include "usb2cfg.h"

#include "mcu.h"
#include "microsd.h"

#define SDC_BURST_SIZE  8 /* how many sectors reads at once */
#define IN_OUT_BUF_SIZE (MMCSD_BLOCK_SIZE * SDC_BURST_SIZE + 8)
static uint8_t outbuf[IN_OUT_BUF_SIZE+8];
static uint8_t inbuf[IN_OUT_BUF_SIZE+8];

/* FS object.*/
static FATFS SDC_FS;
/* FS Root */

/* FS mounted and ready.*/
static bool fs_ready = FALSE;

#define FILENAME_SIZE (255)
char filename[FILENAME_SIZE+4] = { 0 };

#define DATA_TEST_SIZE (18)
uint8_t data[DATA_TEST_SIZE] = "Test data 12345678";

extern uint8_t* sniffer_get_buffer(void);
extern uint32_t sniffer_get_buffer_max_size(void);
extern uint32_t sniffer_get_size(void);

extern uint8_t buf[300];
/* Generic large buffer.*/
extern uint8_t fbuff[1024];

int mount(void);
int umount(void);
int write_file(uint8_t* buffer, uint32_t size);

bool is_fs_ready(void)
{
  return fs_ready;
}

/**
 * @brief   Parody of UNIX badblocks program.
 *
 * @param[in] start       first block to check
 * @param[in] end         last block to check
 * @param[in] blockatonce number of blocks to check at once
 * @param[in] pattern     check pattern
 *
 * @return              The operation status.
 * @retval SDC_SUCCESS  operation succeeded, the requested blocks have been
 *                      read.
 * @retval SDC_FAILED   operation failed, the state of the buffer is uncertain.
 */
bool badblocks(uint32_t start, uint32_t end, uint32_t blockatonce, uint8_t pattern)
{
  uint32_t position = 0;
  uint32_t i = 0;

  chDbgCheck(blockatonce <= SDC_BURST_SIZE);

  /* fill control buffer */
  for (i=0; i < MMCSD_BLOCK_SIZE * blockatonce; i++)
    outbuf[i] = pattern;

  /* fill SD card with pattern. */
  position = start;
  while (position < end){
    if (sdcWrite(&SDCD1, position, outbuf, blockatonce))
      goto ERROR;
    position += blockatonce;
  }

  /* read and compare. */
  position = start;
  while (position < end){
    if (sdcRead(&SDCD1, position, inbuf, blockatonce))
      goto ERROR;
    if (memcmp(inbuf, outbuf, blockatonce * MMCSD_BLOCK_SIZE) != 0)
      goto ERROR;
    position += blockatonce;
  }
  return FALSE;

ERROR:
  return TRUE;
}

/**
 *
 */
void fillbuffer(uint8_t pattern, uint8_t *b)
{
  uint32_t i = 0;
  for (i=0; i < MMCSD_BLOCK_SIZE * SDC_BURST_SIZE; i++)
    b[i] = pattern;
}

/**
 *
 */
void fillbuffers(uint8_t pattern)
{
  fillbuffer(pattern, inbuf);
  fillbuffer(pattern, outbuf);
}

/**
 * SDIO Test Non Destructive
 */
void cmd_sdiotest(t_hydra_console *con, int argc, const char* const* argv)
{
  (void)argc;
  (void)argv;
  uint32_t i = 0;

  chprintf(con->bss, "Trying to connect SDIO... ");
  chThdSleepMilliseconds(100);

  if(!sdcConnect(&SDCD1))
  {

    chprintf(con->bss, "OK\r\n");
    chprintf(con->bss, "*** Card CSD content is: ");
    chprintf(con->bss, "%X %X %X %X \r\n", (&SDCD1)->csd[3], (&SDCD1)->csd[2],
                                      (&SDCD1)->csd[1], (&SDCD1)->csd[0]);

    chprintf(con->bss, "Single aligned read...");
    chThdSleepMilliseconds(100);
    if (sdcRead(&SDCD1, 0, inbuf, 1))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }
    chprintf(con->bss, " OK\r\n");
    chThdSleepMilliseconds(100);

    chprintf(con->bss, "Single unaligned read...");
    chThdSleepMilliseconds(100);
    if (sdcRead(&SDCD1, 0, inbuf + 1, 1))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }
    if (sdcRead(&SDCD1, 0, inbuf + 2, 1))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }
    if (sdcRead(&SDCD1, 0, inbuf + 3, 1))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }
    chprintf(con->bss, " OK\r\n");
    chThdSleepMilliseconds(10);

    chprintf(con->bss, "Multiple aligned reads...");
    chThdSleepMilliseconds(10);
    fillbuffers(0x55);
    /* fill reference buffer from SD card */
    if (sdcRead(&SDCD1, 0, inbuf, SDC_BURST_SIZE))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }

    for (i=0; i<1000; i++)
    {
      if (sdcRead(&SDCD1, 0, outbuf, SDC_BURST_SIZE))
      {
        chprintf(con->bss, "sdcRead KO\r\n");
        umount();
        return;
      }
      if (memcmp(inbuf, outbuf, SDC_BURST_SIZE * MMCSD_BLOCK_SIZE) != 0)
      {
        chprintf(con->bss, "memcmp KO\r\n");
        umount();
        return;
      }
    }
    chprintf(con->bss, " OK\r\n");
    chThdSleepMilliseconds(10);

    chprintf(con->bss, "Multiple unaligned reads...");
    chThdSleepMilliseconds(10);
    fillbuffers(0x55);
    /* fill reference buffer from SD card */
    if(sdcRead(&SDCD1, 0, inbuf + 1, SDC_BURST_SIZE))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }
    for(i=0; i<1000; i++)
    {
      if(sdcRead(&SDCD1, 0, outbuf + 1, SDC_BURST_SIZE))
      {
        chprintf(con->bss, "sdcRead KO\r\n");
        umount();
        return;
      }
      if(memcmp(inbuf, outbuf, SDC_BURST_SIZE * MMCSD_BLOCK_SIZE) != 0)
      {
        chprintf(con->bss, "memcmp KO\r\n");
        umount();
        return;
      }
    }
    chprintf(con->bss, " OK\r\n");
    chThdSleepMilliseconds(10);

    /**
     * Now perform some FS tests.
     */

    FRESULT err;
    uint32_t clusters;
    FATFS *fsp;
    FIL FileObject;
    uint32_t bytes_written;
    uint32_t bytes_read;
    FILINFO filinfo;
    uint8_t teststring[] = {"This is test file\r\n"};

    chprintf(con->bss, "Register working area for filesystem... ");
    chThdSleepMilliseconds(10);
    err = f_mount(&SDC_FS, "", 0);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_mount KO err:%d\r\n", err);
      umount();
      return;
    }
    else
    {
      fs_ready = TRUE;
      chprintf(con->bss, "OK\r\n");
    }

    chprintf(con->bss, "Mount filesystem... ");
    chThdSleepMilliseconds(10);
    err = f_getfree("/", &clusters, &fsp);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_getfree KO err:%d\r\n", err);
      umount();
      return;
    }
    chprintf(con->bss, "OK\r\n");
    chprintf(con->bss,
             "FS: %lu free clusters, %lu sectors per cluster, %lu bytes free\r\n",
             clusters, (uint32_t)SDC_FS.csize,
             clusters * (uint32_t)SDC_FS.csize * (uint32_t)MMCSD_BLOCK_SIZE);


    chprintf(con->bss, "Create file \"chtest.txt\"... ");
    chThdSleepMilliseconds(10);
    err = f_open(&FileObject, "0:chtest.txt", FA_WRITE | FA_OPEN_ALWAYS);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_open KO err:%d\r\n", err);
      umount();
      return;
    }
    chprintf(con->bss, "OK\r\n");
    chprintf(con->bss, "Write some data in it... ");
    chThdSleepMilliseconds(10);
    err = f_write(&FileObject, teststring, sizeof(teststring), (void *)&bytes_written);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_write KO err:%d\r\n", err);
      umount();
      return;
    }
    else
      chprintf(con->bss, "OK\r\n");

    chprintf(con->bss, "Close file \"chtest.txt\"... ");
    err = f_close(&FileObject);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_close KO err:%d\r\n", err);
      umount();
      return;
    }
    else
      chprintf(con->bss, "OK\r\n");

    chprintf(con->bss, "Check file size \"chtest.txt\"... ");
    err = f_stat("0:chtest.txt", &filinfo);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_stat KO err:%d\r\n", err);
      umount();
      return;
    }else
    {
      if (filinfo.fsize == sizeof(teststring))
      {
        chprintf(con->bss, "OK\r\n");
      }else
      {
        chprintf(con->bss, "filinfo.fsize KO obtained=%d expected=%d\r\n",
                filinfo.fsize,sizeof(teststring));
        umount();
        return;
      }
    }

    chprintf(con->bss, "Check file content \"chtest.txt\"... ");
    err = f_open(&FileObject, "0:chtest.txt", FA_READ | FA_OPEN_EXISTING);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_open KO err:%d\r\n", err);
      umount();
      return;
    }
    uint8_t buf[sizeof(teststring)];
    err = f_read(&FileObject, buf, sizeof(teststring), (void *)&bytes_read);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_read KO err:%d\r\n", err);
      umount();
      return;
    }else
    {
      if (memcmp(teststring, buf, sizeof(teststring)) != 0)
      {
        chprintf(con->bss, "memcmp KO err:%d\r\n", err);
        umount();
        return;
      }else
      {
        chprintf(con->bss, "OK\r\n");
      }
    }

    chprintf(con->bss, "Umount filesystem... ");
    f_mount(NULL, "", 0);
    chprintf(con->bss, "OK\r\n");

    chprintf(con->bss, "Disconnecting from SDIO...");
    chThdSleepMilliseconds(10);
    if (sdcDisconnect(&SDCD1))
    {
      chprintf(con->bss, "sdcDisconnect(&SDCD1) error\r\n");
      return;
    }
    chprintf(con->bss, " OK\r\n");
    chprintf(con->bss, "------------------------------------------------------\r\n");
    chprintf(con->bss, "All tests passed successfully.\r\n");
    chThdSleepMilliseconds(10);
  }
  else
  {
    chprintf(con->bss, "sdcConnect(&SDCD1) error\r\n");
  }
}

void cmd_sdc(t_hydra_console *con, int argc, const char* const* argv)
{
  static const char *mode[] = {"SDV11", "SDV20", "MMC", NULL};
  systime_t start, end;
  uint32_t n, startblk;

  if (argc != 1) {
    chprintf(con->bss, "Usage: sdiotest read|write|all\r\n");
    return;
  }

  /* Card presence check.*/
  if (!blkIsInserted(&SDCD1)) {
    chprintf(con->bss, "Card not inserted, aborting.\r\n");
    return;
  }

  /* Connection to the card.*/
  chprintf(con->bss, "Connecting... ");
  if (sdcConnect(&SDCD1)) {
    chprintf(con->bss, "failed\r\n");
    return;
  }
  chprintf(con->bss, "OK\r\n\r\nCard Info\r\n");
  chprintf(con->bss, "CSD      : %08X %8X %08X %08X \r\n",
           SDCD1.csd[3], SDCD1.csd[2], SDCD1.csd[1], SDCD1.csd[0]);
  chprintf(con->bss, "CID      : %08X %8X %08X %08X \r\n",
           SDCD1.cid[3], SDCD1.cid[2], SDCD1.cid[1], SDCD1.cid[0]);
  chprintf(con->bss, "Mode     : %s\r\n", mode[SDCD1.cardmode]);
  chprintf(con->bss, "Capacity : %DMB\r\n", SDCD1.capacity / 2048);

  /* The test is performed in the middle of the flash area.*/
  startblk = (SDCD1.capacity / MMCSD_BLOCK_SIZE) / 2;

  if ((strcmp(argv[0], "read") == 0) ||
      (strcmp(argv[0], "all") == 0)) {

    /* Single block read performance, aligned.*/
    chprintf(con->bss, "Single block aligned read performance:           ");
    start = chVTGetSystemTime();
    end = start + MS2ST(1000);
    n = 0;
    do {
      if (blkRead(&SDCD1, startblk, buf, 1)) {
        chprintf(con->bss, "failed\r\n");
        goto exittest;
      }
      n++;
    } while (chVTIsSystemTimeWithin(start, end));
    chprintf(con->bss, "%D blocks/S, %D bytes/S\r\n", n, n * MMCSD_BLOCK_SIZE);

    /* Multiple sequential blocks read performance, aligned.*/
    chprintf(con->bss, "16 sequential blocks aligned read performance:   ");
    start = chVTGetSystemTime();
    end = start + MS2ST(1000);
    n = 0;
    do {
      if (blkRead(&SDCD1, startblk, buf, SDC_BURST_SIZE)) {
        chprintf(con->bss, "failed\r\n");
        goto exittest;
      }
      n += SDC_BURST_SIZE;
    } while (chVTIsSystemTimeWithin(start, end));
    chprintf(con->bss, "%D blocks/S, %D bytes/S\r\n", n, n * MMCSD_BLOCK_SIZE);

#if STM32_SDC_SDIO_UNALIGNED_SUPPORT
    /* Single block read performance, unaligned.*/
    chprintf(con->bss, "Single block unaligned read performance:         ");
    start = chVTGetSystemTime();
    end = start + MS2ST(1000);
    n = 0;
    do {
      if (blkRead(&SDCD1, startblk, buf + 1, 1)) {
        chprintf(con->bss, "failed\r\n");
        goto exittest;
      }
      n++;
    } while (chVTIsSystemTimeWithin(start, end));
    chprintf(con->bss, "%D blocks/S, %D bytes/S\r\n", n, n * MMCSD_BLOCK_SIZE);

    /* Multiple sequential blocks read performance, unaligned.*/
    chprintf(con->bss, "16 sequential blocks unaligned read performance: ");
    start = chVTGetSystemTime();
    end = start + MS2ST(1000);
    n = 0;
    do {
      if (blkRead(&SDCD1, startblk, buf + 1, SDC_BURST_SIZE)) {
        chprintf(con->bss, "failed\r\n");
        goto exittest;
      }
      n += SDC_BURST_SIZE;
    } while (chVTIsSystemTimeWithin(start, end));
    chprintf(con->bss, "%D blocks/S, %D bytes/S\r\n", n, n * MMCSD_BLOCK_SIZE);
#endif /* STM32_SDC_SDIO_UNALIGNED_SUPPORT */
  }

  if ((strcmp(argv[0], "write") == 0) ||
      (strcmp(argv[0], "all") == 0)) {

  }

  /* Card disconnect and command end.*/
exittest:
  sdcDisconnect(&SDCD1);
}

static FRESULT scan_files(t_hydra_console *con, char *path)
{
  FRESULT res;
  FILINFO fno;
  DIR dir;
  int i;
  char *fn;
  DWORD fsize;
#if _USE_LFN
    static char lfn[_MAX_LFN + 1];   /* Buffer to store the LFN */
    fno.lfname = lfn;
    fno.lfsize = sizeof lfn;
#endif

  res = f_opendir(&dir, path);
  if(res == FR_OK)
  {
    i = strlen(path);
    for (;;)
    {
      res = f_readdir(&dir, &fno);
      if (res != FR_OK || fno.fname[0] == 0)
        break;

      if (fno.fname[0] == '.')
        continue;

#if _USE_LFN
      fn = *fno.lfname ? fno.lfname : fno.fname;
#else
      fn = fno.fname;
#endif
      fsize = fno.fsize;

      if(fno.fattrib & AM_DIR) /* It is a directory */
      {
        sprintf(&path[i], "/%s", fn);
        res = scan_files(con, path);
        if (res != FR_OK)
          break;
        path[i] = 0;
      }else
      {
        if(strlen(path) > 0)
        {
          chprintf(con->bss, "%s", path);
        }else
        {
          chprintf(con->bss, ".");
        }
        chThdSleepMilliseconds(1);
        chprintf(con->bss, "/%s %ld bytes\r\n", fn, fsize);
        chThdSleepMilliseconds(1);
      }
    }
    f_closedir(&dir);
  }

  return res;
}

void write_file_get_last_filename(filename_t* out_filename)
{
  out_filename->filename[0] = 0;
  sprintf(&out_filename->filename[0], "%s", filename);
}

/* Return 0 if OK else < 0 error code */
int write_file(uint8_t* buffer, uint32_t size)
{
  uint32_t i;
  FRESULT err;
  FIL FileObject;
  uint32_t bytes_written;

  if(size == 0)
  {
    return -1;
  }

  if(fs_ready==FALSE)
  {
		if(mount() != 0)
		{
			return -5;
		}
  }else
  {
    umount();
		if(mount() != 0)
		{
			return -5;
		}
  }

  /* Save data in file */
  for(i=0; i<999; i++)
  {
    sprintf(filename, "0:hydrabus_%ld.txt", i);
    err = f_open(&FileObject, filename, FA_WRITE | FA_CREATE_NEW);
    if(err == FR_OK)
    {
      break;
    }
  }
  if(err == FR_OK)
  {
    err = f_write(&FileObject, buffer, size, (void *)&bytes_written);
    if(err != FR_OK)
    {
      f_close(&FileObject);
      umount();
      return -3;
    }

    err = f_close(&FileObject);
    if (err != FR_OK) {
      umount();
      return -4;
    }
  }else
  {
    umount();
    return -2;
  }

  umount();
  return 0;
}

/* return 0 if success else <0 for error */
int mount(void)
{
  FRESULT err;

  /*
   * SDC initialization and FS mount.
   */
  if (sdcConnect(&SDCD1))
  {
    return -1;
  }

  err = f_mount(&SDC_FS, "", 0);
  if (err != FR_OK)
  {
    sdcDisconnect(&SDCD1);
    return -2;
  }

  fs_ready = TRUE;

  return 0;
}

/* return 0 if success else <0 for error */
int umount(void)
{
  if(!fs_ready) {
    /* File System already unmounted */
    return -1;
  }
  f_mount(NULL, "", 0);

  /* SDC Disconnect */
  sdcDisconnect(&SDCD1);
  fs_ready = FALSE;
  return 0;
}

void cmd_sd_mount(t_hydra_console *con, int argc, const char* const* argv)
{
  (void)argc;
  (void)argv;

  FRESULT err;

  if (fs_ready)
  {
    chprintf(con->bss, "File System already mounted\r\n");
    return;
  }

  /*
   * SDC initialization and FS mount.
   */
  if (sdcConnect(&SDCD1))
  {
    chprintf(con->bss, "sdcConnect(&SDCD1) error\r\n");
    return;
  }

  err = f_mount(&SDC_FS, "", 0);
  if (err != FR_OK)
  {
    chprintf(con->bss, "f_mount KO\r\n");
    sdcDisconnect(&SDCD1);
    return;
  }else
  {
    chprintf(con->bss, "f_mount OK\r\n");
  }
  fs_ready = TRUE;
}

void cmd_sd_umount(t_hydra_console *con, int argc, const char* const* argv)
{
  (void)argc;
  (void)argv;

  if(!fs_ready) {
    chprintf(con->bss, "File System already unmounted\r\n");
    return;
  }

  chprintf(con->bss, "Umount filesystem...\r\n");
  f_mount(NULL, "", 0);

  /* SDC Disconnect */
  sdcDisconnect(&SDCD1);
  fs_ready = FALSE;
}

void cmd_sd_ls(t_hydra_console *con, int argc, const char* const* argv)
{
  FRESULT err;
  uint32_t clusters;
  FATFS *fsp;
  uint64_t free_size_bytes;
  uint32_t free_size_kb;
  uint32_t free_size_mb;

  (void)argc;
  (void)argv;

  if(argc > 2)
  {
    chprintf(con->bss, "Error missing argument\r\nusage: %s [optional directory]\r\n", argv[0]);
    return;
  }
  if(argc == 2)
  {
    chsnprintf((char *)fbuff, FILENAME_SIZE, "0:%s", argv[1]);
  }else
  {
    fbuff[0] = 0;
  }

  if (!fs_ready)
  {
    err = mount();
    if(err)
    {
      chprintf(con->bss, "mount error:%d\r\n", err);
      return;
    }
  }
  err = f_getfree("/", &clusters, &fsp);
  if (err != FR_OK)
  {
    chprintf(con->bss, "FS: f_getfree() failed\r\n");
  }else
  {
    free_size_bytes = (uint64_t)(clusters * SDC_FS.csize) * (uint64_t)MMCSD_BLOCK_SIZE;
    free_size_kb = (uint32_t)(free_size_bytes/(uint64_t)1024);
    free_size_mb = (uint32_t)(free_size_kb/1024);
    chprintf(con->bss,
             "FS: %lu free clusters, %lu sectors/cluster, %lu bytes/sector\r\n",
             clusters, (uint32_t)SDC_FS.csize, (uint32_t)MMCSD_BLOCK_SIZE);
    chprintf(con->bss,
             "FS: %lu KBytes free (%lu MBytes free)\r\n",
             free_size_kb, free_size_mb);

    err = scan_files(con, (char *)fbuff);
    if (err != FR_OK)
    {
      chprintf(con->bss, "scan_files() on dir=%s failed\r\n", fbuff);
    }
  }
}

/* Call only with len=multiples of 16 (unless end of dump). */
static void dump_hexbuf(t_hydra_console *con, uint32_t offset,
		const uint8_t *buf, int len)
{
	int b, i;
	char asc[17];

	b = 0;
	while (len) {
		chprintf(con->bss, "%.8x: ", offset + b);
		for (i = 0; i < MIN(len, 16); i++, b++) {
			chprintf(con->bss, "%.2x", buf[b]);
			if (i & 1)
				chprintf(con->bss, " ");
			if (i == 7)
				chprintf(con->bss, " ");
			if (buf[b] >= 0x20 && buf[b] < 0x7f)
				asc[i] = buf[b];
			else
				asc[i] = '.';
		}
		asc[i] = 0;
		chprintf(con->bss, " %s\r\n", asc);
		len -= i;
	}
}

void cmd_sd_cat(t_hydra_console *con, int argc, const char* const* argv)
{
  #define MAX_FILE_SIZE (524288)
  bool hex;
  int offset, filelen;
  uint32_t cnt;
  FRESULT err;
  FIL fp;

  if(argc != 2)
  {
    chprintf(con->bss, "Error missing argument\r\nusage: %s <filename>\r\n", argv[0]);
    return;
  }

  if (!fs_ready)
  {
    err = mount();
    if(err)
    {
      chprintf(con->bss, "mount error:%d\r\n", err);
      return;
    }
  }

  chsnprintf(filename, FILENAME_SIZE, "0:%s", argv[1]);
  err = f_open(&fp, filename, FA_READ | FA_OPEN_EXISTING);
  if (err != FR_OK)
  {
    chprintf(con->bss, "Error to open file %s, err:%d\r\n", filename, err);
    return;
  }

  filelen = fp.fsize;
  if(filelen > MAX_FILE_SIZE)
  {
    chprintf(con->bss, "Read file: %s, size=%d is too big shall not exceed %d\r\n", filename, filelen, MAX_FILE_SIZE);
  }else
  {
    chprintf(con->bss, "Read file: %s, size=%d\r\n", filename, filelen);
  }

  hex = !strcmp(argv[0], "hd");
  offset = 0;
  while(filelen)
  {
    if(filelen >= IN_OUT_BUF_SIZE)
    {
      cnt = IN_OUT_BUF_SIZE;
      filelen -= IN_OUT_BUF_SIZE;
    }else
    {
      cnt = filelen;
      filelen = 0;
    }
    err = f_read(&fp, inbuf, cnt, (void *)&cnt);
    if (err != FR_OK)
    {
      chprintf(con->bss, "Error to read file, err:%d\r\n", err);
      break;
    }
    if (!cnt)
      break;

    if (hex) {
      dump_hexbuf(con, offset, inbuf, cnt);
      offset += cnt;
    } else {
      /* Force end of string at end of buffer */
      inbuf[cnt] = 0;
      chprintf(con->bss, "%s", inbuf);
    }
  }
  if (!hex)
      chprintf(con->bss, "\r\n");
}

/**
 * SDIO Test Destructive
 */
void cmd_sd_erase(t_hydra_console *con, int argc, const char* const* argv){
  (void)argc;
  (void)argv;
  uint32_t i = 0;

  chprintf(con->bss, "SDIO Destructive test will format de SD press UBTN to continue ... ");

  while(1)
  {
    if(USER_BUTTON)
    {
      break;
    }
    chThdSleepMilliseconds(10);
  }

  chprintf(con->bss, "Trying to connect SDIO... ");
  chThdSleepMilliseconds(10);

  if (!sdcConnect(&SDCD1))
  {

    chprintf(con->bss, "OK\r\n");
    chprintf(con->bss, "*** Card CSD content is: ");
    chprintf(con->bss, "%X %X %X %X \r\n", (&SDCD1)->csd[3], (&SDCD1)->csd[2],
                                      (&SDCD1)->csd[1], (&SDCD1)->csd[0]);

    chprintf(con->bss, "Single aligned read...");
    chThdSleepMilliseconds(10);
    if (sdcRead(&SDCD1, 0, inbuf, 1))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }
    chprintf(con->bss, " OK\r\n");
    chThdSleepMilliseconds(10);

    chprintf(con->bss, "Single unaligned read...");
    chThdSleepMilliseconds(10);
    if (sdcRead(&SDCD1, 0, inbuf + 1, 1))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }
    if (sdcRead(&SDCD1, 0, inbuf + 2, 1))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }
    if (sdcRead(&SDCD1, 0, inbuf + 3, 1))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }
    chprintf(con->bss, " OK\r\n");
    chThdSleepMilliseconds(10);

    chprintf(con->bss, "Multiple aligned reads...");
    chThdSleepMilliseconds(10);
    fillbuffers(0x55);

    /* fill reference buffer from SD card */
    if (sdcRead(&SDCD1, 0, inbuf, SDC_BURST_SIZE))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }
    chprintf(con->bss, "\r\n.");

    for (i=0; i<1000; i++)
    {
      if (sdcRead(&SDCD1, 0, outbuf, SDC_BURST_SIZE))
      {
        chprintf(con->bss, "sdcRead KO\r\n");
        umount();
        return;
      }
      if (memcmp(inbuf, outbuf, SDC_BURST_SIZE * MMCSD_BLOCK_SIZE) != 0)
      {
        chprintf(con->bss, "memcmp KO\r\n");
        umount();
        return;
      }

      chprintf(con->bss, ".");
    }
    chprintf(con->bss, " OK\r\n");
    chThdSleepMilliseconds(10);

    chprintf(con->bss, "Multiple unaligned reads...");
    chThdSleepMilliseconds(10);
    fillbuffers(0x55);
    /* fill reference buffer from SD card */
    if (sdcRead(&SDCD1, 0, inbuf + 1, SDC_BURST_SIZE))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }

    for (i=0; i<1000; i++)
    {
      if (sdcRead(&SDCD1, 0, outbuf + 1, SDC_BURST_SIZE))
      {
        chprintf(con->bss, "sdcRead KO\r\n");
        umount();
        return;
      }
      if (memcmp(inbuf, outbuf, SDC_BURST_SIZE * MMCSD_BLOCK_SIZE) != 0)
      {
        chprintf(con->bss, "memcmp KO\r\n");
        umount();
        return;
      }
    }
    chprintf(con->bss, " OK\r\n");
    chThdSleepMilliseconds(10);

/* DESTRUCTIVE TEST START */
    chprintf(con->bss, "Single aligned write...");
    chThdSleepMilliseconds(10);
    fillbuffer(0xAA, inbuf);
    if (sdcWrite(&SDCD1, 0, inbuf, 1))
    {
      chprintf(con->bss, "sdcWrite KO\r\n");
      umount();
      return;
    }
    fillbuffer(0, outbuf);
    if (sdcRead(&SDCD1, 0, outbuf, 1))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }
    if (memcmp(inbuf, outbuf, MMCSD_BLOCK_SIZE) != 0)
    {
      chprintf(con->bss, "memcmp KO\r\n");
      umount();
      return;
    }
    chprintf(con->bss, " OK\r\n");

    chprintf(con->bss, "Single unaligned write...");
    chThdSleepMilliseconds(10);
    fillbuffer(0xFF, inbuf);
    if (sdcWrite(&SDCD1, 0, inbuf+1, 1))
    {
      chprintf(con->bss, "sdcWrite KO\r\n");
      umount();
      return;
    }
    fillbuffer(0, outbuf);
    if (sdcRead(&SDCD1, 0, outbuf+1, 1))
    {
      chprintf(con->bss, "sdcRead KO\r\n");
      umount();
      return;
    }
    if (memcmp(inbuf+1, outbuf+1, MMCSD_BLOCK_SIZE) != 0)
    {
      chprintf(con->bss, "memcmp KO\r\n");
      umount();
      return;
    }
    chprintf(con->bss, " OK\r\n");

    chprintf(con->bss, "Running badblocks at 0x10000 offset...");
    chThdSleepMilliseconds(10);
    if(badblocks(0x10000, 0x11000, SDC_BURST_SIZE, 0xAA))
    {
      chprintf(con->bss, "badblocks KO\r\n");
      umount();
      return;
    }
    chprintf(con->bss, " OK\r\n");
/* DESTRUCTIVE TEST END */

    /**
     * Now perform some FS tests.
     */
    FRESULT err;
    uint32_t clusters;
    FATFS *fsp;
    FIL FileObject;
    uint32_t bytes_written;
    uint32_t bytes_read;
    uint8_t teststring[] = {"This is test file\r\n"};

    chprintf(con->bss, "Register working area for filesystem... ");
    chThdSleepMilliseconds(10);
    err = f_mount(&SDC_FS, "", 0);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_mount err:%d\r\n", err);
      umount();
      return;
    }
    else{
      fs_ready = TRUE;
      chprintf(con->bss, "OK\r\n");
    }

/* DESTRUCTIVE TEST START */
    chprintf(con->bss, "Formatting... ");
    chThdSleepMilliseconds(10);
    err = f_mkfs("",0,0);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_mkfs err:%d\r\n", err);
      umount();
      return;
    }else
    {
      chprintf(con->bss, "OK\r\n");
    }
/* DESTRUCTIVE TEST END */

    chprintf(con->bss, "Mount filesystem... ");
    chThdSleepMilliseconds(10);
    err = f_getfree("/", &clusters, &fsp);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_getfree err:%d\r\n", err);
      umount();
      return;
    }
    chprintf(con->bss, "OK\r\n");
    chprintf(con->bss,
             "FS: %lu free clusters, %lu sectors per cluster, %lu bytes free\r\n",
             clusters, (uint32_t)SDC_FS.csize,
             clusters * (uint32_t)SDC_FS.csize * (uint32_t)MMCSD_BLOCK_SIZE);


    chprintf(con->bss, "Create file \"chtest.txt\"... ");
    chThdSleepMilliseconds(10);
    err = f_open(&FileObject, "0:chtest.txt", FA_WRITE | FA_OPEN_ALWAYS);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_open err:%d\r\n", err);
      umount();
      return;
    }
    chprintf(con->bss, "OK\r\n");
    chprintf(con->bss, "Write some data in it... ");
    chThdSleepMilliseconds(10);
    err = f_write(&FileObject, teststring, sizeof(teststring), (void *)&bytes_written);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_write err:%d\r\n", err);
      umount();
      return;
    }
    else
      chprintf(con->bss, "OK\r\n");

    chprintf(con->bss, "Close file \"chtest.txt\"... ");
    err = f_close(&FileObject);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_close err:%d\r\n", err);
      umount();
      return;
    }
    else
      chprintf(con->bss, "OK\r\n");

    chprintf(con->bss, "Check file content \"chtest.txt\"... ");
    err = f_open(&FileObject, "0:chtest.txt", FA_READ | FA_OPEN_EXISTING);
    chThdSleepMilliseconds(10);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_open err:%d\r\n", err);
      umount();
      return;
    }

    err = f_read(&FileObject, buf, sizeof(teststring), (void *)&bytes_read);
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_read KO\r\n");
      umount();
      return;
    }else
    {
      if (memcmp(teststring, buf, sizeof(teststring)) != 0)
      {
        chprintf(con->bss, "memcmp KO\r\n");
        umount();
        return;
      }
      else{
        chprintf(con->bss, "OK\r\n");
      }
    }

    chprintf(con->bss, "Delete file \"chtest.txt\"... ");
    err = f_unlink("0:chtest.txt");
    if (err != FR_OK)
    {
      chprintf(con->bss, "f_unlink err:%d\r\n", err);
      umount();
      return;
    }

    chprintf(con->bss, "Umount filesystem... ");
    f_mount(NULL, "", 0);
    chprintf(con->bss, "OK\r\n");

    chprintf(con->bss, "Disconnecting from SDIO...");
    chThdSleepMilliseconds(10);
    if (sdcDisconnect(&SDCD1))
    {
      chprintf(con->bss, "sdcDisconnect KO\r\n");
      umount();
      return;
    }
    chprintf(con->bss, " OK\r\n");
    chprintf(con->bss, "------------------------------------------------------\r\n");
    chprintf(con->bss, "All tests passed successfully.\r\n");
    chThdSleepMilliseconds(10);

    umount();
  }
}
