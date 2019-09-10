#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

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
import traceback
import sys
import random
import string
import json
from avocado import Test

# pylint: disable=wrong-import-position
sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')

# pylint: disable=import-error
import server_utils
import write_host_file
from daos_api import (DaosContext, DaosPool, DaosContainer, DaosSnapshot,
                      DaosLog, DaosApiError)

# pylint: disable=broad-except
class BasicSnapshot(Test):
    """
    DAOS-1370 Basic snapshot test

    Test Class Description:
    Test that a snapshot taken of a container remains unchaged even after an
    object in the container has been updated 500 times.
    Create the container.
    Write an object to the container.
    Take a snapshot.
    Write 500 changes to the KV pair of the object.
    Check that the snapshot is still there.
    Confirm that the data in the snapshot is unchanged.
    Destroy the snapshot
    """

    def __init__(self, *args, **kwargs):
        super(BasicSnapshot, self).__init__(*args, **kwargs)
        self.snapshot = None

    def setUp(self):

        # get paths from the build_vars generated by build
        with open('../../../.build_vars.json') as finput:
            build_paths = json.load(finput)
        basepath = os.path.normpath(build_paths['PREFIX'] + "/../")

        server_group = self.params.get("name", '/server_config/',
                                       'daos_server')

        # setup the DAOS python API
        self.context = DaosContext(build_paths['PREFIX'] + '/lib64/')
        self.d_log = DaosLog(self.context)

        self.hostlist = self.params.get("test_machines", '/run/hosts/*')
        hostfile = write_host_file.write_host_file(self.hostlist, self.workdir)

        server_utils.run_server(hostfile, server_group, basepath)

        # Set up the pool and container.
        try:
            # parameters used in pool create
            createmode = self.params.get("mode", '/run/pool/createmode/')
            createsetid = self.params.get("setname", '/run/pool/createset/')
            createsize = self.params.get("size", '/run/pool/createsize/*')
            createuid = os.geteuid()
            creategid = os.getegid()

            # initialize a pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None)

            # need a connection to create container
            self.pool.connect(1 << 1)

            # create a container
            self.container = DaosContainer(self.context)
            self.container.create(self.pool.handle)

            # now open it
            self.container.open()

        except DaosApiError as error:
            print(error)
            print(traceback.format_exc())
            self.fail("Test failed before snapshot taken")

    def tearDown(self):
        try:
            if self.container:
                self.container.close()
                self.container.destroy()
            if self.pool:
                self.pool.disconnect()
                self.pool.destroy(1)
        finally:
            server_utils.stop_server()

    def test_basic_snapshot(self):
        """
        Test ID: DAOS-1370

        Test Description:
        Create a pool, container in the pool, object in the container, add
        one key:value to the object.
        Commit the transaction. Perform a snapshot create on the container.
        Create 500 additional transactions with a small change to the object
        in each and commit each after the object update is done.
        Verify the snapshot is still available and the contents remain in
        their original state.

        :avocado: tags=snap,basicsnap
        """

        try:
            # create an object and write some data into it
            obj_cls = self.params.get("obj_class", '/run/object_class/*')
            thedata = "Now is the winter of our discontent made glorious"
            datasize = len(thedata) + 1
            dkey = "dkey"
            akey = "akey"
            obj, epoch = self.container.write_an_obj(thedata,
                                                     datasize,
                                                     dkey,
                                                     akey,
                                                     obj_cls=obj_cls)
            obj.close()
            # Take a snapshot of the container
            self.snapshot = DaosSnapshot(self.context)
            self.snapshot.create(self.container.coh, epoch)
            print("Wrote an object and created a snapshot")
        except DaosApiError as error:
            self.fail("Test failed during the initial object write.\n{0}"
                      .format(error))

        # Make 500 changes to the data object. The write_an_obj function does a
        # commit when the update is complete
        try:
            rand_str = lambda n: ''.join([random.choice(string.lowercase) for i
                                          in xrange(n)])
            print("Committing 500 additional transactions to the same KV")
            more_transactions = 500
            while more_transactions:
                size = random.randint(1, 250) + 1
                new_data = rand_str(size)
                new_obj, _ = self.container.write_an_obj(new_data,
                                                         size,
                                                         dkey,
                                                         akey,
                                                         obj_cls=obj_cls)
                new_obj.close()
                more_transactions -= 1
        except Exception as error:
            self.fail("Test failed during the write of 500 objects.\n{0}"
                      .format(error))

        # List the snapshot and make sure it reflects the original epoch
        try:
            reported_epoch = self.snapshot.list(self.container.coh)
            if self.snapshot.epoch != reported_epoch:
                raise Exception("The snapshot epoch returned from snapshot "
                                "list is not the same as the original epoch "
                                "snapshotted.")
            print("After 500 additional commits the snapshot is still "
                  "available")
        except Exception as error:
            self.fail("Test was unable to list the snapshot\n{0}"
                      .format(error))

        # Make sure the data in the snapshot is the original data.
        # Get a handle for the snapshot and read the object at dkey, akey.
        # Compare it to the originally written data.
        try:
            obj.open()
            snap_handle = self.snapshot.open(self.container.coh)
            thedata2 = self.container.read_an_obj(datasize, dkey, akey, obj,
                                                  snap_handle.value)
            if thedata2.value != thedata:
                raise Exception("The data in the snapshot is not the same as "
                                "the original data")
            print("The snapshot data matches the data originally written.")
        except Exception as error:
            self.fail("Error when retrieving the snapshot data.\n{0}"
                      .format(error))
        # Now destroy the snapshot
        try:
            self.snapshot.destroy(self.container.coh)
            print("Snapshot successfully destroyed")
        except Exception as error:
            self.fail("{0}".format(error))
