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
//	rebuild          Regenerate imports_generated.go + go.work, rebuild gomc-server.
//	add-gomod        Register a Go package and rebuild gomc-server.
//	rm-gomod         Unregister a Go package and rebuild gomc-server.
//
// Environment query options (for external Makefiles):
//
//	--cflags         Print compiler flags for cmod components.
//	--ldflags        Print linker flags for cmod components.
//	--cmod-dir       Print cmod installation directory.
//	--include-dir    Print cmod headers directory.
//	--launcher-dir   Print launcher Go module source directory.
//	--go             Print Go binary path used to build LinuxCNC.
//	--print-make-inc Print Makefile include snippet for external projects.
package main

import (
	"encoding/json"
	"fmt"
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
    add-gomod <dir>  Register a Go package directory and rebuild gomc-server
    rm-gomod <name>  Unregister a Go package and rebuild gomc-server

Environment query options (for external Makefiles):
    --cflags         Print compiler flags for cmod components
    --ldflags        Print linker flags for cmod components
    --cmod-dir       Print cmod installation directory
    --include-dir    Print cmod headers directory
    --launcher-dir   Print launcher Go module source directory
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
	case "--launcher-dir":
		fmt.Println(config.EMC2LauncherDir)
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
	case "add-gomod":
		if len(os.Args) < 3 {
			fmt.Fprintln(os.Stderr, "modcompile add-gomod: missing directory argument")
			os.Exit(1)
		}
		cmdAddGomod(os.Args[2])
		return
	case "rm-gomod":
		if len(os.Args) < 3 {
			fmt.Fprintln(os.Stderr, "modcompile rm-gomod: missing package name argument")
			os.Exit(1)
		}
		cmdRmGomod(os.Args[2])
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
		apiIncDir := filepath.Join(config.EMC2LauncherDir, "generated", "gmi", api)
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
	fmt.Printf(`$(eval GOMC_CC := %s) $(eval GOMC_CFLAGS := -I%s %s) $(eval GOMC_LDFLAGS := %s) $(eval GOMC_CMOD_DIR := %s) $(eval GOMC_INCLUDE_DIR := %s) $(eval GOMC_LAUNCHER_DIR := %s) $(eval GOMC_GO := %s) $(eval GOMC_LIB_DIR := %s)`,
		cc,
		config.EMC2CmodIncludeDir, defaultCFlags,
		defaultLDFlags,
		config.EMC2CmodDir,
		config.EMC2CmodIncludeDir,
		config.EMC2LauncherDir,
		config.GoBinary,
		libDir,
	)
}

// packagesConfPath returns the path to packages.conf in the launcher dir.
func packagesConfPath() string {
	return filepath.Join(config.EMC2LauncherDir, "packages.conf")
}

// loadRegistry reads packages.conf from the launcher directory.
func loadRegistry() *pkgreg.Registry {
	reg, err := pkgreg.ReadFile(packagesConfPath())
	if err != nil {
		fmt.Fprintf(os.Stderr, "modcompile: reading packages.conf: %v\n", err)
		os.Exit(1)
	}
	return reg
}

// regenerate writes imports_generated.go and go.work from the registry.
func regenerate(reg *pkgreg.Registry) {
	serverDir := config.EMC2LauncherDir

	if err := reg.GenerateImports(serverDir); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile: generating imports: %v\n", err)
		os.Exit(1)
	}

	if err := reg.GenerateGoWork(serverDir); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile: generating go.work: %v\n", err)
		os.Exit(1)
	}
}

// buildServer builds the gomc-server binary.
func buildServer() {
	serverDir := config.EMC2LauncherDir
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
			"-X '%s.EMC2LauncherDir=%s' "+
			"-X '%s.GoBinary=%s' "+
			"-X '%s.EMC2ConfigPath=%s' "+
			"-X '%s.EMC2NCFilesDir=%s' "+
			"-X '%s.EMC2LangDir=%s' "+
			"-X '%s.EMC2ImageDir=%s' "+
			"-X '%s.EMC2TclLibDir=%s' "+
			"-X '%s.HalibDir=%s' "+
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
		pkg, config.EMC2LauncherDir,
		pkg, config.GoBinary,
		pkg, config.EMC2ConfigPath,
		pkg, config.EMC2NCFilesDir,
		pkg, config.EMC2LangDir,
		pkg, config.EMC2ImageDir,
		pkg, config.EMC2TclLibDir,
		pkg, config.HalibDir,
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
		if e.UseDir != "" {
			fmt.Printf("%-8s %-50s %s\n", e.Type, e.ImportPath, e.UseDir)
		} else {
			fmt.Printf("%-8s %s\n", e.Type, e.ImportPath)
		}
	}
}

// cmdRebuild regenerates derived files and rebuilds the server.
func cmdRebuild() {
	reg := loadRegistry()
	regenerate(reg)
	buildServer()
}

// cmdAddGomod adds an external Go package to the registry and rebuilds.
func cmdAddGomod(dir string) {
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

	// Read the module path from go.mod (first "module" line).
	goModData, err := os.ReadFile(goModPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: reading go.mod: %v\n", err)
		os.Exit(1)
	}
	modulePath := ""
	for _, line := range strings.Split(string(goModData), "\n") {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "module ") {
			modulePath = strings.TrimSpace(strings.TrimPrefix(line, "module"))
			break
		}
	}
	if modulePath == "" {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: could not find module path in %s\n", goModPath)
		os.Exit(1)
	}

	reg := loadRegistry()
	e := pkgreg.Entry{
		Type:       pkgreg.TypeGomod,
		ImportPath: modulePath,
		UseDir:     absDir,
	}
	if !reg.Add(e) {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: %s is already registered\n", modulePath)
		os.Exit(1)
	}

	if err := reg.WriteFile(packagesConfPath()); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile add-gomod: writing packages.conf: %v\n", err)
		os.Exit(1)
	}

	fmt.Fprintf(os.Stderr, "Added %s (%s)\n", modulePath, absDir)
	regenerate(reg)
	buildServer()
}

// cmdRmGomod removes a Go package from the registry and rebuilds.
func cmdRmGomod(name string) {
	reg := loadRegistry()

	// Try exact match first, then basename match.
	found := false
	for _, e := range reg.Entries {
		if e.ImportPath == name || filepath.Base(e.ImportPath) == name {
			if reg.Remove(e.ImportPath) {
				fmt.Fprintf(os.Stderr, "Removed %s\n", e.ImportPath)
				found = true
				break
			}
		}
	}

	if !found {
		fmt.Fprintf(os.Stderr, "modcompile rm-gomod: %s not found in registry\n", name)
		os.Exit(1)
	}

	if err := reg.WriteFile(packagesConfPath()); err != nil {
		fmt.Fprintf(os.Stderr, "modcompile rm-gomod: writing packages.conf: %v\n", err)
		os.Exit(1)
	}

	regenerate(reg)
	buildServer()
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
