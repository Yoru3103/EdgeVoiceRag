src/main.cpp
    │
    ├── CommandLineOptions
    │       解析 --config、--once
    │
    ├── AppConfig
    │       加载 RAG/LLM 地址、超时、知识库路径
    │
    ├── RagEngine
    │       本地 C++ 车辆知识检索
    │
    ├── ZmqTextClient
    │       访问远程 RAG/LLM 服务
    │
    ├── EdgeResponseBackend
    │       在本地 RAG、远程 RAG、远程 LLM 之间适配
    │
    └── MultiLevelResponseSystem
            │
            ├── QueryClassifier
            ├── 选择 RagOnly/LlmOnly/Hybrid
            └── 缓存回答

# 实际流程
用户问题
  → QueryClassifier
  → ResponseMode
      ├── Emergency → RagOnly
      ├── Factual   → RagOnly
      ├── Complex   → Hybrid
      ├── Creative  → LlmOnly
      └── Unknown   → Hybrid
