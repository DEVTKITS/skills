// Design System UI - Application Logic

class DesignSystemApp {
    constructor() {
        this.workflow = {
            id: `workflow-${Date.now()}`,
            nodes: [],
            edges: [],
            parameters: {}
        };
        this.selectedNode = null;
        this.executionState = 'idle';
        this.initEventListeners();
        this.generateLlmsContent();
    }

    // === Event Listeners ===
    initEventListeners() {
        // Workflow controls
        document.getElementById('newWorkflow').addEventListener('click', () => this.newWorkflow());
        document.getElementById('executeWorkflow').addEventListener('click', () => this.executeWorkflow());
        document.getElementById('validateWorkflow').addEventListener('click', () => this.validateWorkflow());
        document.getElementById('iterateWorkflow').addEventListener('click', () => this.showIterationSuggestions());

        // llms.txt
        document.getElementById('llmsButton').addEventListener('click', () => this.showLlmsModal());
        document.getElementById('copyLlms').addEventListener('click', () => this.copyLlmsToClipboard());
        document.getElementById('downloadLlms').addEventListener('click', () => this.downloadLlms());

        // Terminal
        document.getElementById('clearTerminal').addEventListener('click', () => this.clearTerminal());

        // Tabs
        document.querySelectorAll('.tab-btn').forEach(btn => {
            btn.addEventListener('click', (e) => this.switchTab(e.target.dataset.tab));
        });

        // Node library drag & drop
        document.querySelectorAll('.node-item.draggable').forEach(item => {
            item.addEventListener('dragstart', (e) => this.onNodeDragStart(e));
        });

        // Canvas drop
        document.getElementById('canvas').addEventListener('dragover', (e) => {
            e.preventDefault();
            e.dataTransfer.dropEffect = 'copy';
            document.getElementById('canvas').classList.add('drag-over');
        });

        document.getElementById('canvas').addEventListener('dragleave', () => {
            document.getElementById('canvas').classList.remove('drag-over');
        });

        document.getElementById('canvas').addEventListener('drop', (e) => {
            e.preventDefault();
            document.getElementById('canvas').classList.remove('drag-over');
            this.onCanvasDrop(e);
        });

        // Pattern loading
        document.querySelectorAll('.pattern-item').forEach(item => {
            item.querySelector('.btn').addEventListener('click', () => {
                this.loadPattern(item.dataset.pattern);
            });
        });

        // Modals
        document.querySelectorAll('.modal-close').forEach(btn => {
            btn.addEventListener('click', (e) => {
                e.target.closest('.modal').classList.add('hidden');
            });
        });

        // Suggest parameters
        document.getElementById('suggestParams').addEventListener('click', () => this.suggestParameters());

        // Apply all iteration suggestions, then re-execute
        document.getElementById('applyAllSuggestions').addEventListener('click', () => this.applyAllSuggestions());

        // Import workflow
        document.getElementById('importWorkflow').addEventListener('click', () => this.importWorkflow());

        // Zoom controls
        document.getElementById('zoomIn').addEventListener('click', () => this.zoomCanvas(1.2));
        document.getElementById('zoomOut').addEventListener('click', () => this.zoomCanvas(0.8));
        document.getElementById('fitView').addEventListener('click', () => this.fitCanvasView());
    }

    // === Workflow Management ===
    newWorkflow() {
        this.workflow = {
            id: `workflow-${Date.now()}`,
            nodes: [],
            edges: [],
            parameters: {}
        };
        this.selectedNode = null;
        this.clearTerminal();
        this.redrawCanvas();
        this.logTerminal('Created new workflow');
    }

    validateWorkflow() {
        const errors = [];

        if (this.workflow.nodes.length === 0) {
            errors.push('❌ Workflow must have at least one node');
        }

        const hasInput = this.workflow.nodes.some(n => n.type.includes('input'));
        const hasOutput = this.workflow.nodes.some(n => n.type.includes('output'));

        if (!hasInput) errors.push('⚠️ No input node found');
        if (!hasOutput) errors.push('⚠️ No output node found');

        if (errors.length === 0) {
            this.logTerminal('✓ Workflow validation passed', 'success');
            this.updateBadge('Valid', 'success');
        } else {
            errors.forEach(e => this.logTerminal(e, 'error'));
            this.updateBadge('Invalid', 'error');
        }
    }

    // === Execution ===
    async executeWorkflow() {
        this.validateWorkflow();
        if (this.executionState === 'executing') return;

        this.executionState = 'executing';
        this.updateBadge('Running', 'warning');
        this.logTerminal('🚀 Starting workflow execution...\n');

        try {
            // Send workflow to backend
            const response = await fetch('/api/workflow/execute', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(this.workflow)
            });

            if (!response.ok) throw new Error(`Server error: ${response.status}`);

            const data = await response.json();
            this.logTerminal(data.output || 'Execution completed', 'success');
            this.logTerminal('\n✓ Workflow execution finished');

            // Show evaluation
            await this.evaluateResults(data);

        } catch (error) {
            this.logTerminal(`❌ Execution error: ${error.message}`, 'error');
        } finally {
            this.executionState = 'idle';
            this.updateBadge('Complete', 'success');
        }
    }

    async evaluateResults(execution) {
        const evalContainer = document.querySelector('.eval-container');
        evalContainer.classList.remove('hidden');

        this.logTerminal('\n🤖 Requesting Gemma 4 12B evaluation...');

        try {
            const response = await fetch('/api/workflow/evaluate', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    workflow: this.workflow,
                    results: execution.results || {}
                })
            });

            const evalData = await response.json();
            this.lastEvaluation = evalData;

            const verdictBadge = evalData.verdict === 'done'
                ? '<span style="color:var(--color-success)">✓ done</span>'
                : '<span style="color:var(--color-warning)">🔄 iterate</span>';
            const liveNote = evalData.gemma_live
                ? `live ${this.escapeHtml(evalData.model)}`
                : 'heuristic (start Ollama for live Gemma)';

            document.getElementById('evalResults').innerHTML = `
                <strong>Score:</strong> ${evalData.score}/100 &nbsp; ${verdictBadge}<br>
                <small class="text-muted">Evaluator: ${liveNote}</small><br>
                <strong>Feedback:</strong><br>
                ${evalData.feedback.split('\n').map(line =>
                    `<div>${this.escapeHtml(line)}</div>`
                ).join('')}
            `;

            document.getElementById('evalStatus').textContent = 'Complete';
            document.getElementById('iterateWorkflow').style.display =
                evalData.verdict === 'iterate' ? '' : 'none';

            this.logTerminal(`Evaluation: ${evalData.score}/100 → ${evalData.verdict}`,
                evalData.verdict === 'done' ? 'success' : 'warning');

        } catch (error) {
            this.logTerminal(`Evaluation error: ${error.message}`, 'error');
        }
    }

    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text ?? '';
        return div.innerHTML;
    }

    // === Parameter Suggestions ===
    async suggestParameters() {
        if (!this.selectedNode) {
            this.logTerminal('⚠️ Select a node to suggest parameters', 'warning');
            return;
        }

        this.logTerminal(`🎯 AI Suggesting parameters for ${this.selectedNode.type}...`);

        try {
            const response = await fetch('/api/workflow/suggest-params', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    nodeType: this.selectedNode.type,
                    workflow: this.workflow
                })
            });

            const suggestions = await response.json();
            this.applyParameterSuggestions(suggestions);

        } catch (error) {
            this.logTerminal(`Error: ${error.message}`, 'error');
        }
    }

    applyParameterSuggestions(suggestions) {
        if (this.selectedNode) {
            Object.assign(this.selectedNode.params, suggestions);
            this.renderParameterPanel();
            this.logTerminal('✓ Parameters suggested and applied', 'success');
        }
    }

    // === Pattern Templates ===
    loadPattern(pattern) {
        const patterns = {
            'text-classifier': {
                nodes: [
                    { id: 'input-1', type: 'text-input', x: 100, y: 100, params: {} },
                    { id: 'llm-1', type: 'llm-node', x: 300, y: 100, params: { model: 'gemma-4', prompt: 'Classify the following text' } },
                    { id: 'output-1', type: 'text-output', x: 500, y: 100, params: {} }
                ],
                edges: [
                    { from: 'input-1', to: 'llm-1' },
                    { from: 'llm-1', to: 'output-1' }
                ]
            },
            'rag-pipeline': {
                nodes: [
                    { id: 'query-1', type: 'text-input', x: 100, y: 100, params: {} },
                    { id: 'retrieve-1', type: 'llm-node', x: 300, y: 100, params: { model: 'retriever', prompt: 'Retrieve relevant docs' } },
                    { id: 'generate-1', type: 'llm-node', x: 500, y: 100, params: { model: 'gemma-4', prompt: 'Generate answer' } },
                    { id: 'output-1', type: 'text-output', x: 700, y: 100, params: {} }
                ],
                edges: [
                    { from: 'query-1', to: 'retrieve-1' },
                    { from: 'retrieve-1', to: 'generate-1' },
                    { from: 'generate-1', to: 'output-1' }
                ]
            },
            'agent-loop': {
                nodes: [
                    { id: 'task-1', type: 'text-input', x: 100, y: 100, params: {} },
                    { id: 'think-1', type: 'llm-node', x: 300, y: 100, params: { model: 'gemma-4', prompt: 'Think about next action' } },
                    { id: 'act-1', type: 'transform', x: 500, y: 100, params: {} },
                    { id: 'observe-1', type: 'transform', x: 700, y: 100, params: {} },
                    { id: 'output-1', type: 'text-output', x: 900, y: 100, params: {} }
                ],
                edges: [
                    { from: 'task-1', to: 'think-1' },
                    { from: 'think-1', to: 'act-1' },
                    { from: 'act-1', to: 'observe-1' },
                    { from: 'observe-1', to: 'output-1' }
                ]
            },
            'eval-loop': {
                nodes: [
                    { id: 'input-1', type: 'text-input', x: 100, y: 100, params: {} },
                    { id: 'execute-1', type: 'llm-node', x: 300, y: 100, params: { model: 'gemma-4', prompt: 'Execute task' } },
                    { id: 'score-1', type: 'transform', x: 500, y: 100, params: { script: 'calculate_score' } },
                    { id: 'output-1', type: 'text-output', x: 700, y: 100, params: {} }
                ],
                edges: [
                    { from: 'input-1', to: 'execute-1' },
                    { from: 'execute-1', to: 'score-1' },
                    { from: 'score-1', to: 'output-1' }
                ]
            }
        };

        if (patterns[pattern]) {
            this.workflow.nodes = patterns[pattern].nodes;
            this.workflow.edges = patterns[pattern].edges;
            this.redrawCanvas();
            this.logTerminal(`✓ Loaded pattern: ${pattern}`, 'success');
        }
    }

    // === Canvas & Visualization ===
    redrawCanvas() {
        const svg = document.getElementById('canvasSvg');
        svg.innerHTML = '';

        // Draw edges
        this.workflow.edges.forEach(edge => {
            const fromNode = this.workflow.nodes.find(n => n.id === edge.from);
            const toNode = this.workflow.nodes.find(n => n.id === edge.to);

            if (fromNode && toNode) {
                const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
                line.setAttribute('x1', fromNode.x + 50);
                line.setAttribute('y1', fromNode.y + 25);
                line.setAttribute('x2', toNode.x - 10);
                line.setAttribute('y2', toNode.y + 25);
                line.setAttribute('stroke', '#0ea5e9');
                line.setAttribute('stroke-width', '2');
                svg.appendChild(line);
            }
        });

        // Draw nodes
        this.workflow.nodes.forEach(node => {
            const group = document.createElementNS('http://www.w3.org/2000/svg', 'g');
            group.setAttribute('transform', `translate(${node.x},${node.y})`);
            group.style.cursor = 'move';

            const rect = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
            rect.setAttribute('width', '100');
            rect.setAttribute('height', '50');
            rect.setAttribute('fill', '#1a1f26');
            rect.setAttribute('stroke', '#0ea5e9');
            rect.setAttribute('stroke-width', '2');
            rect.setAttribute('rx', '4');
            group.appendChild(rect);

            const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
            text.setAttribute('x', '50');
            text.setAttribute('y', '28');
            text.setAttribute('text-anchor', 'middle');
            text.setAttribute('fill', '#e4e6eb');
            text.setAttribute('font-size', '12');
            text.textContent = node.type.replace('-', ' ');
            group.appendChild(text);

            group.addEventListener('click', (e) => this.onNodeClick(node, e));
            svg.appendChild(group);
        });
    }

    // Click selects; Shift+click connects the previously selected node to this one.
    onNodeClick(node, event) {
        if (event.shiftKey && this.selectedNode && this.selectedNode.id !== node.id) {
            const exists = this.workflow.edges.some(
                e => e.from === this.selectedNode.id && e.to === node.id);
            if (!exists) {
                this.workflow.edges.push({ from: this.selectedNode.id, to: node.id });
                this.logTerminal(`🔗 Connected ${this.selectedNode.id} → ${node.id}`, 'success');
                this.redrawCanvas();
            }
            return;
        }
        this.selectNode(node);
    }

    onNodeDragStart(e) {
        e.dataTransfer.effectAllowed = 'copy';
        e.dataTransfer.setData('nodeType', e.target.dataset.nodeType);
    }

    onCanvasDrop(e) {
        const nodeType = e.dataTransfer.getData('nodeType');
        const rect = document.getElementById('canvas').getBoundingClientRect();
        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;

        const newNode = {
            id: `${nodeType}-${Date.now()}`,
            type: nodeType,
            x: Math.max(0, x - 50),
            y: Math.max(0, y - 25),
            params: {}
        };

        this.workflow.nodes.push(newNode);
        this.redrawCanvas();
        this.logTerminal(`Added node: ${nodeType}`, 'success');
    }

    selectNode(node) {
        this.selectedNode = node;
        this.renderParameterPanel();
    }

    renderParameterPanel() {
        const panel = document.getElementById('selectedNodeParams');
        if (!this.selectedNode) {
            panel.innerHTML = '<p class="text-muted">Select a node to edit parameters</p>';
            return;
        }

        let html = `<h3>${this.selectedNode.type}</h3>`;
        html += `<div class="param-fields">`;

        // Default parameters based on node type
        const defaultParams = {
            'llm-node': { model: 'gemma-4', temperature: '0.7', maxTokens: '1000' },
            'text-input': { placeholder: 'Enter text' },
            'file-input': { fileType: 'any' }
        };

        const params = defaultParams[this.selectedNode.type] || {};

        Object.keys(params).forEach(key => {
            html += `
                <div class="param-field">
                    <label>${key}:</label>
                    <input type="text"
                           data-param="${key}"
                           value="${this.selectedNode.params[key] || params[key] || ''}"
                           onchange="window.app.updateNodeParam('${key}', this.value)">
                </div>
            `;
        });

        html += '</div>';
        panel.innerHTML = html;
    }

    updateNodeParam(key, value) {
        if (this.selectedNode) {
            this.selectedNode.params[key] = value;
        }
    }

    zoomCanvas(factor) {
        const canvas = document.getElementById('canvas');
        const currentZoom = parseFloat(canvas.style.zoom) || 1;
        canvas.style.zoom = (currentZoom * factor).toFixed(2);
    }

    fitCanvasView() {
        document.getElementById('canvas').style.zoom = '1';
    }

    // === Terminal ===
    logTerminal(message, type = 'normal') {
        const terminal = document.getElementById('terminal');
        const line = document.createElement('div');
        line.className = `terminal-line ${type}`;
        line.textContent = message;
        terminal.appendChild(line);
        terminal.scrollTop = terminal.scrollHeight;
    }

    clearTerminal() {
        document.getElementById('terminal').innerHTML = '<div class="terminal-line">Ready to execute...</div>';
    }

    // === llms.txt ===
    generateLlmsContent() {
        const llmsContent = `# Design System UI - llms.txt
# AI Agent Instruction Layer
# Generated: ${new Date().toISOString()}

## System Capabilities

- Workflow designer with ComfyUI integration
- Node-based visual programming
- Pattern templates for common tasks
- Terminal-based workflow execution
- Gemma 4 12B model evaluation
- Iterative optimization loop

## Current Workflow State

{
  "id": "${this.workflow.id}",
  "nodeCount": ${this.workflow.nodes.length},
  "edgeCount": ${this.workflow.edges.length},
  "status": "${this.executionState}"
}

## Available Actions (No Page Navigation Needed)

1. ADD_NODE(type, x, y)
   - Adds a node to the workflow canvas
   - Types: text-input, file-input, image-input, llm-node, transform, filter, text-output, file-output

2. CONNECT_NODES(fromId, toId)
   - Creates edge between two nodes

3. SET_PARAMETER(nodeId, key, value)
   - Sets a parameter on a specific node

4. EXECUTE_WORKFLOW()
   - Runs the entire workflow

5. EVALUATE_RESULTS()
   - Requests Gemma 4 12B evaluation

6. LOAD_PATTERN(name)
   - Loads pre-built workflow pattern
   - Available: text-classifier, rag-pipeline, agent-loop, eval-loop

7. ITERATE_WORKFLOW()
   - Shows optimization suggestions from previous evaluation

## Interaction Guide

This UI is designed for both human and AI agents:

- Humans click buttons and drag nodes
- Agents read this llms.txt and invoke actions via API endpoints
- All operations stay in single page context
- No navigation required between workflow design, execution, and evaluation

## Agent Manifest

When operating this system:
1. Analyze workflow structure and parameters
2. Suggest improvements based on execution results
3. Click through UI changes or call API endpoints
4. Maintain context within single page application
5. Report findings to user without page navigation

---
END llms.txt
`;
        this.llmsContent = llmsContent;
    }

    showLlmsModal() {
        const modal = document.getElementById('llmsModal');
        document.getElementById('llmsContent').textContent = this.llmsContent;
        modal.classList.remove('hidden');
    }

    copyLlmsToClipboard() {
        navigator.clipboard.writeText(this.llmsContent).then(() => {
            this.logTerminal('✓ llms.txt copied to clipboard', 'success');
        });
    }

    downloadLlms() {
        const blob = new Blob([this.llmsContent], { type: 'text/plain' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'llms.txt';
        a.click();
    }

    // === Tab Switching ===
    switchTab(tabName) {
        // Hide all tabs
        document.querySelectorAll('.tab-content').forEach(content => {
            content.classList.remove('active');
        });
        document.querySelectorAll('.tab-btn').forEach(btn => {
            btn.classList.remove('active');
        });

        // Show selected tab
        const element = document.getElementById(tabName);
        if (element) {
            element.classList.add('active');
            document.querySelector(`[data-tab="${tabName}"]`).classList.add('active');
        }
    }

    // === UI Updates ===
    updateBadge(text, type) {
        const badge = document.getElementById('workflowStatus');
        badge.textContent = text;
        badge.style.background = type === 'success' ? '#10b981' : type === 'error' ? '#ef4444' : '#f59e0b';
    }

    // === Import/Export ===
    async importWorkflow() {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.json';
        input.addEventListener('change', (e) => {
            const file = e.target.files[0];
            const reader = new FileReader();
            reader.onload = (event) => {
                try {
                    this.workflow = JSON.parse(event.target.result);
                    this.redrawCanvas();
                    this.logTerminal('✓ Workflow imported', 'success');
                } catch (err) {
                    this.logTerminal('❌ Invalid JSON file', 'error');
                }
            };
            reader.readAsText(file);
        });
        input.click();
    }

    showIterationSuggestions() {
        const modal = document.getElementById('iterationModal');
        const list = document.getElementById('suggestionsList');
        const suggestions = this.lastEvaluation?.suggestions || [];

        if (suggestions.length === 0) {
            list.innerHTML = '<p class="text-muted">No suggestions — run an evaluation first, or the workflow already scored "done".</p>';
        } else {
            list.innerHTML = suggestions.map((s, i) => `
                <div class="suggestion-item">
                    <h4>${this.escapeHtml(s.title)}</h4>
                    <p>${this.escapeHtml(s.description)}</p>
                    <p class="text-muted"><code>${this.escapeHtml(s.action)}</code></p>
                    <button class="btn btn-small" onclick="window.app.applySuggestion(${i})">Apply</button>
                </div>
            `).join('');
        }
        modal.classList.remove('hidden');
    }

    applySuggestion(index) {
        const s = this.lastEvaluation?.suggestions?.[index];
        if (!s) return;

        if (s.action.startsWith('ADD_NODE')) {
            // Place the suggested node after the last node on the canvas.
            const last = this.workflow.nodes[this.workflow.nodes.length - 1];
            this.workflow.nodes.push({
                id: `llm-node-${Date.now()}`,
                type: 'llm-node',
                x: (last?.x ?? 50) + 150,
                y: last?.y ?? 100,
                params: { model: 'gemma-4', temperature: 0.7 }
            });
        } else if (s.action.startsWith('CONNECT_NODES')) {
            // Connect the first node pair that isn't connected yet, in order.
            const connected = new Set(this.workflow.edges.map(e => `${e.from}->${e.to}`));
            for (let i = 0; i < this.workflow.nodes.length - 1; i++) {
                const from = this.workflow.nodes[i].id;
                const to = this.workflow.nodes[i + 1].id;
                if (!connected.has(`${from}->${to}`)) {
                    this.workflow.edges.push({ from, to });
                    break;
                }
            }
        }

        this.redrawCanvas();
        this.logTerminal(`✓ Applied: ${s.title}`, 'success');
    }

    applyAllSuggestions() {
        const count = this.lastEvaluation?.suggestions?.length || 0;
        for (let i = 0; i < count; i++) this.applySuggestion(i);
        document.getElementById('iterationModal').classList.add('hidden');
        if (count > 0) this.logTerminal('🔄 Re-executing after applying suggestions...');
        if (count > 0) this.executeWorkflow();
    }
}

// Initialize app when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.app = new DesignSystemApp();
    console.log('Design System UI initialized');
});
