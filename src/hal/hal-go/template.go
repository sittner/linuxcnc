package hal

import (
	"bytes"
	"fmt"
	"math"
	"os"
	"strconv"
	"strings"
	"text/template"
)

// HalTemplateData holds the data context available to HAL file templates.
type HalTemplateData struct {
	INI    map[string]map[string]string
	Axes   []string
	Joints int
	Env    map[string]string
}

// NewHalTemplateData creates a HalTemplateData from an INI data map.
func NewHalTemplateData(ini map[string]map[string]string) *HalTemplateData {
	data := &HalTemplateData{
		INI: ini,
		Env: make(map[string]string),
	}

	// Extract axes from [TRAJ]COORDINATES
	if traj, ok := ini["TRAJ"]; ok {
		if coords, ok := traj["COORDINATES"]; ok {
			for _, c := range strings.TrimSpace(coords) {
				if c != ' ' {
					data.Axes = append(data.Axes, string(c))
				}
			}
		}
	}

	// Extract joints from [KINS]JOINTS
	if kins, ok := ini["KINS"]; ok {
		if joints, ok := kins["JOINTS"]; ok {
			if n, err := strconv.Atoi(strings.TrimSpace(joints)); err == nil {
				data.Joints = n
			}
		}
	}

	// Populate environment
	for _, env := range os.Environ() {
		if k, v, ok := strings.Cut(env, "="); ok {
			data.Env[k] = v
		}
	}

	return data
}

// halTemplateFuncs returns the function map for HAL file templates.
func halTemplateFuncs() template.FuncMap {
	return template.FuncMap{
		// String operations
		"lower":    strings.ToLower,
		"upper":    strings.ToUpper,
		"replace":  strings.ReplaceAll,
		"contains": strings.Contains,
		"split":    strings.Split,
		"join":     strings.Join,
		"printf":   fmt.Sprintf,
		"trim":     strings.TrimSpace,

		// Math operations
		"add": func(a, b float64) float64 { return a + b },
		"sub": func(a, b float64) float64 { return a - b },
		"mul": func(a, b float64) float64 { return a * b },
		"div": func(a, b float64) float64 {
			if b == 0 {
				return math.NaN()
			}
			return a / b
		},
		"neg": func(a float64) float64 { return -a },

		// Iteration helpers
		"seq": func(start, end int) []int {
			result := make([]int, 0, end-start)
			for i := start; i < end; i++ {
				result = append(result, i)
			}
			return result
		},
		"count": func(n int) []int {
			result := make([]int, n)
			for i := range result {
				result[i] = i
			}
			return result
		},

		// INI access (for use in pipelines)
		"ini": func(section, key string, iniData map[string]map[string]string) string {
			if s, ok := iniData[section]; ok {
				if v, ok := s[key]; ok {
					return v
				}
			}
			return ""
		},

		// Environment access
		"env": os.Getenv,

		// Type conversions
		"atoi": strconv.Atoi,
		"atof": func(s string) (float64, error) {
			return strconv.ParseFloat(s, 64)
		},
		"itoa": strconv.Itoa,
	}
}

// RenderHalTemplate renders a HAL file through Go's text/template engine.
// Returns the rendered output as a string.
// If the input contains no template directives (no "{{"), it is returned as-is.
func RenderHalTemplate(name, content string, data *HalTemplateData) (string, error) {
	// Fast path: no template directives
	if !strings.Contains(content, "{{") {
		return content, nil
	}

	tmpl, err := template.New(name).Funcs(halTemplateFuncs()).Parse(content)
	if err != nil {
		return "", fmt.Errorf("template parse error in %s: %w", name, err)
	}

	var buf bytes.Buffer
	if err := tmpl.Execute(&buf, data); err != nil {
		return "", fmt.Errorf("template execute error in %s: %w", name, err)
	}

	return buf.String(), nil
}
