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
from rag.voice_pipeline import ZmqRagClient


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

        self.rag_client = ZmqRagClient(
            endpoint=self.config.rag_endpoint,
            timeout_ms=(
                self.config.rag_timeout_ms
            ),
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

    def run_turn(self) -> Dict[str, Any]:
        self.turn_id += 1
        turn_start = perf_counter()

        input_path = (
            self.output_dir
            / f"turn_{self.turn_id}_input.wav"
        )

        output_path = (
            self.output_dir
            / f"turn_{self.turn_id}_answer.wav"
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

        rag_result = self.rag_client.query(query)

        rag_ms = (
            perf_counter() - rag_start
        ) * 1000

        if not rag_result.ok:
            raise RuntimeError(
                rag_result.error
            )

        self._set_state(AssistantState.SYNTHESIZING)

        tts_start = perf_counter()

        tts_result = self.tts.synthesize(
            text=rag_result.answer,
            output_path=output_path,
        )

        tts_ms = (
            perf_counter() - tts_start
        ) * 1000

        self._set_state(AssistantState.PLAYING)

        self.player.start(output_path)

        barge_in_result = self.barge_in_detector.wait_for_interrupt(self.player)

        playback_result = barge_in_result.playback_result

        total_ms = (
            perf_counter() - turn_start
        ) * 1000

        self._set_state(AssistantState.WAITING)

        return {
            "ok": True,
            "turn_id": self.turn_id,
            "should_stop": False,
            "query": query,
            "answer": rag_result.answer,
            "input_path": str(input_path),
            "output_path": str(output_path),
            "interrupted": barge_in_result.interrupted,
            "barge_in_detection":round(
                barge_in_result.detection_ms,
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
                "rag": round(rag_ms, 2),
                "tts": round(tts_ms, 2),
                "playback": round(
                    playback_result.playback_ms,
                    2,
                ),
                "total": round(
                    total_ms,
                    2,
                ),
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