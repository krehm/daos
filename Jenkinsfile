#!/usr/bin/groovy
/* Copyright (C) 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
// To use a test branch (i.e. PR) until it lands to master
// I.e. for testing library changes
//@Library(value="pipeline-lib@your_branch") _

def arch = ""
def sanitized_JOB_NAME = JOB_NAME.toLowerCase().replaceAll('/', '-').replaceAll('%2f', '-')

def component_repos = "openpa libfabric pmix ompi mercury spdk isa-l fio dpdk protobuf-c fuse pmdk argobots raft cart@daos_devel1"
def daos_repo = "daos@${env.BRANCH_NAME}:${env.BUILD_NUMBER}"
def daos_repos = component_repos + ' ' + daos_repo
def ior_repos = "mpich@PR-10 ior-hpc@daos"

def rpm_test_pre = '''if git show -s --format=%B | grep "^Skip-test: true"; then
                          exit 0
                      fi
                      nodelist=(${NODELIST//,/ })
                      scp -i ci_key src/tests/ftest/data/daos_server_baseline.yaml \
                                    jenkins@${nodelist[0]}:/tmp
                      ssh -i ci_key jenkins@${nodelist[0]} "set -ex\n'''

def rpm_test_daos_test = '''me=\\\$(whoami)
                            for dir in server agent; do
                                sudo mkdir /var/run/daos_\\\$dir
                                sudo chmod 0755 /var/run/daos_\\\$dir
                                sudo chown \\\$me:\\\$me /var/run/daos_\\\$dir
                            done
                            sudo mkdir /tmp/daos_sockets
                            sudo chmod 0755 /tmp/daos_sockets
                            sudo chown \\\$me:\\\$me /tmp/daos_sockets
                            sudo mkdir -p /mnt/daos
                            sudo mount -t tmpfs -o size=16777216k tmpfs /mnt/daos
                            sudo cp /tmp/daos_server_baseline.yaml /usr/etc/daos_server.yml
                            cat /usr/etc/daos_server.yml
                            cat /usr/etc/daos_agent.yml
                            coproc orterun -np 1 -H \\\$HOSTNAME --enable-recovery -x DAOS_SINGLETON_CLI=1 daos_server --debug --config /usr/etc/daos_server.yml start -t 1 -a /tmp -i
                            trap 'set -x; kill -INT \\\$COPROC_PID' EXIT
                            line=\"\"
                            while [[ \"\\\$line\" != *started\\\\ on\\\\ rank\\\\ 0* ]]; do
                                read line <&\\\${COPROC[0]}
                                echo \"Server stdout: \\\$line\"
                            done
                            echo \"Server started!\"
                            daos_agent -o /usr/etc/daos_agent.yml -i &
                            AGENT_PID=\\\$!
                            trap 'set -x; kill -INT \\\$AGENT_PID \\\$COPROC_PID' EXIT
                            orterun -np 1 -x OFI_INTERFACE=eth0 -x CRT_ATTACH_INFO_PATH=/tmp -x DAOS_SINGLETON_CLI=1 daos_test -m'''

// bail out of branch builds that are not on a whitelist
if (!env.CHANGE_ID &&
    (env.BRANCH_NAME != "weekly-testing" &&
     env.BRANCH_NAME != "master")) {
   currentBuild.result = 'SUCCESS'
   return
}

pipeline {
    agent { label 'lightweight' }

    triggers {
        cron(env.BRANCH_NAME == 'master' ? '0 0 * * *\n' : '' +
             env.BRANCH_NAME == 'weekly-testing' ? 'H 0 * * 6' : '')
    }

    environment {
        GITHUB_USER = credentials('daos-jenkins-review-posting')
        BAHTTPS_PROXY = "${env.HTTP_PROXY ? '--build-arg HTTP_PROXY="' + env.HTTP_PROXY + '" --build-arg http_proxy="' + env.HTTP_PROXY + '"' : ''}"
        BAHTTP_PROXY = "${env.HTTP_PROXY ? '--build-arg HTTPS_PROXY="' + env.HTTPS_PROXY + '" --build-arg https_proxy="' + env.HTTPS_PROXY + '"' : ''}"
        UID = sh(script: "id -u", returnStdout: true)
        BUILDARGS = "$env.BAHTTP_PROXY $env.BAHTTPS_PROXY "                   +
                    "--build-arg NOBUILD=1 --build-arg UID=$env.UID "         +
                    "--build-arg JENKINS_URL=$env.JENKINS_URL "               +
                    "--build-arg CACHEBUST=${currentBuild.startTimeInMillis}"
        QUICKBUILD = sh(script: "git show -s --format=%B | grep \"^Quick-build: true\"",
                        returnStatus: true)
        SSH_KEY_ARGS="-ici_key"
        CLUSH_ARGS="-o$SSH_KEY_ARGS"
    }

    options {
        // preserve stashes so that jobs can be started at the test stage
        preserveStashes(buildCount: 5)
        // How can we have different timeouts for weekly and master and PRs?
        timeout(time: 24, unit: 'HOURS')
    }

    stages {
        stage('Cancel Previous Builds') {
            when { changeRequest() }
            steps {
                cancelPreviousBuilds()
            }
        }
        stage('Build') {
            /* Don't use failFast here as whilst it avoids using extra resources
             * and gives faster results for PRs it's also on for master where we
             * do want complete results in the case of partial failure
             */
            //failFast true
            when {
                beforeAgent true
                // expression { skipTest != true }
                expression {
                    sh script: 'git show -s --format=%B | grep "^Skip-build: true"',
                       returnStatus: true
                }
            }
            parallel {
                stage('Build RPM on CentOS 7') {
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            expression { env.CHANGE_TARGET != 'weekly-testing' }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile-mockbuild.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '--build-arg UID=$(id -u) --build-arg JENKINS_URL=' +
                                                env.JENKINS_URL
                            args  '--group-add mock --cap-add=SYS_ADMIN --privileged=true'
                        }
                    }
                    steps {
                         githubNotify credentialsId: 'daos-jenkins-commit-status',
                                      description: env.STAGE_NAME,
                                      context: "build" + "/" + env.STAGE_NAME,
                                      status: "PENDING"
                        checkoutScm withSubmodules: true
                        catchError(stageResult: 'UNSTABLE', buildResult: 'SUCCESS') {
                            sh label: env.STAGE_NAME,
                               script: '''rm -rf artifacts/centos7/
                                          mkdir -p artifacts/centos7/
                                          if git show -s --format=%B | grep "^Skip-build: true"; then
                                              exit 0
                                          fi
                                          if make -C utils/rpms srpm; then
                                              if make -C utils/rpms mockbuild; then
                                                  (cd /var/lib/mock/epel-7-x86_64/result/ &&
                                                   cp -r . $OLDPWD/artifacts/centos7/)
                                                  createrepo artifacts/centos7/
                                              else
                                                  rc=\${PIPESTATUS[0]}
                                                  (cd /var/lib/mock/epel-7-x86_64/result/ &&
                                                   cp -r . $OLDPWD/artifacts/centos7/)
                                                  cp -af utils/rpms/_topdir/SRPMS artifacts/centos7/
                                                  exit \$rc
                                              fi
                                          else
                                              exit \${PIPESTATUS[0]}
                                          fi'''
                        }
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: 'artifacts/centos7/**'
                        }
                        success {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "SUCCESS"
                        }
                        unstable {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "UNSTABLE", ignore_failure: true
                        }
                        failure {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "FAILURE", ignore_failure: true
                        }
                    }
                }
                stage('Build RPM on SLES 12.3') {
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            expression { env.CHANGE_TARGET != 'weekly-testing' }
                            expression { return env.QUICKBUILD == '1' }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile-rpmbuild.sles.12.3'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs '--build-arg UID=$(id -u) --build-arg JENKINS_URL=' +
                                                env.JENKINS_URL +
                                                 " --build-arg CACHEBUST=${currentBuild.startTimeInMillis}"
                        }
                    }
                    steps {
                         githubNotify credentialsId: 'daos-jenkins-commit-status',
                                      description: env.STAGE_NAME,
                                      context: "build" + "/" + env.STAGE_NAME,
                                      status: "PENDING"
                        checkoutScm withSubmodules: true
                        catchError(stageResult: 'UNSTABLE', buildResult: 'SUCCESS') {
                            sh label: env.STAGE_NAME,
                               script: '''rm -rf artifacts/sles12.3/
                                          mkdir -p artifacts/sles12.3/
                                          if git show -s --format=%B | grep "^Skip-build: true"; then
                                              exit 0
                                          fi
                                          rm -rf utils/rpms/_topdir/SRPMS
                                          if make -C utils/rpms srpm; then
                                              rm -rf utils/rpms/_topdir/RPMS
                                              if make -C utils/rpms rpms; then
                                                  ln utils/rpms/_topdir/{RPMS/*,SRPMS}/*  artifacts/sles12.3/
                                                  createrepo artifacts/sles12.3/
                                              else
                                                  exit \${PIPESTATUS[0]}
                                              fi
                                          else
                                              exit \${PIPESTATUS[0]}
                                          fi'''
                        }
                    }
                    post {
                        always {
                            archiveArtifacts artifacts: 'artifacts/sles12.3/**'
                        }
                        success {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "SUCCESS"
                        }
                        unstable {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "UNSTABLE", ignore_failure: true
                        }
                        failure {
                            stepResult name: env.STAGE_NAME, context: "build",
                                       result: "FAILURE", ignore_failure: true
                        }
                    }
                }
                stage('Build on CentOS 7') {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " +
                                                '$BUILDARGS ' +
                                                "--build-arg QUICKBUILD=" + env.QUICKBUILD
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   failure_artifacts: 'config.log-centos7-gcc'
                        stash name: 'CentOS-install', includes: 'install/**'
                        stash name: 'CentOS-build-vars', includes: ".build_vars${arch}.*"
                        stash name: 'CentOS-tests',
                                    includes: '''build/src/rdb/raft/src/tests_main,
                                                 build/src/common/tests/btree_direct,
                                                 build/src/common/tests/btree,
                                                 build/src/common/tests/sched,
                                                 build/src/common/tests/drpc_tests,
                                                 build/src/common/tests/acl_api_tests,
                                                 build/src/common/tests/acl_util_tests,
                                                 build/src/common/tests/acl_util_real,
                                                 build/src/iosrv/tests/drpc_progress_tests,
                                                 build/src/control/src/github.com/daos-stack/daos/src/control/mgmt,
                                                 build/src/client/api/tests/eq_tests,
                                                 build/src/iosrv/tests/drpc_handler_tests,
                                                 build/src/iosrv/tests/drpc_listener_tests,
                                                 build/src/security/tests/cli_security_tests,
                                                 build/src/security/tests/srv_acl_tests,
                                                 build/src/vos/vea/tests/vea_ut,
                                                 build/src/common/tests/umem_test,
                                                 scons_local/build_info/**,
                                                 src/common/tests/btree.sh,
                                                 src/control/run_go_tests.sh,
                                                 src/rdb/raft_tests/raft_tests.py,
                                                 src/vos/tests/evt_ctl.sh'''
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-centos7",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            sh "rm -rf _build.external${arch}"
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                        }
                        unstable {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-centos7-gcc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-centos7-gcc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-centos7-gcc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-centos7-gcc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on CentOS 7 with Clang') {
                    when {
                        beforeAgent true
                        allOf {
                            branch 'master'
                            expression { return env.QUICKBUILD == '1' }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.centos.7'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-centos7 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}", COMPILER: "clang",
                                   failure_artifacts: 'config.log-centos7-clang'
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-centos7-clang",
                                             tools: [ clang(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-centos7-clang
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-centos7-clang',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-centos7-clang
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-centos7-clang',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on Ubuntu 18.04') {
                    when {
                        beforeAgent true
                        allOf {
                            branch 'master'
                            expression { return env.QUICKBUILD == '1' }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu.18.04'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-ubuntu18.04 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   failure_artifacts: 'config.log-ubuntu18.04-gcc'
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-ubuntu18",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-ubuntu18.04-gcc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-ubuntu18.04-gcc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-ubuntu18.04-gcc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-ubuntu18.04-gcc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on Ubuntu 18.04 with Clang') {
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            expression { env.CHANGE_TARGET != 'weekly-testing' }
                            expression { return env.QUICKBUILD == '1' }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.ubuntu.18.04'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-ubuntu18.04 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}", COMPILER: "clang",
                                   failure_artifacts: 'config.log-ubuntu18.04-clag'
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-ubuntu18-clang",
                                             tools: [ clang(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-ubuntu18.04-clang
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-ubuntu18.04-clang',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-ubuntu18.04-clang
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-ubuntu18.04-clang',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on SLES 12.3') {
                    when {
                        beforeAgent true
                        allOf {
                            environment name: 'SLES12_3_DOCKER', value: 'true'
                            expression { return env.QUICKBUILD == '1' }
                            not { branch 'weekly-testing' }
                            expression { env.CHANGE_TARGET != 'weekly-testing' }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.sles.12.3'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-sles12.3 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   failure_artifacts: 'config.log-sles12.3-gcc'
                    }
                    post {
                        always {
                            node('lightweight') {
                                /* Stack dumping for sles12sp3/leap42.3:
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-sles12.3",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                                */
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-sles12.3-gcc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-sles12.3-gcc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-sles12.3-gcc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-sles12.3-gcc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on Leap 42.3') {
                    when {
                        beforeAgent true
                        allOf {
                            environment name: 'LEAP42_3_DOCKER', value: 'true'
                            expression { return env.QUICKBUILD == '1' }
                            not { branch 'weekly-testing' }
                            expression { env.CHANGE_TARGET != 'weekly-testing' }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.42.3'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-leap42.3 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   failure_artifacts: 'config.log-leap42.3-gcc'
                    }
                    post {
                        always {
                            node('lightweight') {
                                /* Stack dumping for sles12sp3/leap42.3:
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-leap42.3",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                                */
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-leap42.3-gcc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-leap42.3-gcc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-leap42.3-gcc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-leap42.3-gcc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on Leap 15') {
                    when {
                        beforeAgent true
                        allOf {
                            branch 'master'
                            expression { return env.QUICKBUILD == '1' }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-leap15 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}",
                                   failure_artifacts: 'config.log-leap15-gcc'
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-leap15",
                                             tools: [ gcc4(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-leap15-gcc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-leap15-gcc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-leap15-gcc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-leap15-gcc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on Leap 15 with Clang') {
                    when {
                        beforeAgent true
                        allOf {
                            branch 'master'
                            expression { return env.QUICKBUILD == '1' }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-leap15 " + '$BUILDARGS'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}", COMPILER: "clang",
                                   failure_artifacts: 'config.log-leap15-clang'
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-leap15-clang",
                                             tools: [ clang(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-leap15-clang
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-leap15-clang',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-leap15-clang
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-leap15-clang',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
                stage('Build on Leap 15 with Intel-C') {
                    when {
                        beforeAgent true
                        allOf {
                            not { branch 'weekly-testing' }
                            expression { env.CHANGE_TARGET != 'weekly-testing' }
                            expression { return env.QUICKBUILD == '1' }
                        }
                    }
                    agent {
                        dockerfile {
                            filename 'Dockerfile.leap.15'
                            dir 'utils/docker'
                            label 'docker_runner'
                            additionalBuildArgs "-t ${sanitized_JOB_NAME}-leap15 " + '$BUILDARGS'
                            args '-v /opt/intel:/opt/intel'
                        }
                    }
                    steps {
                        sconsBuild clean: "_build.external${arch}", COMPILER: "icc",
                                   failure_artifacts: 'config.log-leap15-icc'
                    }
                    post {
                        always {
                            node('lightweight') {
                                recordIssues enabledForFailure: true,
                                             aggregatingResults: true,
                                             id: "analysis-leap15-intelc",
                                             tools: [ intel(), cppCheck() ],
                                             filters: [excludeFile('.*\\/_build\\.external\\/.*'),
                                                       excludeFile('_build\\.external\\/.*')]
                            }
                            /* when JENKINS-39203 is resolved, can probably use stepResult
                               here and remove the remaining post conditions
                               stepResult name: env.STAGE_NAME,
                                          context: 'build/' + env.STAGE_NAME,
                                          result: ${currentBuild.currentResult}
                            */
                        }
                        success {
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                            */
                            sh "rm -rf _build.external${arch}"
                        }
                        unstable {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-leap15-intelc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-leap15-intelc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                            */
                        }
                        failure {
                            sh '''if [ -f config.log ]; then
                                      mv config.log config.log-leap15-intelc
                                  fi'''
                            archiveArtifacts artifacts: 'config.log-leap15-intelc',
                                             allowEmptyArchive: true
                            /* temporarily moved into stepResult due to JENKINS-39203
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'build/' + env.STAGE_NAME,
                                         status: 'ERROR'
                            */
                        }
                    }
                }
            }
        }
        stage('Test') {
            when {
                beforeAgent true
                // expression { skipTest != true }
                expression { env.NO_CI_TESTING != 'true' }
                expression {
                    sh script: 'git show -s --format=%B | grep "^Skip-test: true"',
                       returnStatus: true
                }
            }
            parallel {
                stage('Functional') {
                    agent {
                        label 'ci_vm9'
                    }
                    steps {
                        provisionNodes NODELIST: env.NODELIST,
                                       node_count: 9,
                                       snapshot: true,
                                       inst_repos: daos_repos + ' ' + ior_repos +
                                                   ' romio@PR-1 testmpio@PR-1',
                                       inst_rpms: "ior-hpc mpich-autoload romio-tests"
                        runTest stashes: [ 'CentOS-install', 'CentOS-build-vars' ],
                                script: '''test_tag=$(git show -s --format=%B | sed -ne "/^Test-tag:/s/^.*: *//p")
                                           if [ -z "$test_tag" ]; then
                                               test_tag=pr,-hw
                                           fi
                                           tnodes=$(echo $NODELIST | cut -d ',' -f 1-9)
                                           rm -rf src/tests/ftest/avocado ./*_results.xml
                                           mkdir -p src/tests/ftest/avocado/job-results
                                           ./ftest.sh "$test_tag" $tnodes
                                           # Remove the latest avocado symlink directory to avoid inclusion in the
                                           # jenkins build artifacts
                                           unlink src/tests/ftest/avocado/job-results/latest''',
                                junit_files: "src/tests/ftest/avocado/*/*/*.xml src/tests/ftest/*_results.xml",
                                failure_artifacts: env.STAGE_NAME
                    }
                    post {
                        always {
                            sh '''rm -rf src/tests/ftest/avocado/*/*/html/
                                  if [ -n "$STAGE_NAME" ]; then
                                      rm -rf "$STAGE_NAME/"
                                      mkdir "$STAGE_NAME/"
                                      # compress those potentially huge DAOS logs
                                      if daos_logs=$(ls src/tests/ftest/avocado/job-results/*/daos_logs/*); then
                                          lbzip2 $daos_logs
                                      fi
                                      arts="$arts$(ls *daos{,_agent}.log* 2>/dev/null)" && arts="$arts"$'\n'
                                      arts="$arts$(ls -d src/tests/ftest/avocado/job-results/* 2>/dev/null)" && arts="$arts"$'\n'
                                      arts="$arts$(ls src/tests/ftest/*.stacktrace 2>/dev/null || true)"
                                      if [ -n "$arts" ]; then
                                          mv $(echo $arts | tr '\n' ' ') "$STAGE_NAME/"
                                      fi
                                  else
                                      echo "The STAGE_NAME environment variable is missing!"
                                      false
                                  fi'''
                            archiveArtifacts artifacts: env.STAGE_NAME + '/**'
                            junit env.STAGE_NAME + '/*/results.xml, src/tests/ftest/*_results.xml'
                        }
                        /* temporarily moved into runTest->stepResult due to JENKINS-39203
                        success {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'SUCCESS'
                        }
                        unstable {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'FAILURE'
                        }
                        failure {
                            githubNotify credentialsId: 'daos-jenkins-commit-status',
                                         description: env.STAGE_NAME,
                                         context: 'test/' + env.STAGE_NAME,
                                         status: 'ERROR'
                        }
                        */
                    }
                }
            }
        }
    }
}
