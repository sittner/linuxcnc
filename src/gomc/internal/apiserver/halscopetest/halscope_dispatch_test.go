package halscopetest

import (
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/halscope"
	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
)

// setupTestServer creates an httptest server with mock halscope callbacks registered.
func setupTestServer(t *testing.T) (*httptest.Server, func()) {
	t.Helper()
	MockResetState()

	cb := MockCallbacks()

	reg := apiserver.NewRegistry()
	apiserver.RegisterMeta(halscope.HalscopeMeta)

	err := reg.Register("halscope", 1, "halscope", cb)
	if err != nil {
		FreeMockCallbacks(cb)
		t.Fatalf("Register failed: %v", err)
	}

	srv := apiserver.NewServer(reg, "")
	ts := httptest.NewServer(srv.Handler())
	return ts, func() {
		ts.Close()
		FreeMockCallbacks(cb)
	}
}

func get(t *testing.T, ts *httptest.Server, path string) (int, []byte) {
	t.Helper()
	resp, err := http.Get(ts.URL + "/api/v1/halscope" + path)
	if err != nil {
		t.Fatalf("GET %s: %v", path, err)
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	return resp.StatusCode, body
}

func post(t *testing.T, ts *httptest.Server, path, jsonBody string) (int, []byte) {
	t.Helper()
	resp, err := http.Post(ts.URL+"/api/v1/halscope"+path, "application/json", strings.NewReader(jsonBody))
	if err != nil {
		t.Fatalf("POST %s: %v", path, err)
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	return resp.StatusCode, body
}

func delete_(t *testing.T, ts *httptest.Server, path string) (int, []byte) {
	t.Helper()
	req, err := http.NewRequest(http.MethodDelete, ts.URL+"/api/v1/halscope"+path, nil)
	if err != nil {
		t.Fatalf("DELETE %s: %v", path, err)
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("DELETE %s: %v", path, err)
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	return resp.StatusCode, body
}

func TestGetStatus_Initial(t *testing.T) {
	ts, cleanup := setupTestServer(t)
	defer cleanup()

	code, body := get(t, ts, "/status")
	if code != 200 {
		t.Fatalf("expected 200, got %d: %s", code, body)
	}

	var st halscope.ScopeStatus
	if err := json.Unmarshal(body, &st); err != nil {
		t.Fatalf("unmarshal: %v\nbody: %s", err, body)
	}
	if st.State != 0 {
		t.Errorf("expected state=0, got %d", st.State)
	}
	if st.RecLen != 4000 {
		t.Errorf("expected rec_len=4000, got %d", st.RecLen)
	}
	if st.SampleLen != 0 {
		t.Errorf("expected sample_len=0, got %d", st.SampleLen)
	}
	if st.Channels != nil {
		t.Errorf("expected nil channels, got %v", st.Channels)
	}
}

func TestListPins(t *testing.T) {
	ts, cleanup := setupTestServer(t)
	defer cleanup()

	code, body := get(t, ts, "/pins")
	if code != 200 {
		t.Fatalf("expected 200, got %d: %s", code, body)
	}

	var pins []string
	if err := json.Unmarshal(body, &pins); err != nil {
		t.Fatalf("unmarshal: %v\nbody: %s", err, body)
	}
	if len(pins) != 3 {
		t.Fatalf("expected 3 pins, got %d: %v", len(pins), pins)
	}
	if pins[0] != "joint.0.pos-cmd" {
		t.Errorf("expected joint.0.pos-cmd, got %s", pins[0])
	}
	if pins[2] != "joint.2.pos-cmd" {
		t.Errorf("expected joint.2.pos-cmd, got %s", pins[2])
	}
}

func TestSetChannel(t *testing.T) {
	ts, cleanup := setupTestServer(t)
	defer cleanup()

	code, body := post(t, ts, "/channel", `{"ch":{"channel":0,"pin_name":"joint.2.pos-cmd"}}`)
	if code != 200 {
		t.Fatalf("expected 200, got %d: %s", code, body)
	}

	var result int32
	if err := json.Unmarshal(body, &result); err != nil {
		t.Fatalf("unmarshal: %v\nbody: %s", err, body)
	}
	if result != 0 {
		t.Errorf("expected result=0, got %d", result)
	}

	// Verify status reflects the channel
	code, body = get(t, ts, "/status")
	if code != 200 {
		t.Fatalf("status: expected 200, got %d: %s", code, body)
	}

	var st halscope.ScopeStatus
	if err := json.Unmarshal(body, &st); err != nil {
		t.Fatalf("unmarshal status: %v\nbody: %s", err, body)
	}
	if st.SampleLen != 1 {
		t.Errorf("expected sample_len=1, got %d", st.SampleLen)
	}
	if len(st.Channels) != 1 {
		t.Fatalf("expected 1 channel, got %d", len(st.Channels))
	}
	if st.Channels[0].PinName != "joint.2.pos-cmd" {
		t.Errorf("expected pin_name=joint.2.pos-cmd, got %s", st.Channels[0].PinName)
	}
	if st.Channels[0].Channel != 0 {
		t.Errorf("expected channel=0, got %d", st.Channels[0].Channel)
	}
	if !st.Channels[0].Enabled {
		t.Errorf("expected enabled=true")
	}
}

func TestClearChannel(t *testing.T) {
	ts, cleanup := setupTestServer(t)
	defer cleanup()

	// Add then clear
	post(t, ts, "/channel", `{"ch":{"channel":0,"pin_name":"test.pin"}}`)

	code, body := delete_(t, ts, "/channel/0")
	if code != 200 {
		t.Fatalf("expected 200, got %d: %s", code, body)
	}

	// Status should show no channels
	_, body = get(t, ts, "/status")
	var st halscope.ScopeStatus
	json.Unmarshal(body, &st)
	if st.SampleLen != 0 {
		t.Errorf("expected sample_len=0 after clear, got %d", st.SampleLen)
	}
}

func TestArmAndReset(t *testing.T) {
	ts, cleanup := setupTestServer(t)
	defer cleanup()

	// Arm
	code, body := post(t, ts, "/arm", "")
	if code != 200 {
		t.Fatalf("arm: expected 200, got %d: %s", code, body)
	}

	// Check state changed to ARMED (1)
	_, body = get(t, ts, "/status")
	var st halscope.ScopeStatus
	json.Unmarshal(body, &st)
	if st.State != 1 {
		t.Errorf("expected state=1 (ARMED), got %d", st.State)
	}

	// Reset
	code, body = post(t, ts, "/reset", "")
	if code != 200 {
		t.Fatalf("reset: expected 200, got %d: %s", code, body)
	}

	// Check state back to IDLE (0)
	_, body = get(t, ts, "/status")
	json.Unmarshal(body, &st)
	if st.State != 0 {
		t.Errorf("expected state=0 (IDLE) after reset, got %d", st.State)
	}
}

func TestConfigure(t *testing.T) {
	ts, cleanup := setupTestServer(t)
	defer cleanup()

	code, body := post(t, ts, "/configure",
		`{"config":{"thread_name":"servo-thread","rec_len":8000,"sample_period_mult":1,"pre_trig":4000}}`)
	if code != 200 {
		t.Fatalf("expected 200, got %d: %s", code, body)
	}

	// Verify status reflects new config
	_, body = get(t, ts, "/status")
	var st halscope.ScopeStatus
	json.Unmarshal(body, &st)
	if st.RecLen != 8000 {
		t.Errorf("expected rec_len=8000, got %d", st.RecLen)
	}
	if st.PreTrig != 4000 {
		t.Errorf("expected pre_trig=4000, got %d", st.PreTrig)
	}
}

func TestSetTrigger(t *testing.T) {
	ts, cleanup := setupTestServer(t)
	defer cleanup()

	code, body := post(t, ts, "/trigger",
		`{"trig":{"channel":0,"level":1.5,"edge":1,"force":false,"auto_trig":true}}`)
	if code != 200 {
		t.Fatalf("expected 200, got %d: %s", code, body)
	}

	var result int32
	json.Unmarshal(body, &result)
	if result != 0 {
		t.Errorf("expected result=0, got %d", result)
	}
}

func TestFullCaptureWorkflow(t *testing.T) {
	ts, cleanup := setupTestServer(t)
	defer cleanup()

	// 1. Configure
	code, _ := post(t, ts, "/configure",
		`{"config":{"thread_name":"servo-thread","rec_len":8000,"sample_period_mult":1,"pre_trig":4000}}`)
	if code != 200 {
		t.Fatalf("configure failed: %d", code)
	}

	// 2. Add channel
	code, _ = post(t, ts, "/channel", `{"ch":{"channel":0,"pin_name":"joint.0.pos-cmd"}}`)
	if code != 200 {
		t.Fatalf("set_channel failed: %d", code)
	}

	// 3. Set trigger
	code, _ = post(t, ts, "/trigger",
		`{"trig":{"channel":0,"level":0.0,"edge":0,"force":true,"auto_trig":false}}`)
	if code != 200 {
		t.Fatalf("set_trigger failed: %d", code)
	}

	// 4. Arm
	code, _ = post(t, ts, "/arm", "")
	if code != 200 {
		t.Fatalf("arm failed: %d", code)
	}

	// 5. Verify armed state
	_, body := get(t, ts, "/status")
	var st halscope.ScopeStatus
	json.Unmarshal(body, &st)
	if st.State != 1 {
		t.Errorf("expected state=1 (ARMED), got %d", st.State)
	}
	if st.RecLen != 8000 {
		t.Errorf("expected rec_len=8000, got %d", st.RecLen)
	}
	if len(st.Channels) != 1 {
		t.Fatalf("expected 1 channel, got %d", len(st.Channels))
	}
	if st.Channels[0].PinName != "joint.0.pos-cmd" {
		t.Errorf("expected pin_name=joint.0.pos-cmd, got %s", st.Channels[0].PinName)
	}

	// 6. Reset
	code, _ = post(t, ts, "/reset", "")
	if code != 200 {
		t.Fatalf("reset failed: %d", code)
	}

	// 7. Clear channel
	code, _ = delete_(t, ts, "/channel/0")
	if code != 200 {
		t.Fatalf("clear_channel failed: %d", code)
	}

	// 8. Final status: idle, no channels
	_, body = get(t, ts, "/status")
	json.Unmarshal(body, &st)
	if st.State != 0 {
		t.Errorf("expected state=0, got %d", st.State)
	}
	if st.SampleLen != 0 {
		t.Errorf("expected sample_len=0, got %d", st.SampleLen)
	}
}

func TestNotFound(t *testing.T) {
	ts, cleanup := setupTestServer(t)
	defer cleanup()

	code, _ := get(t, ts, "/nonexistent")
	if code != 404 {
		t.Errorf("expected 404, got %d", code)
	}
}
