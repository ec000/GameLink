package main

import (
	"os"
	"syscall"
)

func lockState(path string) (*os.File, error) {
	name, err := syscall.UTF16PtrFromString(path + ".lock")
	if err != nil {
		return nil, err
	}
	// No sharing: another process cannot open this state lock. Windows closes
	// the handle on process exit/crash; a remaining file is not a stale lock.
	handle, err := syscall.CreateFile(name, syscall.GENERIC_READ|syscall.GENERIC_WRITE,
		0, nil, syscall.OPEN_ALWAYS, syscall.FILE_ATTRIBUTE_NORMAL, 0)
	if err != nil {
		return nil, err
	}
	return os.NewFile(uintptr(handle), path+".lock"), nil
}
