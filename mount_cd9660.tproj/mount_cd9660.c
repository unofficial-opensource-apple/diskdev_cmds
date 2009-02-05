/*
 * Copyright (c) 1999 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 * Copyright (c) 1992, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley
 * by Pace Willisson (pace@blitz.com).  The Rock Ridge Extension
 * Support code is derived from software contributed to Berkeley
 * by Atsushi Murai (amurai@spec.co.jp).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)mount_cd9660.c	8.7 (Berkeley) 5/1/95
 */

#include <sys/param.h>
#define CD9660
#include <sys/mount.h>
#include <sys/../isofs/cd9660/cd9660_mount.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <CoreFoundation/CFBase.h>
#include <IOKit/IOKitLib.h>
#include "../disklib/mntopts.h"

struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_UPDATE,
	{ "extatt", 0, ISOFSMNT_EXTATT, 1 },
	{ "gens", 0, ISOFSMNT_GENS, 1 },
	{ "rrip", 1, ISOFSMNT_NORRIP, 1 },
	{ "joliet", 1, ISOFSMNT_NOJOLIET, 1 },
	{ NULL }
};

typedef struct ISOVolumeDescriptor
{
	char		type[1];
	char		id[5];			// should be "CD001"
	char		version[1];
	char		filler[33];
	char		volumeID[32];
	char		filler2[1976];
} ISOVolumeDescriptor, *ISOVolumeDescriptorPtr;


#define	CDROM_BLOCK_SIZE		2048
#define ISO_STANDARD_ID 		"CD001"
#define ISO_VD_PRIMARY 			0x01  // for ISOVolumeDescriptor.type when it's a primary descriptor

void	usage __P((void));

static int	get_ssector(const char *devpath);
static u_char *	get_cdtoc(const char * devpath);

static u_char *	CreateBufferFromCFData(CFDataRef theData);

int
main(int argc, char **argv)
{
	struct iso_args args;
	int ch, mntflags, opts;
	char *dev, *dir;
	int altflg;
	
	mntflags = opts = 0;
	memset(&args, 0, sizeof args);
	args.ssector = -1;
	while ((ch = getopt(argc, argv, "egjo:rs:")) != EOF)
		switch (ch) {
		case 'e':
			opts |= ISOFSMNT_EXTATT;
			break;
		case 'g':
			opts |= ISOFSMNT_GENS;
			break;
		case 'j':
			opts |= ISOFSMNT_NOJOLIET;
			break;
		case 'o':
			getmntopts(optarg, mopts, &mntflags, &altflg);
			break;
		case 'r':
			opts |= ISOFSMNT_NORRIP;
			break;
		case 's':
			args.ssector = atoi(optarg);
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	dev = argv[0];
	dir = argv[1];

#define DEFAULT_ROOTUID	-2
	/*
	 * ISO 9660 filesystems are not writeable.
	 */
	mntflags |= MNT_RDONLY;
	args.export.ex_flags = MNT_EXRDONLY;
	args.fspec = dev;
	args.export.ex_root = DEFAULT_ROOTUID;
	args.flags = opts;

	/* obtain starting session if neccessary */
	if (args.ssector == -1)
		args.ssector = get_ssector(dev);

        if (mount("cd9660", dir, mntflags, &args) < 0)
		err(1, NULL);

	exit(0);
}

void
usage()
{
	(void)fprintf(stderr,
		"usage: mount_cd9660 [-egjr] [-o options]  [-s startsector] special node\n");
	exit(1);
}


/*
 * Minutes, Seconds, Frames (M:S:F)
 */
struct CDMSF {
	u_char   minute;
	u_char   second;
	u_char   frame;
};

/*
 * Table Of Contents
 */
struct CDTOC_Desc {
	u_char        session;
	u_char        ctrl_adr;  /* typed to be machine and compiler independent */
	u_char        tno;
	u_char        point;
	struct CDMSF  address;
	u_char        zero;
	struct CDMSF  p;
};

struct CDTOC {
	u_short            length;  /* in native cpu endian */
	u_char             first_session;
	u_char             last_session;
	struct CDTOC_Desc  trackdesc[1];
};

#define CD_MIN_TRACK_NO         1
#define CD_MAX_TRACK_NO        99

#define CD_SUBQ_DATA		0
#define CD_CURRENT_POSITION	1
#define CD_MEDIA_CATALOG	2
#define CD_TRACK_INFO		3

#define CD_CTRL_DATA          0x4
#define CD_CTRL_AUDIO         0x8

#define IOKIT_CDMEDIA_TOC    "TOC"


#define MSF_TO_LBA(msf)		\
	(((((msf).minute * 60UL) + (msf).second) * 75UL) + (msf).frame - 150)

/*
 * Determine the start of the last session.  If we can
 * successfully read the TOC of a CD-ROM, use the last
 * data track we find.  Otherwise, just use 0, in order
 * to probe the very first session.
 */
static int
get_ssector(const char *devpath)
{
	struct CDTOC * toc_p;
	struct CDTOC_Desc *toc_desc;
	struct ISOVolumeDescriptor *isovdp;
	char iobuf[CDROM_BLOCK_SIZE];
	int cmpsize = sizeof(isovdp->id);
	int i, count;
	int ssector;
	u_char track;
	int devfd;

	ssector = 0;
	isovdp = (struct ISOVolumeDescriptor *)iobuf;

	devfd = open(devpath, O_RDONLY | O_NDELAY , 0);
	if (devfd <= 0)
		goto exit;

	if ((toc_p = (struct CDTOC *)get_cdtoc(devpath)) == NULL)
		goto exit_close;

	count = (toc_p->length - 2) / sizeof(struct CDTOC_Desc);
	toc_desc = toc_p->trackdesc;

	for (i = count - 1; i >= 0; i--) {
		track = toc_desc[i].point;
		if (track > CD_MAX_TRACK_NO || track < CD_MIN_TRACK_NO)
			continue;

		if ((toc_desc[i].ctrl_adr >> 4) != CD_CURRENT_POSITION)
			continue;

		if (toc_desc[i].ctrl_adr & CD_CTRL_DATA) {
			int sector;

			sector = MSF_TO_LBA(toc_desc[i].p);	
			if (sector == 0)
				break;

			/* 
			 * Kodak Photo CDs have multiple tracks per session
			 * and a primary volume descriptor (PVD) will be in
			 * one of these tracks.  So we check each data track
			 * to find the latest valid PVD.
			 */
			lseek(devfd, ((16 + sector) * CDROM_BLOCK_SIZE), 0);
			if (read(devfd, iobuf, CDROM_BLOCK_SIZE) != CDROM_BLOCK_SIZE) {
				/*
				 * Re-try using the raw device.
				 */
				if (errno == EIO) {
					int rawfd;
					ssize_t readlen;
					char rawname[32];
					char *dp;

					if ((dp = strrchr(devpath, '/')) == 0)
						continue;
					sprintf(rawname, "/dev/r%s", dp + 1);
					rawfd = open(rawname, O_RDONLY | O_NDELAY , 0);
					if (rawfd <= 0)
						continue;
					lseek(rawfd, ((16 + sector) * CDROM_BLOCK_SIZE), 0);
					readlen = read(rawfd, iobuf, CDROM_BLOCK_SIZE);
					close(rawfd);
					if (readlen != CDROM_BLOCK_SIZE)
						continue;
				} else {
					continue;
				}
			}
		
			if ((memcmp(&isovdp->id[0], ISO_STANDARD_ID, cmpsize) == 0)
				&& (isovdp->type[0] == ISO_VD_PRIMARY)) {
				ssector = sector;
				break;
			}
		}
	}
	
	free(toc_p);
exit_close:
	close(devfd);
exit:
	return ssector;
}


static u_char *
get_cdtoc(const char * devpath)
{
	u_char *  result;
	io_iterator_t  iterator;
	io_registry_entry_t  service;
	mach_port_t  port;
	CFDataRef  data;
	CFDictionaryRef  properties;
	char *  devname;

	iterator = 0;
	service = 0;
	port = 0;
	properties = 0;
	data = 0;
	result = NULL;

	/* extract device name from device path */
	if ((devname = strrchr(devpath, '/')) != NULL)
		++devname;
	else
		devname = devpath;

	/* unraw device name */
	if (*devname == 'r')
		++devname;
		
	if ( IOMasterPort(bootstrap_port, &port) != KERN_SUCCESS )
		goto Exit;
		
	if ( IOServiceGetMatchingServices(port, IOBSDNameMatching(port,0,devname),
	                                  &iterator) != KERN_SUCCESS ) {
		goto Exit;
	}		
	service = IOIteratorNext(iterator);
	(void) IOObjectRelease(iterator);
	iterator = 0;

	/* Find the root-level media object */
	while (service && !IOObjectConformsTo(service, "IOCDMedia")) {
		if ( IORegistryEntryGetParentIterator(service, kIOServicePlane,
		                                      &iterator) != KERN_SUCCESS ) {
			goto Exit;
		}

		(void) IOObjectRelease(service);
		service = IOIteratorNext(iterator);
		(void) IOObjectRelease(iterator);
	}

	if (service == NULL)
		goto Exit;
	
	if ( IORegistryEntryCreateCFProperties(service,
	                                       &properties,
	                                       kCFAllocatorDefault,
	                                       kNilOptions) != KERN_SUCCESS ) {
		goto Exit;
	}

	/* Get the Table of Contents (TOC) */
	data = (CFDataRef) CFDictionaryGetValue(properties, CFSTR(IOKIT_CDMEDIA_TOC));
	if (data != NULL) {
		result = CreateBufferFromCFData(data);
		CFRelease(properties);
	}

Exit:
	if (service)
		(void) IOObjectRelease(service);
	
	return result;
}


static u_char *
CreateBufferFromCFData(CFDataRef cfdata)
{
	CFRange range;
	CFIndex buflen;
	u_char * bufptr;
	
	buflen = CFDataGetLength(cfdata) + 1;
	range = CFRangeMake(0, buflen);
	
	bufptr = (u_char *) malloc(buflen);
	if (bufptr != NULL)
		CFDataGetBytes(cfdata, range, bufptr);
		
	return bufptr;
}


