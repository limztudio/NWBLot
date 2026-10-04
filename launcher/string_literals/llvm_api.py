#!/usr/bin/env python3
"""Version-pinned, host-native LLVM C bindings for literal bitcode rewriting."""

from __future__ import annotations

import ctypes
import os
from pathlib import Path
from typing import Tuple


REF = ctypes.c_void_p
REF_OUT = ctypes.POINTER(REF)
UINT = ctypes.c_uint
UINT_OUT = ctypes.POINTER(UINT)
U64 = ctypes.c_uint64
SIZE = ctypes.c_size_t
SIZE_OUT = ctypes.POINTER(SIZE)
BOOL = ctypes.c_int
TEXT = ctypes.c_char_p


class LlvmApi:
    """Load LLVM with the Python process architecture and exact compiler version."""

    def __init__(self, library_path: Path, expected_version: Tuple[int, int, int]) -> None:
        if len(expected_version) != 3 or any(component < 0 for component in expected_version):
            raise ValueError("LLVM requires an explicit major/minor/patch version")
        self.library_path = Path(library_path).resolve(strict=True)
        try:
            self.library = ctypes.CDLL(str(self.library_path))
        except OSError as error:
            raise RuntimeError(
                "Cannot load LLVM C library with the Python process architecture: "
                f"{self.library_path}: {error}"
            ) from error
        self._bind("LLVMGetVersion", None, UINT_OUT, UINT_OUT, UINT_OUT)
        components = (UINT(), UINT(), UINT())
        self.LLVMGetVersion(*(ctypes.byref(component) for component in components))
        self.version = tuple(component.value for component in components)
        if self.version != tuple(expected_version):
            raise RuntimeError(f"LLVM C library version {self.version} differs from compiler {expected_version}")

        functions = (
            ("LLVMContextCreate", REF, ()),
            ("LLVMContextDispose", None, (REF,)),
            ("LLVMDisposeMessage", None, (REF,)),
            ("LLVMCreateMemoryBufferWithContentsOfFile", BOOL, (TEXT, REF_OUT, REF_OUT)),
            ("LLVMDisposeMemoryBuffer", None, (REF,)),
            ("LLVMParseBitcodeInContext2", BOOL, (REF, REF, REF_OUT)),
            ("LLVMDisposeModule", None, (REF,)),
            ("LLVMVerifyModule", BOOL, (REF, UINT, REF_OUT)),
            ("LLVMWriteBitcodeToFile", BOOL, (REF, TEXT)),
            ("LLVMGetTarget", TEXT, (REF,)),
            ("LLVMGetDataLayoutStr", TEXT, (REF,)),
            ("LLVMCreateTargetData", REF, (TEXT,)),
            ("LLVMDisposeTargetData", None, (REF,)),
            ("LLVMByteOrder", UINT, (REF,)),
            ("LLVMABIAlignmentOfType", UINT, (REF, REF)),
            ("LLVMABISizeOfType", U64, (REF, REF)),
            ("LLVMGetFirstGlobal", REF, (REF,)),
            ("LLVMGetNextGlobal", REF, (REF,)),
            ("LLVMGetNamedGlobal", REF, (REF, TEXT)),
            ("LLVMGlobalGetValueType", REF, (REF,)),
            ("LLVMGetInitializer", REF, (REF,)),
            ("LLVMIsGlobalConstant", BOOL, (REF,)),
            ("LLVMGetUnnamedAddress", UINT, (REF,)),
            ("LLVMGetLinkage", UINT, (REF,)),
            ("LLVMGetThreadLocalMode", UINT, (REF,)),
            ("LLVMIsExternallyInitialized", BOOL, (REF,)),
            ("LLVMGetDLLStorageClass", UINT, (REF,)),
            ("LLVMGetSection", TEXT, (REF,)),
            ("LLVMGetAlignment", UINT, (REF,)),
            ("LLVMTypeOf", REF, (REF,)),
            ("LLVMGetPointerAddressSpace", UINT, (REF,)),
            ("LLVMGetValueName2", REF, (REF, SIZE_OUT)),
            ("LLVMGetTypeKind", UINT, (REF,)),
            ("LLVMGetElementType", REF, (REF,)),
            ("LLVMGetIntTypeWidth", UINT, (REF,)),
            ("LLVMGetArrayLength2", U64, (REF,)),
            ("LLVMIsAConstantDataArray", REF, (REF,)),
            ("LLVMIsAConstantArray", REF, (REF,)),
            ("LLVMIsAConstantStruct", REF, (REF,)),
            ("LLVMIsAConstantExpr", REF, (REF,)),
            ("LLVMIsAGlobalVariable", REF, (REF,)),
            ("LLVMIsAConstantInt", REF, (REF,)),
            ("LLVMGetNumOperands", BOOL, (REF,)),
            ("LLVMGetOperand", REF, (REF, UINT)),
            ("LLVMIsNull", BOOL, (REF,)),
            ("LLVMGetRawDataValues", REF, (REF, SIZE_OUT)),
            ("LLVMGetAggregateElement", REF, (REF, UINT)),
            ("LLVMConstIntGetZExtValue", U64, (REF,)),
            ("LLVMIntTypeInContext", REF, (REF, UINT)),
            ("LLVMArrayType2", REF, (REF, U64)),
            ("LLVMStructTypeInContext", REF, (REF, REF_OUT, UINT, BOOL)),
            ("LLVMConstInt", REF, (REF, U64, BOOL)),
            ("LLVMConstNull", REF, (REF,)),
            ("LLVMConstDataArray", REF, (REF, TEXT, SIZE)),
            ("LLVMConstStructInContext", REF, (REF, REF_OUT, UINT, BOOL)),
            ("LLVMAddGlobal", REF, (REF, REF, TEXT)),
            ("LLVMSetInitializer", None, (REF, REF)),
            ("LLVMSetGlobalConstant", None, (REF, BOOL)),
            ("LLVMSetLinkage", None, (REF, UINT)),
            ("LLVMSetVisibility", None, (REF, UINT)),
            ("LLVMSetAlignment", None, (REF, UINT)),
            ("LLVMSetUnnamedAddress", None, (REF, UINT)),
            ("LLVMSetSection", None, (REF, TEXT)),
            ("LLVMGetOrInsertComdat", REF, (REF, TEXT)),
            ("LLVMSetComdatSelectionKind", None, (REF, UINT)),
            ("LLVMSetComdat", None, (REF, REF)),
            ("LLVMConstInBoundsGEP2", REF, (REF, REF, REF_OUT, UINT)),
            ("LLVMReplaceAllUsesWith", None, (REF, REF)),
            ("LLVMDeleteGlobal", None, (REF,)),
        )
        for name, result, arguments in functions:
            self._bind(name, result, *arguments)

    def _bind(self, name: str, result: object, *arguments: object) -> None:
        try:
            function = getattr(self.library, name)
        except AttributeError as error:
            raise RuntimeError(f"LLVM C library {self.library_path} lacks required API {name}") from error
        function.argtypes = list(arguments)
        function.restype = result
        setattr(self, name, function)

    def take_message(self, message: REF) -> str:
        if not message.value:
            return ""
        try:
            return ctypes.string_at(message).decode("utf-8", errors="replace")
        finally:
            self.LLVMDisposeMessage(message)

    def value_name(self, value: int) -> str:
        length = SIZE()
        data = self.LLVMGetValueName2(value, ctypes.byref(length))
        return ctypes.string_at(data, length.value).decode("utf-8", errors="replace") if length.value else ""

    def read_module(self, context: int, path: Path) -> int:
        buffer = REF()
        message = REF()
        if self.LLVMCreateMemoryBufferWithContentsOfFile(os.fsencode(path), ctypes.byref(buffer), ctypes.byref(message)):
            raise RuntimeError(f"Cannot read bitcode {path}: {self.take_message(message)}")
        module = REF()
        try:
            if self.LLVMParseBitcodeInContext2(context, buffer, ctypes.byref(module)):
                raise RuntimeError(f"LLVM {self.version} cannot parse bitcode {path}")
            return module.value
        finally:
            self.LLVMDisposeMemoryBuffer(buffer)

    def verify_module(self, module: int) -> None:
        message = REF()
        invalid = self.LLVMVerifyModule(module, 2, ctypes.byref(message))
        diagnostic = self.take_message(message)
        if invalid:
            raise RuntimeError(f"LLVM module verification failed: {diagnostic}")

