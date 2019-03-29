/**
 * (C) Copyright 2016-2019 Intel Corporation.
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

#include "dfuse_common.h"
#include "dfuse.h"

static bool
ioc_rename_cb(struct ioc_request *request)
{
	struct iof_status_out *out = crt_reply_get(request->rpc);

	IOC_REQUEST_RESOLVE(request, out);
	if (request->rc) {
		IOC_REPLY_ERR(request, request->rc);
		D_GOTO(out, 0);
	}

	IOC_REPLY_ZERO(request);

out:
	/* Clean up the two refs this code holds on the rpc */
	crt_req_decref(request->rpc);
	crt_req_decref(request->rpc);

	D_FREE(request);
	return false;
}

static const struct ioc_request_api api = {
	.on_result	= ioc_rename_cb,
	.have_gah	= true,
	.gah_offset	= offsetof(struct iof_rename_in, old_gah),
};

void
dfuse_cb_rename(fuse_req_t req, fuse_ino_t parent, const char *name,
		fuse_ino_t newparent, const char *newname, unsigned int flags)
{
	struct iof_projection_info *fs_handle = fuse_req_userdata(req);
	struct ioc_request	*request;
	struct iof_rename_in	*in;
	int ret = EIO;
	int rc;

	D_ALLOC_PTR(request);
	if (!request) {
		D_GOTO(out_no_request, ret = ENOMEM);
	}

	IOC_REQUEST_INIT(request, fs_handle);
	IOC_REQUEST_RESET(request);

	IOF_TRACE_UP(request, fs_handle, "rename");
	IOF_TRACE_DEBUG(request, "renaming %s to %s", name, newname);

	request->req = req;
	request->ir_api = &api;

	rc = crt_req_create(fs_handle->proj.crt_ctx,
			    NULL,
			    FS_TO_OP(fs_handle, rename), &request->rpc);
	if (rc || !request->rpc) {
		IOF_LOG_ERROR("Could not create request, rc = %d",
			      rc);
		D_GOTO(out_err, ret = EIO);
	}

	in = crt_req_get(request->rpc);

	strncpy(in->old_name.name, name, NAME_MAX);
	strncpy(in->new_name.name, newname, NAME_MAX);
	in->flags = flags;

	request->ir_inode_num = parent;
	request->ir_ht = RHS_INODE_NUM;

	rc = find_gah(fs_handle, newparent, &in->new_gah);
	if (rc != 0)
		D_GOTO(out_decref, ret = rc);

	crt_req_addref(request->rpc);

	rc = iof_fs_send(request);
	if (rc != 0) {
		D_GOTO(out_decref, ret = EIO);
	}

	return;

out_no_request:
	IOC_REPLY_ERR_RAW(fs_handle, req, ret);
	return;

out_decref:
	crt_req_decref(request->rpc);

out_err:
	IOC_REPLY_ERR(request, ret);
	D_FREE(request);
}
