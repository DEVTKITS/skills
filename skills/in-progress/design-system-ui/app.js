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
        this.lastActivitySeq = 0;
        this.initEventListeners();
        this.generateLlmsContent();
        this.initChat();
        this.startActivityPolling();
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

    // Baut das Parameterformular über DOM-APIs statt über HTML-Strings:
    // Node-Typen und Parameterwerte stammen aus importierten Workflow-JSONs
    // und dürfen deshalb nie als Markup interpretiert werden.
    renderParameterPanel() {
        const panel = document.getElementById('selectedNodeParams');
        panel.replaceChildren();

        if (!this.selectedNode) {
            const hint = document.createElement('p');
            hint.className = 'text-muted';
            hint.textContent = 'Node im Canvas auswählen';
            panel.appendChild(hint);
            return;
        }

        const heading = document.createElement('h3');
        heading.textContent = this.selectedNode.type;
        panel.appendChild(heading);

        const defaultParams = {
            'llm-node': { model: 'gemma-4', temperature: '0.7', maxTokens: '1000' },
            'text-input': { placeholder: 'Enter text' },
            'file-input': { fileType: 'any' }
        };
        const params = defaultParams[this.selectedNode.type] || {};

        const fields = document.createElement('div');
        fields.className = 'param-fields';

        Object.keys(params).forEach(key => {
            const wrap = document.createElement('div');
            wrap.className = 'param-field';

            const label = document.createElement('label');
            label.textContent = `${key}:`;

            const input = document.createElement('input');
            input.type = 'text';
            input.dataset.param = key;
            input.value = this.selectedNode.params[key] ?? params[key] ?? '';
            input.addEventListener('change', () => this.updateNodeParam(key, input.value));

            wrap.append(label, input);
            fields.appendChild(wrap);
        });

        panel.appendChild(fields);
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

// === Agent Activity: red glow + element flash =========================
// Open protocol: any agent (Playwright, terminal, another LLM) can make its
// work visible either by POSTing to /api/agent/activity or, when running
// in-page, by calling window.agentActivity directly.
DesignSystemApp.prototype.startActivityPolling = function () {
    const poll = async () => {
        try {
            const res = await fetch(`/api/agent/activity?since=${this.lastActivitySeq}`);
            const data = await res.json();
            document.body.classList.toggle('agent-active', !!data.active);
            for (const ev of data.events || []) {
                this.lastActivitySeq = Math.max(this.lastActivitySeq, ev.seq);
                this.renderActivityEvent(ev);
            }
        } catch (_) { /* backend briefly away — keep polling */ }
        setTimeout(poll, 1000);
    };
    poll();
};

DesignSystemApp.prototype.renderActivityEvent = function (ev) {
    const text = `${ev.actor} · ${ev.action}${ev.target ? ' → ' + ev.target : ''}` +
                 (ev.detail ? ` (${ev.detail})` : '');
    const bar = document.getElementById('activityText');
    if (bar) bar.textContent = text;

    // Verlauf in der Agenten-Ansicht mitschreiben, damit nachvollziehbar
    // bleibt, was eine KI getan hat — nicht nur der letzte Schritt.
    const logEl = document.getElementById('agentLog');
    if (logEl) {
        if (!this.agentLogLines) this.agentLogLines = [];
        const stamp = (ev.timestamp || '').slice(11, 19);
        this.agentLogLines.push(`${stamp}  ${text}`);
        this.agentLogLines = this.agentLogLines.slice(-100);
        logEl.textContent = this.agentLogLines.join('\n');
        logEl.scrollTop = logEl.scrollHeight;
    }

    if (ev.target) this.flashElement(ev.target);
    if (ev.state === 'error') this.logTerminal(`⚠️ ${text}`, 'error');
};

DesignSystemApp.prototype.flashElement = function (selector) {
    let el = null;
    try { el = document.querySelector(selector); } catch (_) { /* not a selector */ }
    if (!el) {
        // Maybe it names a workflow node id — flash the canvas instead.
        if (this.workflow.nodes.some(n => n.id === selector)) {
            el = document.getElementById('canvas');
        }
    }
    if (el) {
        el.classList.remove('agent-flash');
        void el.offsetWidth; // restart the animation
        el.classList.add('agent-flash');
        setTimeout(() => el.classList.remove('agent-flash'), 2000);
    }
};

// In-page hook for agents running inside the browser context.
window.agentActivity = {
    async post(action, target = '', detail = '', state = 'working', actor = 'in-page-agent') {
        try {
            await fetch('/api/agent/activity', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ actor, action, target, detail, state })
            });
        } catch (_) { /* fire and forget */ }
    },
    start(detail = '') { return this.post('session-start', '', detail); },
    stop(detail = '') { return this.post('session-end', '', detail, 'done'); },
    click(target, detail = '') { return this.post('click', target, detail); }
};

// === Chat tab ==========================================================
DesignSystemApp.prototype.initChat = function () {
    this.chatHistory = [];
    const send = () => this.sendChat();
    document.getElementById('chatSend').addEventListener('click', send);
    document.getElementById('chatInput').addEventListener('keydown', (e) => {
        if (e.key === 'Enter') send();
    });
};

DesignSystemApp.prototype.sendChat = async function () {
    const input = document.getElementById('chatInput');
    const message = input.value.trim();
    if (!message) return;
    input.value = '';
    this.appendChat('user', message);

    const provider = document.getElementById('chatProvider')?.value || 'auto';

    try {
        const res = await fetch('/api/chat', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ message, history: this.chatHistory, provider })
        });
        const data = await res.json();
        const suffix = data.live ? '' : '\n\n[offline — `ollama serve` starten oder Provider wählen]';
        this.appendChat('assistant', (data.reply || '(keine Antwort)') + suffix);
        this.chatHistory.push({ role: 'user', content: message },
                              { role: 'assistant', content: data.reply || '' });
        if (data.action) this.handleChatAction(data.action);
    } catch (err) {
        this.appendChat('assistant', `Fehler: ${err.message}`);
    }
};

// Präfix-Syntax aus dem Chat in echte Builder-Aktionen übersetzen
DesignSystemApp.prototype.handleChatAction = function (action) {
    if (action.type === 'load_pattern') {
        this.loadPattern(action.pattern);
        this.mirrorCanvas();
    } else if (action.type === 'command') {
        const map = {
            execute: () => this.executeWorkflow(),
            validate: () => this.validateWorkflow(),
            clear: () => this.newWorkflow(),
            iterate: () => this.showIterationSuggestions(),
            selftest: () => this.showView('debug')
        };
        (map[action.command] || (() => {}))();
    } else if (action.type === 'stock_results') {
        this.appendChat('assistant', 'Ergebnisse siehe Oberfläche „Stock Medien".');
    }
};

DesignSystemApp.prototype.appendChat = function (role, content) {
    const log = document.getElementById('chatLog');
    const div = document.createElement('div');
    div.className = `chat-msg ${role}`;
    div.textContent = content;
    log.appendChild(div);
    log.scrollTop = log.scrollHeight;
};

// ══════════════════════════════════════════════════════════════════════
// Hamburger-Menü & Oberflächen-Routing
// ══════════════════════════════════════════════════════════════════════
const VIEW_NAMES = {
    builder: 'Workflow Builder', chat: 'Chat & Canvas', studio: 'Content Studio', svg: 'SVG Studio', templates: 'Vorlagen & Artefakte', systems: 'Design-Systeme',
    settings: 'Einstellungen', skills: 'Skill Builder',
    providers: 'Provider & APIs', stock: 'Stock Medien', github: 'GitHub & Skills',
    agents: 'Agenten & Sichtbarkeit', debug: 'Debug & Stresstest', docs: 'Docs'
};

DesignSystemApp.prototype.initShell = function () {
    const open = () => document.body.classList.add('drawer-open');
    const close = () => document.body.classList.remove('drawer-open');

    document.getElementById('hamburger').addEventListener('click', () =>
        document.body.classList.toggle('drawer-open'));
    document.getElementById('drawerClose').addEventListener('click', close);
    document.getElementById('drawerScrim').addEventListener('click', close);
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') { close(); this.closeInfo(); }
    });

    document.querySelectorAll('.drawer-item').forEach(item => {
        item.addEventListener('click', () => {
            this.showView(item.dataset.view);
            close();
        });
    });
};

DesignSystemApp.prototype.showView = function (view) {
    document.querySelectorAll('.view').forEach(v => v.classList.add('hidden'));
    const el = document.getElementById(`view-${view}`);
    if (el) el.classList.remove('hidden');
    document.querySelectorAll('.drawer-item').forEach(i =>
        i.classList.toggle('active', i.dataset.view === view));
    document.getElementById('viewName').textContent = VIEW_NAMES[view] || view;
    this.currentView = view;

    // Lazy-load pro Oberfläche
    if (view === 'providers') this.loadProviders();
    if (view === 'github') this.loadRepo();
    if (view === 'agents') {
        this.loadIntegrations();
        if (this.edition?.features?.openclaw) this.refreshOpenClawStatus();
    }
    if (view === 'docs') this.loadDocs();
    if (view === 'chat') this.mirrorCanvas();
    if (view === 'svg' && window.SvgStudio) window.SvgStudio.show(this);
    if (view === 'templates' && window.Artifacts) window.Artifacts.show(this);
    if (view === 'systems' && window.DesignSystems) window.DesignSystems.show(this);
};

// ══════════════════════════════════════════════════════════════════════
// Info-System: blinkende Buttons mit wechselnden Tipps
// ══════════════════════════════════════════════════════════════════════
DesignSystemApp.prototype.initInfoSystem = async function () {
    this.helpData = { topics: {}, prefixes: {} };
    try {
        const res = await fetch('/api/help');
        this.helpData = await res.json();
    } catch (_) { /* Offline: Popovers zeigen dann einen Hinweis */ }

    this.infoTopic = null;
    this.infoTipIndex = 0;

    document.body.addEventListener('click', (e) => {
        const btn = e.target.closest('.info-btn');
        if (btn) { e.stopPropagation(); this.openInfo(btn); return; }
        if (!e.target.closest('.info-popover')) this.closeInfo();
    });

    document.querySelector('.info-close').addEventListener('click', () => this.closeInfo());
    document.getElementById('infoNextTip').addEventListener('click', () => this.cycleTip(1));
    document.getElementById('infoPrevTip').addEventListener('click', () => this.cycleTip(-1));

    this.renderPrefixes();
};

DesignSystemApp.prototype.openInfo = function (btn) {
    const key = btn.dataset.help;
    const topic = (this.helpData.topics || {})[key];
    const pop = document.getElementById('infoPopover');

    document.querySelectorAll('.info-btn.open').forEach(b => b.classList.remove('open'));
    btn.classList.add('open');

    if (!topic) {
        document.getElementById('infoTitle').textContent = 'Hilfe';
        document.getElementById('infoGuide').textContent =
            'Für dieses Element liegt noch kein Hilfetext vor.';
        document.getElementById('infoTip').textContent = '';
        document.getElementById('infoPatterns').innerHTML = '';
        document.getElementById('infoTipCount').textContent = '';
    } else {
        this.infoTopic = topic;
        this.infoTipIndex = 0;
        document.getElementById('infoTitle').textContent = topic.title;
        document.getElementById('infoGuide').textContent = topic.guide;
        document.getElementById('infoPatterns').innerHTML =
            (topic.patterns || []).map(p => `<code>${this.escapeHtml(p)}</code>`).join('');
        this.renderTip();
        // Tipps wechseln automatisch weiter, solange das Popover offen ist
        clearInterval(this.infoTimer);
        this.infoTimer = setInterval(() => this.cycleTip(1), 4500);
    }

    // Popover neben dem Button positionieren, im Viewport halten
    pop.classList.remove('hidden');
    const r = btn.getBoundingClientRect();
    const pr = pop.getBoundingClientRect();
    let left = Math.min(r.left, window.innerWidth - pr.width - 12);
    let top = r.bottom + 8;
    if (top + pr.height > window.innerHeight - 8) top = Math.max(8, r.top - pr.height - 8);
    pop.style.left = `${Math.max(8, left)}px`;
    pop.style.top = `${top}px`;
};

DesignSystemApp.prototype.renderTip = function () {
    const tips = this.infoTopic?.tips || [];
    if (!tips.length) return;
    const el = document.getElementById('infoTip');
    el.textContent = tips[this.infoTipIndex % tips.length];
    el.style.animation = 'none';
    void el.offsetWidth;
    el.style.animation = '';
    document.getElementById('infoTipCount').textContent =
        `${(this.infoTipIndex % tips.length) + 1}/${tips.length}`;
};

DesignSystemApp.prototype.cycleTip = function (delta) {
    const tips = this.infoTopic?.tips || [];
    if (!tips.length) return;
    this.infoTipIndex = (this.infoTipIndex + delta + tips.length) % tips.length;
    this.renderTip();
};

DesignSystemApp.prototype.closeInfo = function () {
    clearInterval(this.infoTimer);
    document.getElementById('infoPopover').classList.add('hidden');
    document.querySelectorAll('.info-btn.open').forEach(b => b.classList.remove('open'));
};

DesignSystemApp.prototype.renderPrefixes = function () {
    const el = document.getElementById('prefixList');
    if (!el) return;
    const px = this.helpData.prefixes || {};
    el.innerHTML = Object.entries(px).map(([sym, p]) => `
        <div class="prefix-item">
            <code>${this.escapeHtml(sym)}</code> <strong>${this.escapeHtml(p.name)}</strong><br>
            <span class="text-muted">${this.escapeHtml(p.desc)}</span><br>
            <code>${this.escapeHtml(p.example)}</code>
        </div>`).join('') || '<p class="text-muted">Keine Präfixe geladen.</p>';
};

// ══════════════════════════════════════════════════════════════════════
// TTS: 1× Klick markiert, 2× Klick liest vor
// ══════════════════════════════════════════════════════════════════════
DesignSystemApp.prototype.initTTS = function () {
    this.ttsEnabled = true;
    const toggle = document.getElementById('ttsToggle');
    toggle.addEventListener('change', () => {
        this.ttsEnabled = toggle.checked;
        if (!this.ttsEnabled) this.stopSpeaking();
    });

    const SELECTOR = 'p, li, h1, h2, h3, h4, .chat-msg, .terminal-line, .eval-results, .info-guide, .info-tip, .code-block, .prefix-item, .provider-card';

    document.body.addEventListener('click', (e) => {
        if (!this.ttsEnabled) return;
        if (e.target.closest('button, a, input, textarea, select, .drawer-item')) return;
        const block = e.target.closest(SELECTOR);
        if (!block) return;
        document.querySelectorAll('.tts-selected').forEach(el => el.classList.remove('tts-selected'));
        block.classList.add('tts-selected');
        this.ttsBlock = block;
    });

    document.body.addEventListener('dblclick', (e) => {
        if (!this.ttsEnabled) return;
        const block = e.target.closest(SELECTOR);
        if (!block) return;
        e.preventDefault();
        if (this.speakingBlock === block) { this.stopSpeaking(); return; }
        this.speak(block);
    });
};

DesignSystemApp.prototype.speak = function (block) {
    if (!('speechSynthesis' in window)) {
        this.logTerminal('⚠️ Browser unterstützt keine Sprachausgabe', 'warning');
        return;
    }
    this.stopSpeaking();
    const text = (block.innerText || block.textContent || '').trim();
    if (!text) return;

    const u = new SpeechSynthesisUtterance(text.slice(0, 3000));
    u.lang = /[äöüßÄÖÜ]|\b(der|die|das|und|nicht|wird|Klick)\b/.test(text) ? 'de-DE' : 'en-US';
    u.onend = () => { block.classList.remove('tts-speaking'); this.speakingBlock = null; };
    u.onerror = () => { block.classList.remove('tts-speaking'); this.speakingBlock = null; };

    block.classList.add('tts-speaking');
    this.speakingBlock = block;
    window.speechSynthesis.speak(u);
};

DesignSystemApp.prototype.stopSpeaking = function () {
    if ('speechSynthesis' in window) window.speechSynthesis.cancel();
    document.querySelectorAll('.tts-speaking').forEach(el => el.classList.remove('tts-speaking'));
    this.speakingBlock = null;
};

// ══════════════════════════════════════════════════════════════════════
// Provider, Stock, GitHub, Debug, Skills, Docs
// ══════════════════════════════════════════════════════════════════════
DesignSystemApp.prototype.loadProviders = async function () {
    const grid = document.getElementById('providerGrid');
    try {
        const res = await fetch('/api/providers');
        const { providers, summary } = await res.json();
        this.setAmpel(summary);

        grid.innerHTML = Object.values(providers).map(p => `
            <div class="provider-card">
                <header>
                    <span class="ampel" data-state="${p.ampel}"></span>
                    <h4>${this.escapeHtml(p.name)}</h4>
                    <span class="provider-kind">${this.escapeHtml(p.kind)}</span>
                </header>
                <p>${this.escapeHtml(p.note || '')}</p>
                <p><strong>${this.escapeHtml(p.state)}</strong></p>
                <p><code>${this.escapeHtml(p.connect_hint)}</code></p>
                ${p.docs ? `<p class="text-muted">${this.escapeHtml(p.docs)}</p>` : ''}
            </div>`).join('');

        // Chat-Provider-Auswahl mit den LLM-fähigen Anbietern füllen
        const sel = document.getElementById('chatProvider');
        if (sel) {
            const llm = Object.values(providers).filter(p => ['llm', 'local'].includes(p.kind));
            sel.innerHTML = '<option value="auto">auto (lokal)</option>' +
                llm.map(p => `<option value="${p.id}">${this.escapeHtml(p.name)}` +
                             `${p.ampel === 'green' ? ' ✓' : ' ✗'}</option>`).join('');
        }
    } catch (err) {
        grid.innerHTML = `<p class="text-muted">Fehler: ${this.escapeHtml(err.message)}</p>`;
    }
};

DesignSystemApp.prototype.setAmpel = function (summary) {
    const state = summary.connected > 0 ? 'green' : 'red';
    const text = `${summary.connected}/${summary.total}`;
    for (const id of ['headerAmpel', 'drawerAmpel']) {
        const el = document.getElementById(id);
        if (el) el.dataset.state = state;
    }
    const h = document.getElementById('headerAmpelText');
    if (h) h.textContent = text;
    const d = document.getElementById('drawerAmpelText');
    if (d) d.textContent = `${text} Provider verbunden`;
};

DesignSystemApp.prototype.initStock = function () {
    document.getElementById('stockSearch').addEventListener('click', async () => {
        const out = document.getElementById('stockResults');
        const provider = document.getElementById('stockProvider').value;
        const media = document.getElementById('stockMedia').value;
        const query = document.getElementById('stockQuery').value.trim();
        if (!query) { out.textContent = 'Bitte einen Suchbegriff eingeben.'; return; }
        out.textContent = 'Suche…';
        try {
            const res = await fetch('/api/stock/search', {
                method: 'POST', headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ provider, query, media, per_page: 12 })
            });
            const data = await res.json();
            if (!data.ok) {
                out.innerHTML = `<p class="text-muted">${this.escapeHtml(data.error || 'Fehler')}` +
                    `${data.connect ? '<br><code>' + this.escapeHtml(data.connect) + '</code>' : ''}</p>`;
                return;
            }
            const items = data.data.photos || data.data.results || data.data.hits ||
                          data.data.videos || [];
            out.innerHTML = items.length
                ? items.map(it => {
                    const url = it.src?.medium || it.urls?.small || it.webformatURL ||
                                it.image || it.preview || '';
                    return url ? `<img src="${this.escapeHtml(url)}" alt="" loading="lazy">` : '';
                  }).join('')
                : '<p class="text-muted">Keine Treffer.</p>';
        } catch (err) {
            out.textContent = `Fehler: ${err.message}`;
        }
    });
};

DesignSystemApp.prototype.loadRepo = async function () {
    try {
        const st = await (await fetch('/api/github/status')).json();
        document.getElementById('repoStatus').textContent =
            `Branch:  ${st.branch}\nCommit:  ${st.last_commit}\nRemote:  ${st.remote}\n` +
            `Geändert: ${st.dirty_files.length ? '\n  ' + st.dirty_files.join('\n  ') : 'nichts'}`;

        const sk = await (await fetch('/api/github/skills')).json();
        document.getElementById('repoSkills').innerHTML = sk.skills.map(s => `
            <div class="skill-chip" data-ref="${this.escapeHtml(s.ref)}">
                <strong>${this.escapeHtml(s.name)}</strong>
                <span class="text-muted">(${this.escapeHtml(s.bucket)})</span><br>
                <span class="text-muted">${this.escapeHtml((s.description || '').slice(0, 90))}</span>
            </div>`).join('') || '<p class="text-muted">Keine Skills gefunden.</p>';

        document.querySelectorAll('.skill-chip').forEach(chip => {
            chip.addEventListener('click', () => {
                navigator.clipboard?.writeText(chip.dataset.ref);
                this.logTerminal(`📋 ${chip.dataset.ref} kopiert`, 'success');
            });
        });
    } catch (err) {
        document.getElementById('repoStatus').textContent = `Fehler: ${err.message}`;
    }
};

DesignSystemApp.prototype.initDebug = function () {
    const out = document.getElementById('debugOutput');
    document.getElementById('runSelftest').addEventListener('click', async () => {
        out.textContent = 'Selbsttest läuft…';
        const d = await (await fetch('/api/debug/selftest')).json();
        out.textContent = `Ampel: ${d.ampel}   bestanden ${d.passed}, fehlgeschlagen ${d.failed}\n\n` +
            d.checks.map(c => `${c.ok ? '✓' : '✗'} ${c.name}${c.error ? ' — ' + c.error : ''}`).join('\n');
    });
    document.getElementById('runStress').addEventListener('click', async () => {
        const runs = parseInt(document.getElementById('stressRuns').value, 10) || 50;
        out.textContent = `Stresstest mit ${runs} Läufen…`;
        const d = await (await fetch('/api/debug/stress', {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ runs })
        })).json();
        out.textContent = `Ampel: ${d.ampel}\nLäufe: ${d.runs}   Fehler: ${d.errors}\n` +
            `min ${d.ms.min} ms · avg ${d.ms.avg} ms · p95 ${d.ms.p95} ms · max ${d.ms.max} ms\n` +
            `Durchsatz: ${d.throughput_per_s}/s`;
    });
};

DesignSystemApp.prototype.initSkillBuilder = function () {
    const build = () => {
        const name = document.getElementById('skillName').value.trim() || 'unnamed-skill';
        const desc = document.getElementById('skillDesc').value.trim() ||
                     'Beschreibung fehlt — Agenten wählen den Skill danach aus.';
        const trig = document.getElementById('skillTriggers').value.trim();
        return `---\nname: ${name}\ndescription: ${desc}\n---\n\n# ${name}\n\n` +
               `${desc}\n\n## Trigger\n${trig || '(keine angegeben)'}\n\n` +
               `## Workflow\n\`\`\`json\n${JSON.stringify(this.workflow, null, 2)}\n\`\`\`\n`;
    };
    document.getElementById('skillPreview').addEventListener('click', () => {
        document.getElementById('skillOutput').textContent = build();
    });
    document.getElementById('skillExport').addEventListener('click', () => {
        const md = build();
        document.getElementById('skillOutput').textContent = md;
        const blob = new Blob([md], { type: 'text/markdown' });
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = `${document.getElementById('skillName').value.trim() || 'skill'}-SKILL.md`;
        a.click();
    });
};

DesignSystemApp.prototype.loadIntegrations = async function () {
    try {
        const { integrations } = await (await fetch('/api/integrations')).json();
        document.getElementById('integrationList').innerHTML =
            Object.entries(integrations).map(([bucket, items]) => `
                <h4>${this.escapeHtml(bucket)}</h4>
                <ul>${Object.entries(items).map(([k, v]) =>
                    `<li><strong>${this.escapeHtml(k)}</strong>: ${this.escapeHtml(v)}</li>`
                ).join('')}</ul>`).join('');
    } catch (_) { /* ignore */ }

    document.getElementById('agentDemo').addEventListener('click', async () => {
        const steps = [['#executeWorkflow', 'Button anvisieren'],
                       ['#canvas', 'Canvas prüfen'],
                       ['#terminal', 'Terminal lesen']];
        await window.agentActivity.start('UI-Sichtbarkeitstest');
        for (const [target, detail] of steps) {
            await window.agentActivity.click(target, detail);
            await new Promise(r => setTimeout(r, 1400));
        }
        await window.agentActivity.stop('Test beendet');
    }, { once: true });
};

// ── Editionen: "normal" (Hamburger + Kacheln) und "openclaw" (plus Shell) ──
// Bereiche mit data-edition="<id>" erscheinen nur in der passenden Edition.
DesignSystemApp.prototype.applyEdition = async function () {
    let edition = { id: 'normal', name: 'Normal', features: { openclaw: false } };
    try {
        const data = await (await fetch('/api/edition')).json();
        edition = data.edition || edition;
    } catch (_) { /* Offline: bei "normal" bleiben */ }

    this.edition = edition;
    document.body.dataset.edition = edition.id;

    document.querySelectorAll('[data-edition]').forEach(el => {
        const wanted = el.dataset.edition;
        el.classList.toggle('hidden', wanted !== edition.id);
    });

    const chip = document.getElementById('editionChip');
    if (chip) {
        chip.textContent = edition.name;
        chip.title = edition.tagline || edition.name;
        chip.dataset.edition = edition.id;
    }
    return edition;
};

// ── OpenCLAW: Agenten-Shell aus der Oberfläche heraus starten ──────────
// Schreibweise: OpenCLAW. Nicht "OpenCloud"/"opencloude", nicht Claude.
DesignSystemApp.prototype.initOpenClaw = function () {
    const btn = document.getElementById('clawRun');
    if (!btn) return;

    btn.addEventListener('click', async () => {
        const task = document.getElementById('clawTask').value.trim();
        const pattern = document.getElementById('clawPattern').value || null;
        if (!task && !pattern) {
            document.getElementById('clawOutput').textContent =
                'Bitte einen Auftrag beschreiben oder ein Muster wählen.';
            return;
        }
        btn.disabled = true;
        document.getElementById('clawOutput').textContent = 'OpenCLAW wird gestartet…';

        try {
            const res = await fetch('/api/openclaw/run', {
                method: 'POST', headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ task, pattern })
            });
            const data = await res.json();
            if (!data.ok) {
                document.getElementById('clawOutput').textContent =
                    `Nicht gestartet: ${data.error || 'unbekannter Fehler'}`;
                btn.disabled = false;
                return;
            }
            this.pollOpenClaw();
        } catch (err) {
            document.getElementById('clawOutput').textContent = `Fehler: ${err.message}`;
            btn.disabled = false;
        }
    });

    this.refreshOpenClawStatus();
};

DesignSystemApp.prototype.refreshOpenClawStatus = async function () {
    try {
        const s = await (await fetch('/api/openclaw/status')).json();
        const ampel = document.getElementById('clawAmpel');
        const text = document.getElementById('clawStatusText');
        if (!ampel || !text) return s;

        if (s.running) {
            ampel.dataset.state = 'yellow';
            text.textContent = `läuft: ${s.task || '(ohne Beschreibung)'}`;
        } else if (s.shell_available) {
            ampel.dataset.state = 'green';
            text.textContent = `Shell bereit: ${s.shell_cmd}`;
        } else {
            ampel.dataset.state = 'yellow';
            text.textContent = s.shell_configured
                ? `OPENCLAW_CMD gesetzt, aber Programm fehlt — Fallback-Modus`
                : 'keine Shell installiert — Runner fährt die Schleife selbst (Fallback)';
        }
        return s;
    } catch (_) { return null; }
};

DesignSystemApp.prototype.pollOpenClaw = function () {
    const out = document.getElementById('clawOutput');
    const btn = document.getElementById('clawRun');
    const tick = async () => {
        const s = await this.refreshOpenClawStatus();
        if (!s) { setTimeout(tick, 1500); return; }

        if (s.running) {
            out.textContent = (s.log || []).join('\n') || 'OpenCLAW arbeitet…';
            setTimeout(tick, 1200);
            return;
        }
        const r = s.result || {};
        const rounds = (r.rounds || []).map(x =>
            `  Runde ${x.round}: ${x.score}/100 → ${x.verdict}` +
            `${x.gemma_live ? ' (live Gemma)' : ' (Heuristik)'}`).join('\n');
        out.textContent =
            `Modus: ${r.mode || '?'}\n` +
            (r.pattern ? `Muster: ${r.pattern}\n` : '') +
            (r.verdict ? `Urteil: ${r.verdict}` +
                (r.final_score != null ? `  (${r.final_score}/100)` : '') + '\n' : '') +
            (r.error ? `Fehler: ${r.error}\n` : '') +
            (rounds ? `\n${rounds}\n` : '') +
            `\n— Protokoll —\n${(s.log || []).join('\n')}`;
        btn.disabled = false;
    };
    tick();
};

DesignSystemApp.prototype.loadDocs = async function () {
    try {
        const txt = await (await fetch('/llms.txt')).text();
        document.getElementById('docsContent').textContent = txt;
    } catch (err) {
        document.getElementById('docsContent').textContent = `Fehler: ${err.message}`;
    }
};

// Canvas-Spiegel in der Split-Ansicht — echte Knoten klonen statt Markup
// zu serialisieren und neu zu parsen.
DesignSystemApp.prototype.mirrorCanvas = function () {
    const src = document.getElementById('canvasSvg');
    const dst = document.getElementById('canvasMirrorSvg');
    if (!src || !dst) return;
    dst.replaceChildren(...Array.from(src.childNodes, n => n.cloneNode(true)));
};

// ══════════════════════════════════════════════════════════════════════
// Bootstrap
// ══════════════════════════════════════════════════════════════════════
document.addEventListener('DOMContentLoaded', () => {
    const app = new DesignSystemApp();
    window.app = app;

    app.initShell();
    app.initInfoSystem();
    app.initTTS();
    app.initStock();
    app.initDebug();
    app.initSkillBuilder();
    app.initStudio();
    app.initSettings();
    app.initSkillImport();
    app.loadProviders();
    app.applyEdition().then(ed => {
        if (ed.features && ed.features.openclaw) app.initOpenClaw();
    });

    document.getElementById('refreshProviders')?.addEventListener('click', () => app.loadProviders());
    document.getElementById('refreshRepo')?.addEventListener('click', () => app.loadRepo());
    document.getElementById('chatExecute')?.addEventListener('click', () => app.executeWorkflow());

    // Canvas-Spiegel aktuell halten
    const origRedraw = app.redrawCanvas.bind(app);
    app.redrawCanvas = function () { origRedraw(); app.mirrorCanvas(); };

    app.showView('builder');
    console.log('Design System UI initialised');
});

// ══════════════════════════════════════════════════════════════════════
// Content Studio — Podcast/Bericht erzeugen und einbetten
// ══════════════════════════════════════════════════════════════════════
DesignSystemApp.prototype.initStudio = async function () {
    const make = document.getElementById('studioMake');
    if (!make) return;

    try {
        const { formats } = await (await fetch('/api/content/formats')).json();
        document.getElementById('studioFormat').innerHTML =
            Object.entries(formats).map(([k, f]) =>
                `<option value="${this.escapeHtml(k)}">${this.escapeHtml(f.title)}</option>`).join('');
    } catch (_) { /* Formatliste optional */ }
    this.refreshRunInfo();

    const lenIn = document.getElementById('studioLen');
    const lenOut = document.getElementById('studioLenOut');
    lenIn.addEventListener('input', () => { lenOut.textContent = lenIn.value; });

    const szIn = document.getElementById('embedSize');
    const szOut = document.getElementById('embedSizeOut');
    szIn.addEventListener('input', () => { szOut.textContent = szIn.value; });

    make.addEventListener('click', async () => {
        const format = document.getElementById('studioFormat').value;
        const kind = document.getElementById('studioKind').value;
        const out = document.getElementById('studioResult');
        out.textContent = 'Wird erzeugt…';
        make.disabled = true;
        try {
            const body = kind === 'podcast'
                ? { format, minutes: +lenIn.value, style: document.getElementById('studioStyle').value,
                    hosts: document.getElementById('studioHosts').value.split(',').map(h => h.trim()).filter(Boolean) }
                : { format, style: document.getElementById('studioStyle').value };
            const piece = await (await fetch(`/api/content/${kind}`, {
                method: 'POST', headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(body)
            })).json();
            this.lastPiece = piece;

            const parts = piece.segments || piece.sections || [];
            out.textContent =
                `${piece.kind === 'podcast' ? 'Podcast' : 'Bericht'}: ${piece.title}\n` +
                `Lauf: ${piece.run.step_count} Schritte, ${piece.run.error_count} Fehler\n` +
                (piece.estimated_minutes ? `Länge: ~${piece.estimated_minutes} min  ` : '') +
                `Wörter: ${piece.word_count}  Quelle: ${piece.live ? 'Modell' : 'Gerüst'}\n` +
                `${piece.note}\n\n` +
                parts.map(p => `── ${p.title || p.heading}${p.speaker ? ' [' + p.speaker + ']' : ''}\n${p.text}`).join('\n\n') +
                '';
        } catch (err) {
            out.textContent = `Fehler: ${err.message}`;
        } finally { make.disabled = false; this.refreshRunInfo(); }
    });

    document.getElementById('embedMake').addEventListener('click', async () => {
        const out = document.getElementById('embedResult');
        if (!this.lastPiece) { out.textContent = 'Erst einen Beitrag erzeugen.'; return; }
        try {
            const res = await (await fetch('/api/content/embed', {
                method: 'POST', headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    piece: this.lastPiece,
                    form: document.getElementById('embedForm').value,
                    media: { inset_position: document.getElementById('embedPos').value,
                             inset_percent: +szIn.value }
                })
            })).json();
            out.textContent = res.code;
        } catch (err) { out.textContent = `Fehler: ${err.message}`; }
    });
};

// ══════════════════════════════════════════════════════════════════════
// Einstellungen — stufenlos, sofort wirksam
// ══════════════════════════════════════════════════════════════════════
DesignSystemApp.prototype.initSettings = async function () {
    const host = document.getElementById('settingsGroups');
    if (!host) return;

    const [schema, current] = await Promise.all([
        (await fetch('/api/settings/schema')).json(),
        (await fetch('/api/settings')).json()
    ]);
    this.settings = current.settings;
    this.applySettings();

    host.innerHTML = Object.entries(schema.groups).map(([group, items]) => `
        <div class="settings-group">
            <h3>${this.escapeHtml(group)}</h3>
            ${items.map(it => this.settingControl(it)).join('')}
        </div>`).join('');

    host.querySelectorAll('[data-setting]').forEach(el => {
        const name = el.dataset.setting;
        const evt = el.type === 'range' ? 'input' : 'change';
        el.addEventListener(evt, () => {
            const value = el.type === 'checkbox' ? el.checked
                        : el.type === 'range' ? +el.value : el.value;
            this.settings[name] = value;
            const out = host.querySelector(`output[data-for="${name}"]`);
            if (out) out.textContent = value;
            this.applySettings();
            clearTimeout(this._saveTimer);
            this._saveTimer = setTimeout(() => this.saveSettings(), 350);
        });
    });

    document.getElementById('settingsReset').addEventListener('click', async () => {
        const d = await (await fetch('/api/settings/reset', { method: 'POST' })).json();
        this.settings = d.settings;
        this.applySettings();
        this.initSettings();
    });
};

DesignSystemApp.prototype.settingControl = function (it) {
    const v = this.settings[it.name];
    const label = this.escapeHtml(it.label || it.name);
    if (it.type === 'range') {
        return `<label class="setting-row"><span>${label}</span>
            <input type="range" data-setting="${it.name}" min="${it.min}" max="${it.max}"
                   step="${it.step}" value="${v}">
            <output data-for="${it.name}">${v}</output><small>${this.escapeHtml(it.unit || '')}</small></label>`;
    }
    if (it.type === 'bool') {
        return `<label class="setting-row"><span>${label}</span>
            <input type="checkbox" data-setting="${it.name}" ${v ? 'checked' : ''}></label>`;
    }
    if (it.type === 'choice') {
        return `<label class="setting-row"><span>${label}</span>
            <select data-setting="${it.name}" class="mini-select">
            ${it.choices.map(c => `<option value="${this.escapeHtml(c)}"${c === v ? ' selected' : ''}>${this.escapeHtml(c)}</option>`).join('')}
            </select></label>`;
    }
    return '';
};

// Einstellungen in CSS-Variablen übersetzen — sofort sichtbar, ohne Neuladen.
DesignSystemApp.prototype.applySettings = function () {
    const s = this.settings || {};
    const r = document.documentElement.style;
    const h = s.accent_hue ?? 199, sa = s.accent_saturation ?? 89, l = s.accent_lightness ?? 48;
    r.setProperty('--color-primary', `hsl(${h} ${sa}% ${l}%)`);
    r.setProperty('--color-primary-hover', `hsl(${h} ${sa}% ${Math.max(10, l - 8)}%)`);
    r.setProperty('--radius-sm', `${(s.radius ?? 8) * 0.5}px`);
    r.setProperty('--radius-md', `${s.radius ?? 8}px`);
    r.setProperty('--radius-lg', `${(s.radius ?? 8) * 1.4}px`);
    const d = (s.density ?? 100) / 100;
    r.setProperty('--space-xs', `${0.25 * d}rem`);
    r.setProperty('--space-sm', `${0.5 * d}rem`);
    r.setProperty('--space-md', `${1 * d}rem`);
    r.setProperty('--space-lg', `${1.5 * d}rem`);
    document.body.style.fontSize = `${s.font_size ?? 14}px`;
    document.body.style.lineHeight = `${(s.line_height ?? 150) / 100}`;
    document.body.style.filter = (s.contrast ?? 100) !== 100 ? `contrast(${s.contrast}%)` : '';
    const fam = { serif: 'Georgia, "Times New Roman", serif',
                  mono: '"Monaco","Menlo",monospace' }[s.font_family];
    document.body.style.fontFamily = fam || '';
    document.body.classList.toggle('no-animations', s.animations === false);
    document.body.classList.toggle('no-info-blink', s.info_blink === false);
    if (s.theme && s.theme !== 'auto') document.documentElement.dataset.theme = s.theme;
    else delete document.documentElement.dataset.theme;
    r.setProperty('--glow-strength', `${(s.glow_intensity ?? 100) / 100}`);
    if (typeof s.tts_enabled === 'boolean') {
        this.ttsEnabled = s.tts_enabled;
        const t = document.getElementById('ttsToggle');
        if (t) t.checked = s.tts_enabled;
    }
};

DesignSystemApp.prototype.saveSettings = async function () {
    try {
        await fetch('/api/settings', {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ settings: this.settings })
        });
    } catch (_) { /* lokal weiterarbeiten */ }
};

// ══════════════════════════════════════════════════════════════════════
// Skill-Import aus Git
// ══════════════════════════════════════════════════════════════════════
DesignSystemApp.prototype.initSkillImport = function () {
    const btn = document.getElementById('skillImport');
    if (!btn) return;
    btn.addEventListener('click', async () => {
        const url = document.getElementById('skillUrl').value.trim();
        const out = document.getElementById('importedSkills');
        if (!url) { out.textContent = 'Bitte eine Git-URL angeben.'; return; }
        btn.disabled = true;
        out.textContent = 'Wird geholt…';
        try {
            const d = await (await fetch('/api/skills/import', {
                method: 'POST', headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ url })
            })).json();
            if (!d.ok) {
                out.innerHTML = `<p class="text-muted">${this.escapeHtml(d.error)}` +
                    `${d.hint ? '<br>' + this.escapeHtml(d.hint) : ''}</p>`;
            } else {
                this.logTerminal(`✓ Skill importiert: ${d.skill.ref}`, 'success');
                this.loadImportedSkills();
            }
        } catch (err) { out.textContent = `Fehler: ${err.message}`; }
        finally { btn.disabled = false; }
    });
    this.loadImportedSkills();
};

DesignSystemApp.prototype.loadImportedSkills = async function () {
    const out = document.getElementById('importedSkills');
    if (!out) return;
    try {
        const d = await (await fetch('/api/skills/imported')).json();
        out.innerHTML = d.skills.length ? d.skills.map(s => `
            <div class="skill-chip" data-ref="${this.escapeHtml(s.ref)}">
                <strong>${this.escapeHtml(s.name)}</strong>
                ${s.user_invoked ? '<span class="text-muted">(user-invoked)</span>' : ''}<br>
                <span class="text-muted">${this.escapeHtml((s.description || '').slice(0, 100))}</span>
            </div>`).join('') : '<p class="text-muted">Noch keine importiert.</p>';
    } catch (_) { /* still */ }
};

// Wie viel Protokoll liegt vor? Ein leerer Lauf ergibt einen leeren Bericht.
DesignSystemApp.prototype.refreshRunInfo = async function () {
    const el = document.getElementById('studioRunInfo');
    if (!el) return;
    try {
        const d = await (await fetch('/api/agent/activity?since=0')).json();
        const n = (d.events || []).length;
        el.textContent = n
            ? `${n} Ereignisse im Protokoll`
            : 'Protokoll leer — erst einen Agenten laufen lassen';
    } catch (_) { el.textContent = 'Protokoll nicht lesbar'; }
};
