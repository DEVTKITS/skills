# Design System UI - Workflow Builder

Interactive single-page web application for designing, executing, and optimizing AI workflows with integrated ComfyUI, terminal execution, and Gemma 4 12B evaluation.

## 🎯 Features

✨ **ComfyUI Integration**
- Drag-and-drop node-based workflow builder
- Visual workflow canvas with real-time rendering
- Node library with input, processing, and output nodes

🚀 **Execution Engine**
- Isolated terminal execution (no context switching)
- Real-time output streaming
- Workflow validation before execution
- Error handling and logging

🤖 **AI Evaluation**
- Gemma 4 12B local model evaluation
- Automated scoring and feedback
- Optimization suggestions via llms.txt

📋 **Agent-Friendly**
- llms.txt surface layer for AI navigation
- No page navigation required
- All operations in single-page context
- Structured API endpoints

📦 **Pattern Templates**
- Text Classifier
- RAG Pipeline
- Agent Loop
- Evaluation Loop

## 🏗️ Architecture

```
┌─────────────────────────────────────────┐
│ Web UI (Vanilla HTML5/CSS3)            │
│ ├─ ComfyUI Canvas                      │
│ ├─ Config Panel                        │
│ ├─ Terminal Output                     │
│ └─ Evaluation Results                  │
└────────────────┬────────────────────────┘
                 │
         ┌───────▼────────┐
         │ FastAPI Server │
         │ (Python)       │
         └───────┬────────┘
                 │
      ┌──────────┴──────────┐
      │                     │
  ┌───▼────┐          ┌────▼─────┐
  │Executor│          │Evaluator  │
  └────────┘          └───────────┘
```

## 🚀 Quick Start

### Prerequisites
- Python 3.9+
- Node 16+ (optional, for development)
- Gemma 4 12B model (via Ollama or Hugging Face)

### Installation

```bash
cd design-system-ui
pip install -r requirements.txt
```

### Running the Server

```bash
# Development
python server.py

# Production
uvicorn server:app --host 0.0.0.0 --port 8000
```

Server will be available at `http://localhost:8000`

## 📖 Usage

### For Users

1. **Design Workflow**
   - Drag nodes from right panel to canvas
   - Connect nodes with edges
   - Or load a pattern template

2. **Configure**
   - Click nodes to edit parameters
   - Use "AI Suggest Parameters" for smart defaults
   - Validate workflow structure

3. **Execute**
   - Click "Execute Workflow"
   - Watch terminal output in real-time
   - System executes nodes in order

4. **Evaluate**
   - Gemma 4 12B scores the results
   - Provides feedback and suggestions
   - Click "Iterate Workflow" for next round

### For AI Agents

1. **Read llms.txt** at `/llms.txt` or in UI
2. **Understand capabilities** and available actions
3. **Interact via API endpoints**:
   - `POST /api/workflow/execute`
   - `POST /api/workflow/evaluate`
   - `POST /api/workflow/suggest-params`
   - `GET /api/patterns`
4. **Operate without page navigation**

## 📡 API Reference

### Workflow Execution

```bash
POST /api/workflow/execute
Content-Type: application/json

{
  "id": "workflow-123",
  "nodes": [
    {
      "id": "input-1",
      "type": "text-input",
      "x": 100,
      "y": 100,
      "params": {}
    }
  ],
  "edges": [],
  "parameters": {}
}
```

**Response:**
```json
{
  "status": "success",
  "output": "Execution logs...",
  "results": {...},
  "timestamp": "2026-07-13T..."
}
```

### Workflow Evaluation

```bash
POST /api/workflow/evaluate
Content-Type: application/json

{
  "workflow": {...},
  "results": {...}
}
```

**Response:**
```json
{
  "score": 75,
  "feedback": "Evaluation report...",
  "suggestions": [
    {
      "title": "Add LLM node",
      "description": "...",
      "action": "ADD_NODE(type='llm-node')"
    }
  ]
}
```

### Parameter Suggestions

```bash
POST /api/workflow/suggest-params
Content-Type: application/json

{
  "nodeType": "llm-node",
  "workflow": {...}
}
```

**Response:**
```json
{
  "model": "gemma-4",
  "temperature": 0.7,
  "maxTokens": 1000
}
```

### Get Patterns

```bash
GET /api/patterns
```

**Response:**
```json
{
  "patterns": {
    "text-classifier": {
      "name": "Text Classifier",
      "description": "...",
      "icon": "🏷️"
    },
    ...
  }
}
```

## 🔗 WebSocket

Live terminal output via WebSocket:

```
ws://localhost:8000/ws/terminal/{workflow_id}
```

## 📝 Node Types

### Input Nodes
- `text-input` - Text input
- `file-input` - File upload
- `image-input` - Image upload

### Processing Nodes
- `llm-node` - LLM inference
- `transform` - Data transformation
- `filter` - Conditional filtering

### Output Nodes
- `text-output` - Text output
- `file-output` - File download

## 🎨 UI Components

- **Canvas Area** (left): Visual workflow editing
- **Config Panel** (center): Nodes, parameters, templates
- **Execution Panel** (right): Terminal, results, evaluation

## 🔧 Development

### Project Structure

```
design-system-ui/
├── SKILL.md           # Skill definition
├── README.md          # This file
├── index.html         # Web UI
├── styles.css         # Styling
├── app.js             # Frontend logic
├── server.py          # FastAPI backend
├── requirements.txt   # Python dependencies
└── llms.txt          # Agent instructions (generated)
```

### Adding Custom Nodes

Edit `app.js` and `server.py`:

1. Add node type to UI node library
2. Implement execution in `WorkflowExecutor.execute_node()`
3. Add parameter defaults to `renderParameterPanel()`

## 🚢 Deployment

### Docker

```dockerfile
FROM python:3.11-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install -r requirements.txt
COPY . .
CMD ["python", "server.py"]
```

### Production Checklist

- [ ] Use production ASGI server (Gunicorn + Uvicorn)
- [ ] Enable HTTPS/TLS
- [ ] Configure CORS for frontend domain
- [ ] Set up Gemma 4 12B model server
- [ ] Enable request logging
- [ ] Set up monitoring/alerts

## 🧠 Gemma 4 12B Integration

The system uses Gemma 4 12B for workflow evaluation. Set up via:

### Option 1: Ollama (Recommended)

```bash
ollama pull gemma:4-12b
ollama serve
```

Then update `server.py` to call `http://localhost:11434/api/generate`

### Option 2: Hugging Face

```bash
pip install transformers torch
```

Load model in `ModelEvaluator` class

### Option 3: API

Use Gemma 4 via Hugging Face API:
```python
from huggingface_hub import InferenceClient
client = InferenceClient(model="google/gemma-4-12b")
```

## 📊 Patterns & Templates

Each pattern includes:
- Pre-configured node layout
- Default parameter values
- Edge connections
- Documentation

Load patterns via UI or API:
```bash
POST /api/workflow/load-pattern
{
  "pattern": "text-classifier"
}
```

## 🔍 Monitoring & Debugging

### Server Logs
```bash
# Follow logs
tail -f server.log

# Enable debug mode
DEBUG=1 python server.py
```

### Client Console
Open browser DevTools for frontend logs and network requests.

### Health Check
```bash
curl http://localhost:8000/health
```

## 📚 References

- [ComfyUI Documentation](https://github.com/comfyorg/ComfyUI)
- [Gemma Model Card](https://huggingface.co/google/gemma-4)
- [FastAPI Docs](https://fastapi.tiangolo.com)

## 📄 License

This skill is part of the Claude Skills repository and follows the same license.

## 🤝 Contributing

To contribute improvements:

1. Make changes on a feature branch
2. Test in single-page context
3. Ensure llms.txt stays current
4. Document new node types
5. Submit PR with examples

---

**Status**: MVP Phase 1 Complete
**Next**: Gemma 4 12B integration, iteration controller, desktop apps
