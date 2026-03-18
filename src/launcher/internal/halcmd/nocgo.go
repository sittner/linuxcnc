//go:build !cgo

package halcmd

import "errors"

// errNoCGO is returned by all stub functions when CGO is not available.
var errNoCGO = errors.New("halcmd: CGO is required but not available")

func halStartThreads() error                   { return errNoCGO }
func halStopThreads() error                    { return errNoCGO }
func halListComponents() ([]string, error)     { return nil, errNoCGO }
func halUnloadAll(_ int) error                 { return errNoCGO }
func halNewSig(_ string, _ PinType) error      { return errNoCGO }
func halDelSig(_ string) error                 { return errNoCGO }
func halSetS(_, _ string) error                { return errNoCGO }
func halGetS(_ string) (string, error)         { return "", errNoCGO }
func halSType(_ string) (PinType, error)       { return 0, errNoCGO }
func halSetP(_, _ string) error                { return errNoCGO }
func halGetP(_ string) (string, error)         { return "", errNoCGO }
func halPType(_ string) (PinType, error)       { return 0, errNoCGO }
func halNet(_ string, _ []string) error        { return errNoCGO }
func halLinkPS(_, _ string) error              { return errNoCGO }
func halUnlinkP(_ string) error                { return errNoCGO }
func halAddF(_, _ string, _ int) error         { return errNoCGO }
func halDelF(_, _ string) error                { return errNoCGO }
func halSetLock(_ int) error                   { return errNoCGO }
func halGetLock() int                          { return 0 }
func halAlias(_, _, _ string) error            { return errNoCGO }
func halUnAlias(_, _ string) error             { return errNoCGO }
func halLoadRT(_ string, _ []string) error     { return errNoCGO }
func halUnloadRT(_ string) error               { return errNoCGO }
func halCollectRTComps(_ int) ([]string, error) { return nil, errNoCGO }
func halUnloadUSR(_ string) error              { return errNoCGO }
func halWaitUSR(_ string, _ int) error         { return errNoCGO }

func halLoadUSR(_ int, _ string, _ int, _ string, _ []string) error {
	return errNoCGO
}

func halListPins(_ string) ([]string, error)         { return nil, errNoCGO }
func halListSigs(_ string) ([]string, error)         { return nil, errNoCGO }
func halListRetainSigs(_ string) ([]string, error)   { return nil, errNoCGO }
func halListParams(_ string) ([]string, error)       { return nil, errNoCGO }
func halListFuncts(_ string) ([]string, error)       { return nil, errNoCGO }
func halListThreads(_ string) ([]string, error)      { return nil, errNoCGO }

func halShowComps(_ string) ([]CompInfo, error)     { return nil, errNoCGO }
func halShowPins(_ string) ([]PinInfo, error)       { return nil, errNoCGO }
func halShowParams(_ string) ([]ParamInfo, error)   { return nil, errNoCGO }
func halShowSigs(_ string) ([]SigInfo, error)       { return nil, errNoCGO }
func halShowFuncts(_ string) ([]FunctInfo, error)   { return nil, errNoCGO }
func halShowThreads(_ string) ([]ThreadInfo, error) { return nil, errNoCGO }

func halStatus() (*StatusInfo, error)    { return nil, errNoCGO }
func halSave(_ string) ([]string, error) { return nil, errNoCGO }
func halSetDebug(_ int) error            { return errNoCGO }
