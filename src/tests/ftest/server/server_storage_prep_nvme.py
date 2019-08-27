#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.
  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at
      http://www.apache.org/licenses/LICENSE-2.0
  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function

import os

from server_utils import ServerCommand
from apricot import TestWithServers
from avocado.utils import process

class ServerStoragePrepNvme(TestWithServers):
    """Test Class Description:
    Simple test to verify the storage prep function of the server tool.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ServerStoragePrepNvme object."""
        super(ServerStoragePrepNvme, self).__init__(*args, **kwargs)
        self.setup_start_server = False
        self.setup_start_agents = False

    def test_server_storage_nvme_prep(self):
        """
        JIRA ID: DAOS-2891
        Test Description: Test basic server functionality to prep nvme storage
        on system.
        :avocado: tags=all,tiny,pr,prep_nvme,basic
        """
        # Create daos_server command
        server = ServerCommand(
            self.hostlist_servers, os.path.join(self.prefix, "bin"))
        server.get_params(self)

        # Update config and start server
        server.prepare(self.workdir, self.hostfile_servers_slots)
        server.run(None, sudo=True)