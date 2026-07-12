# EdgeVoiceRAG

A C++ + Python edge-side RAG assistant prototype for vehicle manual question answering.

This project starts from a lightweight C++ mock RAG core and gradually evolves into a mixed C++ / Python / ZeroMQ architecture. The C++ side handles the main assistant workflow, query routing, configuration, logging, performance timing, ZeroMQ communication, and JSON answer parsing. The Python side provides document chunking, Chinese tokenization, TF-IDF retrieval, query expansion, and local LLM generation.

Current version:

```text
V4: Local LLM Integration
```

---

## 1. Project Goals

This project is designed as an edge AI deployment learning project.

The main goals are:

```text
1. Build a C++ command-line assistant framework.
2. Implement a simple vehicle manual RAG workflow.
3. Use ZeroMQ for cross-process and cross-language communication.
4. Add a Python retrieval backend.
5. Add a local LLM generation layer.
6. Support configurable backend switching.
7. Prepare the project for later offline voice input and output.
```

Current system pipeline:

```text
User query
    ↓
C++ edge_voice_rag
    ↓
QueryRouter
    ↓
Local C++ RAG / C++ ZMQ RAG / Python ZMQ RAG
    ↓
TF-IDF retrieval
    ↓
Mock LLM / Ollama local LLM
    ↓
generated_answer
    ↓
C++ RagResponseParser
    ↓
User-friendly answer
```

---

## 2. Features

* C++17 command-line assistant framework
* Query routing for vehicle manual questions
* Local C++ mock RAG retrieval
* ZeroMQ-based C++ RAG server
* Python TF-IDF RAG backend
* Chinese tokenization with `jieba`
* Custom user dictionary for vehicle-domain terms
* Synonym-based query expansion
* Mock LLM generator interface
* Ollama-based local LLM backend
* Configurable LLM backend: `mock` / `ollama`
* Ollama health check and model availability check
* LLM request timeout configuration
* Prompt debug mode with `--include-prompt`
* Structured JSON response from Python RAG
* C++ JSON parsing with `nlohmann_json`
* C++ parser prioritizes `generated_answer`
* Configurable backend selection:

  * `local`
  * `zmq`
  * `python_zmq`
* Shell-based integration tests
* Simple performance logging
* Git version tags for staged development

---

## 3. Current Version

```text
V4: Local LLM Integration
```

V4 extends the Python TF-IDF RAG backend with a generation layer.

The system now supports two LLM generation backends:

```text
mock
    Stable test backend. Does not call a real model.

ollama
    Real local LLM backend. Calls local Ollama through HTTP.
```

V4 pipeline:

```text
C++ edge_voice_rag
    ↓
ZeroMQ
Python RAG server
    ↓
TF-IDF retrieval
    ↓
Mock LLM / Ollama local LLM
    ↓
generated_answer
    ↓
C++ RagResponseParser
    ↓
final answer
```

---

## 4. Architecture

### 4.1 Local C++ Backend

```text
edge_voice_rag
    |
    v
QueryRouter
    |
    v
C++ RagEngine
    |
    v
docs/vehicle_manual.txt
```

This mode uses the local C++ mock RAG engine directly.

Config:

```ini
rag_backend=local
```

---

### 4.2 C++ ZeroMQ Backend

```text
edge_voice_rag
    |
    | ZeroMQ REQ
    v
rag_server
    |
    v
C++ RagEngine
    |
    v
docs/vehicle_manual.txt
```

This mode sends queries from the C++ main program to a standalone C++ RAG server.

Config:

```ini
rag_backend=zmq
rag_endpoint=tcp://localhost:5555
```

---

### 4.3 Python ZeroMQ Backend

```text
edge_voice_rag
    |
    | ZeroMQ REQ
    v
python_rag_server.py
    |
    v
TfidfRagSearcher
    |
    v
vector_db/chunks.json
    |
    v
JSON response
    |
    v
C++ RagResponseParser
```

This mode sends queries from the C++ main program to the Python RAG server.

Config:

```ini
rag_backend=python_zmq
rag_endpoint=tcp://localhost:5556
```

---

### 4.4 Python ZeroMQ Backend with Local LLM

```text
edge_voice_rag
    |
    | ZeroMQ REQ
    v
python_rag_server.py
    |
    v
TfidfRagSearcher
    |
    v
retrieved contexts
    |
    v
MockLLMGenerator / OllamaGenerator
    |
    v
JSON response with generated_answer
    |
    v
C++ RagResponseParser
    |
    v
final answer
```

In V4, the Python RAG server no longer only concatenates retrieved chunks. It first retrieves relevant vehicle manual chunks, then passes them to an LLM generator interface. The generator can be either a mock backend for stable testing or an Ollama backend for real local LLM generation.

---

## 5. Repository Structure

```text
EdgeVoiceRAG/
├── CMakeLists.txt
├── README.md
├── config/
│   ├── app.conf
│   ├── dev.conf
│   ├── zmq.conf
│   └── python_zmq.conf
├── docs/
│   └── vehicle_manual.txt
├── include/
│   ├── app_config.h
│   ├── command_line_options.h
│   ├── logger.h
│   ├── perf_timer.h
│   ├── query_router.h
│   ├── rag_client_zmq.h
│   ├── rag_engine.h
│   └── rag_response_parser.h
├── src/
│   ├── app_config.cpp
│   ├── command_line_options.cpp
│   ├── logger.cpp
│   ├── main.cpp
│   ├── perf_timer.cpp
│   ├── query_router.cpp
│   ├── rag_client.cpp
│   ├── rag_client_zmq.cpp
│   ├── rag_engine.cpp
│   ├── rag_response_parser.cpp
│   ├── rag_server.cpp
│   ├── zmq_req_client.cpp
│   └── zmq_rep_server.cpp
├── python/
│   ├── requirements.txt
│   └── rag/
│       ├── __init__.py
│       ├── build_index.py
│       ├── check_env.py
│       ├── debug_tokenize.py
│       ├── llm_generator.py
│       ├── python_rag_server.py
│       ├── search.py
│       ├── test_llm_generator.py
│       ├── tfidf_search.py
│       └── user_dict.txt
├── scripts/
│   ├── build_python_index.sh
│   ├── check_ollama.sh
│   ├── check_python_env.sh
│   ├── run_python_rag_server.sh
│   ├── run_python_rag_server_debug.sh
│   ├── run_python_rag_server_ollama.sh
│   ├── search_python_json.sh
│   ├── search_python_tfidf.sh
│   ├── test_cpp_python_zmq_backend.sh
│   ├── test_mock_llm.sh
│   ├── test_ollama_generator_manual.sh
│   ├── test_once_queries.sh
│   ├── test_python_json_search.sh
│   ├── test_python_rag_server.sh
│   ├── test_python_rag_server_debug_prompt.sh
│   ├── test_python_tfidf.sh
│   └── test_zmq_backend.sh
└── tests/
    └── unit_tests.cpp
```

Generated files:

```text
build/
vector_db/
.venv/
__pycache__/
```

These should be ignored by Git.

---

## 6. Dependencies

### 6.1 C++ Dependencies

Required:

```text
CMake
g++
ZeroMQ
cppzmq
nlohmann_json
```

Install on Ubuntu / WSL:

```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config
sudo apt install -y libzmq3-dev cppzmq-dev nlohmann-json3-dev
```

Verify ZeroMQ:

```bash
ls /usr/include/zmq.h
ls /usr/include/zmq.hpp
ldconfig -p | grep zmq
```

Verify `nlohmann_json`:

```bash
ls /usr/include/nlohmann/json.hpp
```

---

### 6.2 Python Dependencies

This project uses a conda environment.

Create and activate the environment:

```bash
conda create -n edge-rag python=3.10 -y
conda activate edge-rag
```

Install Python dependencies:

```bash
pip install -r python/requirements.txt
```

Current Python dependencies:

```text
numpy
scikit-learn
jieba
pyzmq
requests
```

Check the environment:

```bash
./scripts/check_python_env.sh
```

Expected output:

```text
Python environment check
Python: 3.10.x
NumPy: ...
scikit-learn: ...
pyzmq: ...
jieba tokens: ['空调', '怎么', '打开']
Environment OK
```

---

### 6.3 Ollama Dependency

Ollama is only required when using the real local LLM backend.

Install and prepare a model:

```bash
ollama pull qwen2.5:1.5b
```

Check Ollama service and model:

```bash
./scripts/check_ollama.sh qwen2.5:1.5b
```

If Ollama is not running, try:

```bash
ollama run qwen2.5:1.5b
```

Ollama default local API:

```text
http://localhost:11434
```

---

## 7. Build

Build the C++ project:

```bash
cmake -S . -B build
cmake --build build
```

Generated binaries:

```text
build/edge_voice_rag
build/unit_tests
build/zmq_rep_server
build/zmq_req_client
build/rag_server
build/rag_client
```

---

## 8. Configuration

### 8.1 Local C++ Backend

`config/app.conf`:

```ini
knowledge_path=docs/vehicle_manual.txt
top_k=3
rag_backend=local
rag_endpoint=tcp://localhost:5555
rag_timeout_ms=3000
```

Run:

```bash
./build/edge_voice_rag --config config/app.conf --once "空调怎么打开"
```

---

### 8.2 C++ ZeroMQ Backend

`config/zmq.conf`:

```ini
knowledge_path=docs/vehicle_manual.txt
top_k=3
rag_backend=zmq
rag_endpoint=tcp://localhost:5555
rag_timeout_ms=3000
```

Start C++ RAG server:

```bash
./build/rag_server --config config/app.conf
```

Query through C++ main program:

```bash
./build/edge_voice_rag --config config/zmq.conf --once "空调怎么打开"
```

Stop server:

```bash
./build/edge_voice_rag --config config/zmq.conf --once "exit"
```

---

### 8.3 Python ZeroMQ Backend

`config/python_zmq.conf`:

```ini
knowledge_path=docs/vehicle_manual.txt
top_k=3
rag_backend=python_zmq
rag_endpoint=tcp://localhost:5556
rag_timeout_ms=3000
```

Start Python RAG server with mock LLM:

```bash
conda activate edge-rag
./scripts/run_python_rag_server.sh
```

Query through C++ main program:

```bash
./build/edge_voice_rag --config config/python_zmq.conf --once "空调怎么打开"
```

Stop Python RAG server:

```bash
./build/edge_voice_rag --config config/python_zmq.conf --once "exit"
```

---

## 9. Python RAG Backend

### 9.1 Build Python Index

The Python backend first converts the vehicle manual into structured chunks.

Run:

```bash
conda activate edge-rag
./scripts/build_python_index.sh
```

This generates:

```text
vector_db/chunks.json
```

The `vector_db/` directory is generated at runtime and ignored by Git.

---

### 9.2 Search with TF-IDF

Run:

```bash
./scripts/search_python_tfidf.sh "空调怎么打开"
```

Example output:

```text
1. [0.xxxx] 空调系统
   空调系统：用户可以通过中控屏点击空调按钮，也可以使用语音指令“打开空调”。空调也可用于制冷，温度可以通过中控屏滑动条或方向盘语音键调节。
```

---

### 9.3 Search with JSON Output

Run:

```bash
./scripts/search_python_json.sh "空调怎么打开"
```

Example output:

```json
{
  "ok": true,
  "query": "空调怎么打开",
  "backend": "tfidf",
  "answer": "根据车辆手册：\n1. 空调系统：用户可以通过中控屏点击空调按钮，也可以使用语音指令“打开空调”。空调也可用于制冷，温度可以通过中控屏滑动条或方向盘语音键调节。",
  "result_count": 1,
  "results": [
    {
      "rank": 1,
      "chunk_id": 0,
      "title": "空调系统",
      "content": "用户可以通过中控屏点击空调按钮，也可以使用语音指令“打开空调”。空调也可用于制冷，温度可以通过中控屏滑动条或方向盘语音键调节。",
      "text": "空调系统：用户可以通过中控屏点击空调按钮，也可以使用语音指令“打开空调”。空调也可用于制冷，温度可以通过中控屏滑动条或方向盘语音键调节。",
      "score": 0.4082
    }
  ]
}
```

---

### 9.4 Run Python RAG Server

Start server with mock LLM:

```bash
conda activate edge-rag
./scripts/run_python_rag_server.sh
```

Default endpoint:

```text
tcp://*:5556
```

Test with C++ client:

```bash
./build/rag_client --endpoint tcp://localhost:5556
```

Input:

```text
空调怎么打开
```

The server returns JSON.

---

## 10. Chinese Tokenization and Query Expansion

The Python backend uses `jieba` for Chinese tokenization.

A custom user dictionary is stored in:

```text
python/rag/user_dict.txt
```

Example terms:

```text
雨刷
雨刮
空调
冷气
蓝牙
胎压
轮胎气压
后备箱
尾门
充电桩
充电站
座椅加热
中控屏
方向盘
雨量传感器
```

The TF-IDF backend also uses synonym query expansion.

Examples:

```text
雨刷 → 雨刮
雨刮 → 雨刷
冷气 → 空调, 制冷
空调 → 冷气, 制冷
后备箱 → 尾门
尾门 → 后备箱
充电桩 → 充电站
充电站 → 充电桩
胎压 → 轮胎气压
```

This improves recall for vehicle-domain expressions.

Example:

```bash
python python/rag/tfidf_search.py --query "车里太热了怎么开冷气" --debug
```

Expected debug output:

```text
[DEBUG] query: 车里太热了怎么开冷气
[DEBUG] expanded query: 车里太热了怎么开冷气 空调 制冷
```

---

## 11. Local LLM Backend

The Python RAG server supports configurable LLM generation backends.

### 11.1 Mock LLM Backend

The mock backend is used for stable testing. It does not call any real model.

Start server:

```bash
conda activate edge-rag
./scripts/run_python_rag_server.sh
```

This uses:

```text
--llm-backend mock
```

The mock backend simply formats retrieved contexts into an answer.

Example output:

```text
[SYSTEM] 根据车辆手册：
1. 空调系统：用户可以通过中控屏点击空调按钮，也可以使用语音指令“打开空调”...
```

---

### 11.2 Ollama Backend

The Ollama backend calls a local Ollama service through HTTP.

Prepare model:

```bash
ollama pull qwen2.5:1.5b
```

Check Ollama:

```bash
./scripts/check_ollama.sh qwen2.5:1.5b
```

Start Python RAG server with Ollama:

```bash
conda activate edge-rag
./scripts/run_python_rag_server_ollama.sh qwen2.5:1.5b
```

Then query from the C++ main program:

```bash
./build/edge_voice_rag --config config/python_zmq.conf --once "空调怎么打开"
```

Example output:

```text
[SYSTEM] 您可以通过中控屏点击空调按钮，或者使用语音指令“打开空调”来开启空调。
```

Stop the server:

```bash
./build/edge_voice_rag --config config/python_zmq.conf --once "exit"
```

---

### 11.3 Ollama Runtime Options

The Ollama server script supports environment variables:

```bash
OLLAMA_URL=http://localhost:11434
LLM_TIMEOUT=60
```

Example:

```bash
OLLAMA_URL=http://localhost:11434 LLM_TIMEOUT=120 \
./scripts/run_python_rag_server_ollama.sh qwen2.5:3b
```

The Python server also supports direct arguments:

```bash
python python/rag/python_rag_server.py \
    --endpoint tcp://*:5556 \
    --index vector_db/chunks.json \
    --top-k 3 \
    --llm-backend ollama \
    --llm-model qwen2.5:1.5b \
    --ollama-url http://localhost:11434 \
    --llm-timeout 60
```

Disable LLM health check if needed:

```bash
python python/rag/python_rag_server.py \
    --endpoint tcp://*:5556 \
    --index vector_db/chunks.json \
    --top-k 3 \
    --llm-backend ollama \
    --llm-model qwen2.5:1.5b \
    --disable-llm-health-check
```

---

### 11.4 Ollama Health Check

Run:

```bash
./scripts/check_ollama.sh qwen2.5:1.5b
```

This script checks:

```text
1. Whether Ollama service is reachable
2. Whether the expected model exists
3. Whether /api/generate works
```

If the model is missing:

```bash
ollama pull qwen2.5:1.5b
```

If the service is unavailable:

```bash
ollama run qwen2.5:1.5b
```

---

## 12. Prompt Debug Mode

By default, the Python RAG server does not return the full prompt in the JSON response. This keeps the response smaller and avoids exposing internal prompt templates.

Default response fields:

```json
{
  "ok": true,
  "query": "...",
  "backend": "python_tfidf",
  "llm_backend": "mock_llm",
  "answer": "...",
  "generated_answer": "...",
  "result_count": 1,
  "results": []
}
```

To include the full prompt for debugging, start the debug server:

```bash
conda activate edge-rag
./scripts/run_python_rag_server_debug.sh
```

Or start manually with:

```bash
python python/rag/python_rag_server.py \
    --endpoint tcp://*:5556 \
    --index vector_db/chunks.json \
    --top-k 3 \
    --llm-backend mock \
    --include-prompt
```

Debug response includes:

```json
{
  "prompt": "你是一个车载语音助手..."
}
```

Ollama debug mode:

```bash
LLM_BACKEND=ollama LLM_MODEL=qwen2.5:1.5b \
./scripts/run_python_rag_server_debug.sh
```

---

## 13. JSON Response Format

Default Python RAG response:

```json
{
  "ok": true,
  "query": "空调怎么打开",
  "backend": "python_tfidf",
  "llm_backend": "mock_llm",
  "answer": "根据车辆手册：\n1. 空调系统：...",
  "generated_answer": "根据车辆手册：\n1. 空调系统：...",
  "result_count": 1,
  "results": [
    {
      "rank": 1,
      "chunk_id": 0,
      "title": "空调系统",
      "content": "...",
      "text": "空调系统：...",
      "score": 0.4082
    }
  ]
}
```

Debug response with prompt:

```json
{
  "ok": true,
  "query": "空调怎么打开",
  "backend": "python_tfidf",
  "llm_backend": "mock_llm",
  "answer": "根据车辆手册：...",
  "generated_answer": "根据车辆手册：...",
  "prompt": "你是一个车载语音助手...",
  "result_count": 1,
  "results": []
}
```

C++ answer extraction priority:

```text
generated_answer
    ↓
answer
    ↓
error
    ↓
raw response
```

---

## 14. Tests

Build C++ first:

```bash
cmake -S . -B build
cmake --build build
```

Activate Python environment:

```bash
conda activate edge-rag
```

Run C++ unit tests:

```bash
./build/unit_tests
```

Run local C++ RAG tests:

```bash
./scripts/test_once_queries.sh
```

Run C++ ZeroMQ backend tests:

```bash
./scripts/test_zmq_backend.sh
```

Run Python TF-IDF retrieval tests:

```bash
./scripts/test_python_tfidf.sh
```

Run Python JSON search tests:

```bash
./scripts/test_python_json_search.sh
```

Run mock LLM tests:

```bash
./scripts/test_mock_llm.sh
```

Run Python RAG server tests:

```bash
./scripts/test_python_rag_server.sh
```

Run prompt debug tests:

```bash
./scripts/test_python_rag_server_debug_prompt.sh
```

Run C++ to Python ZeroMQ backend tests:

```bash
./scripts/test_cpp_python_zmq_backend.sh
```

Run Ollama health check manually:

```bash
./scripts/check_ollama.sh qwen2.5:1.5b
```

Run Ollama generator manual test:

```bash
./scripts/test_ollama_generator_manual.sh qwen2.5:1.5b
```

Full default regression test:

```bash
cmake -S . -B build
cmake --build build

conda activate edge-rag

./build/unit_tests
./scripts/test_once_queries.sh
./scripts/test_zmq_backend.sh
./scripts/test_python_tfidf.sh
./scripts/test_python_json_search.sh
./scripts/test_mock_llm.sh
./scripts/test_python_rag_server.sh
./scripts/test_python_rag_server_debug_prompt.sh
./scripts/test_cpp_python_zmq_backend.sh
```

Note: Ollama tests are manual and are not part of the default regression flow because they require a local Ollama service and a pulled model.

---

## 15. Example Usage

### 15.1 Local C++ Query

```bash
./build/edge_voice_rag --config config/app.conf --once "蓝牙怎么连接"
```

Example output:

```text
[SYSTEM] 根据车辆手册：
1. 蓝牙连接：进入车辆设置页面，点击蓝牙选项，打开手机蓝牙后选择车辆名称，即可完成配对连接。 [score=2]
```

---

### 15.2 Python RAG Query Through C++ Main with Mock LLM

Start Python server:

```bash
conda activate edge-rag
./scripts/run_python_rag_server.sh
```

Query:

```bash
./build/edge_voice_rag --config config/python_zmq.conf --once "尾门怎么打开"
```

Example output:

```text
[SYSTEM] 根据车辆手册：
1. 后备箱开启：后备箱也称尾门，用户可以通过车钥匙、车内按键或尾门外部按钮开启后备箱。
```

Stop server:

```bash
./build/edge_voice_rag --config config/python_zmq.conf --once "exit"
```

---

### 15.3 Python RAG Query Through C++ Main with Ollama

Start Ollama backend:

```bash
conda activate edge-rag
./scripts/run_python_rag_server_ollama.sh qwen2.5:1.5b
```

Query:

```bash
./build/edge_voice_rag --config config/python_zmq.conf --once "空调怎么打开"
```

Example output:

```text
[SYSTEM] 您可以通过中控屏点击空调按钮，或者使用语音指令“打开空调”来开启空调。
```

Stop server:

```bash
./build/edge_voice_rag --config config/python_zmq.conf --once "exit"
```

---

## 16. Development Milestones

### V1: C++ Mock RAG Core

Tag:

```text
v1.0-cpp-mock-rag
```

Implemented:

```text
- C++17 project structure
- QueryRouter
- Logger
- PerfTimer
- RagEngine
- vehicle_manual.txt
- local mock retrieval
- --once mode
- unit tests
- shell tests
```

---

### V2: ZeroMQ RAG Server

Tag:

```text
v2.0-zmq-rag-server
```

Implemented:

```text
- ZeroMQ REQ/REP demo
- C++ rag_server
- C++ rag_client
- RagClientZmq
- configurable zmq backend
- timeout handling
- ZMQ backend integration tests
```

---

### V3: Python TF-IDF RAG Backend

Tag:

```text
v3.0-python-rag-backend
```

Implemented:

```text
- Python conda environment
- Python document chunk builder
- chunks.json generation
- jieba tokenization
- custom user dictionary
- synonym query expansion
- TF-IDF top-k retrieval
- JSON search entry
- Python ZeroMQ RAG server
- C++ main support for python_zmq backend
- C++ parsing of Python JSON answer field
- integration tests for C++ → Python ZeroMQ backend
```

---

### V4: Local LLM Integration

Tag:

```text
v4.0-local-llm-integration
```

Implemented:

```text
- Mock LLM generator interface
- retrieval → generation pipeline
- generated_answer field in Python RAG response
- configurable --llm-backend option
- Ollama local LLM backend
- Ollama health check
- Ollama model availability check
- LLM timeout configuration
- prompt debug option with --include-prompt
- C++ parser priority: generated_answer → answer → error → raw
```

---

## 17. Current Limitations

This project is currently a prototype.

Important limitations:

```text
1. The retrieval backend still uses TF-IDF, not dense embedding retrieval.
2. Query understanding is based on token matching, custom dictionary, and synonym expansion.
3. Ollama integration depends on an external local Ollama service.
4. Local LLM output quality depends on the selected model.
5. The knowledge base is still a small mock vehicle manual.
6. There is no ASR or TTS module yet.
7. There is no streaming response support yet.
8. The Python RAG server is still a simple single-process ZeroMQ REP server.
9. There is no Docker deployment yet.
10. There is no edge hardware-specific optimization yet.
```

This project should currently be described as:

```text
A C++ + Python + ZeroMQ edge-side RAG prototype with TF-IDF retrieval and local LLM generation.
```

It should not yet be described as:

```text
A production-ready vehicle voice assistant.
```

---

## 18. Planned Next Steps

### V5: Offline Voice Pipeline

Possible tasks:

```text
- Add mock ASR interface
- Add mock TTS interface
- Add text-in/text-out voice pipeline abstraction
- Connect voice input → text query → RAG → voice output
- Later integrate offline ASR such as Whisper or sherpa-onnx
- Later integrate offline TTS
```

---

### V6: Docker Deployment

Possible tasks:

```text
- Add Dockerfile
- Add docker-compose.yml
- Package C++ binary and Python RAG service
- Provide reproducible deployment commands
```

---

### V7: Edge Device Optimization

Possible tasks:

```text
- Measure latency
- Reduce Python service overhead
- Add CPU-only inference baseline
- Prepare for RK / Jetson / other edge devices
```

---

## 19. Resume Description

A concise resume description:

```text
Built a C++ + Python edge-side RAG assistant prototype for vehicle manual question answering. The C++ side handles routing, configuration, ZeroMQ communication, and JSON answer parsing, while the Python side implements document chunking, jieba-based Chinese tokenization, synonym query expansion, TF-IDF retrieval, and local LLM generation. The system supports local C++ retrieval, C++ ZMQ retrieval, Python ZMQ retrieval, mock LLM generation, and Ollama-based local LLM generation.
```

A shorter version:

```text
Implemented a C++ + Python edge RAG prototype with ZeroMQ-based cross-process communication, TF-IDF retrieval, and local LLM generation via Ollama. C++ handles the main workflow and answer parsing, while Python handles retrieval and generation backends.
```

Chinese resume version:

```text
实现 C++ + Python 混合架构的端侧 RAG 原型系统。C++ 侧负责主流程控制、配置管理、ZeroMQ 通信和 JSON 回答解析，Python 侧负责车辆手册切块、jieba 中文分词、同义词 query expansion、TF-IDF 检索以及本地 LLM 生成；支持 local、C++ ZMQ、Python ZMQ 三种 RAG 后端，并接入 mock LLM 与 Ollama 本地 LLM 后端。
```

---

## 20. Git Tags

Create V4 tag:

```bash
git tag -a v4.0-local-llm-integration -m "V4: Local LLM integration"
```

View tags:

```bash
git tag
```

Expected:

```text
v1.0-cpp-mock-rag
v2.0-zmq-rag-server
v3.0-python-rag-backend
v4.0-local-llm-integration
```

Push tags:

```bash
git push origin main
git push origin --tags
```

If the default branch is `master`:

```bash
git push origin master
git push origin --tags
```

---

## 21. Notes

The current Python RAG backend is intentionally simple. It is useful as a baseline retrieval and generation system before adding embedding models, FAISS, ONNX Runtime, streaming generation, Docker deployment, or offline voice modules.

The current design separates system components clearly:

```text
C++:
    Main workflow, routing, communication, configuration, logging, parsing.

Python:
    Retrieval algorithm, tokenization, query expansion, LLM generation, JSON response generation.

ZeroMQ:
    Cross-language process boundary.

Ollama:
    Local LLM generation backend.
```

This separation makes the project easier to extend toward real edge AI deployment scenarios.
