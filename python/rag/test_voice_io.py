from pathlib import Path

from voice_io import create_asr_backend, create_tts_backend


def main() -> None:
    asr = create_asr_backend(
        backend="mock",
        mock_text="空调怎么打开",
    )
    
    tts = create_tts_backend(
        backend="mock",
    )
    
    asr_result = asr.transcribe(Path("voice_input/mock.wav"))
    
    print("ASR result:")
    print(f"  backend: {asr_result.backend}")
    print(f"  source: {asr_result.source}")
    print(f"  text: {asr_result.text}")
    
    answer = "根据车辆手册：可以通过中控屏点击空调按钮，也可以使用语音指令打开空调。"
    
    tts_result = tts.synthesize(
        text=answer,
        output_path=Path("voice_output/mock_answer.txt")
    )
    
    print("\nTTS result:")
    print(f"  backend: {tts_result.backend}")
    print(f"  output_path: {tts_result.output_path}")
    print(f"  text: {tts_result.text}")

if __name__ == "__main__":
    main()