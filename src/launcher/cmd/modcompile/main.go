// modcompile compiles .comp files into cmod .so plugins for linuxcnc-launcher.
//
// Usage:
//
//	modcompile [options] file.comp...
//
// Options:
//
//	--help        Show this help message.
//	--parse       Parse only — print the parsed AST and exit.
//	--preprocess  Preprocess only — emit generated C to stdout.
//	--document    Generate man page documentation.
//	--view-doc    Generate and display man page.
//	--compile     Compile to .so in the current directory.
//	--install     Compile and install to EMC2_CMOD_DIR.
//	-o FILE       Write output to FILE (for --preprocess, --document).
package main

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/sittner/linuxcnc/src/launcher/internal/modcompile/cgen"
	"github.com/sittner/linuxcnc/src/launcher/internal/modcompile/comp"
	"github.com/sittner/linuxcnc/src/launcher/internal/modcompile/docgen"
)

const usageText = `modcompile: Compile .comp files to cmod shared libraries

Usage:
    modcompile [options] file.comp...

Options:
    --help        Show this help message
    --parse       Parse only — print the parsed AST as JSON
    --preprocess  Preprocess only — emit generated C code
    --document    Generate man page documentation
    --view-doc    Generate and display man page in terminal
    --compile     Compile to .so (not yet implemented)
    --install     Compile and install (not yet implemented)
    -o FILE       Write output to FILE (for --preprocess, --document)

Examples:
    modcompile --preprocess mycomp.comp > mycomp.c
    modcompile --document -o mycomp.9 mycomp.comp
    modcompile --view-doc mycomp.comp
`

func main() {
	if len(os.Args) < 2 {
		fmt.Fprint(os.Stderr, usageText)
		os.Exit(1)
	}

	// Parse arguments
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
		src, err := os.ReadFile(path)
		if err != nil {
			fmt.Fprintf(os.Stderr, "modcompile: %v\n", err)
			os.Exit(1)
		}

		pkg, err := comp.Parse(path, string(src))
		if err != nil {
			fmt.Fprintf(os.Stderr, "modcompile: %v\n", err)
			os.Exit(1)
		}

		switch mode {
		case "--parse":
			enc := json.NewEncoder(os.Stdout)
			enc.SetIndent("", "  ")
			enc.Encode(pkg)

		case "--preprocess":
			out := os.Stdout
			if outputFile != "" {
				f, err := os.Create(outputFile)
				if err != nil {
					fmt.Fprintf(os.Stderr, "modcompile: %v\n", err)
					os.Exit(1)
				}
				defer f.Close()
				out = f
			}
			if err := cgen.Generate(out, pkg); err != nil {
				fmt.Fprintf(os.Stderr, "modcompile: %v\n", err)
				os.Exit(1)
			}

		case "--document":
			out := os.Stdout
			if outputFile != "" {
				f, err := os.Create(outputFile)
				if err != nil {
					fmt.Fprintf(os.Stderr, "modcompile: %v\n", err)
					os.Exit(1)
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
					fmt.Fprintf(os.Stderr, "modcompile: %v\n", err)
					os.Exit(1)
				}
				defer f.Close()
				out = f
			}
			if err := docgen.Generate(out, pkg); err != nil {
				fmt.Fprintf(os.Stderr, "modcompile: %v\n", err)
				os.Exit(1)
			}

		case "--view-doc":
			// Generate to temp file and display with man
			tmpFile, err := os.CreateTemp("", "modcompile-*.man")
			if err != nil {
				fmt.Fprintf(os.Stderr, "modcompile: %v\n", err)
				os.Exit(1)
			}
			tmpName := tmpFile.Name()
			defer os.Remove(tmpName)

			if err := docgen.Generate(tmpFile, pkg); err != nil {
				tmpFile.Close()
				fmt.Fprintf(os.Stderr, "modcompile: %v\n", err)
				os.Exit(1)
			}
			tmpFile.Close()

			// Run man to display
			cmd := exec.Command("man", tmpName)
			cmd.Stdout = os.Stdout
			cmd.Stderr = os.Stderr
			cmd.Stdin = os.Stdin
			if err := cmd.Run(); err != nil {
				fmt.Fprintf(os.Stderr, "modcompile: %v\n", err)
				os.Exit(1)
			}

		case "--compile", "--install":
			fmt.Fprintf(os.Stderr, "modcompile: %s not yet implemented\n", mode)
			os.Exit(1)

		default:
			fmt.Fprintf(os.Stderr, "modcompile: unknown mode %q\n", mode)
			os.Exit(1)
		}
	}
}
