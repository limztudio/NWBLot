"""Publish generated UI skin files as one recoverable transaction."""

import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
from typing import Optional

SOURCE_IMAGE_NAME = "source.png"
TEXTURE_OUTPUT_NAME = "texture"
TEX_CONV_OUTPUT_FLAG = "--output"
TEX_CONV_FORCE_FLAG = "--force"


class SkinPublisher:
    def __init__(self, directory: Path, converter: Optional[Path], generate_into):
        self.directory = directory
        self.converter = converter
        self.generate_into = generate_into

    @staticmethod
    def inspect_file(path: Path) -> bool:
        try:
            info = path.lstat()
        except FileNotFoundError:
            return False
        if not stat.S_ISREG(info.st_mode):
            raise RuntimeError(f"not a regular file: {path}")
        return True

    @staticmethod
    def report_cleanup(message: str, active_error: Optional[BaseException]) -> None:
        try:
            print(message, file=sys.stderr)
        except (OSError, ValueError):
            if active_error is None:
                raise
            try:
                add_note = getattr(active_error, "add_note", None)
                if add_note is not None:
                    add_note(message)
                else:
                    active_error.args += (message,)
            except Exception:
                pass

    def publish(self) -> None:
        directory, converter = self.directory, self.converter
        directory.mkdir(parents=True, exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix=".ui_skin_", dir=directory))
        names = [SOURCE_IMAGE_NAME, "atlas.nwb"]
        if converter:
            names.extend(["texture.nwb", "texture.tex"])
        known_names = set(names) | {name + ".previous" for name in names}
        records = []
        preserve = False
        try:
            self.generate_into(stage)
            if converter:
                subprocess.run([str(converter.resolve()), str(stage / SOURCE_IMAGE_NAME), TEX_CONV_OUTPUT_FLAG, str(stage / TEXTURE_OUTPUT_NAME), TEX_CONV_FORCE_FLAG], check=True)
            for name in names:
                source = stage / name
                target = directory / name
                if not self.inspect_file(source):
                    raise RuntimeError(f"missing generated file: {source}")
                if self.inspect_file(target) and os.name == "posix":
                    target_info = target.lstat()
                    source_info = source.lstat()
                    if (source_info.st_uid, source_info.st_gid) != (target_info.st_uid, target_info.st_gid):
                        os.chown(source, target_info.st_uid, target_info.st_gid)
                    os.chmod(source, stat.S_IMODE(target_info.st_mode))
            for name in names:
                source = stage / name
                target = directory / name
                previous = stage / (name + ".previous")
                had_previous = self.inspect_file(target)
                records.append((source, target, previous, had_previous))
                if had_previous:
                    os.replace(target, previous)
                os.replace(source, target)
        except BaseException as error:
            preserve = True
            failures = []
            for source, target, previous, had_previous in reversed(records):
                try:
                    if self.inspect_file(previous):
                        os.replace(previous, target)
                    elif not had_previous and not self.inspect_file(source) and self.inspect_file(target):
                        target.unlink()
                except (OSError, RuntimeError) as rollback_error:
                    failures.append(f"{target}: {rollback_error}")
            if failures:
                raise RuntimeError(f"UI skin rollback failed; recovery files remain in {stage}: {'; '.join(failures)}") from error
            preserve = False
            raise
        finally:
            active_error = sys.exc_info()[1]
            if preserve:
                self.report_cleanup(f"UI skin recovery files remain in {stage}", active_error)
            else:
                try:
                    for item in stage.iterdir():
                        if item.name in known_names and item.is_file() and not item.is_symlink():
                            item.unlink()
                    stage.rmdir()
                except OSError as cleanup_error:
                    self.report_cleanup(f"UI skin temporary files remain in {stage}: {cleanup_error}", active_error)
