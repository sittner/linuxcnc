package apiserver

// Integration test for axisui-shaped watch data flowing through the WS
// infrastructure. Uses mock watch/command handlers that return the same
// JSON shapes as the real axisui cmod, verifying the subscribe→push→command
// round-trip contract.

import (
	"context"
	"encoding/json"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"nhooyr.io/websocket"
)

// axisui data types (mirror the generated Go structs)
type testJogInputs struct {
	Disable bool `json:"disable"`
	XPlus   bool `json:"x_plus"`
	XMinus  bool `json:"x_minus"`
}

type testSliderInputs struct {
	Scale            float64 `json:"scale"`
	Feedoverride     int32   `json:"feedoverride"`
	FeedoverrideAbs  int32   `json:"feedoverride_abs"`
	Jogspeed         int32   `json:"jogspeed"`
	Maxvel           int32   `json:"maxvel"`
}

type testNotificationInputs struct {
	NotificationsClear bool `json:"notifications_clear"`
	ResumeInhibit      bool `json:"resume_inhibit"`
}

// TestAxisuiWatchSubscribeAllChannels verifies that subscribing to all 3 axisui
// watch functions produces periodic updates with correct data shapes.
func TestAxisuiWatchSubscribeAllChannels(t *testing.T) {
	var jogCount, sliderCount, notifCount int32

	reg := NewWatchRegistry()
	reg.Register(&WatchAPI{
		APIName:  "axisui",
		Instance: "axisui",
		Watches: []WatchFuncMeta{
			{
				Name:        "get_jog_inputs",
				DefaultRate: 20 * time.Millisecond,
				Watch: func() (json.RawMessage, error) {
					atomic.AddInt32(&jogCount, 1)
					return json.Marshal(testJogInputs{XPlus: true})
				},
			},
			{
				Name:        "get_slider_inputs",
				DefaultRate: 50 * time.Millisecond,
				Watch: func() (json.RawMessage, error) {
					atomic.AddInt32(&sliderCount, 1)
					return json.Marshal(testSliderInputs{
						Scale:        0.01,
						Feedoverride: 42,
						Maxvel:       100,
					})
				},
			},
			{
				Name:        "get_notification_inputs",
				DefaultRate: 200 * time.Millisecond,
				Watch: func() (json.RawMessage, error) {
					atomic.AddInt32(&notifCount, 1)
					return json.Marshal(testNotificationInputs{ResumeInhibit: true})
				},
			},
		},
	})

	handler := NewWatchHandler(reg)
	srv := httptest.NewServer(handler)
	defer srv.Close()

	wsURL := "ws" + strings.TrimPrefix(srv.URL, "http")
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	conn, _, err := websocket.Dial(ctx, wsURL, nil)
	if err != nil {
		t.Fatalf("dial: %v", err)
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	// Subscribe to all 3 channels (matching axis.py startup)
	for _, sub := range []wsSubscribe{
		{Action: "subscribe", API: "axisui", Instance: "axisui", Func: "get_jog_inputs", RateMS: 20},
		{Action: "subscribe", API: "axisui", Instance: "axisui", Func: "get_slider_inputs", RateMS: 50},
		{Action: "subscribe", API: "axisui", Instance: "axisui", Func: "get_notification_inputs", RateMS: 200},
	} {
		data, _ := json.Marshal(sub)
		if err := conn.Write(ctx, websocket.MessageText, data); err != nil {
			t.Fatalf("write subscribe %s: %v", sub.Func, err)
		}
	}

	// Collect updates for 300ms — should get all 3 types
	seen := map[string]bool{}
	readCtx, readCancel := context.WithTimeout(ctx, 300*time.Millisecond)
	defer readCancel()

	for len(seen) < 3 {
		_, data, err := conn.Read(readCtx)
		if err != nil {
			break
		}
		var update wsUpdate
		json.Unmarshal(data, &update)
		if update.Type == "update" {
			seen[update.Func] = true

			// Validate data shape for each channel
			switch update.Func {
			case "get_jog_inputs":
				var jog testJogInputs
				if err := json.Unmarshal(update.Data, &jog); err != nil {
					t.Fatalf("unmarshal jog: %v", err)
				}
				if !jog.XPlus {
					t.Fatal("expected x_plus=true")
				}
			case "get_slider_inputs":
				var sl testSliderInputs
				if err := json.Unmarshal(update.Data, &sl); err != nil {
					t.Fatalf("unmarshal slider: %v", err)
				}
				if sl.Feedoverride != 42 {
					t.Fatalf("expected feedoverride=42, got %d", sl.Feedoverride)
				}
				if sl.Scale != 0.01 {
					t.Fatalf("expected scale=0.01, got %f", sl.Scale)
				}
			case "get_notification_inputs":
				var n testNotificationInputs
				if err := json.Unmarshal(update.Data, &n); err != nil {
					t.Fatalf("unmarshal notif: %v", err)
				}
				if !n.ResumeInhibit {
					t.Fatal("expected resume_inhibit=true")
				}
			}
		}
	}

	for _, ch := range []string{"get_jog_inputs", "get_slider_inputs", "get_notification_inputs"} {
		if !seen[ch] {
			t.Errorf("never received update for %s", ch)
		}
	}
}

// TestAxisuiCommandRoundTrip verifies command dispatch through the WS connection
// using the same function names and argument shapes as the real axisui API.
func TestAxisuiCommandRoundTrip(t *testing.T) {
	var lastAxis string
	var lastIncrement float64
	var isRunning bool

	reg := NewWatchRegistry()
	reg.Register(&WatchAPI{
		APIName:  "axisui",
		Instance: "axisui",
		Commands: []CommandMeta{
			{
				Name: "set_jog_axis",
				Handler: func(req json.RawMessage) (json.RawMessage, error) {
					var args struct {
						Axis string `json:"axis"`
					}
					json.Unmarshal(req, &args)
					lastAxis = args.Axis
					return json.Marshal(map[string]bool{"ok": true})
				},
			},
			{
				Name: "set_jog_increment",
				Handler: func(req json.RawMessage) (json.RawMessage, error) {
					var args struct {
						Increment float64 `json:"increment"`
					}
					json.Unmarshal(req, &args)
					lastIncrement = args.Increment
					return json.Marshal(map[string]bool{"ok": true})
				},
			},
			{
				Name: "set_is_running",
				Handler: func(req json.RawMessage) (json.RawMessage, error) {
					var args struct {
						Running bool `json:"running"`
					}
					json.Unmarshal(req, &args)
					isRunning = args.Running
					return json.Marshal(map[string]bool{"ok": true})
				},
			},
		},
	})

	handler := NewWatchHandler(reg)
	srv := httptest.NewServer(handler)
	defer srv.Close()

	wsURL := "ws" + strings.TrimPrefix(srv.URL, "http")
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	conn, _, err := websocket.Dial(ctx, wsURL, nil)
	if err != nil {
		t.Fatalf("dial: %v", err)
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	// Test set_jog_axis
	call := wsCall{
		Action: "call", API: "axisui", Instance: "axisui",
		Func: "set_jog_axis", ID: 1,
		Args: json.RawMessage(`{"axis":"x"}`),
	}
	data, _ := json.Marshal(call)
	conn.Write(ctx, websocket.MessageText, data)

	_, resp, _ := conn.Read(ctx)
	var result wsResult
	json.Unmarshal(resp, &result)
	if result.Error != "" {
		t.Fatalf("set_jog_axis error: %s", result.Error)
	}
	if lastAxis != "x" {
		t.Fatalf("expected axis=x, got %q", lastAxis)
	}

	// Test set_jog_increment
	call = wsCall{
		Action: "call", API: "axisui", Instance: "axisui",
		Func: "set_jog_increment", ID: 2,
		Args: json.RawMessage(`{"increment":0.001}`),
	}
	data, _ = json.Marshal(call)
	conn.Write(ctx, websocket.MessageText, data)

	_, resp, _ = conn.Read(ctx)
	json.Unmarshal(resp, &result)
	if result.Error != "" {
		t.Fatalf("set_jog_increment error: %s", result.Error)
	}
	if lastIncrement != 0.001 {
		t.Fatalf("expected increment=0.001, got %f", lastIncrement)
	}

	// Test set_is_running
	call = wsCall{
		Action: "call", API: "axisui", Instance: "axisui",
		Func: "set_is_running", ID: 3,
		Args: json.RawMessage(`{"running":true}`),
	}
	data, _ = json.Marshal(call)
	conn.Write(ctx, websocket.MessageText, data)

	_, resp, _ = conn.Read(ctx)
	json.Unmarshal(resp, &result)
	if result.Error != "" {
		t.Fatalf("set_is_running error: %s", result.Error)
	}
	if !isRunning {
		t.Fatal("expected isRunning=true")
	}
}
