// Package inirest exposes the parsed INI file via the generated ini GMI API.
package inirest

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/iniapi"
	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
	"github.com/sittner/linuxcnc/src/gomc/pkg/inifile"
)

type iniImpl struct {
	ini *inifile.IniFile
}

func (im *iniImpl) Query(items []iniapi.IniQueryItem) ([]iniapi.IniQueryResult, error) {
	if im.ini == nil {
		return nil, fmt.Errorf("INI file not loaded")
	}

	results := make([]iniapi.IniQueryResult, len(items))
	for i, q := range items {
		if q.All != nil && *q.All {
			vals := im.ini.GetAll(q.Section, q.Key)
			if vals == nil {
				vals = []string{}
			}
			results[i] = iniapi.IniQueryResult{Values: vals}
		} else {
			v := im.ini.Get(q.Section, q.Key)
			if v == "" && !im.keyExists(q.Section, q.Key) {
				results[i] = iniapi.IniQueryResult{}
			} else {
				results[i] = iniapi.IniQueryResult{Value: v}
			}
		}
	}
	return results, nil
}

func (im *iniImpl) GetParameterFile() (string, error) {
	if im.ini == nil {
		return "", fmt.Errorf("INI file not loaded")
	}
	rel := im.ini.Get("RS274NGC", "PARAMETER_FILE")
	if rel == "" {
		return "", fmt.Errorf("[RS274NGC]PARAMETER_FILE not set")
	}
	path := rel
	if !filepath.IsAbs(path) {
		path = filepath.Join(filepath.Dir(im.ini.SourceFile()), rel)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return "", fmt.Errorf("paramfile: %w", err)
	}
	return string(data), nil
}

func (im *iniImpl) keyExists(section, key string) bool {
	entries := im.ini.GetSection(section)
	for _, e := range entries {
		if e.Key == key {
			return true
		}
	}
	return false
}

// Register registers the INI REST API with the given registry.
func Register(reg *apiserver.Registry, parsed *inifile.IniFile) error {
	apiserver.RegisterMeta(iniapi.IniMeta)
	impl := &iniImpl{ini: parsed}
	return iniapi.RegisterIniAPI(reg, "ini", impl)
}
