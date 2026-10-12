package generator

import (
	"net/http"
	"strings"
	"time"
)

// DefaultColibriBaseURL matches generator/colibri_client.py DEFAULT_BASE_URL.
// Empty Settings → AI server field uses this for reachability probes and
// SuggestProfile / quality opinion fallbacks.
const DefaultColibriBaseURL = "http://127.0.0.1:8080"

// NormalizeColibriBaseURL trims input and falls back to the default local
// Colibri address when empty (same contract as the Python client).
func NormalizeColibriBaseURL(baseURL string) string {
	baseURL = strings.TrimSpace(baseURL)
	if baseURL == "" {
		return DefaultColibriBaseURL
	}
	return strings.TrimRight(baseURL, "/")
}

// ColibriAvailable reports whether a Colibri-compatible OpenAI server answers
// GET /v1/models at baseURL. Never errors to the caller — unreachable local
// servers are the expected default (no Colibri installed). Pure Go HTTP so
// Settings can probe without requiring Python.
func ColibriAvailable(baseURL string) bool {
	baseURL = NormalizeColibriBaseURL(baseURL)
	client := &http.Client{Timeout: 1500 * time.Millisecond}
	resp, err := client.Get(baseURL + "/v1/models")
	if err != nil {
		return false
	}
	defer resp.Body.Close()
	return resp.StatusCode >= 200 && resp.StatusCode < 300
}
