"""Process termination fakes shared by deterministic smoke-harness regression checks."""
from __future__ import annotations


def make_abnormal_exit_terminator(app, events):
    def terminate(process, name, window_handle=None):
        if process is app:
            events.append(("app-stop", name, window_handle))
            return 7, "simulated abnormal exit"
        assert process is None
        events.append(("cleanup-none", name, window_handle))
        return None, ""

    return terminate
