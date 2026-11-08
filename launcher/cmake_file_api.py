#!/usr/bin/env python3
"""CMake File API query and target metadata access."""

from __future__ import annotations

import fnmatch
import json
import os
from pathlib import Path
from typing import Optional

from launcher.models import CMakeTargetInfo
from launcher.constants import (CACHE_FILE_MODE, CACHE_FILE_ENCODING, FILE_API_ARTIFACTS_KEY, FILE_API_CODEMODEL, FILE_API_CODEMODEL_VERSION,
    FILE_API_CONFIGURATIONS_KEY, FILE_API_DIR_API, FILE_API_DIR_CMAKE, FILE_API_DIR_QUERY, FILE_API_DIR_REPLY, FILE_API_DIR_V1, FILE_API_INDEX_GLOB,
    FILE_API_JSON_FILE_KEY, FILE_API_KIND_CODEMODEL, FILE_API_KIND_KEY, FILE_API_NAME_KEY, FILE_API_OBJECTS_KEY, FILE_API_PATH_KEY,
    FILE_API_TARGETS_KEY, FILE_API_TYPE_KEY, FILE_API_VERSION_KEY, FILE_API_VERSION_MAJOR, EMPTY_STRING)


class CmakeFileApi:
    """Read CMake target metadata and manage the codemodel query."""

    @staticmethod
    def file_api_query_path(build_dir: Path) -> Path:
        return build_dir / FILE_API_DIR_CMAKE / FILE_API_DIR_API / FILE_API_DIR_V1 / FILE_API_DIR_QUERY / FILE_API_CODEMODEL

    @staticmethod
    def file_api_reply_dir(build_dir: Path) -> Path:
        return build_dir / FILE_API_DIR_CMAKE / FILE_API_DIR_API / FILE_API_DIR_V1 / FILE_API_DIR_REPLY

    @staticmethod
    def ensure_file_api_query(build_dir: Path) -> None:
        query = CmakeFileApi.file_api_query_path(build_dir)
        query.parent.mkdir(parents=True, exist_ok=True)
        query.touch()

    @staticmethod
    def latest_file_api_index(build_dir: Path) -> Optional[Path]:
        reply_dir = CmakeFileApi.file_api_reply_dir(build_dir)
        try:
            with os.scandir(reply_dir) as entries:
                indexes = [reply_dir / entry.name for entry in entries if fnmatch.fnmatch(entry.name, FILE_API_INDEX_GLOB)]
        except FileNotFoundError:
            return None
        if not indexes:
            return None
        return max(indexes, key=lambda path: (path.stat().st_mtime, path.name))

    @staticmethod
    def file_api_has_reply(build_dir: Path) -> bool:
        return CmakeFileApi.latest_file_api_index(build_dir) is not None

    @staticmethod
    def read_json(path: Path):
        with path.open(CACHE_FILE_MODE, encoding=CACHE_FILE_ENCODING) as file:
            return json.load(file)

    @staticmethod
    def load_cmake_target_info(build_dir: Path, target_name: str, config: str) -> Optional[CMakeTargetInfo]:
        index_path = CmakeFileApi.latest_file_api_index(build_dir)
        if index_path is None:
            return None

        index = CmakeFileApi.read_json(index_path)
        codemodel_file = None
        for item in index.get(FILE_API_OBJECTS_KEY, []):
            if item.get(FILE_API_KIND_KEY) != FILE_API_KIND_CODEMODEL:
                continue
            version = item.get(FILE_API_VERSION_KEY, {})
            if version.get(FILE_API_VERSION_MAJOR) == FILE_API_CODEMODEL_VERSION:
                codemodel_file = item.get(FILE_API_JSON_FILE_KEY)
                break
        if not codemodel_file:
            return None

        reply_dir = index_path.parent
        codemodel = CmakeFileApi.read_json(reply_dir / codemodel_file)
        configurations = codemodel.get(FILE_API_CONFIGURATIONS_KEY, [])
        configuration = next((entry for entry in configurations if entry.get(FILE_API_NAME_KEY) == config), None)
        if configuration is None:
            return None

        for target_ref in configuration.get(FILE_API_TARGETS_KEY, []):
            if target_ref.get(FILE_API_NAME_KEY) != target_name:
                continue

            target = CmakeFileApi.read_json(reply_dir / target_ref[FILE_API_JSON_FILE_KEY])
            artifacts = []
            for artifact in target.get(FILE_API_ARTIFACTS_KEY, []):
                artifact_path = Path(artifact[FILE_API_PATH_KEY])
                if not artifact_path.is_absolute():
                    artifact_path = build_dir / artifact_path
                artifacts.append(artifact_path)

            return CMakeTargetInfo(name=target.get(FILE_API_NAME_KEY, target_name), target_type=target.get(FILE_API_TYPE_KEY, EMPTY_STRING), artifacts=tuple(artifacts),)

        return None

