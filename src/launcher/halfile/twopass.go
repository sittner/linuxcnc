package halfile

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/sittner/linuxcnc/src/launcher/inifile"
)

// loadrtParams holds the accumulated parameters for a single loadrt module.
type loadrtParams struct {
	// form is the counting form used: "count", "num_chan", "names", or "".
	// count/num_chan/names are mutually exclusive; an error is returned if mixed.
	form string

	count    int
	numChan  int
	names    string // comma-separated
	personality string // comma-separated
	debug    int
	other    string // space-separated key=value pairs
}

// twopassOptions holds parsed options from the [HAL]TWOPASS INI value.
type twopassOptions struct {
	verbose  bool
	nodelete bool
	raw      string // the original INI value, used when delegating to legacy haltcl
}

// parseTwopassOptions parses the options string from [HAL]TWOPASS.
// Any non-empty string enables TWOPASS; the string may contain the
// comma- or space-separated keywords "verbose" and "nodelete".
func parseTwopassOptions(value string) twopassOptions {
	opts := twopassOptions{raw: value}
	for _, part := range strings.FieldsFunc(value, func(r rune) bool {
		return r == ',' || r == ' ' || r == '\t'
	}) {
		switch strings.ToLower(strings.TrimSpace(part)) {
		case "verbose":
			opts.verbose = true
		case "nodelete":
			opts.nodelete = true
		}
	}
	return opts
}

// hasNoTwopass reports whether a .hal file contains the magic comment
// "#NOTWOPASS" (case-insensitive, ignoring whitespace).
func hasNoTwopass(path string) (bool, error) {
	f, err := os.Open(path)
	if err != nil {
		return false, fmt.Errorf("opening HAL file %q: %w", path, err)
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := scanner.Text()
		// Strip all whitespace from the line and lower-case it, then check
		// whether it starts with "#notwopass" (matching legacy twopass.tcl behaviour).
		stripped := strings.ToLower(strings.ReplaceAll(line, " ", ""))
		stripped = strings.ReplaceAll(stripped, "\t", "")
		if strings.HasPrefix(stripped, "#notwopass") {
			return true, nil
		}
	}
	return false, scanner.Err()
}

// parseLoadrt parses a "loadrt <module> [params...]" command line (after INI
// substitution) and returns the module name and a loadrtParams struct.
//
// The line should already have had the leading "loadrt" keyword stripped or
// contain it as the first token; either form is accepted.
func parseLoadrt(line string) (module string, params loadrtParams, err error) {
	fields := strings.Fields(line)
	if len(fields) == 0 {
		return "", loadrtParams{}, fmt.Errorf("empty loadrt command")
	}
	// Strip the "loadrt" keyword if present as the first token.
	if strings.ToLower(fields[0]) == "loadrt" {
		fields = fields[1:]
	}
	if len(fields) == 0 {
		return "", loadrtParams{}, fmt.Errorf("loadrt: missing module name")
	}
	module = fields[0]
	fields = fields[1:]

	// Re-join remaining fields so we can handle each key=value token.
	// Note: quoted values containing spaces (e.g. config="num_stepgens=3 num_pwmgens=2")
	// are not supported by simple field splitting; the legacy twopass.tcl handles
	// these via backslash-escaping.  In practice such complex config= values work
	// because halcmd receives them as separate argv entries anyway.
	for _, tok := range fields {
		eqIdx := strings.Index(tok, "=")
		if eqIdx < 0 {
			// bare token with no '=' – treat as "other"
			if params.other == "" {
				params.other = tok
			} else {
				params.other += " " + tok
			}
			continue
		}
		key := tok[:eqIdx]
		val := tok[eqIdx+1:]
		switch key {
		case "count":
			n, e := strconv.Atoi(val)
			if e != nil {
				return module, params, fmt.Errorf("loadrt %s: invalid count=%q: %w", module, val, e)
			}
			params.count += n
			if params.form == "" {
				params.form = "count"
			} else if params.form != "count" {
				return module, params, fmt.Errorf("loadrt %s: cannot mix count= with %s=", module, params.form)
			}
		case "num_chan":
			n, e := strconv.Atoi(val)
			if e != nil {
				return module, params, fmt.Errorf("loadrt %s: invalid num_chan=%q: %w", module, val, e)
			}
			params.numChan += n
			if params.form == "" {
				params.form = "num_chan"
			} else if params.form != "num_chan" {
				return module, params, fmt.Errorf("loadrt %s: cannot mix num_chan= with %s=", module, params.form)
			}
		case "names":
			if params.names == "" {
				params.names = val
			} else {
				params.names += "," + val
			}
			if params.form == "" {
				params.form = "names"
			} else if params.form != "names" {
				return module, params, fmt.Errorf("loadrt %s: cannot mix names= with %s=", module, params.form)
			}
		case "personality":
			if params.personality == "" {
				params.personality = val
			} else {
				params.personality += "," + val
			}
		case "debug":
			n, e := strconv.Atoi(val)
			if e != nil {
				return module, params, fmt.Errorf("loadrt %s: invalid debug=%q: %w", module, val, e)
			}
			params.debug |= n
		default:
			if params.other == "" {
				params.other = tok
			} else {
				params.other += " " + tok
			}
		}
	}
	return module, params, nil
}

// mergeLoadrt merges src into dst.  The two structs must use compatible forms
// (count/num_chan/names are mutually exclusive); an error is returned on conflict.
func mergeLoadrt(dst *loadrtParams, src loadrtParams, module string) error {
	// Validate form compatibility.
	if src.form != "" {
		if dst.form == "" {
			dst.form = src.form
		} else if dst.form != src.form {
			return fmt.Errorf("loadrt %s: cannot mix %s= and %s= forms across files",
				module, dst.form, src.form)
		}
	}
	dst.count += src.count
	dst.numChan += src.numChan
	if src.names != "" {
		if dst.names == "" {
			dst.names = src.names
		} else {
			dst.names += "," + src.names
		}
	}
	if src.personality != "" {
		if dst.personality == "" {
			dst.personality = src.personality
		} else {
			dst.personality += "," + src.personality
		}
	}
	dst.debug |= src.debug
	if src.other != "" {
		if dst.other == "" {
			dst.other = src.other
		} else {
			dst.other += " " + src.other
		}
	}
	return nil
}

// buildLoadrtArgs constructs the halcmd argument list for a merged loadrt.
// The returned slice starts with the module name followed by parameters.
func buildLoadrtArgs(module string, p loadrtParams) []string {
	args := []string{"loadrt", module}
	switch p.form {
	case "count":
		args = append(args, fmt.Sprintf("count=%d", p.count))
	case "num_chan":
		args = append(args, fmt.Sprintf("num_chan=%d", p.numChan))
	case "names":
		args = append(args, "names="+p.names)
	}
	if p.personality != "" {
		args = append(args, "personality="+p.personality)
	}
	if p.debug != 0 {
		args = append(args, fmt.Sprintf("debug=%d", p.debug))
	}
	if p.other != "" {
		args = append(args, strings.Fields(p.other)...)
	}
	return args
}

// readHalLines reads a .hal file and returns its lines after INI variable
// substitution and backslash line continuation handling.
func (e *Executor) readHalLines(path string) ([]string, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("opening HAL file %q: %w", path, err)
	}
	defer f.Close()

	var result []string
	var continuation string
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		raw := scanner.Text()
		line := e.substituteLine(raw)
		// Handle backslash line continuation.
		if strings.HasSuffix(line, "\\") {
			continuation += strings.TrimSuffix(line, "\\")
			continue
		}
		if continuation != "" {
			line = continuation + line
			continuation = ""
		}
		result = append(result, line)
	}
	// If the file ends with a continuation backslash, flush it.
	if continuation != "" {
		result = append(result, continuation)
	}
	return result, scanner.Err()
}

// cmdName extracts the first token (command name) from a HAL command line,
// ignoring leading whitespace and returning "" for blank/comment lines.
func cmdName(line string) string {
	trimmed := strings.TrimSpace(line)
	if trimmed == "" || strings.HasPrefix(trimmed, "#") {
		return ""
	}
	return strings.ToLower(strings.Fields(trimmed)[0])
}

// halfileEntry holds a resolved HALFILE path with its optional arguments.
type halfileEntry struct {
	resolved string
	args     []string
	isTCL    bool
}

// executeTwopass implements two-phase HAL file execution when [HAL]TWOPASS is set.
//
// If any HALFILE is a .tcl file, the legacy haltcl twopass.tcl is invoked
// instead of the native Go implementation, because .tcl files may depend on
// the ::tp:: namespace API (passnumber, alter_puts, etc.).
func (e *Executor) executeTwopass(twopassValue string) error {
	opts := parseTwopassOptions(twopassValue)

	// Collect all [HAL] entries in order.
	halEntries := e.ini.GetSection("HAL")

	// Resolve all HALFILE paths and check for .tcl files.
	var halfiles []halfileEntry
	for _, entry := range halEntries {
		if entry.Key != "HALFILE" {
			continue
		}
		fields := strings.Fields(entry.Value)
		if len(fields) == 0 {
			continue
		}
		resolved, err := e.resolvePath(fields[0])
		if err != nil {
			return fmt.Errorf("TWOPASS: resolving HAL file %q: %w", fields[0], err)
		}
		halfiles = append(halfiles, halfileEntry{
			resolved: resolved,
			args:     fields[1:],
			isTCL:    strings.HasSuffix(resolved, ".tcl"),
		})
	}

	// If any HALFILE is a .tcl, fall back to legacy haltcl twopass.tcl.
	for _, hf := range halfiles {
		if hf.isTCL {
			if opts.verbose {
				e.logger.Info("TWOPASS: .tcl HALFILE detected, delegating to legacy haltcl twopass.tcl")
			}
			return e.executeTwopassLegacy(twopassValue)
		}
	}

	// All files are .hal — use native Go two-pass processing.
	return e.executeTwopassNative(opts, halEntries, halfiles)
}

// executeTwopassLegacy delegates to the legacy haltcl twopass.tcl when .tcl
// HALFILEs are present.  It finds twopass.tcl via HALLIB_PATH and invokes it
// with the same INI file.
func (e *Executor) executeTwopassLegacy(twopassValue string) error {
	halibDir := e.halibDir()
	if halibDir == "" {
		return fmt.Errorf("TWOPASS: cannot find HALLIB_DIR for twopass.tcl delegation")
	}
	twopassTcl := filepath.Join(halibDir, "twopass.tcl")
	// Also check the parent directory (where tcl/ files may live in a RIP build).
	if _, err := os.Stat(twopassTcl); err != nil {
		twopassTcl = filepath.Join(filepath.Dir(halibDir), "tcl", "twopass.tcl")
		if _, err2 := os.Stat(twopassTcl); err2 != nil {
			return fmt.Errorf("TWOPASS: cannot find twopass.tcl (tried %s and %s)",
				filepath.Join(halibDir, "twopass.tcl"), twopassTcl)
		}
	}
	e.logger.Info("TWOPASS: delegating to legacy twopass.tcl", "path", twopassTcl)
	return e.runHaltcl(twopassTcl, nil)
}

// halSectionEntry represents a single entry from the [HAL] INI section:
// either a resolved HALFILE or an inline HALCMD command.
// In the native two-pass implementation, HALCMD entries are executed during
// pass 1 in the order they appear relative to HALFILE entries.
type halSectionEntry struct {
	isHALFILE bool
	resolved  string
	halcmd    string
	noTwopass bool // for HALFILE: skip two-pass, execute immediately
}

// executeTwopassNative implements the two-phase processing for .hal-only configs.
func (e *Executor) executeTwopassNative(
	opts twopassOptions,
	halEntries []inifile.Entry,
	halfiles []halfileEntry,
) error {
	// Build the ordered list of HAL section entries (HALFILE + HALCMD interleaved).
	// We need to map resolved paths back into the ordered entry list.
	hfIdx := 0
	var sectionEntries []halSectionEntry
	for _, entry := range halEntries {
		switch entry.Key {
		case "HALFILE":
			if hfIdx >= len(halfiles) {
				continue
			}
			hf := halfiles[hfIdx]
			hfIdx++
			notp, err := hasNoTwopass(hf.resolved)
			if err != nil {
				return fmt.Errorf("TWOPASS: checking #NOTWOPASS in %q: %w", hf.resolved, err)
			}
			sectionEntries = append(sectionEntries, halSectionEntry{
				isHALFILE: true,
				resolved:  hf.resolved,
				noTwopass: notp,
			})
		case "HALCMD":
			sectionEntries = append(sectionEntries, halSectionEntry{
				isHALFILE: false,
				halcmd:    strings.TrimSpace(entry.Value),
			})
		}
	}

	// --- Pass 0: scan & collect loadrt, execute loadusr, execute #NOTWOPASS files ---
	if opts.verbose {
		e.logger.Info("TWOPASS: pass0 begin")
	}

	// moduleOrder tracks the insertion order of modules (for deterministic loadrt ordering).
	var moduleOrder []string
	// moduleParams accumulates merged loadrt parameters per module.
	moduleParams := make(map[string]*loadrtParams)

	for _, se := range sectionEntries {
		if !se.isHALFILE {
			// HALCMD entries are deferred to pass 1.
			continue
		}
		if se.noTwopass {
			// Execute the file immediately via "halcmd -vkf <file>".
			if opts.verbose {
				e.logger.Info("TWOPASS: pass0: executing #NOTWOPASS file", "path", se.resolved)
			}
			var args []string
			if e.ini != nil && e.ini.SourceFile() != "" {
				args = append(args, "-i", e.ini.SourceFile())
			}
			args = append(args, "-vkf", se.resolved)
			if err := e.runHalcmdArgs(args); err != nil {
				return fmt.Errorf("TWOPASS: pass0: executing #NOTWOPASS file %q: %w", se.resolved, err)
			}
			continue
		}

		// Read the .hal file and process it line by line.
		lines, err := e.readHalLines(se.resolved)
		if err != nil {
			return fmt.Errorf("TWOPASS: pass0: reading %q: %w", se.resolved, err)
		}
		for _, line := range lines {
			cmd := cmdName(line)
			switch cmd {
			case "loadrt":
				module, params, err := parseLoadrt(line)
				if err != nil {
					return fmt.Errorf("TWOPASS: pass0: %q: %w", se.resolved, err)
				}
				if opts.verbose {
					e.logger.Info("TWOPASS: pass0: collecting loadrt", "module", module)
				}
				if existing, ok := moduleParams[module]; ok {
					if err := mergeLoadrt(existing, params, module); err != nil {
						return fmt.Errorf("TWOPASS: pass0: merging loadrt for %q: %w", module, err)
					}
				} else {
					cp := params
					moduleParams[module] = &cp
					moduleOrder = append(moduleOrder, module)
				}
			case "loadusr":
				// Execute loadusr immediately in pass 0.
				trimmed := strings.TrimSpace(line)
				if opts.verbose {
					e.logger.Info("TWOPASS: pass0: executing loadusr", "cmd", trimmed)
				}
				if err := e.runHalcmd(trimmed); err != nil {
					return fmt.Errorf("TWOPASS: pass0: loadusr in %q: %w", se.resolved, err)
				}
			default:
				// All other commands (net, addf, setp, etc.) are skipped in pass 0.
			}
		}
	}

	// Execute all merged loadrt commands at the end of pass 0.
	for _, module := range moduleOrder {
		p := moduleParams[module]
		args := buildLoadrtArgs(module, *p)
		if opts.verbose {
			e.logger.Info("TWOPASS: pass0: executing merged loadrt", "args", strings.Join(args, " "))
		}
		var halArgs []string
		if e.ini != nil && e.ini.SourceFile() != "" {
			halArgs = append(halArgs, "-i", e.ini.SourceFile())
		}
		halArgs = append(halArgs, args...)
		if err := e.runHalcmdArgs(halArgs); err != nil {
			return fmt.Errorf("TWOPASS: pass0: loadrt %s: %w", module, err)
		}
	}

	if opts.verbose {
		e.logger.Info("TWOPASS: pass0 end")
	}

	// --- Pass 1: execute all commands except loadrt ---
	if opts.verbose {
		e.logger.Info("TWOPASS: pass1 begin")
	}

	for _, se := range sectionEntries {
		if !se.isHALFILE {
			// Execute HALCMD entries during pass 1.
			if se.halcmd == "" {
				continue
			}
			if opts.verbose {
				e.logger.Debug("TWOPASS: pass1: executing HALCMD", "cmd", se.halcmd)
			}
			if err := e.runHalcmd(se.halcmd); err != nil {
				return fmt.Errorf("TWOPASS: pass1: executing HALCMD %q: %w", se.halcmd, err)
			}
			continue
		}
		if se.noTwopass {
			// #NOTWOPASS files were already executed in pass 0, skip in pass 1.
			continue
		}

		lines, err := e.readHalLines(se.resolved)
		if err != nil {
			return fmt.Errorf("TWOPASS: pass1: reading %q: %w", se.resolved, err)
		}
		for _, line := range lines {
			cmd := cmdName(line)
			if cmd == "" {
				continue
			}
			if cmd == "loadrt" || cmd == "loadusr" {
				// loadrt already done; loadusr already done in pass 0.
				continue
			}
			trimmed := strings.TrimSpace(line)
			if opts.verbose {
				e.logger.Debug("TWOPASS: pass1: executing", "cmd", trimmed)
			}
			if err := e.runHalcmd(trimmed); err != nil {
				return fmt.Errorf("TWOPASS: pass1: %q: executing %q: %w", se.resolved, trimmed, err)
			}
		}
	}

	if opts.verbose {
		e.logger.Info("TWOPASS: pass1 end")
	}
	return nil
}
