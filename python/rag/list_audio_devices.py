import json

from rag.microphone_io import (
    list_input_devices,
)

def main() -> None:
    devices = list_input_devices()

    print(
        json.dumps(
            {
                "ok": True,
                "input_device_count": len(
                    devices
                ),
                "devices": devices,
            },
            ensure_ascii=False,
            indent=2,
        )
    )

if __name__ == "__main__":
    main()