# EdgeVoiceRAG

A C++ + Python edge-side RAG assistant prototype for vehicle manual question answering.

This project starts from a lightweight C++ mock RAG core and gradually evolves into a mixed C++ / Python architecture. The C++ side handles the main assistant workflow, routing, configuration, logging, performance timing, ZeroMQ communication, and JSON response parsing. The Python side provides a TF-IDF based retrieval backend with Chinese tokenization, user dictionary support, synonym query expansion, and structured JSON responses.

Current version:

```text
V3: Python TF-IDF RAG Backend
```

---

## 1. Project Goals

This project is designed as an edge AI deployment learning project.

The main goals are:

```text
1. Build a C++ command-line assistant framework.
2. Implement a simple vehicle manual RAG workflow.
3. Use ZeroMQ for cross-process communication.
4. Add a Python retrieval backend.
5. Support configurable backend switching.
6. Prepare the project for later local LLM and voice pipeline integration.
```

Current system capabilities:

```text
User query
    ↓
C++ edge_voice_rag
    ↓
QueryRouter
    ↓
Local C++ RAG / C++ ZMQ RAG / Python ZMQ RAG
    ↓
Vehicle manual retrieval
    ↓
Natural-language answer
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
* Structured JSON response from Python RAG
* C++ JSON parsing with `nlohmann_json`
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
V3: Python TF-IDF RAG Backend
```

V3 adds a Python-based retrieval backend. The C++ main program communicates with the Python RAG server through ZeroMQ. The Python server performs TF-IDF retrieval over vehicle manual chunks and returns a JSON response containing both:

```text
answer
    A user-friendly answer string.

results
    Structured top-k retrieval results.
```

The C++ side parses the JSON response and prints only the `answer` field.

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
JSON response with answer
    |
    v
C++ RagResponseParser
```

This mode sends queries from the C++ main program to the Python TF-IDF RAG server.

Config:

```ini
rag_backend=python_zmq
rag_endpoint=tcp://localhost:5556
```

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
│       ├── python_rag_server.py
│       ├── search.py
│       ├── tfidf_search.py
│       └── user_dict.txt
├── scripts/
│   ├── build_python_index.sh
│   ├── check_python_env.sh
│   ├── run_python_rag_server.sh
│   ├── search_python_json.sh
│   ├── search_python_tfidf.sh
│   ├── test_cpp_python_zmq_backend.sh
│   ├── test_once_queries.sh
│   ├── test_python_json_search.sh
│   ├── test_python_rag_server.sh
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

These are ignored by Git.

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

Start Python RAG server:

```bash
conda activate edge-rag
./scripts/run_python_rag_server.sh
```

Query through C++ main program:

```bash
./build/edge_voice_rag --config config/python_zmq.conf --once "空调怎么打开"
```

Example output:

```text
[SYSTEM] 根据车辆手册：
1. 空调系统：用户可以通过中控屏点击空调按钮，也可以使用语音指令“打开空调”。空调也可用于制冷，温度可以通过中控屏滑动条或方向盘语音键调节。
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

Start server:

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

## 11. Tests

### 11.1 C++ Unit Tests

```bash
./build/unit_tests
```

---

### 11.2 Local C++ RAG Tests

```bash
./scripts/test_once_queries.sh
```

---

### 11.3 C++ ZeroMQ Backend Tests

```bash
./scripts/test_zmq_backend.sh
```

---

### 11.4 Python TF-IDF Retrieval Tests

```bash
conda activate edge-rag
./scripts/test_python_tfidf.sh
```

---

### 11.5 Python JSON Search Tests

```bash
conda activate edge-rag
./scripts/test_python_json_search.sh
```

---

### 11.6 Python RAG Server Tests

```bash
conda activate edge-rag
./scripts/test_python_rag_server.sh
```

---

### 11.7 C++ to Python ZeroMQ Backend Tests

```bash
conda activate edge-rag
./scripts/test_cpp_python_zmq_backend.sh
```

---

### 11.8 Full Regression Test

```bash
cmake -S . -B build
cmake --build build

conda activate edge-rag

./build/unit_tests
./scripts/test_once_queries.sh
./scripts/test_zmq_backend.sh
./scripts/test_python_tfidf.sh
./scripts/test_python_json_search.sh
./scripts/test_python_rag_server.sh
./scripts/test_cpp_python_zmq_backend.sh
```

---

## 12. Example Usage

### 12.1 Local C++ Query

```bash
./build/edge_voice_rag --config config/app.conf --once "蓝牙怎么连接"
```

Example output:

```text
[SYSTEM] 根据车辆手册：
1. 蓝牙连接：进入车辆设置页面，点击蓝牙选项，打开手机蓝牙后选择车辆名称，即可完成配对连接。 [score=2]
```

---

### 12.2 Python RAG Query Through C++ Main

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

## 13. Development Milestones

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

## 14. Current Limitations

This project is currently a prototype.

Important limitations:

```text
1. The Python backend uses TF-IDF retrieval, not dense embedding retrieval.
2. Query understanding is based on token matching, custom dictionary, and synonym expansion.
3. The system does not yet call a local LLM for final response generation.
4. The knowledge base is a small mock vehicle manual, not a production-scale manual.
5. There is no ASR or TTS module yet.
6. The current Python server is single-process and uses ZeroMQ REQ/REP.
7. The answer is generated from retrieved text directly, not synthesized by a language model.
```

This project should currently be described as:

```text
A C++ + Python edge-side RAG prototype with TF-IDF retrieval.
```

It should not yet be described as:

```text
A full LLM-based semantic RAG system.
```

---

## 15. Planned Next Steps

### V4: Local LLM Integration

Possible tasks:

```text
- Add mock LLM generation interface
- Add local LLM backend config
- Use retrieved context as prompt input
- Generate final answer from context
- Compare direct retrieval answer vs LLM-generated answer
```

---

### V5: Offline Voice Pipeline

Possible tasks:

```text
- Add ASR module interface
- Add TTS module interface
- Connect voice input → text query → RAG → voice output
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

## 16. Resume Description

A concise resume description:

```text
Built a C++ + Python edge-side RAG assistant prototype for vehicle manual question answering. The C++ side handles routing, configuration, ZeroMQ communication, and JSON response parsing, while the Python side implements jieba-based Chinese tokenization, custom domain dictionary, synonym query expansion, TF-IDF top-k retrieval, and a ZeroMQ RAG server. The system supports local C++ retrieval, C++ ZMQ retrieval, and Python ZMQ retrieval backends through config-based switching.
```

A shorter version:

```text
Implemented a C++ + Python edge RAG prototype with ZeroMQ-based cross-process communication. Python provides jieba + TF-IDF retrieval and structured JSON responses, while C++ handles the main workflow, backend switching, and answer extraction.
```

---

## 17. Git Tags

Create V3 tag:

```bash
git tag -a v3.0-python-rag-backend -m "V3: Python TF-IDF RAG backend"
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

## 18. Notes

The current Python RAG backend is intentionally simple. It is useful as a baseline retrieval system before adding embedding models, FAISS, ONNX Runtime, or local LLM inference.

The current design separates system components clearly:

```text
C++:
    Main workflow, routing, communication, configuration, logging, parsing.

Python:
    Retrieval algorithm, tokenization, query expansion, JSON response generation.

ZeroMQ:
    Cross-language process boundary.
```

This separation makes the project easier to extend toward real edge AI deployment scenarios.
