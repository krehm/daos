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

#ifndef __IOF_DEFINES_H__
#define __IOF_DEFINES_H__

#include <inttypes.h>

#ifdef IOF_DECLARE_WEAK
/* For LD_PRELOAD, declaring public symbols as weak allows 3rd
 * party libraries to use the headers without knowing beforehand
 * if the iof libraries will be present at runtime
 */
#define IOF_PUBLIC __attribute__((visibility("default"), weak))
#else
#define IOF_PUBLIC __attribute__((visibility("default")))
#endif

#endif /* __IOF_DEFINES_H__ */
