#!/usr/bin/env python3
"""
Design System UI - FastAPI Backend
Handles workflow execution, Gemma 4 12B evaluation, and terminal management
"""

from fastapi import FastAPI, WebSocket, HTTPException
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from pydantic import BaseModel
from typing import List, Dict, Any, Optional
import json
import subprocess
import asyncio
import logging
import os
from datetime import datetime
from pathlib import Path

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

app = FastAPI(title="Design System UI API")

# === Pydantic Models ===

class NodeParam(BaseModel):
    model: Optional[str] = None
    temperature: Optional[float] = 0.7
    maxTokens: Optional[int] = 1000
    prompt: Optional[str] = None
    placeholder: Optional[str] = None
    fileType: Optional[str] = "any"
    script: Optional[str] = None
    [key: str]: Any

class Node(BaseModel):
    id: str
    type: str
    x: float
    y: float
    params: Dict[str, Any] = {}

class Edge(BaseModel):
    frm: str = None
    from_: str = None
    to: str

    @property
    def from_node(self):
        return self.frm or self.from_

class Workflow(BaseModel):
    id: str
    nodes: List[Node]
    edges: List[Edge]
    parameters: Dict[str, Any] = {}

class ExecutionResult(BaseModel):
    status: str
    output: str
    results: Dict[str, Any] = {}
    timestamp: str

class EvaluationRequest(BaseModel):
    workflow: Workflow
    results: Dict[str, Any]

class EvaluationResult(BaseModel):
    score: int
    feedback: str
    suggestions: List[Dict[str, str]] = []

# === Workflow Executor ===

class WorkflowExecutor:
    """Executes workflows with isolated terminal sessions"""

    def __init__(self):
        self.sessions = {}
        self.execution_logs = {}

    async def execute(self, workflow: Workflow) -> ExecutionResult:
        """Execute a workflow"""
        session_id = workflow.id
        self.execution_logs[session_id] = []

        try:
            logger.info(f"Starting execution for workflow {session_id}")
            self.log_output(session_id, "🚀 Workflow execution started")
            self.log_output(session_id, f"Workflow ID: {workflow.id}")
            self.log_output(session_id, f"Nodes: {len(workflow.nodes)}, Edges: {len(workflow.edges)}")

            # Validate workflow
            validation = self.validate_workflow(workflow)
            if not validation['valid']:
                for error in validation['errors']:
                    self.log_output(session_id, f"❌ {error}")
                return ExecutionResult(
                    status="error",
                    output="\n".join(self.execution_logs[session_id]),
                    timestamp=datetime.now().isoformat()
                )

            # Execute nodes in order
            context = {}
            for node in workflow.nodes:
                logger.info(f"Executing node {node.id} of type {node.type}")
                result = await self.execute_node(node, context)
                context[node.id] = result

            self.log_output(session_id, "✓ Workflow execution completed")

            return ExecutionResult(
                status="success",
                output="\n".join(self.execution_logs[session_id]),
                results=context,
                timestamp=datetime.now().isoformat()
            )

        except Exception as e:
            error_msg = f"Execution error: {str(e)}"
            self.log_output(session_id, f"❌ {error_msg}")
            logger.error(error_msg, exc_info=True)

            return ExecutionResult(
                status="error",
                output="\n".join(self.execution_logs[session_id]),
                timestamp=datetime.now().isoformat()
            )

    async def execute_node(self, node: Node, context: Dict) -> Any:
        """Execute a single node"""
        node_id = node.id
        node_type = node.type

        try:
            if node_type == "text-input":
                self.log_output(node_id, f"📝 Text Input: waiting for input")
                return {"type": "text", "value": ""}

            elif node_type == "llm-node":
                return await self.execute_llm_node(node, context)

            elif node_type == "transform":
                return await self.execute_transform(node, context)

            elif node_type == "filter":
                return await self.execute_filter(node, context)

            elif node_type == "text-output":
                self.log_output(node_id, f"📤 Text Output")
                return {"type": "output", "status": "completed"}

            else:
                self.log_output(node_id, f"⚠️ Unknown node type: {node_type}")
                return None

        except Exception as e:
            self.log_output(node_id, f"❌ Node execution error: {str(e)}")
            raise

    async def execute_llm_node(self, node: Node, context: Dict) -> Dict:
        """Execute LLM node using Gemma 4 12B"""
        self.log_output(node.id, f"🤖 LLM Node - Model: {node.params.get('model', 'gemma-4')}")

        # Simulate model inference
        # In production, use ollama or huggingface inference
        prompt = node.params.get('prompt', '')
        model = node.params.get('model', 'gemma-4')
        temperature = node.params.get('temperature', 0.7)

        self.log_output(node.id, f"  Prompt: {prompt[:100]}...")
        self.log_output(node.id, f"  Temperature: {temperature}")

        # Simulate model output
        mock_output = f"LLM response from {model} model"

        self.log_output(node.id, f"✓ LLM output: {mock_output[:100]}...")

        return {
            "type": "llm_output",
            "model": model,
            "prompt": prompt,
            "output": mock_output,
            "temperature": temperature
        }

    async def execute_transform(self, node: Node, context: Dict) -> Dict:
        """Execute transform node"""
        self.log_output(node.id, f"⚡ Transform Node")
        return {"type": "transform", "status": "completed"}

    async def execute_filter(self, node: Node, context: Dict) -> Dict:
        """Execute filter node"""
        self.log_output(node.id, f"🔍 Filter Node")
        return {"type": "filter", "status": "completed"}

    def validate_workflow(self, workflow: Workflow) -> Dict:
        """Validate workflow structure"""
        errors = []

        if not workflow.nodes:
            errors.append("No nodes in workflow")

        has_input = any(n.type.endswith("input") for n in workflow.nodes)
        has_output = any(n.type.endswith("output") for n in workflow.nodes)

        if not has_input:
            errors.append("No input node found")
        if not has_output:
            errors.append("No output node found")

        return {
            "valid": len(errors) == 0,
            "errors": errors
        }

    def log_output(self, session_id: str, message: str):
        """Log execution output"""
        if session_id not in self.execution_logs:
            self.execution_logs[session_id] = []
        self.execution_logs[session_id].append(message)
        logger.info(f"[{session_id}] {message}")

# === Model Evaluator ===

class ModelEvaluator:
    """Evaluates workflow results using Gemma 4 12B"""

    async def evaluate(self, workflow: Workflow, results: Dict) -> EvaluationResult:
        """Evaluate workflow results"""
        try:
            # In production, use actual Gemma 4 12B inference
            # For now, simulate evaluation

            score = self.calculate_score(workflow, results)
            feedback = self.generate_feedback(workflow, results, score)
            suggestions = self.generate_suggestions(workflow, results)

            return EvaluationResult(
                score=score,
                feedback=feedback,
                suggestions=suggestions
            )

        except Exception as e:
            logger.error(f"Evaluation error: {str(e)}")
            return EvaluationResult(
                score=0,
                feedback=f"Evaluation error: {str(e)}",
                suggestions=[]
            )

    def calculate_score(self, workflow: Workflow, results: Dict) -> int:
        """Calculate workflow score 0-100"""
        score = 50

        # Add points for workflow structure
        if len(workflow.nodes) >= 3:
            score += 20
        if len(workflow.edges) > 0:
            score += 15
        if any(n.type == "llm-node" for n in workflow.nodes):
            score += 15

        return min(100, score)

    def generate_feedback(self, workflow: Workflow, results: Dict, score: int) -> str:
        """Generate evaluation feedback"""
        feedback = f"Workflow Evaluation Report\n"
        feedback += f"Score: {score}/100\n\n"
        feedback += f"Structure:\n"
        feedback += f"- {len(workflow.nodes)} nodes\n"
        feedback += f"- {len(workflow.edges)} connections\n\n"
        feedback += f"Analysis:\n"

        has_llm = any(n.type == "llm-node" for n in workflow.nodes)
        if has_llm:
            feedback += "✓ Contains LLM node for AI processing\n"
        else:
            feedback += "⚠️ Consider adding LLM node for intelligent processing\n"

        if len(workflow.edges) > 0:
            feedback += "✓ Nodes are connected\n"
        else:
            feedback += "⚠️ Nodes are disconnected\n"

        return feedback

    def generate_suggestions(self, workflow: Workflow, results: Dict) -> List[Dict[str, str]]:
        """Generate improvement suggestions"""
        suggestions = []

        if not any(n.type == "llm-node" for n in workflow.nodes):
            suggestions.append({
                "title": "Add LLM node",
                "description": "Enhance workflow with AI-powered processing",
                "action": "ADD_NODE(type='llm-node')"
            })

        if len(workflow.edges) < len(workflow.nodes) - 1:
            suggestions.append({
                "title": "Connect more nodes",
                "description": "Create data flow between existing nodes",
                "action": "CONNECT_NODES(fromId, toId)"
            })

        return suggestions

# === Route Handlers ===

executor = WorkflowExecutor()
evaluator = ModelEvaluator()

@app.get("/")
async def root():
    """Serve index.html"""
    return FileResponse("index.html", media_type="text/html")

@app.get("/llms.txt")
async def get_llms():
    """Serve llms.txt for AI agents"""
    content = """# Design System UI - llms.txt
# AI Agent Instruction Layer

## System Overview

This is an interactive workflow design and execution system with integrated:
- ComfyUI node-based workflow builder
- Terminal-based execution
- Gemma 4 12B model evaluation
- Iterative optimization loop

## Available API Endpoints

POST /api/workflow/execute
  Execute a workflow

POST /api/workflow/evaluate
  Evaluate workflow results with Gemma 4 12B

POST /api/workflow/suggest-params
  Get AI parameter suggestions

GET /api/patterns
  Get available workflow patterns

GET /api/workflow/status/:id
  Get workflow execution status

## Agent Interaction Guide

1. Read this llms.txt file
2. Analyze workflow structure from JSON
3. Call API endpoints to:
   - Execute workflows
   - Get parameter suggestions
   - Evaluate results
4. Present results without page navigation

## No Page Navigation Required

All operations happen in a single-page application context.
Use API endpoints only, no URL changes needed.
"""
    return {"content": content}

@app.post("/api/workflow/execute")
async def execute_workflow(workflow: Workflow):
    """Execute a workflow"""
    result = await executor.execute(workflow)
    return result

@app.post("/api/workflow/evaluate")
async def evaluate_workflow(request: EvaluationRequest):
    """Evaluate workflow results"""
    result = await evaluator.evaluate(request.workflow, request.results)
    return result

@app.post("/api/workflow/suggest-params")
async def suggest_params(data: Dict[str, Any]):
    """Suggest parameters for a node type"""
    node_type = data.get("nodeType")

    suggestions = {
        "llm-node": {
            "model": "gemma-4",
            "temperature": 0.7,
            "maxTokens": 1000
        },
        "text-input": {
            "placeholder": "Enter text here"
        },
        "transform": {
            "script": "default_transform"
        }
    }

    return suggestions.get(node_type, {})

@app.get("/api/patterns")
async def get_patterns():
    """Get available workflow patterns"""
    patterns = {
        "text-classifier": {
            "name": "Text Classifier",
            "description": "Classify text into categories",
            "icon": "🏷️"
        },
        "rag-pipeline": {
            "name": "RAG Pipeline",
            "description": "Retrieval-Augmented Generation workflow",
            "icon": "🔍"
        },
        "agent-loop": {
            "name": "Agent Loop",
            "description": "Think-Act-Observe agent pattern",
            "icon": "🤖"
        },
        "eval-loop": {
            "name": "Evaluation Loop",
            "description": "Execute and evaluate iteratively",
            "icon": "📊"
        }
    }

    return {"patterns": patterns}

@app.get("/api/workflow/status/{workflow_id}")
async def get_workflow_status(workflow_id: str):
    """Get workflow execution status"""
    return {
        "id": workflow_id,
        "status": "idle",
        "lastExecution": None
    }

# === WebSocket for Live Terminal ===

connections: List[WebSocket] = []

@app.websocket("/ws/terminal/{workflow_id}")
async def websocket_terminal(websocket: WebSocket, workflow_id: str):
    """WebSocket for live terminal output"""
    await websocket.accept()
    connections.append(websocket)

    try:
        while True:
            data = await websocket.receive_text()
            # Broadcast to all connections
            for connection in connections:
                try:
                    await connection.send_text(f"[{workflow_id}] {data}")
                except:
                    pass
    except Exception as e:
        logger.error(f"WebSocket error: {e}")
    finally:
        connections.remove(websocket)

# === Health Check ===

@app.get("/health")
async def health_check():
    """Health check endpoint"""
    return {
        "status": "healthy",
        "service": "Design System UI API",
        "timestamp": datetime.now().isoformat()
    }

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(
        app,
        host="0.0.0.0",
        port=8000,
        log_level="info"
    )
