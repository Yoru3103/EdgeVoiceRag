from rag.streaming_sentence_buffer import StreamingSentenceBuffer

def test_sentence_actoss_chunks() -> None:
    buffer = StreamingSentenceBuffer()

    assert buffer.push("空调温") == []
    assert buffer.push("度可以通") == []

    sentences = buffer.push("过中控屏调节。")

    assert sentences == ["空调温度可以通过中控屏调节。"]

    assert buffer.pending_text == ""

def test_multiple_sentences_in_chunk() -> None:
    buffer = StreamingSentenceBuffer()

    sentences = buffer.push("空调已经开启。温度设置为二十四度！")

    assert sentences == [
        "空调已经开启。",
        "温度设置为二十四度！",
    ]

def test_keeps_incomplete_tail() -> None:
    buffer = StreamingSentenceBuffer()

    sentences = buffer.push("第一句。第二句还没有结束")

    assert sentences == ["第一句。"]
    assert (
        buffer.pending_text ==
        "第二句还没有结束"
    )

def test_flush_incomplete_sentence() -> None:
    buffer = StreamingSentenceBuffer()

    buffer.push("没有句号的最终回答")

    assert buffer.flush() == [
        "没有句号的最终回答"
    ]

    assert buffer.pending_text == ""

def test_long_text_prefers_comma() -> None:
    buffer = StreamingSentenceBuffer(
        max_chars=10
    )

    sentences = buffer.push(
        "这是第一部分，这是后面的很长内容"
    )

    assert sentences[0] == "这是第一部分"

def test_reset() -> None:
    buffer = StreamingSentenceBuffer()

    buffer.push("未完成内容")
    buffer.reset()

    assert buffer.pending_text == ""
    assert buffer.flush() == []

def test_sentence_ending_beyond_limit() -> None:
    buffer = StreamingSentenceBuffer(
        max_chars=10
    )

    sentences = buffer.push("一二三四五六七八九十一二三四五。")

    assert sentences
    assert len(sentences[0]) <= 10
