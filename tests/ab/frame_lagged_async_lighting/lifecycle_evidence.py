"""Acceptance of the frame-lagged lighting lifecycle from runtime logs."""

from __future__ import annotations

from window_capture_smoke import STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, require_normal_process_exit

LIT_BOOTSTRAP = "bootstrap"
LIT_ACTIVE_HISTORY = "active history"
LIT_CURRENT_FRAME = "current frame"


NO_DEDICATED_ASYNC_COMPUTE = "RendererSystem: frame-lagged async lighting Graphics queue route accepted (no dedicated Compute queue"
BOOTSTRAP_ACCEPTED = "RendererSystem: frame-lagged async lighting bootstrap accepted"
ACTIVE_HISTORY_ACCEPTED = "RendererSystem: frame-lagged async lighting active history accepted"
CURRENT_FRAME_ACCEPTED = "RendererSystem: frame-lagged async lighting current-frame path accepted"
FORBIDDEN_LOG_MESSAGES = (*STRICT_LOG_FAILURE_MESSAGES, "cannot safely continue after an unresolved frame recovery submission", "deferred graph build with optional lagged lighting-history capture failed",
    "deferred lagged lighting-history tail was unavailable", "lagged lighting-history capture skipped because its source state was unavailable", "graph-owned lagged lighting-history capture record/submission was rejected",)

# Share the accepted lifecycle sequence between the live poller and offline self-test.
LAGGED_LIGHTING_LIFECYCLE = ((LIT_BOOTSTRAP, BOOTSTRAP_ACCEPTED), (LIT_ACTIVE_HISTORY, ACTIVE_HISTORY_ACCEPTED), (LIT_CURRENT_FRAME, CURRENT_FRAME_ACCEPTED), (LIT_BOOTSTRAP, BOOTSTRAP_ACCEPTED), (LIT_ACTIVE_HISTORY, ACTIVE_HISTORY_ACCEPTED),)


class DedicatedComputeUnavailable(SmokeSkip):
    """The application uses its Graphics queue route rather than the opt-in async topology."""


class LightingLifecycleEvidence:
    @staticmethod
    def accepted_lifecycle_events(log_text: str) -> tuple[str, ...]:
        """Return accepted lifecycle markers in log order, including repeated transition types."""
        occurrences: list[tuple[int, str]] = []
        for event_name, marker in ((LIT_BOOTSTRAP, BOOTSTRAP_ACCEPTED), (LIT_ACTIVE_HISTORY, ACTIVE_HISTORY_ACCEPTED), (LIT_CURRENT_FRAME, CURRENT_FRAME_ACCEPTED),):
            offset = 0
            while True:
                found = log_text.find(marker, offset)
                if found < 0:
                    break
                occurrences.append((found, event_name))
                offset = found + len(marker)

        occurrences.sort(key=lambda occurrence: occurrence[0])
        return tuple(event_name for _, event_name in occurrences)

    @staticmethod
    def validate_lifecycle_order(log_text: str) -> tuple[str, ...]:
        """Reject an out-of-order or duplicated accepted lifecycle event."""
        expected_events = tuple(event_name for event_name, _ in LAGGED_LIGHTING_LIFECYCLE)
        events = LightingLifecycleEvidence.accepted_lifecycle_events(log_text)
        allowed_prefix = expected_events[:len(events)]
        if events != allowed_prefix:
            raise SmokeFailure("frame-lagged async-lighting smoke observed an invalid accepted lifecycle: " f"expected prefix {expected_events}, observed {events}")
        return events

    @staticmethod
    def require_lifecycle_stage(log_text: str, expected_event_count: int) -> tuple[str, ...]:
        """Require a valid accepted lifecycle to have reached a specific stage."""
        if expected_event_count < 0 or expected_event_count > len(LAGGED_LIGHTING_LIFECYCLE):
            raise ValueError(f"invalid expected lifecycle event count: {expected_event_count}")

        expected_events = tuple(event_name for event_name, _ in LAGGED_LIGHTING_LIFECYCLE)
        events = LightingLifecycleEvidence.validate_lifecycle_order(log_text)
        if len(events) < expected_event_count:
            raise SmokeFailure("frame-lagged async-lighting smoke has not reached the required accepted lifecycle stage: " f"expected {expected_events[:expected_event_count]}, observed {events}")
        return events

    @staticmethod
    def reject_forbidden_messages(log_text: str) -> None:
        rejected = [marker for marker in FORBIDDEN_LOG_MESSAGES if marker in log_text]
        if rejected:
            raise SmokeFailure(f"frame-lagged async-lighting smoke found forbidden log messages: {rejected}")

    @staticmethod
    def require_final_run_verdict(log_text: str, app_exit_code: int | None, app_exit_tail: str) -> None:
        """Validate shutdown and complete diagnostics before classifying an unavailable async topology."""
        require_normal_process_exit(app_exit_code, app_exit_tail, "testbed")
        LightingLifecycleEvidence.reject_forbidden_messages(log_text)
        if NO_DEDICATED_ASYNC_COMPUTE in log_text:
            raise DedicatedComputeUnavailable("frame-lagged async-lighting smoke skipped: the requested feature accepted the Graphics queue route " "because this adapter has no dedicated Compute queue")
        LightingLifecycleEvidence.require_lifecycle_stage(log_text, len(LAGGED_LIGHTING_LIFECYCLE))
