package hal

import (
	"strings"
	"testing"
)

func TestRenderHalTemplate_NoDirectives(t *testing.T) {
	input := "loadrt trivkins\naddf servo-thread\nstart\n"
	out, err := RenderHalTemplate("test.hal", input, &HalTemplateData{})
	if err != nil {
		t.Fatal(err)
	}
	if out != input {
		t.Errorf("expected passthrough, got %q", out)
	}
}

func TestRenderHalTemplate_INISubstitution(t *testing.T) {
	data := &HalTemplateData{
		INI: map[string]map[string]string{
			"AXIS_X": {"SCALE": "1000"},
		},
	}
	input := `setp axis.x.scale {{index .INI "AXIS_X" "SCALE"}}`
	out, err := RenderHalTemplate("test.hal", input, data)
	if err != nil {
		t.Fatal(err)
	}
	expected := "setp axis.x.scale 1000"
	if out != expected {
		t.Errorf("expected %q, got %q", expected, out)
	}
}

func TestRenderHalTemplate_RangeAxes(t *testing.T) {
	data := &HalTemplateData{
		Axes: []string{"X", "Y", "Z"},
		INI:  map[string]map[string]string{},
	}
	input := "{{range .Axes}}loadrt pid names=pid.{{lower .}}\n{{end}}"
	out, err := RenderHalTemplate("test.hal", input, data)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out, "pid.x") || !strings.Contains(out, "pid.y") || !strings.Contains(out, "pid.z") {
		t.Errorf("expected pid.x/y/z, got %q", out)
	}
}

func TestRenderHalTemplate_MathFunctions(t *testing.T) {
	data := &HalTemplateData{INI: map[string]map[string]string{}}
	input := "setp comp.gain {{add 1.5 2.5}}"
	out, err := RenderHalTemplate("test.hal", input, data)
	if err != nil {
		t.Fatal(err)
	}
	if out != "setp comp.gain 4" {
		t.Errorf("expected 'setp comp.gain 4', got %q", out)
	}
}

func TestRenderHalTemplate_SeqIteration(t *testing.T) {
	data := &HalTemplateData{INI: map[string]map[string]string{}}
	input := "{{range seq 0 3}}joint.{{.}}.enable\n{{end}}"
	out, err := RenderHalTemplate("test.hal", input, data)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out, "joint.0.enable") || !strings.Contains(out, "joint.2.enable") {
		t.Errorf("expected joint.0-2, got %q", out)
	}
}

func TestNewHalTemplateData_Axes(t *testing.T) {
	ini := map[string]map[string]string{
		"TRAJ": {"COORDINATES": "X Y Z"},
		"KINS": {"JOINTS": "3"},
	}
	data := NewHalTemplateData(ini)
	if len(data.Axes) != 3 {
		t.Errorf("expected 3 axes, got %d: %v", len(data.Axes), data.Axes)
	}
	if data.Joints != 3 {
		t.Errorf("expected 3 joints, got %d", data.Joints)
	}
}
