/**
 * (C) Copyright 2017-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#ifndef __IOF_API_H__
#define __IOF_API_H__

#include <stdbool.h>
#include "ioil_defines.h"

#if defined(__cplusplus)
extern "C" {
#endif

enum iof_bypass_status {
	IOF_IO_EXTERNAL = 0,	/** File is not forwarded by IOF */
	IOF_IO_BYPASS,		/** Kernel bypass is enabled */
	IOF_IO_DIS_MMAP,	/** Bypass disabled for mmap'd file */
	IOF_IO_DIS_FLAG,	/* Bypass is disabled for file because
				 *  O_APPEND or O_PATH was used
				 */
	IOF_IO_DIS_FCNTL,	/* Bypass is disabled for file because
				 * bypass doesn't support an fcntl
				 */
	IOF_IO_DIS_STREAM,	/* Bypass is disabled for file opened as a
				 * stream.
				 */
	IOF_IO_DIS_RSRC,	/* Bypass is disabled due to lack of
				 * resources in interception library
				 */
};

/** Return a value indicating the status of the file with respect to
 *  IOF.  Possible values are defined in /p enum iof_bypass_status
 */
IOF_PUBLIC int iof_get_bypass_status(int fd);

#endif /* __IOF_API_H__ */
