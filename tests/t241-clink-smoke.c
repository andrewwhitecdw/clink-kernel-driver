/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * User-space smoke test for t241-clink ioctl validation paths.
 *
 * Build: gcc -Wall -Wextra -I.. tests/t241-clink-smoke.c -o t241-clink-smoke
 *
 * This opens /dev/t241-clink and verifies that malformed requests are rejected.
 * If the device node is not present, the test exits with code 77 (skipped).
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>
#include <linux/types.h>

#ifndef __user
#define __user
#endif
#ifndef __iomem
#define __iomem
#endif
#ifndef __force
#define __force
#endif

#include "t241-clink-ioctl.h"

#define DEVNODE "/dev/t241-clink"

static int expect_err(int ret, int want, const char *desc)
{
	if (ret < 0 && errno == want) {
		printf("PASS: %s\n", desc);
		return 0;
	}
	fprintf(stderr, "FAIL: %s (ret=%d, errno=%d, want=%d)\n",
		desc, ret, errno, want);
	return 1;
}

int main(void)
{
	int fd, ret;
	struct tegra_nvlink_send_dlcmd dlcmd;
	struct tegra_nvlink_setup_tc setup;

	fd = open(DEVNODE, O_RDWR);
	if (fd < 0) {
		if (errno == ENOENT || errno == ENODEV || errno == EACCES) {
			printf("SKIP: %s not present\n", DEVNODE);
			return 77;
		}
		perror("FAIL: open " DEVNODE);
		return 1;
	}

	memset(&dlcmd, 0, sizeof(dlcmd));
	memset(&setup, 0, sizeof(setup));

	/* Wrong IOC magic. */
	ret = ioctl(fd, _IOWR('X', 0, struct tegra_nvlink_send_dlcmd), &dlcmd);
	if (expect_err(ret, EINVAL, "wrong ioctl magic rejected"))
		return 1;

	/* IOC number out of range. */
	ret = ioctl(fd, _IOWR(T241_NVLINK_IOC_MAGIC, TNVLINK_IOCTL_NUM_IOCTLS,
				struct tegra_nvlink_send_dlcmd), &dlcmd);
	if (expect_err(ret, EINVAL, "out-of-range ioctl number rejected"))
		return 1;

	/* Argument size mismatch. */
	ret = ioctl(fd, _IOWR(T241_NVLINK_IOC_MAGIC, 0, unsigned char), &dlcmd);
	if (expect_err(ret, EINVAL, "ioctl argument size mismatch rejected"))
		return 1;

	/* Bad userspace pointer. Only exercise this for write-capable
	 * ioctls so copy_from_user fails before the handler runs.
	 */
	if (_IOC_DIR(TNVLINK_IOCTL_SEND_DLCMD) & _IOC_WRITE) {
		ret = ioctl(fd, TNVLINK_IOCTL_SEND_DLCMD, NULL);
		if (expect_err(ret, EFAULT, "bad userspace pointer rejected"))
			return 1;
	} else {
		printf("SKIP: bad-pointer test (cmd is read-only)\n");
	}

	/* Out-of-range socket_id. */
	memset(&dlcmd, 0, sizeof(dlcmd));
	dlcmd.socket_id = 0xff;
	dlcmd.link_id = 0;
	ret = ioctl(fd, TNVLINK_IOCTL_SEND_DLCMD, &dlcmd);
	if (expect_err(ret, EINVAL, "out-of-range socket_id rejected"))
		return 1;

	/* Out-of-range link_id. */
	memset(&dlcmd, 0, sizeof(dlcmd));
	dlcmd.socket_id = 0;
	dlcmd.link_id = 0xff;
	ret = ioctl(fd, TNVLINK_IOCTL_SEND_DLCMD, &dlcmd);
	if (expect_err(ret, EINVAL, "out-of-range link_id rejected"))
		return 1;

	/* Unhandled enum value in per-handler switch (setup_tc). */
	setup.socket_id = 0;
	setup.link_id = 0;
	setup.exp = 0xff;
	setup.counter_mask = 0;
	ret = ioctl(fd, TNVLINK_IOCTL_SETUP_TC, &setup);
	if (expect_err(ret, EINVAL, "invalid setup_tc experiment rejected"))
		return 1;

	close(fd);
	printf("PASS: all t241-clink ioctl smoke checks passed\n");
	return 0;
}
