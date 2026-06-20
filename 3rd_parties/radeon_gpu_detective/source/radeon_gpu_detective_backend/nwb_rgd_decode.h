//=============================================================================
// NWB-authored shim (NOT vendored AMD source).
//
// A single narrow entry point into the Radeon GPU Detective backend that decodes a .rgd GPU-crash capture
// into a human-readable text SUMMARY entirely in-process — the in-library equivalent of running
// `rgd --parse <file>`. It replaces the engine logserver's former rgd.exe subprocess. The assembly mirrors
// the RGD CLI frontend's SerializeTextOutput but omits the shader/ISA disassembly sections, whose code-object
// decode path is not compiled into the vendored backend. All exceptions are contained: this never throws
// across the boundary, so the long-lived ingest worker that calls it can stay exception-free.
//=============================================================================
#ifndef NWB_RGD_DECODE_H_
#define NWB_RGD_DECODE_H_

#include <string>

namespace nwb_rgd{

// Decode the .rgd capture at rgd_file_path into a text summary written to out_text.
// Returns true on a successful decode (out_text holds the report); false on failure
// (out_text holds a short single-line reason such as "parse_failed").
bool DecodeCrashDumpToText(const std::string& rgd_file_path, std::string& out_text);

}

#endif // NWB_RGD_DECODE_H_
