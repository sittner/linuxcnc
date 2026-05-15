// modcompile compiles .comp files into cmod .so plugins for gomc-server,
// and manages the package registry for compiled-in Go modules.
//
// Usage:
//
//	modcompile [options] file.comp...
//
// Options:
//
//	--help           Show this help message.
//	--parse          Parse only — print the parsed AST and exit.
//	--preprocess     Preprocess only — emit generated C to stdout.
//	--document       Generate man page documentation.
//	--view-doc       Generate and display man page.
//	--compile        Compile to .so in the current directory.
//	--install        Compile and install to EMC2_CMOD_DIR.
//	-o FILE          Write output to FILE (for --preprocess, --document).
//
// Package registry commands:
//
//	list             List registered packages.
//	rebuild          Regenerate imports_generated.go and rebuild gomc-server.
//	add-gomod        Copy a Go package into gomc and rebuild gomc-server.
//	rm-gomod         Remove a Go package and rebuild gomc-server.
//
// Environment query options (for external Makefiles):
//
//	--cflags         Print compiler flags for cmod components.
//	--ldflags        Print linker flags for cmod components.
//	--cmod-dir       Print cmod installation directory.
//	--include-dir    Print cmod headers directory.
//	--gomc-dir       Print gomc Go module source directory.
//	--go             Print Go binary path used to build LinuxCNC.
//	--print-make-inc Print Makefile include snippet for external projects.
package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/sittner/linuxcnc/src/gomc/internal/config"
	gmiast "github.com/sittner/linuxcnc/src/gomc/internal/gmicompile/ast"
	gmicgen "github.com/sittner/linuxcnc/src/gomc/internal/gmicompile/cgen"
	gmiparser "github.com/sittner/linuxcnc/src/gomc/internal/gmicompile/parser"
	"github.com/sittner/linuxcnc/src/gomc/internal/modcompile/ast"
	"github.com/sittner/linuxcnc/src/gomc/internal/modcompile/cgen"
	"github.com/sittner/linuxcnc/src/gomc/internal/modcompile/comp"
	"github.com/sittner/linuxcnc/src/gomc/internal/modcompile/docgen"
	"github.com/sittner/linuxcnc/src/gomc/internal/pkgreg"
)

const usageText = `modcompile: Compile .comp files, generate GMI code, and manage gomc-server packages

Usage:
    modcompile [options] file.comp...
    modcompile gmi [options] file.gmi...
    modcompile list | rebuild | add-gomod | rm-gomod
    modcompile --cflags | --ldflags | --cmod-dir | --include-dir
    modcompile --print-make-inc

Compile options (.comp):
    --help           Show this help message
    --parse          Parse only — print the parsed AST as JSON
    --preprocess     Preprocess only — emit generated C code
    --document       Generate man page documentation
    --view-doc       Generate and display man page in terminal
    --compile        Compile .comp to .so in the current directory
    --install        Compile .comp and install to cmod directory
    -o FILE          Write output to FILE (for --preprocess, --document)

GMI code generation (.gmi):
    modcompile gmi --parse file.gmi
    modcompile gmi --server-c file.gmi -o api.h
    modcompile gmi --client-c file.gmi -o client
    modcompile gmi --server-go file.gmi -o api.go
    modcompile gmi --client-go file.gmi -o client.go
    modcompile gmi --client-python file.gmi -o client.py

Package registry commands:
    list             List packages compiled into gomc-server
    rebuild          Regenerate imports + rebuild gomc-server from packages.conf
    add-gomod <dir>  Copy a Go package into gomc, register, and rebuild
    rm-gomod <name>  Unregister, delete source, and rebuild gomc-server

Environment query options (for external Makefiles):
    --cflags         Print compiler flags for cmod components
    --ldflags        Print linker flags for cmod components
    --cmod-dir       Print cmod installation directory
    --include-dir    Print cmod headers directory
    --gomc-dir       Print gomc Go module source directory
    --go             Print Go binary path used to build LinuxCNC
    --print-make-inc Print Makefile include snippet for external projects

Examples:
    # Compile a .comp file
    modcompile --compile mycomp.comp
    modcompile --install mycomp.comp

    # Generate GMI code from .gmi IDL
    modcompile gmi --server-c kins.gmi -o kins_api.h
    modcompile gmi --client-python manualtoolchange.gmi -o mtc_client.py

    # Generate documentation
    modcompile --document -o mycomp.9 mycomp.comp
    modcompile --view-doc mycomp.comp

    # Manage compiled-in Go modules
    modcompile list
    modcompile add-gomod /path/to/galv-formula
    modcompile rm-gomod galv-formula
    modcompile rebuild

    # Use in external Makefile:
    $(eval $(shell modcompile --print-make-inc))
    mycomp.so: mycomp.c
        $(GOMC_CC) $(GOMC_CFLAGS) -o $@ $< $(GOMC_LDFLAGS)
`

// Compiler/linker settings
const (
	defaultCC      = "gcc"
	defaultCFlags  = "-fPIC -Os -Wall"
	defaultLDFlags = "-shared -lm"
)

func main() {
	if len(os.Args) < 2 {
		fmt.Fprint(os.Stderr, usageText)
		os.Exit(1)
	}

	// Handle environment query options first (no files needed)
	switch os.Args[1] {
	case "--cflags":
		fmt.Printf("-I%s %s\n", config.EMC2CmodIncludeDir, defaultCFlags)
		return
	case "--ldflags":
		fmt.Println(defaultLDFlags)
		return
	case "--cmod-dir":
		fmt.Println(config.EMC2CmodDir)
		return
	case "--include-dir":
		fmt.Println(config.EMC2CmodIncludeDir)
		return
	case "--gomc-dir", "--launcher-dir":
		fmt.Println(config.EMC2GomcDir)
		return
	case "--go":
		fmt.Println(config.GoBinary)
		return
	case "--print-make-inc":
		printMakeInc()
		return

	// Package registry subcommands
	case "list":
		cmdList()
		return
	case "rebuild":
		cmdRebuild()
		return
	case "regenerate-imports":
		cmdRegenerateImports()
		return
	case "add-gomod":
		force := false
		var dir string
		for _, a := range os.Args[2:] {
			if a == "--force" || a == "-f" {
				force = true
			} else if !strings.HasPrefix(a, "-") {
				dir = a
			}
		}
		if dir == "" {
			fmt.Fprintln(os.Stderr, "modcompile add-gomod: missing directory argument")
			os.Exit(1)
		}
		cmdAddGomod(dir, force)
		return
	case "rm-gomod":
		if len(os.Args) < 3 {
			fmt.Fprintln(os.Stderr, "modcompile rm-gomod: missing package name argument")
			os.Exit(1)
		}
		cmdRmGomod(os.Args[2])
		return

	case "add-gmi":
		if len(os.Args) < 3 {
			fmt.Fprintln(os.Stderr, "modcompile add-gmi: missing import path argument")
			os.Exit(1)
		}
		cmdAddGmi(os.Args[2])
		return
	case "rm-gmi":
		if len(os.Args) < 3 {
			fmt.Fprintln(os.Stderr, "modcompile rm-gmi: missing import path argument")
			os.Exit(1)
		}
		cmdRmGmi(os.Args[2])
		return

	// GMI code generation subcommand
	case "gmi":
		cmdGMI(os.Args[2:])
		return
	}

	// Parse arguments for file-processing modes
	var mode string
	var outputFile string
	var files []string

	args := os.Args[1:]
	for i := 0; i < len(args); i++ {
		arg := args[i]
		switch {
		case arg == "--help" || arg == "-h":
			fmt.Print(usageText)
			os.Exit(0)
		case arg == "-o" && i+1 < len(args):
			outputFile = args[i+1]
			i++
		case strings.HasPrefix(arg, "-o"):
			outputFile = arg[2:]
		case strings.HasPrefix(arg, "-"):
			mode = arg
		default:
			files = append(files, arg)
		}
	}

	if mode == "" {
		mode = "--parse"
	}

	if len(files) == 0 {
		fmt.Fprintf(os.Stderr, "modcompile: no input files\n")
		os.Exit(1)
	}

	for _, path := range files {
		if err := processFile(path, mode, outputFile); err != nil {
			fmt.Fprintf(os.Stderr, "modcompile: %v\n", err)
			os.Exit(1)
		}
	}
}

func processFile(path, mode, outputFile string) error {
	src, err := os.ReadFile(path)
	if err != nil {
		return err
	}

	pkg, err := comp.Parse(path, string(src))
	if err != nil {
		return err
	}

	switch mode {
	case "--parse":
		enc := json.NewEncoder(os.Stdout)
		enc.SetIndent("", "  ")
		return enc.Encode(pkg)

	case "--preprocess":
		out := os.Stdout
		if outputFile != "" {
			f, err := os.Create(outputFile)
			if err != nil {
				return err
			}
			defer f.Close()
			out = f
		}
		return cgen.Generate(out, pkg)

	case "--document":
		out := os.Stdout
		if outputFile != "" {
			f, err := os.Create(outputFile)
			if err != nil {
				return err
			}
			defer f.Close()
			out = f
		} else {
			// Default output filename
			base := strings.TrimSuffix(filepath.Base(path), ".comp")
			section := "9"
			if pkg.Component.Options["userspace"] == "yes" {
				section = "1"
			}
			outName := base + "." + section
			f, err := os.Create(outName)
			if err != nil {
				return err
			}
			defer f.Close()
			out = f
		}
		return docgen.Generate(out, pkg)

	case "--view-doc":
		// Generate to temp file and display with man
		tmpFile, err := os.CreateTemp("", "modcompile-*.man")
		if err != nil {
			return err
		}
		tmpName := tmpFile.Name()
		defer os.Remove(tmpName)

		if err := docgen.Generate(tmpFile, pkg); err != nil {
			tmpFile.Close()
			return err
		}
		tmpFile.Close()

		// Run man to display
		cmd := exec.Command("man", tmpName)
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		cmd.Stdin = os.Stdin
		return cmd.Run()

	case "--compile":
		return compileComp(path, pkg, ".")

	case "--install":
		return compileComp(path, pkg, config.EMC2CmodDir)

	default:
		return fmt.Errorf("unknown mode %q", mode)
	}
}

// compileComp compiles a .comp file to a .so in the given output directory.
func compileComp(compPath string, pkg *ast.Package, outDir string) error {
	base := strings.TrimSuffix(filepath.Base(compPath), ".comp")
	soPath := filepath.Join(outDir, base+".so")

	// Create temp file for generated C
	tmpFile, err := os.CreateTemp("", "modcompile-*.c")
	if err != nil {
		return fmt.Errorf("creating temp file: %w", err)
	}
	tmpCPath := tmpFile.Name()
	defer os.Remove(tmpCPath)

	// Generate C code
	if err := cgen.Generate(tmpFile, pkg); err != nil {
		tmpFile.Close()
		return fmt.Errorf("generating C: %w", err)
	}
	tmpFile.Close()

	// Ensure output directory exists
	if err := os.MkdirAll(outDir, 0755); err != nil {
		return fmt.Errorf("creating output directory: %w", err)
	}

	// Compile with gcc
	cc := os.Getenv("CC")
	if cc == "" {
		cc = defaultCC
	}

	args := []string{
		"-I" + config.EMC2CmodIncludeDir,
		"-I" + filepath.Join(config.EMC2Home, "include"),
	}

	// Add -I for each GMI API referenced (gmi_provide / gmi_consume).
	gmiAPIs := make(map[string]bool)
	for _, api := range pkg.Component.GMIProvide {
		gmiAPIs[api] = true
	}
	for _, api := range pkg.Component.GMIConsume {
		gmiAPIs[api] = true
	}
	for api := range gmiAPIs {
		apiIncDir := filepath.Join(config.EMC2GomcDir, "generated", "gmi", api)
		args = append(args, "-I"+apiIncDir)
	}

	args = append(args,
		"-fPIC", "-Os", "-Wall",
		"-shared",
		"-o", soPath,
		tmpCPath,
		"-lm",
	)

	cmd := exec.Command(cc, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("compiling %s: %w", base, err)
	}

	return nil
}

// printMakeInc outputs a Makefile snippet for external projects.
// Each variable is wrapped in $(eval) so $(shell) newline→space conversion works.
func printMakeInc() {
	cc := os.Getenv("CC")
	if cc == "" {
		cc = defaultCC
	}

	libDir := filepath.Join(config.EMC2Home, "lib")

	// Each line wrapped in $(eval ...) because $(shell) converts newlines to spaces.
	// The outer $(eval $(shell ...)) then evaluates each inner $(eval) properly.
	fmt.Printf(`$(eval GOMC_CC := %s) $(eval GOMC_CFLAGS := -I%s %s) $(eval GOMC_LDFLAGS := %s) $(eval GOMC_CMOD_DIR := %s) $(eval GOMC_INCLUDE_DIR := %s) $(eval GOMC_DIR := %s) $(eval GOMC_GO := %s) $(eval GOMC_LIB_DIR := %s)`,
		cc,
		config.EMC2CmodIncludeDir, defaultCFlags,
		defaultLDFlags,
		config.EMC2CmodDir,
		config.EMC2CmodIncludeDir,
		config.EMC2GomcDir,
		config.GoBinary,
		libDir,
	)
}

// ensureRuntimeFiles copies .in base files to their working copies if they
// don't exist yet. This happens on fresh checkouts or after "git clean".
func ensureRuntimeFiles() {
	gomcDir := config.EMC2GomcDir
	if gomcDir == "" {
		return
	}
	for _, name := range []string{"packages.conf", "go.mod"} {
		dst := filepath.Join(gomcDir, name)
		if _, err := os.Stat(dst); err == nil {
			continue // already exists
		}
		src := filepath.Join(gomcDir, name+".in")
		data, err := os.ReadFile(src)
		if err != nil {
			fmt.Fprintf(os.Stderr, "modcompile: %s.in not found: %v\n", name, err)
			os.Exit(1)
		}
		if err := os.WriteFile(dst, data, 0644); err != nil {
			fmt.Fprintf(os.Stderr, "modcompile: creating %s: %v\n", name, err)
			os.Exit(1)
		}
		fmt.Fprintf(os.Stderr, "Initialized %s from %s.in\n", name, name)
	}
}

// packagesConfPath returns the path to packages.conf in the gomc dir.
func packagesConfPath() string {
	return filepath.Join(config.EMC2GomcDir, "packages.conf")
}

// loadRegistry reads packages.conf from the gomc directory.
func loadRegistry() *pkgreg.Registry {
	ensureRuntimeFiles()
	reg, err := pkgreg.ReadFile(packagesConfPath())
	if err != nil {
		fmt.Fprintf(os.Stderr, "modcompile: reading packages.conf: %v\n", err)
		os.Exit(1)
	}
	return reg
}

// regenerate writes imports_generated.go from the registry.
func regenerate(reg *pkgreg.Registry) {
	serverDir := config.EMC2GomcDir

	if err := reg.GenerateImports(serverDir); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile: generating imports: %v\n", err)
		os.Exit(1)
	}
}

// buildServer builds the gomc-server binary.
func buildServer() {
	serverDir := config.EMC2GomcDir
	binDir := config.EMC2BinDir
	gobin := config.GoBinary
	if gobin == "" {
		gobin = "go"
	}

	outPath := filepath.Join(binDir, "gomc-server")

	// Build ldflags to inject compile-time config into the new binary.
	// modcompile already has these values baked in, so we propagate them.
	pkg := "github.com/sittner/linuxcnc/src/gomc/internal/config"
	ldflags := fmt.Sprintf(
		"-X '%s.EMC2Home=%s' "+
			"-X '%s.EMC2BinDir=%s' "+
			"-X '%s.EMC2TclDir=%s' "+
			"-X '%s.EMC2HelpDir=%s' "+
			"-X '%s.EMC2RtlibDir=%s' "+
			"-X '%s.EMC2CmodDir=%s' "+
			"-X '%s.EMC2CmodIncludeDir=%s' "+
			"-X '%s.EMC2GomcDir=%s' "+
			"-X '%s.GoBinary=%s' "+
			"-X '%s.EMC2ConfigPath=%s' "+
			"-X '%s.EMC2NCFilesDir=%s' "+
			"-X '%s.EMC2LangDir=%s' "+
			"-X '%s.EMC2ImageDir=%s' "+
			"-X '%s.EMC2TclLibDir=%s' "+
			"-X '%s.HalibDir=%s' "+
			"-X '%s.EMC2WebAppDir=%s' "+
			"-X '%s.EMC2Version=%s' "+
			"-X '%s.RunInPlace=%s' "+
			"-X '%s.DefaultNmlFile=%s' "+
			"-X '%s.ModExt=%s' "+
			"-X '%s.KernelVers=%s'",
		pkg, config.EMC2Home,
		pkg, config.EMC2BinDir,
		pkg, config.EMC2TclDir,
		pkg, config.EMC2HelpDir,
		pkg, config.EMC2RtlibDir,
		pkg, config.EMC2CmodDir,
		pkg, config.EMC2CmodIncludeDir,
		pkg, config.EMC2GomcDir,
		pkg, config.GoBinary,
		pkg, config.EMC2ConfigPath,
		pkg, config.EMC2NCFilesDir,
		pkg, config.EMC2LangDir,
		pkg, config.EMC2ImageDir,
		pkg, config.EMC2TclLibDir,
		pkg, config.HalibDir,
		pkg, config.EMC2WebAppDir,
		pkg, config.EMC2Version,
		pkg, config.RunInPlace,
		pkg, config.DefaultNmlFile,
		pkg, config.ModExt,
		pkg, config.KernelVers,
	)

	cmd := exec.Command(gobin, "build", "-ldflags", ldflags, "-o", outPath, "./cmd/gomc-server")
	cmd.Dir = serverDir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	// CGO needs to find headers and libraries.
	// RIP: headers in src/, libs in lib/ under EMC2Home.
	// Installed: headers in includedir, libs in libdir.
	var cgoC, cgoLD string
	if config.RunInPlace == "yes" {
		srcDir := filepath.Join(config.EMC2Home, "src")
		cgoC = fmt.Sprintf("-I%s -I%s/hal -I%s/rtapi -I%s/../include",
			srcDir, srcDir, srcDir, srcDir)
		libDir := filepath.Join(config.EMC2Home, "lib")
		cgoLD = fmt.Sprintf("-L%s -Wl,-rpath,%s", libDir, libDir)
	} else {
		// Installed: use standard paths relative to EMC2Home.
		incDir := filepath.Join(config.EMC2Home, "include", "linuxcnc")
		libDir := filepath.Join(config.EMC2Home, "lib")
		cgoC = "-I" + incDir
		cgoLD = fmt.Sprintf("-L%s -Wl,-rpath,%s", libDir, libDir)
	}
	cmd.Env = append(os.Environ(),
		"CGO_CFLAGS="+cgoC,
		"CGO_LDFLAGS="+cgoLD,
	)

	fmt.Fprintf(os.Stderr, "Building gomc-server...\n")
	if err := cmd.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile: building gomc-server: %v\n", err)
		os.Exit(1)
	}
	fmt.Fprintf(os.Stderr, "gomc-server built successfully: %s\n", outPath)
}

// cmdList lists all packages in the registry.
func cmdList() {
	reg := loadRegistry()
	for _, e := range reg.Entries {
		fmt.Printf("%-8s %s\n", e.Type, e.ImportPath)
	}
}

// cmdRebuild regenerates derived files and rebuilds the server.
func cmdRebuild() {
	reg := loadRegistry()
	regenerate(reg)
	buildServer()
}

// cmdRegenerateImports only regenerates imports_generated.go from packages.conf,
// without building gomc-server.  Used by Makefile to avoid race conditions with
// parallel builds (-j).
func cmdRegenerateImports() {
	reg := loadRegistry()
	regenerate(reg)
}

// cmdAddGomod copies an external Go package into external/<name>/ and rebuilds.
func cmdAddGomod(dir string, force bool) {
	absDir, err := filepath.Abs(dir)
	if err != nil {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: resolving path: %v\n", err)
		os.Exit(1)
	}

	// Validate the directory exists and has a go.mod.
	goModPath := filepath.Join(absDir, "go.mod")
	if _, err := os.Stat(goModPath); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: %s does not exist or has no go.mod\n", absDir)
		os.Exit(1)
	}

	// Package name = directory basename.
	name := filepath.Base(absDir)
	importPath := "external/" + name
	extDir := filepath.Join(config.EMC2GomcDir, "external", name)
	originFile := filepath.Join(extDir, ".origin")

	// Check for collision.
	if info, err := os.Stat(extDir); err == nil && info.IsDir() {
		originData, _ := os.ReadFile(originFile)
		existingOrigin := strings.TrimSpace(string(originData))
		if existingOrigin == absDir {
			// Same source — auto-force (reinstall).
			fmt.Fprintf(os.Stderr, "Reinstalling %s from %s\n", name, absDir)
		} else if !force {
			fmt.Fprintf(os.Stderr, "modcompile add-gomod: external/%s already installed", name)
			if existingOrigin != "" {
				fmt.Fprintf(os.Stderr, " from %s", existingOrigin)
			}
			fmt.Fprintf(os.Stderr, "\nUse --force to overwrite.\n")
			os.Exit(1)
		} else {
			fmt.Fprintf(os.Stderr, "Overwriting external/%s (--force)\n", name)
		}
	}

	// rsync --delete source into external/<name>/.
	if err := os.MkdirAll(extDir, 0755); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: creating directory: %v\n", err)
		os.Exit(1)
	}

	// Mirror source directory into external/<name>/, excluding build artifacts
	// and module boundary files (the copy becomes a sub-package of the gomc module).
	excludeSet := map[string]bool{
		".git": true, "go.work": true, "go.work.sum": true,
		"go.mod": true, "go.sum": true,
	}
	if err := dirMirror(absDir, extDir, excludeSet); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: copying files: %v\n", err)
		os.Exit(1)
	}

	// Merge third-party dependencies from the external module's go.mod
	// into the gomc go.mod (the copy has no go.mod).
	mergeGoDeps(goModPath)

	// Write .origin to track where the source came from.
	if err := os.WriteFile(originFile, []byte(absDir+"\n"), 0644); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: writing .origin: %v\n", err)
		os.Exit(1)
	}

	// Update package registry.
	reg := loadRegistry()
	reg.Remove(importPath) // remove old entry if reinstalling
	reg.Add(pkgreg.Entry{Type: pkgreg.TypeGomod, ImportPath: importPath})

	if err := reg.WriteFile(packagesConfPath()); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: writing packages.conf: %v\n", err)
		os.Exit(1)
	}

	fmt.Fprintf(os.Stderr, "Installed %s → external/%s\n", absDir, name)
	regenerate(reg)
	buildServer()
}

// mergeGoDeps reads require directives from an external module's go.mod and
// runs "go get" in the gomc module directory for each third-party dependency.
func mergeGoDeps(extGoModPath string) {
	gobin := config.GoBinary
	if gobin == "" {
		gobin = "go"
	}
	gomcDir := config.EMC2GomcDir

	// Parse external go.mod.
	editCmd := exec.Command(gobin, "mod", "edit", "-json", extGoModPath)
	editCmd.Env = append(os.Environ(), "GOTOOLCHAIN=local")
	out, err := editCmd.Output()
	if err != nil {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: reading external go.mod: %v\n", err)
		os.Exit(1)
	}

	var modInfo struct {
		Require []struct {
			Path    string
			Version string
		}
		Replace []struct {
			Old struct{ Path string }
			New struct{ Path string }
		}
	}
	if err := json.Unmarshal(out, &modInfo); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: parsing external go.mod: %v\n", err)
		os.Exit(1)
	}

	// Collect local replace targets to skip (stub dirs, etc.)
	localReplaces := make(map[string]bool)
	for _, r := range modInfo.Replace {
		if strings.HasPrefix(r.New.Path, ".") || strings.HasPrefix(r.New.Path, "/") {
			localReplaces[r.Old.Path] = true
		}
	}

	// Build list of deps to add.
	var getArgs []string
	for _, req := range modInfo.Require {
		if localReplaces[req.Path] {
			continue
		}
		if strings.HasPrefix(req.Path, "github.com/sittner/linuxcnc/") {
			continue
		}
		getArgs = append(getArgs, req.Path+"@"+req.Version)
	}

	if len(getArgs) == 0 {
		return
	}

	fmt.Fprintf(os.Stderr, "Adding %d dependencies to gomc go.mod...\n", len(getArgs))

	args := append([]string{"get"}, getArgs...)
	getCmd := exec.Command(gobin, args...)
	getCmd.Dir = gomcDir
	getCmd.Env = append(os.Environ(), "GOTOOLCHAIN=local")
	getCmd.Stdout = os.Stdout
	getCmd.Stderr = os.Stderr
	if err := getCmd.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: go get failed: %v\n", err)
		os.Exit(1)
	}

	// Tidy to clean up unused deps from previous installs.
	tidyCmd := exec.Command(gobin, "mod", "tidy")
	tidyCmd.Dir = gomcDir
	tidyCmd.Env = append(os.Environ(), "GOTOOLCHAIN=local")
	tidyCmd.Stdout = os.Stdout
	tidyCmd.Stderr = os.Stderr
	if err := tidyCmd.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: go mod tidy warning: %v\n", err)
	}
}

// cmdRmGomod removes a Go package from the registry, deletes its source, and rebuilds.
func cmdRmGomod(name string) {
	reg := loadRegistry()

	// Try exact match first, then basename match.
	var found *pkgreg.Entry
	for i := range reg.Entries {
		e := &reg.Entries[i]
		if e.ImportPath == name || filepath.Base(e.ImportPath) == name {
			found = e
			break
		}
	}

	if found == nil {
		fmt.Fprintf(os.Stderr, "modcompile rm-gomod: %s not found in registry\n", name)
		os.Exit(1)
	}

	importPath := found.ImportPath
	reg.Remove(importPath)
	fmt.Fprintf(os.Stderr, "Removed %s from registry\n", importPath)

	// If the package lives under external/, delete its directory.
	if strings.HasPrefix(importPath, "external/") {
		extDir := filepath.Join(config.EMC2GomcDir, importPath)
		if err := os.RemoveAll(extDir); err != nil {
			fmt.Fprintf(os.Stderr, "modcompile rm-gomod: removing %s: %v\n", extDir, err)
			os.Exit(1)
		}
		fmt.Fprintf(os.Stderr, "Deleted %s\n", extDir)
	}

	if err := reg.WriteFile(packagesConfPath()); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile rm-gomod: writing packages.conf: %v\n", err)
		os.Exit(1)
	}

	regenerate(reg)
	goModTidy()
	buildServer()
}

// cmdAddGmi adds a GMI package to the registry idempotently.
// importPath is relative to the gomc module, e.g. "generated/gmi/axisui".
func cmdAddGmi(importPath string) {
	reg := loadRegistry()
	if reg.Add(pkgreg.Entry{Type: pkgreg.TypeGMI, ImportPath: importPath}) {
		if err := reg.WriteFile(packagesConfPath()); err != nil {
			fmt.Fprintf(os.Stderr, "modcompile add-gmi: writing packages.conf: %v\n", err)
			os.Exit(1)
		}
		regenerate(reg)
	}
}

// cmdRmGmi removes a GMI package from the registry.
func cmdRmGmi(importPath string) {
	reg := loadRegistry()
	if !reg.Remove(importPath) {
		fmt.Fprintf(os.Stderr, "modcompile rm-gmi: %s not found in registry\n", importPath)
		os.Exit(1)
	}

	if err := reg.WriteFile(packagesConfPath()); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile rm-gmi: writing packages.conf: %v\n", err)
		os.Exit(1)
	}

	regenerate(reg)
}

// goModTidy runs "go mod tidy" in the gomc module directory to clean up
// unused dependencies (e.g. after removing an external module).
func goModTidy() {
	gobin := config.GoBinary
	if gobin == "" {
		gobin = "go"
	}
	cmd := exec.Command(gobin, "mod", "tidy")
	cmd.Dir = config.EMC2GomcDir
	cmd.Env = append(os.Environ(), "GOTOOLCHAIN=local")
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile: go mod tidy warning: %v\n", err)
	}
}

// ---------------------------------------------------------------------------
// Directory mirror (pure Go, replaces rsync -a --delete)
// ---------------------------------------------------------------------------

// dirMirror copies srcDir into dstDir, mirroring the contents exactly.
// Files in dstDir that don't exist in srcDir are deleted (except .origin).
// Top-level entries whose name is in exclude are skipped during copy.
func dirMirror(srcDir, dstDir string, exclude map[string]bool) error {
	// Phase 1: copy / update files from src → dst.
	srcSet := make(map[string]bool) // relative paths present in source
	err := filepath.Walk(srcDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		rel, _ := filepath.Rel(srcDir, path)
		if rel == "." {
			return nil
		}

		// Skip excluded top-level entries.
		topLevel := strings.SplitN(rel, string(filepath.Separator), 2)[0]
		if exclude[topLevel] {
			if info.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}

		srcSet[rel] = true
		dst := filepath.Join(dstDir, rel)

		if info.IsDir() {
			return os.MkdirAll(dst, info.Mode()|0700)
		}

		// Copy file if it doesn't exist or differs in size/modtime.
		dstInfo, dstErr := os.Lstat(dst)
		if dstErr == nil && dstInfo.Size() == info.Size() && !dstInfo.ModTime().Before(info.ModTime()) {
			return nil // up to date
		}

		return copyFile(path, dst, info.Mode())
	})
	if err != nil {
		return fmt.Errorf("copying: %w", err)
	}

	// Phase 2: delete files in dst that are not in src (except .origin).
	return filepath.Walk(dstDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		rel, _ := filepath.Rel(dstDir, path)
		if rel == "." || rel == ".origin" {
			return nil
		}
		if !srcSet[rel] {
			if info.IsDir() {
				os.RemoveAll(path)
				return filepath.SkipDir
			}
			os.Remove(path)
		}
		return nil
	})
}

func copyFile(src, dst string, mode os.FileMode) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.OpenFile(dst, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, mode)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)
	return err
}

// ---------------------------------------------------------------------------
// GMI code generation (modcompile gmi)
// ---------------------------------------------------------------------------

const gmiUsageText = `modcompile gmi: Compile GMI interface definitions

Usage:
    modcompile gmi [options] file.gmi...

Options:
    --help           Show this help message
    --parse          Parse only — print AST as JSON
    --server-c       Generate C server header (types, callback typedefs)
    --client-c       Generate C REST client (header + source)
    --server-go      Generate Go server handlers
    --client-go      Generate Go REST client
    --client-python  Generate Python REST client
    --client-ts      Generate TypeScript REST client
    --client-ts-ws   Generate TypeScript WebSocket watch client
    -o PATH          Output file or directory
`

type gmiMode int

const (
	gmiModeParse gmiMode = iota
	gmiModeServerC
	gmiModeClientC
	gmiModeServerGo
	gmiModeClientGo
	gmiModeClientPython
	gmiModeServerWS
	gmiModeClientPythonWS
	gmiModeClientTS
	gmiModeClientTSWS
)

func cmdGMI(args []string) {
	if len(args) == 0 {
		fmt.Fprint(os.Stderr, gmiUsageText)
		os.Exit(1)
	}

	var m gmiMode
	var outputPath string
	var files []string

	for i := 0; i < len(args); i++ {
		arg := args[i]
		switch arg {
		case "--help", "-h":
			fmt.Print(gmiUsageText)
			os.Exit(0)
		case "--parse":
			m = gmiModeParse
		case "--server-c":
			m = gmiModeServerC
		case "--client-c":
			m = gmiModeClientC
		case "--server-go":
			m = gmiModeServerGo
		case "--client-go":
			m = gmiModeClientGo
		case "--client-python":
			m = gmiModeClientPython
		case "--server-ws":
			m = gmiModeServerWS
		case "--client-python-ws":
			m = gmiModeClientPythonWS
		case "--client-ts":
			m = gmiModeClientTS
		case "--client-ts-ws":
			m = gmiModeClientTSWS
		case "-o":
			if i+1 < len(args) {
				i++
				outputPath = args[i]
			}
		default:
			if len(arg) > 0 && arg[0] != '-' {
				files = append(files, arg)
			}
		}
	}

	if len(files) == 0 {
		fmt.Fprintln(os.Stderr, "modcompile gmi: no input files")
		os.Exit(1)
	}

	for _, file := range files {
		if err := processGMIFile(file, m, outputPath); err != nil {
			fmt.Fprintf(os.Stderr, "modcompile gmi: %v\n", err)
			os.Exit(1)
		}
	}
}

func processGMIFile(file string, m gmiMode, outputPath string) error {
	src, err := os.ReadFile(file)
	if err != nil {
		return err
	}

	api, errors := gmiparser.Parse(file, string(src))
	if len(errors) > 0 {
		for _, e := range errors {
			fmt.Fprintln(os.Stderr, e)
		}
		return fmt.Errorf("parse failed")
	}

	switch m {
	case gmiModeParse:
		enc := json.NewEncoder(os.Stdout)
		enc.SetIndent("", "  ")
		return enc.Encode(api)
	case gmiModeServerC:
		return gmiGenerateServerC(api, outputPath)
	case gmiModeClientC:
		if !api.RestExport {
			return fmt.Errorf("%s: --client-c requires @rest_export true", file)
		}
		return gmiGenerateClientC(api, outputPath)
	case gmiModeServerGo:
		return gmiGenerateServerGo(api, outputPath)
	case gmiModeClientGo:
		if !api.RestExport {
			return fmt.Errorf("%s: --client-go requires @rest_export true", file)
		}
		return gmiGenerateClientGo(api, outputPath)
	case gmiModeClientPython:
		if !api.RestExport {
			return fmt.Errorf("%s: --client-python requires @rest_export true", file)
		}
		return gmiGenerateClientPython(api, outputPath)
	case gmiModeServerWS:
		if !gmicgen.HasWatchFuncs(api) {
			return fmt.Errorf("%s: --server-ws requires at least one @watch function", file)
		}
		return gmiGenerateServerWS(api, outputPath)
	case gmiModeClientPythonWS:
		if !gmicgen.HasWatchFuncs(api) {
			return fmt.Errorf("%s: --client-python-ws requires at least one @watch function", file)
		}
		return gmiGenerateClientPythonWS(api, outputPath)
	case gmiModeClientTS:
		if !api.RestExport {
			return fmt.Errorf("%s: --client-ts requires @rest_export true", file)
		}
		return gmiGenerateClientTS(api, outputPath)
	case gmiModeClientTSWS:
		if !gmicgen.HasWatchFuncs(api) {
			return fmt.Errorf("%s: --client-ts-ws requires at least one @watch function", file)
		}
		return gmiGenerateClientTSWS(api, outputPath)
	}
	return nil
}

func gmiGenerateServerC(api *gmiast.API, outputPath string) error {
	if outputPath == "" {
		outputPath = api.Name + "_api.h"
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := gmicgen.GenerateServerHeader(f, api); err != nil {
		return err
	}
	fmt.Fprintf(os.Stderr, "generated %s\n", outputPath)

	// Generate Go cgo dispatch file alongside the header.
	dir := filepath.Dir(outputPath)
	goPath := filepath.Join(dir, api.Name+"_cgo.go")

	pkgName := api.Name
	if dir != "." && dir != "" {
		pkgName = filepath.Base(dir)
	}

	headerFile := filepath.Base(outputPath)

	gf, err := os.Create(goPath)
	if err != nil {
		return err
	}
	defer gf.Close()

	if err := gmicgen.GenerateDispatchC(gf, api, pkgName, headerFile); err != nil {
		return err
	}
	fmt.Fprintf(os.Stderr, "generated %s\n", goPath)

	// Generate publish ring header + Go drain if the API has @publish functions.
	pubPath := filepath.Join(dir, api.Name+"_pub.h")
	pf, err := os.Create(pubPath)
	if err != nil {
		return err
	}
	defer pf.Close()

	hasPub, err := gmicgen.GeneratePublishHeader(pf, api)
	if err != nil {
		return err
	}
	if hasPub {
		fmt.Fprintf(os.Stderr, "generated %s\n", pubPath)

		pubGoPath := filepath.Join(dir, api.Name+"_pub.go")
		pgf, err := os.Create(pubGoPath)
		if err != nil {
			return err
		}
		defer pgf.Close()

		if _, err := gmicgen.GeneratePublishGo(pgf, api, pkgName); err != nil {
			return err
		}
		fmt.Fprintf(os.Stderr, "generated %s\n", pubGoPath)
	} else {
		// No publish functions — remove empty file.
		pf.Close()
		os.Remove(pubPath)
	}

	// Generate push converter if the API has @watch functions returning structs.
	pushPath := filepath.Join(dir, api.Name+"_push.go")
	pushF, err := os.Create(pushPath)
	if err != nil {
		return err
	}
	defer pushF.Close()

	hasPush, err := gmicgen.GeneratePushConvert(pushF, api, pkgName)
	if err != nil {
		return err
	}
	if hasPush {
		fmt.Fprintf(os.Stderr, "generated %s\n", pushPath)
	} else {
		pushF.Close()
		os.Remove(pushPath)
	}

	return nil
}

func gmiGenerateClientC(api *gmiast.API, outputPath string) error {
	var baseName string
	if outputPath == "" {
		baseName = api.Name + "_client"
	} else {
		baseName = strings.TrimSuffix(outputPath, filepath.Ext(outputPath))
	}

	headerPath := baseName + ".h"
	sourcePath := baseName + ".c"

	hf, err := os.Create(headerPath)
	if err != nil {
		return err
	}
	defer hf.Close()
	if err := gmicgen.GenerateClientHeader(hf, api); err != nil {
		return err
	}
	fmt.Fprintf(os.Stderr, "generated %s\n", headerPath)

	sf, err := os.Create(sourcePath)
	if err != nil {
		return err
	}
	defer sf.Close()
	if err := gmicgen.GenerateClientSource(sf, api); err != nil {
		return err
	}
	fmt.Fprintf(os.Stderr, "generated %s\n", sourcePath)

	return nil
}

func gmiGenerateServerGo(api *gmiast.API, outputPath string) error {
	if outputPath == "" {
		outputPath = api.Name + "_api.go"
	}

	pkgName := api.Name
	if dir := filepath.Dir(outputPath); dir != "." && dir != "" {
		pkgName = filepath.Base(dir)
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := gmicgen.GenerateServerGo(f, api, pkgName); err != nil {
		return err
	}

	fmt.Fprintf(os.Stderr, "generated %s\n", outputPath)
	return nil
}

func gmiGenerateClientGo(api *gmiast.API, outputPath string) error {
	if outputPath == "" {
		outputPath = api.Name + "_client.go"
	}

	pkgName := api.Name + "client"
	if dir := filepath.Dir(outputPath); dir != "." && dir != "" {
		pkgName = filepath.Base(dir)
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := gmicgen.GenerateClientGo(f, api, pkgName); err != nil {
		return err
	}

	fmt.Fprintf(os.Stderr, "generated %s\n", outputPath)
	return nil
}

func gmiGenerateClientPython(api *gmiast.API, outputPath string) error {
	if outputPath == "" {
		outputPath = api.Name + "_client.py"
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := gmicgen.GenerateClientPython(f, api); err != nil {
		return err
	}

	fmt.Fprintf(os.Stderr, "generated %s\n", outputPath)
	return nil
}

func gmiGenerateServerWS(api *gmiast.API, outputPath string) error {
	if outputPath == "" {
		outputPath = api.Name + "_ws.go"
	}

	pkgName := api.Name
	dir := filepath.Dir(outputPath)
	if dir != "." && dir != "" {
		pkgName = filepath.Base(dir)
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := gmicgen.GenerateServerWS(f, api, pkgName); err != nil {
		return err
	}

	fmt.Fprintf(os.Stderr, "generated %s\n", outputPath)
	return nil
}

func gmiGenerateClientPythonWS(api *gmiast.API, outputPath string) error {
	if outputPath == "" {
		outputPath = api.Name + "_watch_client.py"
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := gmicgen.GenerateClientPythonWS(f, api); err != nil {
		return err
	}

	fmt.Fprintf(os.Stderr, "generated %s\n", outputPath)
	return nil
}

func gmiGenerateClientTS(api *gmiast.API, outputPath string) error {
	if outputPath == "" {
		outputPath = api.Name + "_client.ts"
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := gmicgen.GenerateClientTS(f, api); err != nil {
		return err
	}

	fmt.Fprintf(os.Stderr, "generated %s\n", outputPath)
	return nil
}

func gmiGenerateClientTSWS(api *gmiast.API, outputPath string) error {
	if outputPath == "" {
		outputPath = api.Name + "_watch_client.ts"
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := gmicgen.GenerateClientTSWS(f, api); err != nil {
		return err
	}

	fmt.Fprintf(os.Stderr, "generated %s\n", outputPath)
	return nil
}
