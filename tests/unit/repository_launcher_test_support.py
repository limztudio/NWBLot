"""Fake process APIs and argument builders shared by the repository launcher test modules."""


LIT_WAIT = "wait"
LIT_TERMINATE = "terminate"
LIT_KILL = "kill"


class FakeSpawnedProcess:
    def __init__(self, pid, exit_code, events):
        self.pid = pid
        self.exit_code = exit_code
        self.events = events
        self.returncode = None

    def wait(self, timeout=None):
        self.events.append((LIT_WAIT, timeout))
        self.returncode = self.exit_code
        return self.exit_code

    def poll(self):
        return self.returncode

    def terminate(self):
        self.events.append((LIT_TERMINATE, self.pid))

    def kill(self):
        self.events.append((LIT_KILL, self.pid))
