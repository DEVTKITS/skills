package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"

	"github.com/go-chi/chi/v5"
	"github.com/go-chi/chi/v5/middleware"
	"github.com/go-chi/cors"
)

// Node represents a workflow node
type Node struct {
	ID     string            `json:"id"`
	Type   string            `json:"type"`
	X      float64           `json:"x"`
	Y      float64           `json:"y"`
	Params map[string]string `json:"params"`
}

// Edge represents a connection between nodes
type Edge struct {
	From string `json:"from"`
	To   string `json:"to"`
}

// Workflow represents a complete workflow
type Workflow struct {
	ID         string            `json:"id"`
	Nodes      []Node            `json:"nodes"`
	Edges      []Edge            `json:"edges"`
	Parameters map[string]string `json:"parameters"`
}

// ExecutionResult represents the result of workflow execution
type ExecutionResult struct {
	Status    string      `json:"status"`
	Output    string      `json:"output"`
	Results   interface{} `json:"results"`
	Timestamp string      `json:"timestamp"`
}

// App struct represents the application
type App struct {
	apiURL string
	router chi.Router
}

// NewApp creates a new application instance
func NewApp() *App {
	app := &App{
		apiURL: "http://localhost:8000",
		router: chi.NewRouter(),
	}

	// Middleware
	app.router.Use(middleware.Logger)
	app.router.Use(middleware.Recoverer)
	app.router.Use(cors.Handler(cors.Options{
		AllowedOrigins:   []string{"*"},
		AllowedMethods:   []string{"GET", "POST", "PUT", "DELETE", "OPTIONS"},
		AllowedHeaders:   []string{"Accept", "Authorization", "Content-Type"},
		ExposedHeaders:   []string{"Link"},
		AllowCredentials: false,
		MaxAge:           300,
	}))

	// Routes
	app.setupRoutes()

	return app
}

// setupRoutes configures all HTTP routes
func (a *App) setupRoutes() {
	a.router.Get("/", a.handleIndex)
	a.router.Post("/api/workflow/execute", a.handleExecuteWorkflow)
	a.router.Post("/api/workflow/evaluate", a.handleEvaluateWorkflow)
	a.router.Get("/api/patterns", a.handleGetPatterns)
	a.router.Get("/health", a.handleHealth)
}

// handleIndex serves the main page
func (a *App) handleIndex(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/html")
	fmt.Fprintf(w, `<!DOCTYPE html>
<html>
<head>
	<title>Design System UI - Desktop App</title>
	<link rel="stylesheet" href="styles.css">
</head>
<body>
	<div id="app"></div>
	<script src="app.js"></script>
</body>
</html>
`)
}

// handleExecuteWorkflow executes a workflow
func (a *App) handleExecuteWorkflow(w http.ResponseWriter, r *http.Request) {
	var workflow Workflow
	if err := json.NewDecoder(r.Body).Decode(&workflow); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	// Validate workflow
	if len(workflow.Nodes) == 0 {
		http.Error(w, "Workflow must have at least one node", http.StatusBadRequest)
		return
	}

	// Forward to Python backend
	result, err := a.forwardRequest("POST", "/api/workflow/execute", workflow)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(result)
}

// handleEvaluateWorkflow evaluates workflow results
func (a *App) handleEvaluateWorkflow(w http.ResponseWriter, r *http.Request) {
	var request map[string]interface{}
	if err := json.NewDecoder(r.Body).Decode(&request); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	// Forward to Python backend
	result, err := a.forwardRequest("POST", "/api/workflow/evaluate", request)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(result)
}

// handleGetPatterns returns available workflow patterns
func (a *App) handleGetPatterns(w http.ResponseWriter, r *http.Request) {
	patterns := map[string]interface{}{
		"patterns": map[string]interface{}{
			"text-classifier": map[string]string{
				"name":        "Text Classifier",
				"description": "Classify text into categories",
				"icon":        "🏷️",
			},
			"rag-pipeline": map[string]string{
				"name":        "RAG Pipeline",
				"description": "Retrieval-Augmented Generation workflow",
				"icon":        "🔍",
			},
			"agent-loop": map[string]string{
				"name":        "Agent Loop",
				"description": "Think-Act-Observe agent pattern",
				"icon":        "🤖",
			},
			"eval-loop": map[string]string{
				"name":        "Evaluation Loop",
				"description": "Execute and evaluate iteratively",
				"icon":        "📊",
			},
		},
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(patterns)
}

// handleHealth returns health status
func (a *App) handleHealth(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"status": "healthy",
		"service": "Design System UI - Desktop App",
	})
}

// forwardRequest forwards a request to the Python backend
func (a *App) forwardRequest(method, path string, body interface{}) (interface{}, error) {
	url := a.apiURL + path

	data, err := json.Marshal(body)
	if err != nil {
		return nil, err
	}

	req, err := http.NewRequest(method, url, nil)
	if err != nil {
		return nil, err
	}

	if method == "POST" {
		req.Header.Set("Content-Type", "application/json")
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	var result interface{}
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return nil, err
	}

	return result, nil
}

// Run starts the HTTP server (for desktop app)
func (a *App) Run(port string) error {
	log.Printf("Design System UI - Desktop App starting on port %s", port)
	return http.ListenAndServe(":"+port, a.router)
}

// Desktop App Exports (for Wails/Electron)

// ExecuteWorkflow executes a workflow from desktop app
func (a *App) ExecuteWorkflow(workflow Workflow) (ExecutionResult, error) {
	log.Printf("Executing workflow: %s", workflow.ID)

	// Call backend
	data, err := json.Marshal(workflow)
	if err != nil {
		return ExecutionResult{Status: "error"}, err
	}

	resp, err := http.Post(
		a.apiURL+"/api/workflow/execute",
		"application/json",
		bytes.NewReader(data),
	)
	if err != nil {
		return ExecutionResult{Status: "error"}, err
	}
	defer resp.Body.Close()

	var result ExecutionResult
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return ExecutionResult{Status: "error"}, err
	}

	return result, nil
}

// GetWorkflowPatterns returns available patterns
func (a *App) GetWorkflowPatterns() (map[string]interface{}, error) {
	resp, err := http.Get(a.apiURL + "/api/patterns")
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	var patterns map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&patterns); err != nil {
		return nil, err
	}

	return patterns, nil
}

func main() {
	port := os.Getenv("PORT")
	if port == "" {
		port = "3000"
	}

	app := NewApp()
	if err := app.Run(port); err != nil {
		log.Fatalf("Failed to run app: %v", err)
	}
}
