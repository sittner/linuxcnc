// gmicompile compiles .gmi interface definitions to C/Go/Python code.
//
// Usage:
//
//	gmicompile [options] file.gmi
//
// Options:
//
//	--help           Show this help message
//	--parse          Parse only — print AST as JSON
//	--server-c       Generate C server callbacks and types
//	--server-go      Generate Go server handlers
//	--client-c       Generate C REST client (cJSON/libcurl)
//	--client-go      Generate Go REST client
//	--client-python  Generate Python REST client
//	-o PATH          Output file or directory
package main

import (
	"encoding/json"
	"fmt"
	"os"

	"github.com/sittner/linuxcnc/src/launcher/internal/gmicompile/parser"
)

const usageText = `gmicompile: Compile GMI interface definitions

Usage:
    gmicompile [options] file.gmi

Options:
    --help           Show this help message
    --parse          Parse only — print AST as JSON

Code generation (not yet implemented):
    --server-c       Generate C server callbacks and types
    --server-go      Generate Go server handlers
    --client-c       Generate C REST client (cJSON/libcurl)
    --client-go      Generate Go REST client
    --client-python  Generate Python REST client
    -o PATH          Output file or directory

Examples:
    gmicompile --parse hal.gmi
    gmicompile --server-c hal.gmi -o hal_api.h
    gmicompile --client-python halcmd.gmi -o halcmd_client.py
`

func main() {
	if len(os.Args) < 2 {
		fmt.Fprint(os.Stderr, usageText)
		os.Exit(1)
	}

	var parseOnly bool
	var files []string

	for i := 1; i < len(os.Args); i++ {
		arg := os.Args[i]
		switch arg {
		case "--help", "-h":
			fmt.Print(usageText)
			os.Exit(0)
		case "--parse":
			parseOnly = true
		case "--server-c", "--server-go", "--client-c", "--client-go", "--client-python":
			fmt.Fprintf(os.Stderr, "gmicompile: %s not yet implemented\n", arg)
			os.Exit(1)
		case "-o":
			i++ // skip output path for now
		default:
			if len(arg) > 0 && arg[0] != '-' {
				files = append(files, arg)
			}
		}
	}

	if len(files) == 0 {
		fmt.Fprintln(os.Stderr, "gmicompile: no input files")
		os.Exit(1)
	}

	for _, file := range files {
		src, err := os.ReadFile(file)
		if err != nil {
			fmt.Fprintf(os.Stderr, "gmicompile: %v\n", err)
			os.Exit(1)
		}

		api, errors := parser.Parse(file, string(src))
		if len(errors) > 0 {
			for _, e := range errors {
				fmt.Fprintln(os.Stderr, e)
			}
			os.Exit(1)
		}

		if parseOnly {
			enc := json.NewEncoder(os.Stdout)
			enc.SetIndent("", "  ")
			enc.Encode(api)
		}
	}
}
