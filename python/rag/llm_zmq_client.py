import json
import uuid
from dataclasses import dataclass
from typing import Any, Dict

import zmq

PROTOCOL_VERSION = 1

@dataclass
class LlmZmqResult:
    answer: str
    backend: str
    elapsed_ms: float
    
class LlmZmqClient:
    def __init__(self, endpoint: str, timeout_seconds: int = 60) -> None:
        if not endpoint:
            raise ValueError("LLM endpoint must not be empty")
        
        if timeout_seconds <= 0:
            raise ValueError("LLM timeout must be greater than zero")
        
        self.endpoint = endpoint
        self.timeout_ms = timeout_seconds * 1000
        
    def generate(self, prompt: str) -> LlmZmqResult:
        if not prompt.strip():
            raise ValueError("LLM prompt must not be empty")
        
        request_id = uuid.uuid4().hex
        
        request = {
            "version": PROTOCOL_VERSION,
            "type": "generate",
            "request_id": request_id,
            "prompt": prompt,
            "stream": False,
        }
        
        context = zmq.Context()
        socket = context.socket(zmq.REQ)
        
        socket.setsockopt(zmq.LINGER, 0)
        socket.setsockopt(zmq.SNDTIMEO, self.timeout_ms)
        socket.setsockopt(zmq.RCVTIMEO, self.timeout_ms)
        
        try:
            socket.connect(self.endpoint)
            socket.send_string(
                json.dumps(request, ensure_ascii=False)
            )
            
            raw_response = socket.recv_string()
            response = self._decode_response(
                raw_response=raw_response,
                expected_request_id=request_id,
            )
            
            return LlmZmqResult(
                answer=response["answer"].strip(),
                backend=response["backend"],
                elapsed_ms=float(response["elapsed_ms"])
            )
        except zmq.Again as exc:
            raise RuntimeError(
                "LLM request timed out after "
                f"{self.timeout_ms} ms: {self.endpoint}"
            ) from exc
        except zmq.ZMQError as exc:
            raise RuntimeError(
                f"LLM ZeroMQ request failed: {exc}"
            ) from exc
        finally:
            socket.close(linger=0)
            context.term()
            
    @staticmethod
    def _decode_response(
        raw_response: str,
        expected_request_id: str,
    ) -> Dict[str, Any]:
        try:
            response = json.loads(raw_response)
        except json.JSONDecodeError as exc:
            raise RuntimeError(
                f"LLM returned invalid JSON: {exc}"
            ) from exc
            
        if not isinstance(response, dict):
            raise RuntimeError("LLM response must be a JSON object")
        
        if response.get("version") != PROTOCOL_VERSION:
            raise RuntimeError("unsupported LLM protocol version")
        
        if response.get("type") != "generation_result":
            raise RuntimeError("unsupported LLM response type")
        
        request_id = response.get("request_id")
        
        if request_id != expected_request_id:
            raise RuntimeError(
                "LLM response request_id does not match request"
            )
            
        if response.get("finished") is not True:
            raise RuntimeError(
                "non-streaming LLM response is not finished"
            )
            
        if response.get("ok") is not True:
            error = response.get("error")
            
            if not isinstance(error, str) or not error.strip():
                error = "LLM server returned ok=false"
                
            raise RuntimeError(error)
        
        answer = response.get("answer")
        
        if not isinstance(answer, str) or not answer.strip():
            raise RuntimeError(
                "successful LLM response has empty answer"
            )
            
        backend = response.get("backend")
        
        if not isinstance(backend, str) or not backend.strip():
            raise RuntimeError(
                "LLM response has empty backend"
            )
            
        elapsed_ms = response.get("elapsed_ms", 0.0)
        
        if not isinstance(elapsed_ms, (int, float)):
            raise RuntimeError(
                "LLM response elapsed_ms must be numeric"
            )
            
        return response