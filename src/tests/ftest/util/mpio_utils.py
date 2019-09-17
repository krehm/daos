#!/usr/bin/python
'''
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
  The Governments rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function

import os
import subprocess
import paramiko
import socket
from general_utils import exports_cmd

class MpioFailed(Exception):
    """Raise if MPIO failed"""

class MpioUtils():
    """MpioUtils Class"""

    def __init__(self):

        self.mpichinstall = None

    def mpich_installed(self, hostlist):
        """Check if mpich is installed"""

        try:
            # checking mpich install
            self.mpichinstall = subprocess.check_output(
                ["ssh", hostlist[0],
                 "command -v mpichversion"]).rstrip()[:-len('bin/mpichversion')]

            # Obtaning the location where mpich is installed by executing
            # "which mpichversion", which should return
            # /some_path/bin/mpichversion, hence removing 17 characters
            # to obtain just the installed location
            print(self.mpichinstall)

            return True

        except subprocess.CalledProcessError as excep:
            print("Mpich not installed \n {}".format(excep))
            return False

    def run_romio(self, basepath, hostlist, romio_test_repo):
        """
            Running ROMIO testsuite under mpich
            Function Arguments:
                basepath --path where all daos and it's dependencies can be
                           fetched from
                hostlist --list of client hosts
                romio_test_repo --built romio test directory
        """

        # environment variables only to be set on client node
        env = {
            "CRT_ATTACH_INFO_PATH": "{}/install/tmp/".format(basepath),
            "MPI_LIB": "\"\"",
            "DAOS_SINGLETON_CLI": 1,
        }

        # setting attributes
        run_cmd = exports_cmd(env) + 'cd ' + romio_test_repo + ' && ' + \
                  './runtests -fname=daos:test1 -subset -daos'
        print("Romio test run command: {}".format(run_cmd))

        try:
            # establish conection and run romio test
            # All the tests must pass with "No Errors"
            # if any test fails it should return "non-zero exit code"
            ssh = paramiko.SSHClient()
            ssh.load_system_host_keys()
            ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            ssh.connect(hostlist[0])
            _ssh_stdin, ssh_stdout, ssh_stderr = ssh.exec_command(run_cmd)
            print(ssh_stdout.read())
            print(ssh_stderr.read())
        except (IOError, OSError, paramiko.SSHException, socket.error) as excep:
            raise MpioFailed("<ROMIO Test FAILED> \nException occurred: {}"
                             .format(str(excep)))
    # pylint: disable=R0913
    def run_llnl_mpi4py_hdf5(self, basepath, hostfile, pool_uuid, test_repo,
                             test_name, client_processes):
        """
            Running LLNL, MPI4PY and HDF5 testsuites
            Function Arguments:
                basepath          --path where all daos and it's dependencies
                                    can be fetched from
                hostfile          --client hostfile
                pool_uuid         --Pool UUID
                test_repo         --test repo location
                test_name         --name of test to be tested
        """
        print("self.mpichinstall: {}".format(self.mpichinstall))
        # environment variables only to be set on client node
        env = {
            "CRT_ATTACH_INFO_PATH": "{}/install/tmp/"\
                .format(basepath),
            "MPI_LIB": '',
            "MPIO_USER_PATH": "daos:",
            "DAOS_POOL": "{}".format(pool_uuid),
            "DAOS_SVCL": "{}".format(0),
            "HDF5_PARAPREFIX": "daos:"
        }
        # setting attributes
        cmd = exports_cmd(env) + 'cd ' + test_repo + ' && '
        # running 8 client processes
        if test_name == "llnl" and os.path.isfile(test_repo + "/testmpio_daos"):
            cmd += "mpirun -np {} --hostfile {} ./testmpio_daos 1".format(
                client_processes, hostfile)
        elif test_name == "mpi4py" and \
             os.path.isfile(test_repo + "/test_io_daos.py"):
            cmd += "mpiexec -n {} --hostfile {} "   \
                   "module load mpi/mpich-x86_64; " \
                   "python test_io_daos.py".format(client_processes, hostfile)
        elif test_name == "hdf5" and \
             (os.path.isfile(test_repo + "/testphdf5") and
              os.path.isfile(test_repo + "/t_shapesame")):
            for test in ["testphdf5", "t_shapesame"]:
                cmd += "echo ***Running {0}*** ;" \
                       " mpirun -np {1} --hostfile {2} ./{0} ;".format(
                           test, client_processes, hostfile)
        else:
            try:
                print(subprocess.check_output(["ls", "-l", test_repo],
                                              shell=False))
            except:
                try:
                    print(subprocess.check_output(["ls", "-ld", test_repo],
                                                  shell=False))
                except:
                    print(subprocess.check_output(["rpm", "-qa"], shell=False))
            raise MpioFailed("Wrong test name ({}) or test repo location ({}) "
                             "specified".format(test_name, test_repo))

        print("run command: {}".format(cmd))

        try:
            process = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, shell=True)
            while True:
                output = process.stdout.readline()
                if output == '' and process.poll() is not None:
                    break
                if output:
                    print(output.strip())
            if process.poll() != 0:
                raise MpioFailed("{} Run process".format(test_name)
                                 + " Failed with non zero exit"
                                 + " code:{}".format(process.poll()))

        except (ValueError, OSError) as excep:
            raise MpioFailed("<Test FAILED> \nException occurred: {}"\
                                 .format(str(excep)))
