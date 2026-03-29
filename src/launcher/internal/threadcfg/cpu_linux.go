package threadcfg

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
)

// cpuTopology holds the detected CPU topology for thread CPU assignment.
type cpuTopology struct {
	online        []int        // all online CPUs
	isolated      []int        // isolated CPUs (from kernel cmdline)
	physicalCores map[int]bool // true if CPU is the primary (lowest-numbered) in its HT group
	siblingOf     map[int]int  // maps each CPU to its primary sibling
}

// detectTopology reads Linux sysfs to determine online CPUs, isolated CPUs,
// and hyperthreading sibling relationships.
func detectTopology() (*cpuTopology, error) {
	topo := &cpuTopology{
		physicalCores: make(map[int]bool),
		siblingOf:     make(map[int]int),
	}

	nCPU := runtime.NumCPU()
	for i := 0; i < nCPU; i++ {
		if cpuOnline(i) {
			topo.online = append(topo.online, i)
		}
	}

	topo.isolated = parseIsolatedCPUs()

	// Determine HT sibling groups from sysfs topology.
	// For each online CPU, read thread_siblings_list. The lowest-numbered
	// CPU in each group is the "physical core"; all others are HT shadows.
	seen := make(map[int]bool)
	for _, cpu := range topo.online {
		if seen[cpu] {
			continue
		}
		siblings := readSiblingsList(cpu)
		if len(siblings) == 0 {
			siblings = []int{cpu}
		}
		sort.Ints(siblings)
		primary := siblings[0]
		for _, s := range siblings {
			seen[s] = true
			topo.siblingOf[s] = primary
		}
		topo.physicalCores[primary] = true
	}

	return topo, nil
}

// cpuExists reports whether a CPU number is online.
func (t *cpuTopology) cpuExists(cpu int) bool {
	for _, c := range t.online {
		if c == cpu {
			return true
		}
	}
	return false
}

// isHTSibling reports whether a CPU is a secondary HT logical core
// (not the primary/physical core of its sibling group).
func (t *cpuTopology) isHTSibling(cpu int) bool {
	primary, ok := t.siblingOf[cpu]
	if !ok {
		return false
	}
	return primary != cpu
}

// assignCPUs computes per-thread CPU assignments according to the rules:
//  1. Validate user-pinned CPUs: must exist, must not be HT sibling.
//  2. Build pool of assignable CPUs: isolated physical cores, minus user-pinned.
//  3. Auto-assign unpinned threads from pool (highest CPU first).
//  4. If pool exhausted, remaining threads get -1 (no affinity).
func (t *cpuTopology) assignCPUs(threads []ThreadConfig) ([]ThreadConfig, error) {
	// Validate user-specified CPUs.
	for i, th := range threads {
		if th.CPU < 0 {
			continue
		}
		if !t.cpuExists(th.CPU) {
			return nil, fmt.Errorf("[THREAD-%s] CPU=%d: CPU does not exist (online CPUs: %v)",
				threads[i].section, th.CPU, t.online)
		}
		if t.isHTSibling(th.CPU) {
			primary := t.siblingOf[th.CPU]
			return nil, fmt.Errorf("[THREAD-%s] CPU=%d: is a hyperthreaded sibling of physical core %d — pin to %d instead",
				threads[i].section, th.CPU, primary, primary)
		}
	}

	// Build pool: isolated physical cores not pinned by user.
	userPinned := make(map[int]bool)
	for _, th := range threads {
		if th.CPU >= 0 {
			userPinned[th.CPU] = true
		}
	}

	var pool []int
	for _, cpu := range t.isolated {
		if t.physicalCores[cpu] && !userPinned[cpu] {
			pool = append(pool, cpu)
		}
	}
	// Sort descending — assign highest first.
	sort.Sort(sort.Reverse(sort.IntSlice(pool)))

	// Assign unpinned threads from pool.
	result := make([]ThreadConfig, len(threads))
	copy(result, threads)
	poolIdx := 0
	for i := range result {
		if result[i].CPU >= 0 {
			continue // user-specified
		}
		if poolIdx < len(pool) {
			result[i].CPU = pool[poolIdx]
			poolIdx++
		}
		// else: stays -1 (no affinity)
	}

	return result, nil
}

// cpuOnline checks if a CPU is online via sysfs.
func cpuOnline(cpu int) bool {
	if cpu == 0 {
		return true // CPU 0 is always online
	}
	path := fmt.Sprintf("/sys/devices/system/cpu/cpu%d/online", cpu)
	data, err := os.ReadFile(path)
	if err != nil {
		// If sysfs entry missing, check if the CPU directory exists.
		dir := fmt.Sprintf("/sys/devices/system/cpu/cpu%d", cpu)
		if _, err := os.Stat(dir); err == nil {
			return true
		}
		return false
	}
	return strings.TrimSpace(string(data)) == "1"
}

// parseIsolatedCPUs reads /sys/devices/system/cpu/isolated and returns the
// set of isolated CPU numbers.
func parseIsolatedCPUs() []int {
	data, err := os.ReadFile("/sys/devices/system/cpu/isolated")
	if err != nil {
		return nil
	}
	return parseCPUList(strings.TrimSpace(string(data)))
}

// readSiblingsList reads the thread_siblings_list for a CPU from sysfs.
func readSiblingsList(cpu int) []int {
	path := filepath.Join(fmt.Sprintf("/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu))
	data, err := os.ReadFile(path)
	if err != nil {
		return nil
	}
	return parseCPUList(strings.TrimSpace(string(data)))
}

// parseCPUList parses a Linux cpulist format string (e.g. "0-3,5,7-9")
// into a sorted slice of CPU numbers.
func parseCPUList(s string) []int {
	if s == "" {
		return nil
	}
	var result []int
	for _, token := range strings.Split(s, ",") {
		token = strings.TrimSpace(token)
		if token == "" {
			continue
		}
		if idx := strings.IndexByte(token, '-'); idx >= 0 {
			a, err1 := strconv.Atoi(token[:idx])
			b, err2 := strconv.Atoi(token[idx+1:])
			if err1 != nil || err2 != nil {
				continue
			}
			for i := a; i <= b; i++ {
				result = append(result, i)
			}
		} else {
			n, err := strconv.Atoi(token)
			if err != nil {
				continue
			}
			result = append(result, n)
		}
	}
	sort.Ints(result)
	return result
}
