from typing import List

from rag.tts_text import normalize_tts_text

class StreamingSentenceBuffer:
    SENTENCE_ENDINGS = {
        "。",
        "！",
        "？",
        "!",
        "?",
        "；",
        ";",
    }

    SOFT_ENDINGS = {
        "，",
        ",",
        "、",
    }

    def __init__(
        self,
        max_chars: int = 60,
    ) -> None:
        if max_chars <= 0:
            raise ValueError("max_chars must be greater than zero")

        self.max_chars = max_chars
        self._buffer = ""

    @property
    def pending_text(self) -> str:
        return self._buffer

    def push(
        self,
        chunk: str,
    ) -> List[str]:
        if not isinstance(chunk, str):
            raise TypeError("stream chunk must be a string")

        if not chunk:
            return []

        self._buffer += chunk

        sentences = []

        while True:
            boundary = self._find_sentence_boundary()

            if boundary is not None:
                raw_sentence = self._buffer[:boundary]
                self._buffer = self._buffer[boundary:]

                sentence = normalize_tts_text(raw_sentence)

                if sentence:
                    sentences.append(sentence)

                continue

            if len(self._buffer) >= self.max_chars:
                raw_sentence = self._take_long_segment()

                sentence = normalize_tts_text(raw_sentence)

                if sentence:
                    sentences.append(sentence)

                continue

            break

        return sentences

    def flush(self) -> List[str]:
        if not self._buffer:
            return []

        raw_sentence = self._buffer
        self._buffer = ""

        sentence = normalize_tts_text(raw_sentence)

        if not sentence:
            return []

        return [sentence]

    def reset(self) -> None:
        self._buffer = ""

    def _find_sentence_boundary(self) -> int | None:
        search_limit = min(len(self._buffer), self.max_chars)
        
        for index, character in enumerate(self._buffer[:search_limit]):
            if character in self.SENTENCE_ENDINGS:
                return index + 1

        return None

    def _take_long_segment(self) -> str:
        search_limit = min(len(self._buffer), self.max_chars)

        for index in range(search_limit - 1, -1, -1):
            if (self._buffer[index] in self.SOFT_ENDINGS):
                boundary = index + 1
                segment = self._buffer[:boundary]
                self._buffer = self._buffer[boundary:]
                return segment

        segment = self._buffer[:self.max_chars]

        self._buffer = self._buffer[self.max_chars:]

        return segment
