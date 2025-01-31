def can_build(env, platform):
    return platform != "web" and platform != "android"


def configure(env):
    pass


def get_doc_classes():
    return [
        "Speech",
        "SpeechDecoder",
        "SpeechProcessor",
        "PlaybackStats",
        "VoipJitterBuffer",
        "JitterBuffer",
        "JitterBufferPacket",
    ]


def get_doc_path():
    return "doc_classes"
