from llm_generator import create_llm_generator

def main() -> None:
    generator = create_llm_generator("mock")
    
    query = "空调怎么打开"
    contexts = [
         "空调系统：用户可以通过中控屏点击空调按钮，也可以使用语音指令“打开空调”。"
    ]
    
    results = generator.generate(query=query, contexts=contexts)
    
    print("Backend:")
    print(results.backend)
    
    print("\nPrompt:")
    print(results.prompt)
    
    print("\nAnswer:")
    print(results.answer)
    
if __name__ == "__main__":
    main()