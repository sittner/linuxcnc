//go:build !cgo

package rtapi

import "errors"

// errNoCGO is returned by all stub functions when CGO is not available.
var errNoCGO = errors.New("rtapi: CGO is required but not available")

func Init() error                              { return errNoCGO }
func LoadModule(_ string, _ ...string) error   { return errNoCGO }
func UnloadModule(_ string) error              { return errNoCGO }
