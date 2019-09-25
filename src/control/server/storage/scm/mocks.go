//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//
package scm

type (
	MockSysConfig struct {
		IsMountedRetBool bool
		IsMountedRetErr  error
		MountRetErr      error
		UnmountRetErr    error
		MkfsRetErr       error
		GetfsRetStr      string
		GetfsRetErr      error
	}

	MockSysProvider struct {
		cfg MockSysConfig
	}
)

func (msp *MockSysProvider) IsMounted(_ string) (bool, error) {
	return msp.cfg.IsMountedRetBool, msp.cfg.IsMountedRetErr
}

func (msp *MockSysProvider) Mount(_, _, _ string, _ uintptr, _ string) error {
	if msp.cfg.MountRetErr == nil {
		msp.cfg.IsMountedRetBool = true
	}
	return msp.cfg.MountRetErr
}

func (msp *MockSysProvider) Unmount(_ string, _ int) error {
	if msp.cfg.UnmountRetErr == nil {
		msp.cfg.IsMountedRetBool = false
	}
	return msp.cfg.UnmountRetErr
}

func (msp *MockSysProvider) Mkfs(_, _ string, _ bool) error {
	return msp.cfg.MkfsRetErr
}

func (msp *MockSysProvider) Getfs(_ string) (string, error) {
	return msp.cfg.GetfsRetStr, msp.cfg.GetfsRetErr
}

func NewMockSysProvider(cfg *MockSysConfig) *MockSysProvider {
	if cfg == nil {
		cfg = &MockSysConfig{}
	}
	return &MockSysProvider{
		cfg: *cfg,
	}
}

func DefaultMockSysProvider() *MockSysProvider {
	return NewMockSysProvider(nil)
}
