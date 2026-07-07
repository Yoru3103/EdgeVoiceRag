# EdgeVoiceRAG

EdgeVoiceRAG is a C++-based prototype for an edge-side voice RAG assistant.
The current version implements a minimal offline vehicle manual retrieval system, including query routing, keyword-based mock RAG retrieval, configuration loading, command-line options, logging, performance profiling, and basic testing.

This project is being developed step by step as a learning and engineering practice project for edge AI deployment, C++ system design, ZeroMQ-based modular communication, local RAG, and future voice interaction.

## Current Version

Current milestone:

```text
V1: C++ Mock RAG Core
```

V1 focuses on building a clean C++ engineering foundation before introducing ZeroMQ, Python-based vector retrieval, LLM inference, ASR, TTS, and Docker deployment.

The current system supports:

* Interactive query mode
* One-shot query mode with `--once`
* Configurable knowledge base path and `top_k`
* Query routing
* Keyword-rule-based mock RAG retrieval
* Top-k scoring
* Structured logging
* Performance timing
* C++ unit tests
* Shell-based end-to-end tests

## Architecture

Current V1 architecture:

```text
User Query
    |
    v
CommandLineOptions
    |
    v
AppConfig
    |
    v
QueryRouter
    |
    +----------------------+
    |                      |
    v                      v
VehicleManual          Chat / Unknown
    |
    v
RagEngine
    |
    v
docs/vehicle_manual.txt
    |
    v
Logger + PerfTimer
```

Module responsibilities:

```text
main.cpp
    Main process orchestration.

CommandLineOptions
    Parses command-line arguments such as --config and --once.

AppConfig
    Loads runtime configuration from config files.

QueryRouter
    Classifies user queries into vehicle_manual, chat, or unknown.

RagEngine
    Performs keyword-rule-based mock retrieval over the local vehicle manual.

Logger
    Provides unified log output.

PerfTimer
    Measures query latency and retrieval latency.
```

## Directory Structure

```text
EdgeVoiceRAG/
├── CMakeLists.txt
├── README.md
├── config/
│   ├── app.conf
│   └── dev.conf
├── docs/
│   └── vehicle_manual.txt
├── include/
│   ├── app_config.h
│   ├── command_line_options.h
│   ├── logger.h
│   ├── perf_timer.h
│   ├── query_router.h
│   └── rag_engine.h
├── scripts/
│   └── test_once_queries.sh
├── src/
│   ├── app_config.cpp
│   ├── command_line_options.cpp
│   ├── logger.cpp
│   ├── main.cpp
│   ├── perf_timer.cpp
│   ├── query_router.cpp
│   └── rag_engine.cpp
└── tests/
    └── unit_tests.cpp
```

## Build

Requirements:

```text
C++17
CMake >= 3.16
Linux / WSL / macOS shell environment
```

Build commands:

```bash
cmake -S . -B build
cmake --build build
```

Generated executables:

```text
build/edge_voice_rag
build/unit_tests
```

## Run

### Interactive Mode

```bash
./build/edge_voice_rag
```

Example:

```text
Please input your question, or type exit to quit:
> 空调怎么打开
```

Expected output:

```text
[USER] 空调怎么打开
[ROUTE] vehicle_manual
[SYSTEM] 根据车辆手册：
1. 空调系统：...
[PERF] rag_search handled in ... ms
[PERF] single_query handled in ... ms
```

### One-shot Mode

```bash
./build/edge_voice_rag --once "空调怎么打开"
```

Use a custom config file:

```bash
./build/edge_voice_rag --config config/dev.conf --once "蓝牙怎么连接"
```

Short form:

```bash
./build/edge_voice_rag -c config/dev.conf --once "胎压报警怎么办"
```

### Help

```bash
./build/edge_voice_rag --help
```

## Configuration

Default config file:

```text
config/app.conf
```

Example:

```ini
knowledge_path=docs/vehicle_manual.txt
top_k=3
```

Development config example:

```ini
knowledge_path=docs/vehicle_manual.txt
top_k=1
```

Configuration fields:

```text
knowledge_path
    Path to the local vehicle manual text file.

top_k
    Maximum number of retrieved knowledge entries.
```

## Tests

### Unit Tests

Run:

```bash
./build/unit_tests
```

The unit tests directly validate:

```text
QueryRouter classification
RagEngine knowledge base loading
RagEngine top-k retrieval
Unrelated query handling
```

### End-to-end Tests

Run:

```bash
./scripts/test_once_queries.sh
```

The end-to-end script validates the complete one-shot query pipeline:

```text
--once command-line argument
    ↓
config loading
    ↓
query routing
    ↓
RAG retrieval
    ↓
output validation
```

## Current Retrieval Method

The current retrieval implementation is a mock RAG engine based on keyword rules.

Example rule:

```text
Triggers: 冷气 / 制冷 / 空调
Retrieval keyword: 空调
```

This allows queries such as:

```text
车里太热了怎么开冷气
```

to retrieve the air-conditioning manual entry.

Current scoring method:

```text
score = number of matched retrieval keywords
```

The system sorts matched documents by score and returns the top-k results.

## Limitations

V1 is intentionally simple. Current limitations include:

```text
No semantic embedding
No vector database
No Python RAG service
No LLM generation
No ASR or TTS
No ZeroMQ communication
No Docker deployment
No streaming response
```

The current RAG module is keyword-rule-based and should not be described as a full semantic RAG system.

## Roadmap

Planned milestones:

```text
V1: C++ Mock RAG Core
    C++ project structure, config, command-line options, mock RAG, tests.

V2: ZeroMQ Modular Communication
    Split the system into client/server modules using ZeroMQ.

V3: Python RAG Backend
    Replace mock C++ retrieval with Python-based embedding retrieval.

V4: Local LLM Integration
    Add local LLM inference through Ollama or llama.cpp.

V5: Offline Voice Pipeline
    Add ASR and TTS modules.

V6: Docker Deployment
    Containerize modules and provide docker-compose deployment.

V7: Edge Device Optimization
    Prepare migration path for RK/Jetson-style edge deployment.
```

## Engineering Notes

This project is developed incrementally with Git commits at each milestone.
The design goal is to keep every module independently testable and replaceable.

Current core interfaces are intentionally stable:

```cpp
QueryType type = router.classify(question);
std::vector<SearchResult> results = rag_engine.searchTopK(question, top_k);
```

Later versions can replace the internal retrieval backend without heavily changing the main process.
