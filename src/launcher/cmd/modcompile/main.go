// modcompile compiles .comp files into cmod .so plugins for linuxcnc-launcher.
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
// Environment query options (for external Makefiles):
//
//	--cflags         Print compiler flags for cmod components.
//	--ldflags        Print linker flags for cmod components.
//	--cmod-dir       Print cmod installation directory.
//	--include-dir    Print cmod headers directory.
//	--gomod-dir      Print gomod directory.
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

	"github.com/sittner/linuxcnc/src/launcher/internal/config"
	"github.com/sittner/linuxcnc/src/launcher/internal/modcompile/ast"
	"github.com/sittner/linuxcnc/src/launcher/internal/modcompile/cgen"
	"github.com/sittner/linuxcnc/src/launcher/internal/modcompile/comp"
	"github.com/sittner/linuxcnc/src/launcher/internal/modcompile/docgen"
)

const usageText = `modcompile: Compile .comp files to cmod shared libraries

Usage:
    modcompile [options] file.comp...
    modcompile --cflags | --ldflags | --cmod-dir | --include-dir | --gomod-dir
    modcompile --print-make-inc

Compile options:
    --help           Show this help message
    --parse          Parse only — print the parsed AST as JSON
    --preprocess     Preprocess only — emit generated C code
    --document       Generate man page documentation
    --view-doc       Generate and display man page in terminal
    --compile        Compile .comp to .so in the current directory
    --install        Compile .comp and install to cmod directory
    -o FILE          Write output to FILE (for --preprocess, --document)

Environment query options (for external Makefiles):
    --cflags         Print compiler flags for cmod components
    --ldflags        Print linker flags for cmod components
    --cmod-dir       Print cmod installation directory
    --include-dir    Print cmod headers directory
    --gomod-dir      Print gomod directory
    --launcher-dir   Print launcher Go module source directory
    --go             Print Go binary path used to build LinuxCNC
    --print-make-inc Print Makefile include snippet for external projects

Examples:
    # Compile a .comp file
    modcompile --compile mycomp.comp
    modcompile --install mycomp.comp

    # Generate documentation
    modcompile --document -o mycomp.9 mycomp.comp
    modcompile --view-doc mycomp.comp

    # Use in external Makefile:
    $(eval $(shell modcompile --print-make-inc))
    mycomp.so: mycomp.c
        $(GOMC_CC) $(GOMC_CFLAGS) -o $@ $< $(GOMC_LDFLAGS)

    # Or query individual flags:
    CFLAGS := $(shell modcompile --cflags)
    LDFLAGS := $(shell modcompile --ldflags)
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
	case "--gomod-dir":
		fmt.Println(config.EMC2GomodDir)
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
	fmt.Printf(`$(eval GOMC_CC := %s) $(eval GOMC_CFLAGS := -I%s %s) $(eval GOMC_LDFLAGS := %s) $(eval GOMC_CMOD_DIR := %s) $(eval GOMC_GOMOD_DIR := %s) $(eval GOMC_INCLUDE_DIR := %s) $(eval GOMC_LAUNCHER_DIR := %s) $(eval GOMC_GO := %s) $(eval GOMC_LIB_DIR := %s)`,
		cc,
		config.EMC2CmodIncludeDir, defaultCFlags,
		defaultLDFlags,
		config.EMC2CmodDir,
		config.EMC2GomodDir,
		config.EMC2CmodIncludeDir,
		config.EMC2LauncherDir,
		config.GoBinary,
		libDir,
	)
}
