// modcompile compiles .comp files into cmod .so plugins for linuxcnc-launcher.
//
// Usage:
//
//	modcompile [options] file.comp...
//
// Options:
//
//	--parse       Parse only — print the parsed AST and exit.
//	--preprocess  Preprocess only — emit generated C to stdout.
//	--compile     Compile to .so in the current directory.
//	--install     Compile and install to EMC2_CMOD_DIR.
package main

import (
	"encoding/json"
	"fmt"
	"os"

	"github.com/sittner/linuxcnc/src/launcher/internal/modcompile/cgen"
	"github.com/sittner/linuxcnc/src/launcher/internal/modcompile/comp"
)

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintf(os.Stderr, "usage: modcompile [--parse|--preprocess|--compile|--install] file.comp...\n")
		os.Exit(1)
	}

	// Simple arg parsing for Phase 1.
	files := os.Args[1:]
	mode := "--parse"
	if len(files) > 0 && len(files[0]) > 0 && files[0][0] == '-' {
		mode = files[0]
		files = files[1:]
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
			if err := cgen.Generate(os.Stdout, pkg); err != nil {
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
