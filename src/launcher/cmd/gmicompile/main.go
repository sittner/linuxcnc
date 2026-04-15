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
	"path/filepath"
	"strings"

	"github.com/sittner/linuxcnc/src/launcher/internal/gmicompile/ast"
	"github.com/sittner/linuxcnc/src/launcher/internal/gmicompile/cgen"
	"github.com/sittner/linuxcnc/src/launcher/internal/gmicompile/parser"
)

const usageText = `gmicompile: Compile GMI interface definitions

Usage:
    gmicompile [options] file.gmi

Options:
    --help           Show this help message
    --parse          Parse only — print AST as JSON

Code generation:
    --server-c       Generate C server header (types, callback typedefs)
    --client-c       Generate C REST client (header + source, cJSON/libcurl)
    --server-go      Generate Go server handlers (not yet implemented)
    --client-go      Generate Go REST client (not yet implemented)
    --client-python  Generate Python REST client (not yet implemented)
    -o PATH          Output file or directory

Examples:
    gmicompile --parse hal.gmi
    gmicompile --server-c hal.gmi -o hal_api.h
    gmicompile --client-c halcmd.gmi -o halcmd_client
`

type mode int

const (
	modeParse mode = iota
	modeServerC
	modeClientC
	modeServerGo
	modeClientGo
	modeClientPython
)

func main() {
	if len(os.Args) < 2 {
		fmt.Fprint(os.Stderr, usageText)
		os.Exit(1)
	}

	var m mode
	var outputPath string
	var files []string

	for i := 1; i < len(os.Args); i++ {
		arg := os.Args[i]
		switch arg {
		case "--help", "-h":
			fmt.Print(usageText)
			os.Exit(0)
		case "--parse":
			m = modeParse
		case "--server-c":
			m = modeServerC
		case "--client-c":
			m = modeClientC
		case "--server-go":
			m = modeServerGo
		case "--client-go":
			m = modeClientGo
		case "--client-python":
			m = modeClientPython
		case "-o":
			if i+1 < len(os.Args) {
				i++
				outputPath = os.Args[i]
			}
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
		if err := processFile(file, m, outputPath); err != nil {
			fmt.Fprintf(os.Stderr, "gmicompile: %v\n", err)
			os.Exit(1)
		}
	}
}

func processFile(file string, m mode, outputPath string) error {
	src, err := os.ReadFile(file)
	if err != nil {
		return err
	}

	api, errors := parser.Parse(file, string(src))
	if len(errors) > 0 {
		for _, e := range errors {
			fmt.Fprintln(os.Stderr, e)
		}
		return fmt.Errorf("parse failed")
	}

	if errs := validateAPI(api); len(errs) > 0 {
		for _, e := range errs {
			fmt.Fprintln(os.Stderr, e)
		}
		return fmt.Errorf("validation failed")
	}

	switch m {
	case modeParse:
		enc := json.NewEncoder(os.Stdout)
		enc.SetIndent("", "  ")
		return enc.Encode(api)

	case modeServerC:
		return generateServerC(api, outputPath)

	case modeClientC:
		if !api.RestExport {
			return fmt.Errorf("%s: --client-c requires @rest_export true", file)
		}
		return generateClientC(api, outputPath)

	case modeServerGo, modeClientGo, modeClientPython:
		return fmt.Errorf("mode not yet implemented")
	}

	return nil
}

func generateServerC(api *ast.API, outputPath string) error {
	// Default output name
	if outputPath == "" {
		outputPath = api.Name + "_api.h"
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := cgen.GenerateServerHeader(f, api); err != nil {
		return err
	}

	fmt.Fprintf(os.Stderr, "generated %s\n", outputPath)
	return nil
}

func generateClientC(api *ast.API, outputPath string) error {
	// If outputPath ends with .h or .c, use it as base
	// Otherwise treat as base name
	var baseName string
	if outputPath == "" {
		baseName = api.Name + "_client"
	} else {
		baseName = strings.TrimSuffix(outputPath, filepath.Ext(outputPath))
	}

	headerPath := baseName + ".h"
	sourcePath := baseName + ".c"

	// Generate header
	hf, err := os.Create(headerPath)
	if err != nil {
		return err
	}
	defer hf.Close()
	if err := cgen.GenerateClientHeader(hf, api); err != nil {
		return err
	}
	fmt.Fprintf(os.Stderr, "generated %s\n", headerPath)

	// Generate source
	sf, err := os.Create(sourcePath)
	if err != nil {
		return err
	}
	defer sf.Close()
	if err := cgen.GenerateClientSource(sf, api); err != nil {
		return err
	}
	fmt.Fprintf(os.Stderr, "generated %s\n", sourcePath)

	return nil
}

// validateAPI checks semantic constraints on a parsed API.
func validateAPI(api *ast.API) []string {
	var errs []string
	if !api.RestExport {
		return nil
	}

	// ptr type is forbidden in REST-exported APIs
	for _, fn := range api.Funcs {
		for _, p := range fn.Params {
			if typeUsesPtr(p.Type) {
				errs = append(errs, fmt.Sprintf("%s: func %s param %q uses ptr, which is forbidden in @rest_export true APIs",
					fn.Pos, fn.Name, p.Name))
			}
		}
		if fn.Return != nil && typeUsesPtr(*fn.Return) {
			errs = append(errs, fmt.Sprintf("%s: func %s return type uses ptr, which is forbidden in @rest_export true APIs",
				fn.Pos, fn.Name))
		}
	}
	for _, t := range api.Types {
		for _, f := range t.Fields {
			if typeUsesPtr(f.Type) {
				errs = append(errs, fmt.Sprintf("%s: type %s field %q uses ptr, which is forbidden in @rest_export true APIs",
					f.Pos, t.Name, f.Name))
			}
		}
	}
	return errs
}

func typeUsesPtr(t ast.TypeRef) bool {
	if t.Kind == ast.TypePrimitive && t.Name == ast.PrimPtr {
		return true
	}
	if t.Elem != nil {
		return typeUsesPtr(*t.Elem)
	}
	return false
}
