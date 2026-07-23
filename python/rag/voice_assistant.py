import json
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from time import perf_counter
from typing import Any, Dict, Optional

from rag.audio_player import (
    AudioPlayer,
    parse_output_device,
)
from rag.microphone_io import parse_device
from rag.vad_recorder import (
    VadMicrophoneRecorder,
)
from rag.voice_io import (
    create_asr_backend,
    create_tts_backend,
)
from rag.barge_in_detector import (
    BargeInDetector,
)
from rag.rag_stream_client import RagStreamClient
from rag.streaming_sentence_buffer import StreamingSentenceBuffer


class AssistantState(str, Enum):
    INITIALIZING = "initializing"
    WAITING = "waiting"
    RECORDING = "recording"
    RECOGNIZING = "recognizing"
    RETRIEVING = "retrieving"
    SYNTHESIZING = "synthesizing"
    PLAYING = "playing"
    ERROR = "error"
    STOPPED = "stopped"

@dataclass
class VoiceAssistantConfig:
    vad_model: str
    asr_model: str
    tts_model_dir: str
    rag_endpoint: str

    rag_stream_endpoint: str = "tcp://localhost:5557"

    stream_sentence_max_chars: int = 60

    microphone_device: Optional[str] = None
    output_device: Optional[str] = None

    asr_device: str = "cpu"
    asr_compute_type: str = "int8"

    vad_threshold: float = 0.25
    vad_min_silence: float = 0.8
    vad_max_wait: float = 10.0

    tts_num_threads: int = 2
    tts_speed: float = 1.0

    rag_timeout_ms: int = 10000
    output_dir: str = "voice_output/assistant"

class VoiceAssistant:
    def __init__(
        self,
        config: VoiceAssistantConfig,
    ) -> None:
        self.config = config
        self.state = AssistantState.INITIALIZING
        self.turn_id = 0

        output_dir = Path(self.config.output_dir)
        output_dir.mkdir(
            parents=True,
            exist_ok=True,
        )
        self.output_dir = output_dir

        load_start = perf_counter()

        self.asr = create_asr_backend(
            backend="faster_whisper",
            model_name_or_path=(
                self.config.asr_model
            ),
            device=self.config.asr_device,
            compute_type=(
                self.config.asr_compute_type
            ),
            language="zh",
        )

        self.tts = create_tts_backend(
            backend="sherpa_onnx",
            model_dir=(
                self.config.tts_model_dir
            ),
            num_threads=(
                self.config.tts_num_threads
            ),
            speaker_id=0,
            speed=self.config.tts_speed,
        )

        self.recorder = VadMicrophoneRecorder(
            model_path=self.config.vad_model,
            device=parse_device(
                self.config.microphone_device
            ),
            threshold=(
                self.config.vad_threshold
            ),
            min_silence_duration=(
                self.config.vad_min_silence
            ),
            max_wait_seconds=(
                self.config.vad_max_wait
            ),
        )

        self.rag_stream_client = RagStreamClient(
            endpoint=self.config.rag_stream_endpoint,
            timeout_ms=self.config.rag_timeout_ms,
        )

        self.player = AudioPlayer(
            device=parse_output_device(
                self.config.output_device
            )
        )

        self.barge_in_detector = BargeInDetector(
            model_path=self.config.vad_model,
            device=parse_device(
                self.config.microphone_device
            )
        )

        self.model_load_ms = (
            perf_counter() - load_start
        ) * 1000

        self.state = AssistantState.WAITING

    def _set_state(
        self,
        state: AssistantState,
    ) -> None:
        self.state = state

        print(
            json.dumps(
                {
                    "event": "state_changed",
                    "state": state.value,
                    "turn_id": self.turn_id,
                },
                ensure_ascii=False,
            )
        )

    def _synthesize_and_play_sentence(
        self,
        sentence: str,
        segment_index: int,
        barge_in_input_path: Path,
    ) -> Dict[str, Any]:
        output_path = self.output_dir / (
            f"turn_{self.turn_id}_"
            f"segment_{segment_index}.wav"
        )

        self._set_state(AssistantState.SYNTHESIZING)

        tts_start = perf_counter()

        tts_result = self.tts.synthesize(
            text=sentence,
            output_path=output_path,
        )

        tts_ms = (
            perf_counter() - tts_start
        ) * 1000

        self._set_state(AssistantState.PLAYING)

        barge_in_result = self.barge_in_detector.play_and_wait(
            player=self.player,
            audio_path=output_path,
            speech_output_path=barge_in_input_path,
        )

        if barge_in_result.interrupted:
            print(
                json.dumps(
                    {
                        "event": "barge_in_speech_saved",
                        "turn_id": self.turn_id,
                        "segment_index": segment_index,
                        "speech_path": barge_in_result.speech_path,
                        "duration_seconds": barge_in_result.speech_duration_seconds,
                    },
                    ensure_ascii=False,
                )
            )

        return {
            "sentence": sentence,
            "output_path": tts_result.output_path,
            "tts_ms": tts_ms,
            "playback_ms": barge_in_result.playback_result.playback_ms,
            "interrupted": barge_in_result.interrupted,
            "detection_ms": barge_in_result.detection_ms,
            "speech_path": barge_in_result.speech_path,
        }

    def run_turn(self) -> Dict[str, Any]:
        self.turn_id += 1
        turn_start = perf_counter()

        input_path = (
            self.output_dir
            / f"turn_{self.turn_id}_input.wav"
        )

        barge_in_input_path = (
            self.output_dir
            / f"turn_{self.turn_id + 1}_input.wav"
        )

        self._set_state(AssistantState.RECORDING)

        recording_result = (
            self.recorder.record(
                output_path=input_path,
            )
        )

        self._set_state(AssistantState.RECOGNIZING)

        asr_start = perf_counter()

        asr_result = self.asr.transcribe(input_path)

        asr_ms = (
            perf_counter() - asr_start
        ) * 1000

        query = asr_result.text.strip()

        if not query:
            raise ValueError(
                "ASR returned empty query"
            )

        if query.lower() in {
            "exit",
            "quit",
            "退出",
            "结束",
            "停止",
        }:
            self.state = AssistantState.STOPPED

            return {
                "ok": True,
                "turn_id": self.turn_id,
                "should_stop": True,
                "query": query,
            }

        self._set_state(AssistantState.RETRIEVING)

        rag_start = perf_counter()

        sentence_buffer = StreamingSentenceBuffer(max_chars=self.config.stream_sentence_max_chars)

        stream = self.rag_stream_client.query(query)

        answer = ""
        final_answer = ""
        llm_backend = ""

        segment_results = []
        segment_index = 0

        first_chunk_ms = None
        first_sentence_ms = None
        rag_server_elapsed_ms = 0.0

        interrupted = False
        barge_in_detection_ms = 0.0
        barge_in_speech_path = None

        try:
            for event in stream:
                rag_server_elapsed_ms = event.elapsed_ms
                llm_backend = event.llm_backend

                sentences = []

                if event.type == "rag_chunk":
                    if first_chunk_ms is None:
                        first_chunk_ms = (
                            perf_counter() - rag_start
                        ) * 1000

                    answer += event.delta

                    sentences = sentence_buffer.push(event.delta)

                elif event.type == "rag_finished":
                    final_answer = event.answer

                    sentences = sentence_buffer.flush()

                for sentence in sentences:
                    if first_sentence_ms is None:
                        first_sentence_ms = (
                            perf_counter() - rag_start
                        ) * 1000

                    segment_index += 1

                    segment_result = (self._synthesize_and_play_sentence(
                        sentence=sentence,
                        segment_index=segment_index,
                        barge_in_input_path=barge_in_input_path,
                        )
                    )

                    segment_results.append(segment_result)

                    if segment_result["interrupted"]:
                        interrupted = True
                        barge_in_detection_ms = segment_result["detection_ms"]
                        barge_in_speech_path = segment_result["speech_path"]

                        break

                if interrupted:
                    break
        finally:
            if hasattr(stream, "close"):
                stream.close()

        if not answer:
            raise RuntimeError("RAG stream returned empty answer")

        if not interrupted:
            if not final_answer:
                raise RuntimeError(
                    "RAG stream ended without "
                    "final answer"
                )

            if answer != final_answer:
                raise RuntimeError(
                    "RAG chunks do not match "
                    "final answer"
                )

        if not segment_results:
            raise RuntimeError(
                "RAG stream produced no "
                "speakable sentence"
            )

        tts_ms = sum(item["tts_ms"] for item in segment_results)
        playback_ms = sum(item["playback_ms"] for item in segment_results)
        output_paths = [item["output_path"] for item in segment_results]
        rag_ms = (
            perf_counter() - rag_start
        ) * 1000

        total_ms = (
            perf_counter() - turn_start
        ) * 1000

        self._set_state(AssistantState.WAITING)

        return {
            "ok": True,
            "turn_id": self.turn_id,
            "should_stop": False,
            "query": query,
            "answer": final_answer or answer,
            "llm_backend": llm_backend,
            "input_path": str(input_path),
            "output_path": output_paths[-1],
            "output_paths": output_paths,
            "segment_count": len(segment_results),
            "segments": segment_results,
            "interrupted": interrupted,
            "barge_in_speech_path": barge_in_speech_path,
            "barge_in_detection_ms":round(
                barge_in_detection_ms,
                2,
            ),
            "timings_ms": {
                "waiting": round(
                    recording_result.waiting_ms,
                    2,
                ),
                "recording": round(
                    recording_result.recording_ms,
                    2,
                ),
                "asr": round(asr_ms, 2),
                "first_chunk": (
                    round(first_chunk_ms, 2)
                    if first_chunk_ms is not None
                    else None
                ),
                "first_sentence": (
                    round(first_sentence_ms, 2)
                    if first_sentence_ms is not None
                    else None
                ),
                "rag_server": round(rag_server_elapsed_ms, 2),
                "stream_with_audio": round(rag_ms, 2),
                "tts": round(tts_ms, 2),
                "playback": round(playback_ms, 2),
                "total": round(total_ms, 2),
            },
        }

    def run_loop(
        self,
        max_turns: int = 0,
    ) -> None:
        completed_turns = 0

        print(
            json.dumps(
                {
                    "event": "assistant_started",
                    "model_load_ms": round(
                        self.model_load_ms,
                        2,
                    ),
                },
                ensure_ascii=False,
            )
        )

        while True:
            if (
                max_turns > 0
                and completed_turns >= max_turns
            ):
                break

            try:
                result = self.run_turn()

                print(
                    json.dumps(
                        result,
                        ensure_ascii=False,
                        indent=2,
                    )
                )

                completed_turns += 1

                if result.get("should_stop"):
                    break

            except TimeoutError as exc:
                self._set_state(
                    AssistantState.WAITING
                )

                print(
                    json.dumps(
                        {
                            "ok": False,
                            "turn_id": self.turn_id,
                            "error_type": "timeout",
                            "error": str(exc),
                        },
                        ensure_ascii=False,
                    )
                )

            except KeyboardInterrupt:
                print(
                    "\n[INFO] KeyboardInterrupt"
                )
                break

            except Exception as exc:
                self._set_state(
                    AssistantState.ERROR
                )

                print(
                    json.dumps(
                        {
                            "ok": False,
                            "turn_id": self.turn_id,
                            "error_type": (
                                type(exc).__name__
                            ),
                            "error": str(exc),
                        },
                        ensure_ascii=False,
                    )
                )

                self._set_state(AssistantState.WAITING)

        self.player.stop()
        self._set_state(AssistantState.STOPPED)
