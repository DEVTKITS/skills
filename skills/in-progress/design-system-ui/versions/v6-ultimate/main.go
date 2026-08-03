// Design System UI - Go client / desktop bridge (standard library only).
//
// Builds with `go build` — no external modules, no network fetch required.
// Two modes:
//
//	go run main.go serve          # standalone static+API server (mirrors server.py)
//	go run main.go run <wf.json>  # execute a workflow against a running backend
//
// The desktop/mobile app shell (Wails/Gio) can embed the `serve` handler.
package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// Node is a single workflow node.
type Node struct {
	ID     string                 `json:"id"`
	Type   string                 `json:"type"`
	X      float64                `json:"x"`
	Y      float64                `json:"y"`
	Params map[string]interface{} `json:"params"`
}

// Edge connects two nodes.
type Edge struct {
	From string `json:"from"`
	To   string `json:"to"`
}

// Workflow is a complete node graph.
type Workflow struct {
	ID         string                 `json:"id"`
	Nodes      []Node                 `json:"nodes"`
	Edges      []Edge                 `json:"edges"`
	Parameters map[string]interface{} `json:"parameters"`
}

const defaultBackend = "http://localhost:8000"

func main() {
	if len(os.Args) < 2 {
		fmt.Println("usage: main [serve|run <workflow.json>]")
		os.Exit(2)
	}
	switch os.Args[1] {
	case "serve":
		serve()
	case "run":
		if len(os.Args) < 3 {
			log.Fatal("run: expected a workflow.json path")
		}
		if err := runWorkflow(os.Args[2]); err != nil {
			log.Fatalf("run: %v", err)
		}
	default:
		log.Fatalf("unknown command %q", os.Args[1])
	}
}

// runWorkflow POSTs a workflow file to the backend and prints the result.
func runWorkflow(path string) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	var wf Workflow
	if err := json.Unmarshal(data, &wf); err != nil {
		return fmt.Errorf("invalid workflow json: %w", err)
	}
	backend := envOr("BACKEND_URL", defaultBackend)
	resp, err := http.Post(backend+"/api/workflow/execute",
		"application/json", bytes.NewReader(data))
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	fmt.Printf("workflow %s -> %s\n%s\n", wf.ID, resp.Status, body)
	return nil
}

// serve runs a standalone static+API server using only net/http.
func serve() {
	root := selfDir()
	mux := http.NewServeMux()

	static := map[string]string{
		"/":           "index.html",
		"/index.html": "index.html",
		"/app.js":     "app.js",
		"/styles.css": "styles.css",
		"/llms.txt":   "llms.txt",
	}
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		cors(w)
		if name, ok := static[r.URL.Path]; ok {
			http.ServeFile(w, r, filepath.Join(root, name))
			return
		}
		writeJSON(w, http.StatusNotFound, map[string]string{"error": "not found"})
	})
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		cors(w)
		writeJSON(w, http.StatusOK, map[string]string{
			"status": "healthy", "service": "design-system-ui-go",
			"timestamp": time.Now().UTC().Format(time.RFC3339),
		})
	})
	mux.HandleFunc("/api/patterns", func(w http.ResponseWriter, r *http.Request) {
		cors(w)
		writeJSON(w, http.StatusOK, patterns())
	})

	port := envOr("PORT", "8080")
	log.Printf("Design System UI (Go) on http://localhost:%s", port)
	log.Fatal(http.ListenAndServe(":"+port, mux))
}

func patterns() map[string]interface{} {
	return map[string]interface{}{"patterns": map[string]interface{}{
		"text-classifier": map[string]string{"name": "Text Classifier",
			"description": "Classify text into categories", "icon": "🏷️"},
		"rag-pipeline": map[string]string{"name": "RAG Pipeline",
			"description": "Retrieval-Augmented Generation", "icon": "🔍"},
		"agent-loop": map[string]string{"name": "Agent Loop",
			"description": "Think-Act-Observe agent pattern", "icon": "🤖"},
		"eval-loop": map[string]string{"name": "Evaluation Loop",
			"description": "Execute and evaluate iteratively", "icon": "📊"},
	}}
}

func cors(w http.ResponseWriter) {
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
	w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
}

func writeJSON(w http.ResponseWriter, status int, v interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

func envOr(key, def string) string {
	if v := strings.TrimSpace(os.Getenv(key)); v != "" {
		return v
	}
	return def
}

// selfDir returns the directory that holds the static assets.
func selfDir() string {
	if exe, err := os.Executable(); err == nil {
		if d := filepath.Dir(exe); d != "" && d != "." {
			if _, err := os.Stat(filepath.Join(d, "index.html")); err == nil {
				return d
			}
		}
	}
	wd, _ := os.Getwd()
	return wd
}
