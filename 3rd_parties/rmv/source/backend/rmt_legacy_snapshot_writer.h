//=============================================================================
// Copyright (c) 2023-2026 Advanced Micro Devices, Inc. All rights reserved.
/// @author AMD Developer Tools Team
/// @file
/// @brief  Class that handles writing snapshot data to legacy trace files.
//=============================================================================

#ifndef RMV_BACKEND_RMT_LEGACY_SNAPSHOT_WRITER_H_
#define RMV_BACKEND_RMT_LEGACY_SNAPSHOT_WRITER_H_

#include "rmt_data_set.h"
#include "rmt_error.h"

#include "rmt_snapshot_writer.h"

// A class that handles writing snapshot data for legacy trace files.
class RmtLegacySnapshotWriter : public RmtSnapshotWriter
{
public:
    /// Constructor for the RmtLegacySnapshotWriter
    ///
    /// @param [in]  data_set                             A pointer to the <c><i>RmtDataSet</i></c> object.
    explicit RmtLegacySnapshotWriter(RmtDataSet* data_set);

    /// Destructor for the RmtSnapshotWriter
    ~RmtLegacySnapshotWriter();

    /// Add a snapshot to a trace file.
    ///
    /// @param [in]  name                                 The name of the snapshot.
    /// @param [in]  timestamp                            The position on the timeline of the snapshot.
    /// @param [in]  new_snapshot_index                   The index of the snapshot in the data set to be added.
    ///
    /// @retval
    /// kRmtOk                                            The operation completed successfully.
    RmtErrorCode Add(const char* name, const uint64_t timestamp, const uint16_t new_snapshot_index) const;

    /// Remove a snapshot from a trace file.
    ///
    /// @param [in]  removed_snapshot_index               The index of the snapshot in the data set to be removed.
    ///
    /// @retval
    /// kRmtOk                                            The operation completed successfully.
    RmtErrorCode Remove(const uint16_t removed_snapshot_index) const;
};
#endif  // #ifndef RMV_BACKEND_RMT_LEGACY_SNAPSHOT_WRITER_H_
