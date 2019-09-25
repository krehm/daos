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

import (
	"fmt"
	"os"
	"os/exec"
	"strings"
	"sync"

	"github.com/pkg/errors"

	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
)

const (
	defaultUnmountFlags = 0
	defaultMountFlags   = 0

	defaultMountPointPerms = 0700

	fsTypeNone  = "none"
	fsTypeExt4  = "ext4"
	fsTypeTmpfs = "tmpfs"

	dcpmFsType    = fsTypeExt4
	dcpmMountOpts = "dax"

	ramFsType = fsTypeTmpfs
)

type (
	// Module represents a SCM DIMM.
	//
	// This is a simplified representation of the raw struct used in the ipmctl package.
	Module struct {
		ChannelID       uint32
		ChannelPosition uint32
		ControllerID    uint32
		SocketID        uint32
		PhysicalID      uint32
		Capacity        uint64
	}

	// Namespace represents a mapping between AppDirect regions and block device files.
	Namespace struct {
		UUID        string
		BlockDevice string
		Name        string
		NumaNode    uint32 `json:"numa_node"`
	}
)

type (
	PrepareRequest struct {
		State types.ScmState
	}
	PrepareResponse struct {
		State          types.ScmState
		RebootRequired bool
		Namespaces     []Namespace
	}

	DcpmParams struct {
		Devices []string
	}
	RamdiskParams struct {
		Size uint
	}
	FormatRequest struct {
		Reformat   bool
		Mountpoint string
		Ramdisk    *RamdiskParams
		Dcpm       *DcpmParams
	}
	FormatResponse struct {
		Mountpoint string
		Formatted  bool
	}

	MountRequest struct {
		Source string
		Target string
		FsType string
		Flags  uintptr
		Data   string
	}
	MountResponse struct {
		Target  string
		Mounted bool
	}

	UpdateRequest  struct{}
	UpdateResponse struct{}

	ScanRequest struct {
		Rescan bool
	}
	ScanResponse struct {
		Modules    []Module
		Namespaces []Namespace
	}

	scmBackend interface {
		Discover() ([]Module, error)
		Prep(types.ScmState) (bool, []Namespace, error)
		PrepReset(types.ScmState) (bool, error)
		GetState() (types.ScmState, error)
		GetNamespaces() ([]Namespace, error)
	}

	systemProvider interface {
		IsMounted(target string) (bool, error)
		Mount(source, target, fstype string, flags uintptr, data string) error
		Unmount(target string, flags int) error
		Mkfs(fsType, device string, force bool) error
		Getfs(device string) (string, error)
	}

	scmSystemProvider struct {
		system.LinuxProvider
	}

	Provider struct {
		sync.RWMutex
		scanCompleted bool
		modules       []Module
		namespaces    []Namespace

		log     logging.Logger
		backend scmBackend
		sys     systemProvider
	}
)

func checkDevice(device string) error {
	st, err := os.Stat(device)
	if err != nil {
		return errors.Wrapf(err, "stat failed on %s", device)
	}

	if st.Mode()&os.ModeDevice == 0 {
		return errors.Errorf("%s is not a device file", device)
	}

	return nil
}

func (ssp *scmSystemProvider) Mkfs(fsType, device string, force bool) error {
	cmdPath, err := exec.LookPath(fmt.Sprintf("mkfs.%s", fsType))
	if err != nil {
		return errors.Wrapf(err, "unable to find mkfs.%s", fsType)
	}

	if err := checkDevice(device); err != nil {
		return err
	}

	var forceOpt string
	if force {
		forceOpt = "-F"
	}

	// TODO: Think about a way to allow for some kind of progress
	// callback so that the user has some visibility into long-running
	// format operations (very large devices).
	out, err := exec.Command(cmdPath, forceOpt, device).Output()
	if err != nil {
		return &runCmdError{
			wrapped: err,
			stdout:  string(out),
		}
	}

	return nil
}

func (ssp *scmSystemProvider) Getfs(device string) (string, error) {
	cmdPath, err := exec.LookPath("file")
	if err != nil {
		return fsTypeNone, errors.Wrap(err, "unable to find file")
	}

	if err := checkDevice(device); err != nil {
		return fsTypeNone, err
	}

	out, err := exec.Command(cmdPath, device).Output()
	if err != nil {
		return fsTypeNone, &runCmdError{
			wrapped: err,
			stdout:  string(out),
		}
	}

	return parseFsType(string(out))
}

func parseFsType(input string) (string, error) {
	// /dev/pmem0: Linux rev 1.0 ext4 filesystem data, UUID=09619a0d-0c9e-46b4-add5-faf575dd293d
	// /dev/pmem1: data
	parts := strings.Split(input, " ")
	switch len(parts) {
	case 2:
		if parts[1] == "data" {
			return fsTypeNone, nil
		}
	case 0, 1, 3, 4:
		return fsTypeNone, errors.Errorf("unable to parse %q", input)
	default:
		return parts[4], nil
	}

	return fsTypeNone, errors.Errorf("unable to determine fs type from %q", input)
}

func DefaultProvider(log logging.Logger) *Provider {
	lp := system.DefaultProvider()
	p := &scmSystemProvider{
		LinuxProvider: *lp,
	}
	return NewProvider(log, defaultIpmCtlRunner(log), p)
}

func NewProvider(log logging.Logger, backend scmBackend, sys systemProvider) *Provider {
	return &Provider{
		log:     log,
		backend: backend,
		sys:     sys,
	}
}

func (p *Provider) isInitialized() bool {
	p.RLock()
	defer p.RUnlock()
	return p.scanCompleted
}

func (p *Provider) Prepare(req PrepareRequest) (*PrepareResponse, error) {
	return nil, nil
}

func (p *Provider) CheckFormat(req FormatRequest) (*FormatResponse, error) {
	if !p.isInitialized() {
		if _, err := p.Scan(ScanRequest{}); err != nil {
			return nil, err
		}
	}
	res := &FormatResponse{
		Mountpoint: req.Mountpoint,
		Formatted:  true,
	}

	isMounted, err := p.sys.IsMounted(req.Mountpoint)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to check if %s is mounted", req.Mountpoint)
	}
	if isMounted {
		return res, nil
	}

	if req.Dcpm != nil {
		if len(req.Dcpm.Devices) != 1 || len(req.Dcpm.Devices[0]) == 0 {
			return nil, FaultFormatInvalidDeviceCount
		}
		dev := req.Dcpm.Devices[0]
		fsType, err := p.sys.Getfs(dev)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to check if %s is formatted", dev)
		}

		p.log.Debugf("device %s filesystem: %s", dev, fsType)
		if fsType != fsTypeNone {
			return res, nil
		}
	}

	res.Formatted = false
	return res, nil
}

func (p *Provider) clearMount(req FormatRequest) error {
	mounted, err := p.sys.IsMounted(req.Mountpoint)
	if err != nil {
		return errors.Wrapf(err, "failed to check if %s is mounted", req.Mountpoint)
	}

	if mounted {
		_, err := p.unmount(req.Mountpoint, defaultUnmountFlags)
		if err != nil {
			return err
		}
	}

	if err := os.RemoveAll(req.Mountpoint); err != nil {
		if !os.IsNotExist(err) {
			return errors.Wrapf(err, "failed to remove %s", req.Mountpoint)
		}
	}

	return nil
}

func (p *Provider) formatRamdisk(req FormatRequest) (*FormatResponse, error) {
	if req.Ramdisk == nil {
		return nil, FaultFormatMissingParam
	}

	res, err := p.MountRamdisk(req.Mountpoint, req.Ramdisk.Size)
	if err != nil {
		return nil, err
	}
	return &FormatResponse{
		Mountpoint: res.Target,
		Formatted:  res.Mounted,
	}, nil
}

func (p *Provider) formatDcpm(req FormatRequest) (*FormatResponse, error) {
	if req.Dcpm == nil {
		return nil, FaultFormatMissingParam
	}
	if len(req.Dcpm.Devices) != 1 {
		return nil, FaultFormatInvalidDeviceCount
	}

	src := req.Dcpm.Devices[0]
	alreadyMounted, err := p.sys.IsMounted(src)
	if err != nil {
		return nil, errors.Wrapf(err, "unable to check if %s is already mounted", src)
	}
	if alreadyMounted {
		return nil, errors.Wrap(FaultFormatDeviceAlreadyMounted, src)
	}

	p.log.Debugf("running mkfs.%s %s", dcpmFsType, src)
	if err := p.sys.Mkfs(src, dcpmFsType, req.Reformat); err != nil {
		return nil, errors.Wrapf(err, "failed to format %s", src)
	}

	res, err := p.MountDcpm(src, req.Mountpoint)
	if err != nil {
		return nil, err
	}
	return &FormatResponse{
		Mountpoint: res.Target,
		Formatted:  res.Mounted,
	}, nil
}

func (p *Provider) MountDcpm(device, target string) (*MountResponse, error) {
	return p.mount(device, target, dcpmFsType, defaultMountFlags, dcpmMountOpts)
}

func (p *Provider) MountRamdisk(target string, size uint) (*MountResponse, error) {
	var opts string
	if size > 0 {
		opts = fmt.Sprintf("size=%dg", size)
	}

	return p.mount(ramFsType, target, ramFsType, defaultMountFlags, opts)
}

func (p *Provider) Mount(req MountRequest) (*MountResponse, error) {
	return p.mount(req.Source, req.Target, req.FsType, req.Flags, req.Data)
}

func (p *Provider) mount(src, target, fsType string, flags uintptr, opts string) (*MountResponse, error) {
	if err := os.Mkdir(target, defaultMountPointPerms); err != nil {
		return nil, errors.Wrapf(err, "failed to create mountpoint %s", target)
	}

	p.log.Debugf("mount %s->%s (%s) (%s)", src, target, fsType, opts)
	if err := p.sys.Mount(src, target, fsType, flags, opts); err != nil {
		return nil, errors.Wrapf(err, "mount %s->%s failed", src, target)
	}

	return &MountResponse{
		Target:  target,
		Mounted: true,
	}, nil
}

func (p *Provider) Unmount(req MountRequest) (*MountResponse, error) {
	return p.unmount(req.Target, int(req.Flags))
}

func (p *Provider) unmount(target string, flags int) (*MountResponse, error) {
	if err := p.sys.Unmount(target, flags); err != nil {
		return nil, errors.Wrapf(err, "failed to unmount %s", target)
	}

	return &MountResponse{
		Target:  target,
		Mounted: false,
	}, nil
}

func (p *Provider) IsMounted(target string) (bool, error) {
	return p.sys.IsMounted(target)
}

func (p *Provider) Format(req FormatRequest) (*FormatResponse, error) {
	if req.Ramdisk != nil && req.Dcpm != nil {
		return nil, FaultFormatConflictingParam
	}
	if req.Mountpoint == "" {
		return nil, FaultFormatMissingMountpoint
	}

	check, err := p.CheckFormat(req)
	if err != nil {
		return nil, err
	}
	if check.Formatted {
		if !req.Reformat {
			return nil, FaultFormatNoReformat
		}
	}

	if err := p.clearMount(req); err != nil {
		return nil, errors.Wrap(err, "failed to clear existing mount")
	}

	switch {
	case req.Ramdisk != nil:
		return p.formatRamdisk(req)
	case req.Dcpm != nil:
		return p.formatDcpm(req)
	default:
		return nil, FaultFormatMissingParam
	}
}

func (p *Provider) Update(req UpdateRequest) (*UpdateResponse, error) {
	return nil, nil
}

func (p *Provider) createScanResponse() *ScanResponse {
	p.RLock()
	defer p.RUnlock()

	return &ScanResponse{
		Modules:    p.modules,
		Namespaces: p.namespaces,
	}
}

func (p *Provider) Scan(req ScanRequest) (*ScanResponse, error) {
	if p.isInitialized() && !req.Rescan {
		return p.createScanResponse(), nil
	}

	modules, err := p.backend.Discover()
	if err != nil {
		return nil, err
	}

	p.Lock()
	p.scanCompleted = true
	p.modules = modules
	p.Unlock()

	namespaces, err := p.backend.GetNamespaces()
	if err != nil {
		// FIXME: Don't really like this, but need to add it for now to
		// maintain compatibility with the logic added for DAOS-3307.
		return p.createScanResponse(), err
	}

	p.Lock()
	p.namespaces = namespaces
	p.Unlock()

	return p.createScanResponse(), nil
}
