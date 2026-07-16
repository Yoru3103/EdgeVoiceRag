from rag.tts_text import (
    normalize_tts_text,
    split_tts_text,
)

def test_normalize_tts_text_removes_quotes() -> None:
    text = '可以使用语音指令“打开空调”。'
    
    result = normalize_tts_text(text)
    
    assert "“" not in result
    assert "”" not in result
    assert "打开空调" in result
    
def test_normalize_tts_text_replaces_colon() -> None:
    text = "根据车辆手册：打开中控屏。"

    result = normalize_tts_text(text)

    assert "：" not in result
    assert "，" in result
    
def test_normalize_tts_text_removes_list_numbers() -> None:
    text = "1. 打开空调。\n2. 调整温度。"

    result = normalize_tts_text(text)

    assert "1." not in result
    assert "2." not in result
    assert "打开空调" in result
    assert "调整温度" in result
    
def test_split_tts_text_by_sentence() -> None:
    text = "打开空调。调整温度。连接蓝牙。"

    result = split_tts_text(
        text,
        max_chars=20,
    )

    assert result == [
        "打开空调。",
        "调整温度。",
        "连接蓝牙。",
    ]
    
def test_split_long_sentence() -> None:
    text = (
        "用户可以通过中控屏点击空调按钮，"
        "也可以使用语音指令打开空调，"
        "还可以通过滑动条调节温度。"
    )

    result = split_tts_text(
        text,
        max_chars=25,
    )

    assert len(result) >= 2
    assert all(
        len(item) <= 25
        for item in result
    )
    
def test_split_empty_text() -> None:
    assert split_tts_text("") == []
    assert split_tts_text("   ") == []