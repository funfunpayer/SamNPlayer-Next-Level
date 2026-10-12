package generator

import (
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestNormalizeColibriBaseURL(t *testing.T) {
	if got := NormalizeColibriBaseURL(""); got != DefaultColibriBaseURL {
		t.Fatalf("empty → default, got %q", got)
	}
	if got := NormalizeColibriBaseURL("  http://127.0.0.1:9999/  "); got != "http://127.0.0.1:9999" {
		t.Fatalf("trim slash, got %q", got)
	}
}

func TestColibriAvailable(t *testing.T) {
	okSrv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/v1/models" {
			t.Fatalf("path %s", r.URL.Path)
		}
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte(`{"data":[]}`))
	}))
	defer okSrv.Close()

	if !ColibriAvailable(okSrv.URL) {
		t.Fatal("expected available against live /v1/models")
	}
	if ColibriAvailable("http://127.0.0.1:1") {
		t.Fatal("port 1 must be unavailable")
	}
}
