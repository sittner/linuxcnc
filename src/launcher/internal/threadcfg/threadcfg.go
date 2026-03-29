// Package threadcfg parses [THREAD-*] INI sections, validates thread ordering,
// detects CPU topology, and computes per-thread CPU assignments.
package threadcfg

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/sittner/linuxcnc/src/launcher/pkg/inifile"
)

const threadSectionPrefix = "THREAD-"

// ThreadConfig represents a single HAL realtime thread parsed from a
// [THREAD-<SECTION>] INI section.
type ThreadConfig struct {
	section string // original INI section suffix (e.g. "SERVO")
	Name    string // HAL thread name (e.g. "servo-thread")
	Period  int64  // period in nanoseconds
	FP      int    // uses floating point (0 or 1, default 1)
	CPU     int    // CPU to pin to (-1 = auto-assign)
}

// ParseThreads discovers all [THREAD-*] sections in the INI file and returns
// them in file order. Returns an error if no thread sections are found or if
// any section has invalid configuration.
func ParseThreads(ini *inifile.IniFile) ([]ThreadConfig, error) {
	var threads []ThreadConfig

	for _, sec := range ini.Sections {
		if !strings.HasPrefix(sec.Name, threadSectionPrefix) {
			continue
		}
		suffix := sec.Name[len(threadSectionPrefix):]
		if suffix == "" {
			return nil, fmt.Errorf("[%s]: section suffix is empty", sec.Name)
		}

		entries := ini.GetSection(sec.Name)

		// PERIOD is required.
		periodStr := sectionGet(entries, "PERIOD")
		if periodStr == "" {
			return nil, fmt.Errorf("[%s]: PERIOD is required", sec.Name)
		}
		period, err := strconv.ParseInt(periodStr, 10, 64)
		if err != nil || period <= 0 {
			return nil, fmt.Errorf("[%s]: PERIOD=%q is not a valid positive integer", sec.Name, periodStr)
		}

		// NAME is optional; default: lowercase(suffix) + "-thread".
		name := sectionGet(entries, "NAME")
		if name == "" {
			name = strings.ToLower(suffix) + "-thread"
		}

		// FP is optional; default: 1.
		fp := 1
		fpStr := sectionGet(entries, "FP")
		if fpStr != "" {
			fp64, err := strconv.ParseInt(fpStr, 10, 32)
			if err != nil || (fp64 != 0 && fp64 != 1) {
				return nil, fmt.Errorf("[%s]: FP=%q must be 0 or 1", sec.Name, fpStr)
			}
			fp = int(fp64)
		}

		// CPU is optional; default: -1 (auto-assign).
		cpu := -1
		cpuStr := sectionGet(entries, "CPU")
		if cpuStr != "" {
			cpu64, err := strconv.ParseInt(cpuStr, 10, 32)
			if err != nil || cpu64 < 0 {
				return nil, fmt.Errorf("[%s]: CPU=%q must be a non-negative integer", sec.Name, cpuStr)
			}
			cpu = int(cpu64)
		}

		threads = append(threads, ThreadConfig{
			section: suffix,
			Name:    name,
			Period:  period,
			FP:      fp,
			CPU:     cpu,
		})
	}

	if len(threads) == 0 {
		return nil, fmt.Errorf("no [THREAD-*] sections found in INI file")
	}

	return threads, nil
}

// ValidateOrder checks that threads are ordered fastest-first (ascending
// periods). HAL enforces this at creation time; validating up-front gives a
// clear error pointing at the INI file rather than a cryptic HAL error.
func ValidateOrder(threads []ThreadConfig) error {
	for i := 1; i < len(threads); i++ {
		if threads[i].Period < threads[i-1].Period {
			return fmt.Errorf("[THREAD-%s] PERIOD=%d must appear after [THREAD-%s] PERIOD=%d — "+
				"threads must be ordered fastest-first (ascending period)",
				threads[i].section, threads[i].Period,
				threads[i-1].section, threads[i-1].Period)
		}
	}
	return nil
}

// AssignCPUs detects CPU topology and computes per-thread CPU assignments.
// Validates user-pinned CPUs (must exist, must not be HT sibling) and
// auto-assigns unpinned threads to isolated physical cores (highest first).
// Returns a new slice with CPU fields populated.
func AssignCPUs(threads []ThreadConfig) ([]ThreadConfig, error) {
	topo, err := detectTopology()
	if err != nil {
		return nil, fmt.Errorf("detecting CPU topology: %w", err)
	}
	return topo.assignCPUs(threads)
}

// sectionGet returns the value of the first entry matching key (case-insensitive).
func sectionGet(entries []inifile.Entry, key string) string {
	for _, e := range entries {
		if strings.EqualFold(e.Key, key) {
			return e.Value
		}
	}
	return ""
}
